#include <stdbool.h>
#include <stdlib.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "touch.h"

#define TOUCH_ADDRESS 0x2E
#define TOUCH_REGISTER 0xE0
#define SWIPE_MIN_PIXELS 48

static i2c_master_dev_handle_t device;
static touch_status_t current_status = TOUCH_STATUS_OFF;

esp_err_t touch_init(void)
{
    i2c_master_bus_handle_t bus;
    i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = TOUCH_PIN_SDA,
        .scl_io_num = TOUCH_PIN_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &bus);
    if (err != ESP_OK) return err;
    // This panel can NACK its first probe, then ACK the same address milliseconds later.
    for (int attempt = 0; attempt < 3; attempt++) {
        err = i2c_master_probe(bus, TOUCH_ADDRESS, 25);
        if (err != ESP_ERR_NOT_FOUND) break;
        if (attempt < 2) {
            ESP_LOGW("touch", "0x2E did not ACK; retrying in 20 ms");
            vTaskDelay(pdMS_TO_TICKS(20));
        }
    }
    if (err == ESP_OK) {
        i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = TOUCH_ADDRESS,
            .scl_speed_hz = 100000,
        };
        err = i2c_master_bus_add_device(bus, &config, &device);
    }
    if (err != ESP_OK) {
        ESP_LOGW("touch", "Init failed: %s; SCL=%d SDA=%d (idle levels should both be 1)",
                 esp_err_to_name(err), gpio_get_level(TOUCH_PIN_SCL), gpio_get_level(TOUCH_PIN_SDA));
        int found = 0, nack = 0, timeout = 0;
        for (int address = 0x08; address <= 0x77; address++) {
            esp_err_t probe = i2c_master_probe(bus, address, 25);
            if (probe == ESP_OK) {
                ESP_LOGW("touch", "I2C device responds at 0x%02X", address);
                found++;
            } else if (probe == ESP_ERR_NOT_FOUND) {
                nack++;
            } else {
                timeout++;
            }
        }
        ESP_LOGW("touch", "I2C scan: %d found, %d NACK, %d other errors; SCL=%d SDA=%d",
                 found, nack, timeout, gpio_get_level(TOUCH_PIN_SCL), gpio_get_level(TOUCH_PIN_SDA));
        i2c_del_master_bus(bus);
        return err;
    }
    ESP_LOGI("touch", "CHSC6417 at 0x2E, SDA=%d SCL=%d; swipe then release to change tier",
             TOUCH_PIN_SDA, TOUCH_PIN_SCL);
    current_status = TOUCH_STATUS_UP;
    return ESP_OK;
}

touch_swipe_t touch_poll(touch_status_t *status)
{
    static int64_t last_poll;
    static bool down, cancelled, read_failed;
    static int start_x, start_y, last_x, last_y;
    int64_t now = esp_timer_get_time();
    *status = current_status;
    if (!device || now - last_poll < 10000) return TOUCH_SWIPE_NONE;
    last_poll = now;

    const uint8_t reg = TOUCH_REGISTER;
    uint8_t data[3];
    esp_err_t err = i2c_master_transmit_receive(device, &reg, 1, data, sizeof(data), 10);
    if (err != ESP_OK) {
        *status = current_status = TOUCH_STATUS_ERROR;
        if (!read_failed) ESP_LOGW("touch", "Read failed: %s; gesture cancelled", esp_err_to_name(err));
        read_failed = true;
        cancelled = true;
        return TOUCH_SWIPE_NONE;
    }
    read_failed = false;
    int points = data[0] & 0x03;
    *status = current_status = points ? TOUCH_STATUS_DOWN : TOUCH_STATUS_UP;
    if (points) {
        // Packed coordinates from the bundled CHSC6417 vendor driver.
        int x = ((data[0] & 0x40) << 2) | data[1];
        int y = ((data[0] & 0x80) << 1) | data[2];
        if (points != 1 || x >= LCD_WIDTH || y >= LCD_HEIGHT) cancelled = true;
        if (cancelled) return TOUCH_SWIPE_NONE;
        if (!down) {
            start_x = x;
            start_y = y;
            down = true;
        }
        last_x = x;
        last_y = y;
        return TOUCH_SWIPE_NONE;
    }

    touch_swipe_t swipe = TOUCH_SWIPE_NONE;
    if (down && !cancelled) {
        int dx = last_x - start_x, dy = last_y - start_y;
        if (abs(dy) >= SWIPE_MIN_PIXELS && abs(dy) * 2 >= abs(dx) * 3) {
            swipe = dy > 0 ? TOUCH_SWIPE_DOWN : TOUCH_SWIPE_UP;
            ESP_LOGI("touch", "Swipe %s, dx=%d dy=%d", dy > 0 ? "down" : "up", dx, dy);
        }
    }
    down = cancelled = false;
    return swipe;
}
