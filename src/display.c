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
    void (*done)(void *context);
    void *context;
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
static portMUX_TYPE transfer_lock = portMUX_INITIALIZER_UNLOCKED;

static IRAM_ATTR void transfer_done(spi_transaction_t *trans)
{
    slot_t *slot = (slot_t *)trans;
    portENTER_CRITICAL_ISR(&transfer_lock);
    pending--;
    portEXIT_CRITICAL_ISR(&transfer_lock);
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
    portENTER_CRITICAL(&transfer_lock);
    pending++;
    portEXIT_CRITICAL(&transfer_lock);
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

// Only the LVGL flush in the main task submits pixels; done runs in the SPI ISR.
static void queue_band(int x1, int y1, int x2, int y2, const void *pixels,
                       void (*done)(void *context), void *context)
{
    size_t row_bytes = (x2 - x1) * 2;
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
        size_t pixel_bytes = (end - y) * row_bytes;
        slot->trans.cmd = LCD_SINGLE_WIRE ? OPCODE_COMMAND : OPCODE_PIXELS_QUAD;
        slot->trans.addr = CMD_RAMWR << 8;
        slot->trans.flags = LCD_SINGLE_WIRE ? 0 : SPI_TRANS_MODE_QIO;
        slot->trans.length = pixel_bytes * 8;
        slot->trans.tx_buffer = data;
        if (last) {
            slot->done = done;
            slot->context = context;
        }
        queue(slot);
        data += pixel_bytes;
    }
}

static void flush_done(void *context)
{
    // LVGL explicitly permits flush_ready from an ISR. No other LVGL work here.
    lv_display_flush_ready(context);
}

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    size_t count = lv_area_get_size(area);
    lv_draw_sw_rgb565_swap(pixels, count); // CO5300 consumes big-endian RGB565.
    queue_band(area->x1, area->y1, area->x2 + 1, area->y2 + 1, pixels, flush_done, display);
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
    ESP_LOGI("display", "%s %d MHz direct SPI, drive cap %d, %dx%d, two %u-byte internal DMA buffers",
             LCD_SINGLE_WIRE ? "1-wire SPI" : "QSPI", LCD_QSPI_HZ / 1000000, LCD_DRIVE_CAP,
             LCD_WIDTH, LCD_HEIGHT, (unsigned)buffer_size);
    return display;
}
