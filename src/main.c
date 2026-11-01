#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "battery.h"
#include "display.h"
#include "ina226.h"
#include "touch.h"
#include "ui.h"
#include "network/wifi.h"
#include "time/clock.h"

static battery_monitor_t battery_monitor;

static void report_stack(void)
{
    static UBaseType_t previous_minimum = UINT32_MAX;
    // ESP-IDF reports the high-water mark in bytes, not StackType_t units.
    UBaseType_t minimum = uxTaskGetStackHighWaterMark(NULL);
    if (minimum < previous_minimum) {
        previous_minimum = minimum;
        ESP_LOGI("main", "Main stack minimum free: %u bytes", (unsigned)minimum);
    }
}

static void log_touch(void)
{
    static touch_status_t previous = TOUCH_STATUS_UP;
    touch_status_t status;
    touch_tap_t tap;
    touch_poll(&status, &tap); // The driver logs recognized swipes.
    if (tap.detected) {
        int64_t now = esp_timer_get_time();
        ui_action_t action = ui_handle_network_tap(tap.x, tap.y, now);
        if (action == UI_ACTION_OPEN_WIFI) wifi_open_portal(now);
        else if (action == UI_ACTION_CANCEL_WIFI) wifi_cancel(now);
        else if (action == UI_ACTION_SYNC_TIME) wifi_sync_time(now);
        else ui_handle_tap(tap.x, tap.y);
    }
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
    battery_update(&battery_monitor, err == ESP_OK ? &sample : NULL, esp_timer_get_time());
    ui_show_battery(&battery_monitor.reading);
    if (err != ESP_OK) {
        ESP_LOGW("battery", "INA226 read failed: %s; sample omitted (check calibration / range / wiring)",
                 esp_err_to_name(err));
        return;
    }
    // raw undoes the gain only to collect linearity points; remove once calibration is settled.
    ESP_LOGI("battery", "VBUS=%.3f V (raw %.4f), I=%+.2f mA (+ charge / - discharge)",
             (double)sample.voltage_v, (double)(sample.voltage_v / INA226_BUS_VOLTAGE_GAIN), (double)(sample.current_a * 1000.0f));
    ESP_LOGI("battery", "SOC~%d%% state=%d, session=%.4f Wh / %.2f mAh",
             battery_monitor.reading.percent, battery_monitor.reading.state,
             battery_monitor.reading.session_wh, battery_monitor.reading.session_mah);
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
    battery_init(&battery_monitor);
    ui_init(display);
    lv_refr_now(display);
    esp_err_t touch_err = touch_init();
    if (touch_err != ESP_OK) {
        ESP_LOGW("main", "Touch disabled: %s (SCL GPIO%d / SDA GPIO%d); display remains available",
                 esp_err_to_name(touch_err), TOUCH_PIN_SCL, TOUCH_PIN_SDA);
    }
    esp_err_t ina226_err = ina226_init();
    if (ina226_err != ESP_OK) {
        ui_show_battery(&battery_monitor.reading);
        ESP_LOGW("main", "INA226 disabled: %s (0x44, SDA GPIO%d / SCL GPIO%d); display remains available",
                 esp_err_to_name(ina226_err), INA226_PIN_SDA, INA226_PIN_SCL);
    }
    clock_init();
    esp_err_t wifi_err = wifi_init();
    if (wifi_err != ESP_OK) ESP_LOGW("main", "Wi-Fi unavailable: %s; existing display/sampling unaffected", esp_err_to_name(wifi_err));
    int64_t last_report = esp_timer_get_time();
    for (;;) {
        log_touch();
        lv_timer_handler();
        int64_t now = esp_timer_get_time();
        wifi_poll(now);
        wifi_view_t wifi;
        clock_view_t clock;
        wifi_get_view(&wifi);
        clock_get_view(now, &clock);
        ui_show_network(&wifi, &clock);
        if (now - last_report >= 1000000) {
            last_report = now;
            if (ina226_err == ESP_OK) report_battery();
            report_stack();
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}
