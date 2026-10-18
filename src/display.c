#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_co5300.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"

static esp_lcd_panel_handle_t panel;
static esp_lcd_panel_io_handle_t io;
static SemaphoreHandle_t startup_done;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t completed_frames, completed_bytes;
static int64_t frame_start_us, frame_total_us, frame_max_us, sample_start_us;
static size_t transfer_bytes;
static bool frame_open, last_chunk;

// Vendor sequence verbatim (esp32_lcd_qspi_lvgl esp_lcd_tk018.c), verified on this
// panel over 1-wire SPI. CE/D8 are undocumented in the CO5300 datasheet.
static const co5300_lcd_init_cmd_t init_commands[] = {
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xCE, (uint8_t[]){0x0D, 0x00}, 2, 0},
    {0xD8, (uint8_t[]){0x08, 0x00}, 2, 0},
    {0x11, NULL, 0, 120},
    {0xFE, (uint8_t[]){0x00}, 1, 0},
    {0xC4, (uint8_t[]){0x80}, 1, 0},
    {0x3A, (uint8_t[]){0x55}, 1, 0},
    {0x53, (uint8_t[]){0x20}, 1, 0},
    {0x63, (uint8_t[]){0xFF}, 1, 0},
    {0x51, (uint8_t[]){0x30}, 1, 0},
    {0x58, (uint8_t[]){0x07}, 1, 0}, // Color enhancement (datasheet 58h).
    {0x29, NULL, 0, 80},
};

static bool transfer_done(esp_lcd_panel_io_handle_t io,
                          esp_lcd_panel_io_event_data_t *event, void *context)
{
    if (startup_done) {
        BaseType_t woken = pdFALSE;
        xSemaphoreGiveFromISR(startup_done, &woken);
        return woken == pdTRUE;
    }
    portENTER_CRITICAL_ISR(&stats_lock);
    completed_bytes += transfer_bytes;
    if (last_chunk) {
        int64_t elapsed = esp_timer_get_time() - frame_start_us;
        completed_frames++;
        frame_total_us += elapsed;
        if (elapsed > frame_max_us) frame_max_us = elapsed;
        frame_open = false;
    }
    portEXIT_CRITICAL_ISR(&stats_lock);
    // LVGL explicitly permits flush_ready from an ISR. No other LVGL work here.
    lv_display_flush_ready(context);
    return false;
}

// The official driver always sends pixels with quad opcode 0x32. For the 1-wire
// diagnostic, write the window and pixels with opcode 0x02 on SIO0 only, as the
// vendor example does (datasheet p.20: 0x02 = command / pixel write).
static esp_err_t draw(int x1, int y1, int x2, int y2, const void *pixels)
{
#if LCD_SINGLE_WIRE
    uint8_t columns[] = {x1 >> 8, x1 & 0xFF, (x2 - 1) >> 8, (x2 - 1) & 0xFF};
    uint8_t rows[] = {y1 >> 8, y1 & 0xFF, (y2 - 1) >> 8, (y2 - 1) & 0xFF};
    esp_err_t err = esp_lcd_panel_io_tx_param(io, 0x02002A00, columns, sizeof(columns));
    if (err == ESP_OK) err = esp_lcd_panel_io_tx_param(io, 0x02002B00, rows, sizeof(rows));
    if (err == ESP_OK) err = esp_lcd_panel_io_tx_color(io, 0x02002C00, pixels, (x2 - x1) * (y2 - y1) * 2);
    return err;
#else
    return esp_lcd_panel_draw_bitmap(panel, x1, y1, x2, y2, pixels);
#endif
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t count = lv_area_get_size(area);
    lv_draw_sw_rgb565_swap(pixels, count); // CO5300 consumes big-endian RGB565.
    portENTER_CRITICAL(&stats_lock);
    if (!frame_open) {
        frame_start_us = esp_timer_get_time();
        frame_open = true;
    }
    last_chunk = lv_display_flush_is_last(display);
    transfer_bytes = count * sizeof(uint16_t);
    portEXIT_CRITICAL(&stats_lock);
    // LVGL waits for the prior flush before calling this again. The other buffer
    // can be rendered while DMA owns this buffer; the ISR releases it.
    ESP_ERROR_CHECK(draw(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels));
}

static void round_area(lv_event_t *event)
{
    lv_area_t *area = lv_event_get_param(event);
    // Even width/height keeps RGB565 DMA chunks 4-byte aligned.
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void draw_test_screen(const void *buffer)
{
    for (int y = 0; y < LCD_HEIGHT; y += LCD_BUFFER_LINES) {
        int end = y + LCD_BUFFER_LINES < LCD_HEIGHT ? y + LCD_BUFFER_LINES : LCD_HEIGHT;
        ESP_ERROR_CHECK(draw(0, y, LCD_WIDTH, end, buffer));
        if (xSemaphoreTake(startup_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE("display", "Test screen DMA timed out at row %d", y);
            ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
        }
    }
}

// Quad pixel writes put byte bits 4/0 on SIO0, 5/1 on SIO1, 6/2 on SIO2 and 7/3 on
// SIO3 (datasheet p.21). Each line alone passed with static levels (0x11 << n).
// Byte ((1 << n) - 1) << 4 toggles D0..D(n-1) together on every clock, so the
// first failing step shows how many simultaneously switching lines break QSPI.
static void test_switching_lines(uint8_t *buffer)
{
    size_t half = LCD_WIDTH * LCD_BUFFER_LINES; // Bytes in half the buffer's rows.
    for (int lines = 1; lines <= 4; lines++) {
        ESP_LOGI("display", "Switching test: D0-D%d toggle every clock, stripes on black (3 seconds)",
                 lines - 1);
        memset(buffer, ((1 << lines) - 1) << 4, half);
        memset(buffer + half, 0, half);
        draw_test_screen(buffer);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}

static void test_solid_colors(uint16_t *buffer)
{
    static const uint16_t colors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    static const char *names[] = {"RED", "GREEN", "BLUE", "WHITE", "BLACK"};
    for (size_t c = 0; c < sizeof(colors) / sizeof(colors[0]); c++) {
        ESP_LOGI("display", "Solid color test: %s (2 seconds)", names[c]);
        uint16_t swapped = __builtin_bswap16(colors[c]);
        for (int i = 0; i < LCD_WIDTH * LCD_BUFFER_LINES; i++) buffer[i] = swapped;
        draw_test_screen(buffer);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}

lv_display_t *display_init(void)
{
    lv_init();
    lv_tick_set_cb(tick_ms);
    lv_display_t *display = lv_display_create(LCD_WIDTH, LCD_HEIGHT);
    assert(display);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    size_t buffer_size = LCD_WIDTH * LCD_BUFFER_LINES * sizeof(uint16_t);
    void *first = heap_caps_aligned_alloc(4, buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    void *second = heap_caps_aligned_alloc(4, buffer_size, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(first && second);
    lv_display_set_buffers(display, first, second, buffer_size, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    lv_display_add_event_cb(display, round_area, LV_EVENT_INVALIDATE_AREA, NULL);
    lv_timer_set_period(lv_display_get_refr_timer(display), 16);

#if LCD_SINGLE_WIRE
    spi_bus_config_t bus = CO5300_PANEL_BUS_SPI_CONFIG(LCD_PIN_SCK, LCD_PIN_D0, buffer_size);
#else
    spi_bus_config_t bus = CO5300_PANEL_BUS_QSPI_CONFIG(LCD_PIN_SCK, LCD_PIN_D0,
        LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3, buffer_size);
#endif
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    // Slower edges for the ~8 cm Dupont wiring. Verified with the panel GND next to
    // GPIO7 at 1 MHz; at 40 MHz neither cap 0 nor cap 2 displayed correctly.
    static const int lcd_pins[] = {LCD_PIN_SCK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3};
    for (size_t i = 0; i < sizeof(lcd_pins) / sizeof(lcd_pins[0]); i++) {
        ESP_ERROR_CHECK(gpio_set_drive_capability(lcd_pins[i], LCD_DRIVE_CAP));
    }
    esp_lcd_panel_io_spi_config_t io_config =
        CO5300_PANEL_IO_QSPI_CONFIG(LCD_PIN_CS, transfer_done, display);
    io_config.pclk_hz = LCD_QSPI_HZ;
    // CO5300 datasheet p.20: SCL idles high, sampled on rising edge (mode 3), as in
    // the vendor example; the official macro's mode 0 does not drive this panel.
    io_config.spi_mode = 3;
    io_config.trans_queue_depth = 2;
    io_config.flags.quad_mode = !LCD_SINGLE_WIRE;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_SPI_HOST, &io_config, &io));
    const co5300_vendor_config_t vendor = {
        .init_cmds = init_commands,
        .init_cmds_size = sizeof(init_commands) / sizeof(init_commands[0]),
        .flags.use_qspi_interface = true,
    };
    const esp_lcd_panel_dev_config_t config = {
        .reset_gpio_num = LCD_PIN_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_ERROR_CHECK(esp_lcd_new_panel_co5300(io, &config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    // Driver waits 80 ms. CO5300 requires >=120 ms from SWRESET in Sleep Out
    // to the next SLPOUT; adding 70 ms gives >=150 ms even on a warm MCU reset.
    vTaskDelay(pdMS_TO_TICKS(70));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    startup_done = xSemaphoreCreateBinary();
    assert(startup_done);
    if (!LCD_SINGLE_WIRE) test_switching_lines(first);
    test_solid_colors(first);
    vSemaphoreDelete(startup_done);
    startup_done = NULL; // All transfers completed before LVGL starts using the buffers.
    sample_start_us = esp_timer_get_time();
    ESP_LOGI("display", "%s %d MHz, drive cap %d, %dx%d RGB565, two %u-byte internal DMA buffers",
             LCD_SINGLE_WIRE ? "1-wire SPI" : "QSPI", LCD_QSPI_HZ / 1000000, LCD_DRIVE_CAP, LCD_WIDTH, LCD_HEIGHT, (unsigned)buffer_size);
    return display;
}

display_stats_t display_take_stats(void)
{
    int64_t now = esp_timer_get_time();
    portENTER_CRITICAL(&stats_lock);
    uint32_t frames = completed_frames, bytes = completed_bytes;
    int64_t total = frame_total_us, maximum = frame_max_us;
    completed_frames = completed_bytes = 0;
    frame_total_us = frame_max_us = 0;
    portEXIT_CRITICAL(&stats_lock);
    double seconds = (now - sample_start_us) / 1000000.0;
    sample_start_us = now;
    return (display_stats_t) {
        .fps = seconds > 0 ? frames / seconds : 0,
        .megabytes_per_second = seconds > 0 ? bytes / seconds / 1000000.0 : 0,
        .full_frame_fps = seconds > 0 ? bytes / seconds / (LCD_WIDTH * LCD_HEIGHT * 2.0) : 0,
        .average_ms = frames ? total / (frames * 1000.0) : 0,
        .maximum_ms = maximum / 1000.0,
    };
}
