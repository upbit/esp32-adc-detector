#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "display.h"
#include "ina226.h"
#include "touch.h"
#include "ui.h"

static void log_touch(void)
{
    static touch_status_t previous = TOUCH_STATUS_UP;
    touch_status_t status;
    touch_poll(&status); // The driver logs recognized swipes.
    // Compare valid contacts only; recovery from an I2C error is not a release.
    if (status == TOUCH_STATUS_ERROR || status == TOUCH_STATUS_OFF) return;
    if (status != previous) {
        if (status == TOUCH_STATUS_DOWN) ESP_LOGI("touch", "Contact down");
        if (status == TOUCH_STATUS_UP) ESP_LOGI("touch", "Contact up");
        previous = status;
    }
}

static void report_battery(void)
{
    ina226_sample_t sample;
    esp_err_t err = ina226_read(&sample);
    if (err != ESP_OK) {
        ui_show_sample(NULL);
        ESP_LOGW("battery", "INA226 read failed: %s; sample omitted (check calibration / range / wiring)",
                 esp_err_to_name(err));
        return;
    }
    ui_show_sample(&sample);
    // raw undoes the gain only to collect linearity points; remove once calibration is settled.
    ESP_LOGI("battery", "VBUS=%.3f V (raw %.4f), I=%+.2f mA (+ charge / - discharge)",
             (double)sample.voltage_v, (double)(sample.voltage_v / INA226_BUS_VOLTAGE_GAIN), (double)(sample.current_a * 1000.0f));
}

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));
    ESP_LOGI("main", "ESP32-S3 rev %d, %d cores; flash=%lu MB, PSRAM=%u MB",
             chip.revision, chip.cores, (unsigned long)(flash_size / (1024 * 1024)),
             (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    lv_display_t *display = display_init();
    ui_init(display);
    lv_refr_now(display);
    esp_err_t touch_err = touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW("main", "Touch disabled: %s (SCL GPIO%d / SDA GPIO%d); display remains available",
                 esp_err_to_name(touch_err), TOUCH_PIN_SCL, TOUCH_PIN_SDA);
    }
    esp_err_t ina226_err = ina226_init();
    if (ina226_err != ESP_OK) {
        ui_show_sample(NULL);
        ESP_LOGW("main", "INA226 disabled: %s (0x44, SDA GPIO%d / SCL GPIO%d); display remains available",
                 esp_err_to_name(ina226_err), INA226_PIN_SDA, INA226_PIN_SCL);
    }
    int64_t last_report = esp_timer_get_time();
    for (;;) {
        log_touch();
        lv_timer_handler();
        int64_t now = esp_timer_get_time();
        if (now - last_report >= 1000000) {
            last_report = now;
            if (ina226_err == ESP_OK) report_battery();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
