// Waveshare ESP32-S3-RLCD-4.2 屏幕驱动（ST7305，单色 1bpp）
// 初始化序列和像素排布照搬官方示例 09_LVGL_V9_Test/components/port_bsp/display_bsp.cpp
#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_lcd_io_spi.h"
#include "esp_lcd_panel_io.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "rlcd.h"

#define PIN_DC 5
#define PIN_SCL 11
#define PIN_SDA 12
#define PIN_CS 40
#define PIN_RST 41
#define SPI_HOST SPI3_HOST
#define PCLK_HZ (10 * 1000 * 1000)

#define FB_LEN (RLCD_W * RLCD_H / 8)
// 灰度 ≥ 阈值才算白。字体是 4bpp 抗锯齿，14 px 汉字细笔画边缘多为浅灰，阈值取 128 会把笔画断掉
#define WHITE_THRESHOLD 192

static esp_lcd_panel_io_handle_t io;
static uint8_t *fb;
static SemaphoreHandle_t tx_done;

static bool on_tx_done(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *ctx)
{
    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(tx_done, &woken);
    return woken;
}

static void cmd(uint8_t c, const uint8_t *data, size_t len)
{
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(io, c, data, len));
}

#define CMD(c, ...) cmd(c, (const uint8_t[]){__VA_ARGS__}, sizeof((const uint8_t[]){__VA_ARGS__}))

static void reset(void)
{
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
    gpio_set_level(PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(50));
}

static void flush(void)
{
    CMD(0x2A, 0x12, 0x2A);  // 列地址
    CMD(0x2B, 0x00, 0xC7);  // 行地址
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_color(io, 0x2C, fb, FB_LEN));
    xSemaphoreTake(tx_done, portMAX_DELAY);  // 等 DMA 发完再改 fb
}

void rlcd_init(void)
{
    spi_bus_config_t bus = {
        .mosi_io_num = PIN_SDA,
        .miso_io_num = -1,
        .sclk_io_num = PIN_SCL,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = FB_LEN,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io_cfg = {
        .dc_gpio_num = PIN_DC,
        .cs_gpio_num = PIN_CS,
        .pclk_hz = PCLK_HZ,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
        .on_color_trans_done = on_tx_done,
    };
    tx_done = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)SPI_HOST, &io_cfg, &io));

    gpio_config_t rst = {
        .pin_bit_mask = 1ULL << PIN_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_ERROR_CHECK(gpio_config(&rst));

    fb = heap_caps_malloc(FB_LEN, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    assert(fb);

    reset();
    CMD(0xD6, 0x17, 0x02);  // NVM Load Control
    CMD(0xD1, 0x01);        // Booster Enable
    CMD(0xC0, 0x11, 0x04);  // Gate Voltage Control
    CMD(0xC1, 0x69, 0x69, 0x69, 0x69);  // VSHP
    CMD(0xC2, 0x19, 0x19, 0x19, 0x19);  // VSLP
    CMD(0xC4, 0x4B, 0x4B, 0x4B, 0x4B);  // VSHN
    CMD(0xC5, 0x19, 0x19, 0x19, 0x19);  // VSLN
    CMD(0xD8, 0x80, 0xE9);
    CMD(0xB2, 0x02);  // 帧率
    CMD(0xB3, 0xE5, 0xF6, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45);
    CMD(0xB4, 0x05, 0x46, 0x77, 0x77, 0x77, 0x77, 0x76, 0x45);
    CMD(0x62, 0x32, 0x03, 0x1F);
    CMD(0xB7, 0x13);
    CMD(0xB0, 0x64);
    cmd(0x11, NULL, 0);  // Sleep Out
    vTaskDelay(pdMS_TO_TICKS(200));
    CMD(0xC9, 0x00);
    CMD(0x36, 0x48);  // MADCTL
    CMD(0x3A, 0x11);  // 数据格式
    CMD(0xB9, 0x20);  // Gamma：单色
    CMD(0xB8, 0x29);
    cmd(0x21, NULL, 0);  // 反色
    CMD(0x2A, 0x12, 0x2A);
    CMD(0x2B, 0x00, 0xC7);
    CMD(0x35, 0x00);  // TE
    CMD(0xD0, 0xFF);
    cmd(0x38, NULL, 0);
    cmd(0x29, NULL, 0);  // Display On

    memset(fb, 0xFF, FB_LEN);  // bit 1 = 白
    flush();
}

void rlcd_draw_l8(const uint8_t *px)
{
    // 横屏排布：每字节 2 列 × 4 行，y 方向翻转
    const int h4 = RLCD_H / 4;
    memset(fb, 0, FB_LEN);
    for (int y = 0; y < RLCD_H; y++) {
        int inv_y = RLCD_H - 1 - y;
        int block_y = inv_y >> 2, local_y = inv_y & 3;
        for (int x = 0; x < RLCD_W; x++) {
            if (px[y * RLCD_W + x] >= WHITE_THRESHOLD) {
                int bit = 7 - ((local_y << 1) | (x & 1));
                fb[(x >> 1) * h4 + block_y] |= 1 << bit;
            }
        }
    }
    flush();
}
