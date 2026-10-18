#include <assert.h>
#include <stdint.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"

// CO5300 QSPI framing (datasheet p.20-21): 8-bit opcode and 24-bit address
// {0x00, cmd, 0x00} on SIO0, then parameters (1-wire) or pixels (opcode 0x32, 4-wire).
#define OPCODE_COMMAND 0x02
#define OPCODE_PIXELS_QUAD 0x32
#define CMD_CASET 0x2A
#define CMD_RASET 0x2B
#define CMD_RAMWR 0x2C
#define CMD_COLMOD 0x3A
#define MAX_PIXEL_BYTES 32768 // SPI_LL_DMA_MAX_BIT_LEN / 8 on ESP32-S3.
#define SLOT_COUNT 48 // Queued SPI transactions: three per band (CASET, RASET, RAMWR).

typedef struct {
    uint8_t cmd;
    uint8_t data[4];
    uint8_t length;
    uint16_t delay_ms;
} init_command_t;

typedef struct {
    spi_transaction_t trans; // First member: post_cb receives this pointer.
    display_band_done_cb_t done;
    void *context;
    uint32_t pixel_bytes;
    int64_t frame_start_us; // Set on a frame's last band only; 0 otherwise.
} slot_t;

// Vendor sequence verbatim (esp32_lcd_qspi_lvgl esp_lcd_tk018.c), verified on this
// panel. CE/D8 are undocumented in the CO5300 datasheet. MADCTL 36h keeps RGB order.
static const init_command_t init_commands[] = {
    {0xFE, {0x00}, 1, 0},
    {0xCE, {0x0D, 0x00}, 2, 0},
    {0xD8, {0x08, 0x00}, 2, 0},
    {0x11, {0}, 0, 120},
    {0xFE, {0x00}, 1, 0},
    {0xC4, {0x80}, 1, 0},
    {0x36, {0x00}, 1, 0},
    {CMD_COLMOD, {0x55}, 1, 0},
    {0x53, {0x20}, 1, 0},
    {0x63, {0xFF}, 1, 0},
    {0x51, {0x30}, 1, 0},
    {0x58, {0x07}, 1, 0}, // Color enhancement (datasheet 58h).
    {0x29, {0}, 0, 80},
};

static spi_device_handle_t device;
static slot_t slots[SLOT_COUNT];
static unsigned next_slot;
static SemaphoreHandle_t free_slots;
static volatile uint32_t pending;
static display_format_t format = DISPLAY_RGB565;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static uint32_t completed_frames, completed_bytes;
static int64_t frame_total_us, frame_max_us, sample_start_us, open_frame_start_us;

static IRAM_ATTR void transfer_done(spi_transaction_t *trans)
{
    slot_t *slot = (slot_t *)trans;
    portENTER_CRITICAL_ISR(&stats_lock);
    completed_bytes += slot->pixel_bytes;
    if (slot->frame_start_us) {
        int64_t elapsed = esp_timer_get_time() - slot->frame_start_us;
        completed_frames++;
        frame_total_us += elapsed;
        if (elapsed > frame_max_us) frame_max_us = elapsed;
    }
    pending--;
    portEXIT_CRITICAL_ISR(&stats_lock);
    if (slot->done) slot->done(slot->context);
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(free_slots, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// Transactions complete in queue order, so the ring slot at next_slot is free
// whenever the semaphore grants one. Only one task queues.
static slot_t *take_slot(void)
{
    xSemaphoreTake(free_slots, portMAX_DELAY);
    slot_t *slot = &slots[next_slot];
    next_slot = (next_slot + 1) % SLOT_COUNT;
    *slot = (slot_t){0};
    return slot;
}

static void queue(slot_t *slot)
{
    portENTER_CRITICAL(&stats_lock);
    pending++;
    portEXIT_CRITICAL(&stats_lock);
    ESP_ERROR_CHECK(spi_device_queue_trans(device, &slot->trans, portMAX_DELAY));
}

static void send_command(uint8_t cmd, const uint8_t *data, size_t length)
{
    assert(length <= 4);
    slot_t *slot = take_slot();
    slot->trans.cmd = OPCODE_COMMAND;
    slot->trans.addr = cmd << 8;
    slot->trans.flags = length ? SPI_TRANS_USE_TXDATA : 0; // No data phase otherwise.
    slot->trans.length = length * 8;
    for (size_t i = 0; i < length; i++) slot->trans.tx_data[i] = data[i];
    queue(slot);
}

static void wait_idle(void)
{
    while (pending) vTaskDelay(1);
}

void display_queue_band(int x1, int y1, int x2, int y2, const void *pixels, bool frame_last,
                        display_band_done_cb_t done, void *context)
{
    if (!open_frame_start_us) open_frame_start_us = esp_timer_get_time();
    size_t row_bytes = (x2 - x1) * (format == DISPLAY_RGB332 ? 1 : 2);
    // One DMA transaction carries at most 2^18 bits (SPI_LL_DMA_MAX_BIT_LEN). Split by
    // rows, each with its own window, so no continuation-write command is needed.
    int rows_per_chunk = MAX_PIXEL_BYTES / row_bytes;
    const uint8_t *data = pixels;
    for (int y = y1; y < y2; y += rows_per_chunk) {
        int end = y + rows_per_chunk < y2 ? y + rows_per_chunk : y2;
        bool last = end == y2;
        send_command(CMD_CASET, (uint8_t[]){x1 >> 8, x1 & 0xFF, (x2 - 1) >> 8, (x2 - 1) & 0xFF}, 4);
        send_command(CMD_RASET, (uint8_t[]){y >> 8, y & 0xFF, (end - 1) >> 8, (end - 1) & 0xFF}, 4);
        slot_t *slot = take_slot();
        slot->pixel_bytes = (end - y) * row_bytes;
        slot->trans.cmd = LCD_SINGLE_WIRE ? OPCODE_COMMAND : OPCODE_PIXELS_QUAD;
        slot->trans.addr = CMD_RAMWR << 8;
        slot->trans.flags = LCD_SINGLE_WIRE ? 0 : SPI_TRANS_MODE_QIO;
        slot->trans.length = slot->pixel_bytes * 8;
        slot->trans.tx_buffer = data;
        if (last) {
            slot->done = done;
            slot->context = context;
        }
        if (last && frame_last) {
            slot->frame_start_us = open_frame_start_us;
            open_frame_start_us = 0;
        }
        queue(slot);
        data += slot->pixel_bytes;
    }
}

void display_set_format(display_format_t new_format)
{
    format = new_format;
    // IFPF[2:0] (datasheet p.176): 101 = 16 bpp, 010 = RGB332. Public command.
    send_command(CMD_COLMOD, (uint8_t[]){format == DISPLAY_RGB332 ? 0x22 : 0x55}, 1);
}

display_format_t display_get_format(void)
{
    return format;
}

static void flush_done(void *context)
{
    // LVGL explicitly permits flush_ready from an ISR. No other LVGL work here.
    lv_display_flush_ready(context);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t count = lv_area_get_size(area);
    if (format == DISPLAY_RGB332) {
        const uint16_t *in = (const uint16_t *)pixels;
        for (size_t i = 0; i < count; i++) { // In place: byte i never overtakes pixel i.
            uint16_t c = in[i];
            pixels[i] = (c >> 13) << 5 | ((c >> 8) & 0x7) << 2 | ((c >> 3) & 0x3);
        }
    } else {
        lv_draw_sw_rgb565_swap(pixels, count); // CO5300 consumes big-endian RGB565.
    }
    display_queue_band(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels,
                       lv_display_flush_is_last(display), flush_done, display);
}

static void round_area(lv_event_t *event)
{
    lv_area_t *area = lv_event_get_param(event);
    // Even width/height keeps pixel DMA chunks 4-byte aligned.
    area->x1 &= ~1;
    area->y1 &= ~1;
    area->x2 |= 1;
    area->y2 |= 1;
}

static uint32_t tick_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static uint16_t test_pixel(int x, int y)
{
    static const uint16_t bars[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000};
    if (y < LCD_HEIGHT * 2 / 5) return bars[x * 5 / LCD_WIDTH];
    // Smooth ramps: every pixel differs from its neighbour, so any bit error from
    // marginal QSPI timing shows as speckles instead of hiding in a regular pattern.
    int band = (y - LCD_HEIGHT * 2 / 5) * 4 / (LCD_HEIGHT - LCD_HEIGHT * 2 / 5);
    uint16_t r = x * 31 / (LCD_WIDTH - 1), g = x * 63 / (LCD_WIDTH - 1);
    switch (band) {
    case 0: return r << 11;
    case 1: return g << 5;
    case 2: return r;
    default: return r << 11 | g << 5 | r;
    }
}

static void give_semaphore(void *context)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(context, &woken);
    if (woken) portYIELD_FROM_ISR();
}

// Single startup screen: R/G/B/W/K bars on top, red/green/blue/gray ramps below.
static void test_pattern(uint16_t *buffer)
{
    SemaphoreHandle_t band_done = xSemaphoreCreateBinary();
    assert(band_done);
    ESP_LOGI("display", "Test pattern: color bars + RGB/gray ramps (3 seconds)");
    for (int y = 0; y < LCD_HEIGHT; y += LCD_BUFFER_LINES) {
        int end = y + LCD_BUFFER_LINES < LCD_HEIGHT ? y + LCD_BUFFER_LINES : LCD_HEIGHT;
        for (int row = y; row < end; row++) {
            for (int x = 0; x < LCD_WIDTH; x++) {
                buffer[(row - y) * LCD_WIDTH + x] = __builtin_bswap16(test_pixel(x, row));
            }
        }
        display_queue_band(0, y, LCD_WIDTH, end, buffer, false, give_semaphore, band_done);
        if (xSemaphoreTake(band_done, pdMS_TO_TICKS(1000)) != pdTRUE) {
            ESP_LOGE("display", "Test pattern DMA timed out at row %d", y);
            ESP_ERROR_CHECK(ESP_ERR_TIMEOUT);
        }
    }
    vSemaphoreDelete(band_done);
    vTaskDelay(pdMS_TO_TICKS(3000));
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

    spi_bus_config_t bus = {
        .sclk_io_num = LCD_PIN_SCK,
        .data0_io_num = LCD_PIN_D0,
        .data1_io_num = LCD_SINGLE_WIRE ? -1 : LCD_PIN_D1,
        .data2_io_num = LCD_SINGLE_WIRE ? -1 : LCD_PIN_D2,
        .data3_io_num = LCD_SINGLE_WIRE ? -1 : LCD_PIN_D3,
        .max_transfer_sz = MAX_PIXEL_BYTES,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO));
    // Edge rate vs. ground bounce on the ~8 cm cable, whose QSPI bundle has no GND
    // wire. Cap 0 verified up to 40 MHz with the bundled cable and shield wire.
    static const int lcd_pins[] = {LCD_PIN_SCK, LCD_PIN_D0, LCD_PIN_D1, LCD_PIN_D2, LCD_PIN_D3};
    for (size_t i = 0; i < sizeof(lcd_pins) / sizeof(lcd_pins[0]); i++) {
        ESP_ERROR_CHECK(gpio_set_drive_capability(lcd_pins[i], LCD_DRIVE_CAP));
    }
    free_slots = xSemaphoreCreateCounting(SLOT_COUNT, SLOT_COUNT);
    assert(free_slots);
    spi_device_interface_config_t device_config = {
        .command_bits = 8,
        .address_bits = 24,
        // CO5300 datasheet p.20: SCL idles high, sampled on rising edge (mode 3).
        .mode = 3,
        .clock_speed_hz = LCD_QSPI_HZ,
        .spics_io_num = LCD_PIN_CS,
        .flags = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_RETURN_RESULT,
        .queue_size = SLOT_COUNT,
        .post_cb = transfer_done,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &device_config, &device));

    // Software reset: CO5300 needs >=120 ms before the next SLPOUT (datasheet p.127).
    send_command(0x01, NULL, 0);
    wait_idle();
    vTaskDelay(pdMS_TO_TICKS(150));
    for (size_t i = 0; i < sizeof(init_commands) / sizeof(init_commands[0]); i++) {
        send_command(init_commands[i].cmd, init_commands[i].data, init_commands[i].length);
        wait_idle();
        vTaskDelay(pdMS_TO_TICKS(init_commands[i].delay_ms));
    }
    test_pattern(first);
    sample_start_us = esp_timer_get_time();
    ESP_LOGI("display", "%s %d MHz direct SPI, drive cap %d, %dx%d, two %u-byte internal DMA buffers",
             LCD_SINGLE_WIRE ? "1-wire SPI" : "QSPI", LCD_QSPI_HZ / 1000000, LCD_DRIVE_CAP,
             LCD_WIDTH, LCD_HEIGHT, (unsigned)buffer_size);
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
    double bytes_per_second = seconds > 0 ? bytes / seconds : 0;
    return (display_stats_t) {
        .fps = seconds > 0 ? frames / seconds : 0,
        .megabytes_per_second = bytes_per_second / 1000000.0,
        .bus_percent = bytes_per_second / (LCD_QSPI_HZ / 2.0) * 100.0, // 4 bits per clock.
        .average_ms = frames ? total / (frames * 1000.0) : 0,
        .maximum_ms = maximum / 1000.0,
    };
}
