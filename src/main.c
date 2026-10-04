#include "driver/gpio.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"

#include "ble_scan.h"
#include "devices.h"
#include "ui.h"
#include "wifi_scan.h"

// 按键按下为低
#define BOOT_GPIO GPIO_NUM_0  // 短按扫一轮，长按清基线
#define KEY_GPIO GPIO_NUM_18  // BOOT 左侧的 KEY 键：翻页
#define POLL_MS 20            // 按键轮询周期
#define DEBOUNCE_MS 50        // 短于此的按下当作抖动
#define LONG_PRESS_MS 1000
#define IDLE_STATUS "Idle. BOOT: scan, hold: clear base. KEY: page"

static const char *TAG = "main";

static void print_help(void)
{
    ESP_LOGI(TAG, "BOOT short = scan one round (first round = baseline), BOOT hold 1 s = clear baseline, KEY = next page");
    ESP_LOGI(TAG, "serial: s = scan, c = clear baseline, p = next page");
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ui_init();
    ui_set_status("Init radio...");
    wifi_scan_init();
    ble_scan_init();

    // 串口命令（调试用，和按键并存）：日志改走驱动，输入输出共用 USB Serial/JTAG
    usb_serial_jtag_driver_config_t usb_cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(usb_serial_jtag_driver_install(&usb_cfg));
    usb_serial_jtag_vfs_use_driver();

    gpio_config_t key_cfg = {
        .pin_bit_mask = 1ULL << BOOT_GPIO | 1ULL << KEY_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&key_cfg));

    print_help();
    ui_set_status(IDLE_STATUS);
    int boot_prev = 1, key_prev = 1;
    TickType_t boot_down = 0;
    for (;;) {
        // 按键和串口都在主任务里处理，不会和扫描同时访问设备表。按键统一转成串口命令字符
        char cmd = 0;
        int boot = gpio_get_level(BOOT_GPIO), key = gpio_get_level(KEY_GPIO);
        if (boot_prev && !boot) {
            boot_down = xTaskGetTickCount();
        } else if (!boot_prev && boot) {  // 松开时按按下时长区分短按 / 长按
            uint32_t held_ms = pdTICKS_TO_MS(xTaskGetTickCount() - boot_down);
            cmd = held_ms < DEBOUNCE_MS ? 0 : held_ms < LONG_PRESS_MS ? 's' : 'c';
        }
        if (key_prev && !key) {
            cmd = 'p';
        }
        boot_prev = boot;
        key_prev = key;

        if (!cmd && usb_serial_jtag_read_bytes(&cmd, 1, pdMS_TO_TICKS(POLL_MS)) != 1) {
            continue;
        }
        switch (cmd) {
        case 's':
            devices_begin_round();
            ESP_LOGI(TAG, "scanning Wi-Fi...");
            ui_set_status("Scanning Wi-Fi...");
            wifi_scan_run();
            ESP_LOGI(TAG, "scanning BLE...");
            ui_set_status("Scanning BLE...");
            ble_scan_run();
            devices_end_round();
            ui_show_report();
            ui_set_status(IDLE_STATUS);
            break;
        case 'p':
            ui_next_page();
            break;
        case 'c':
            devices_clear_baseline();
            ESP_LOGI(TAG, "baseline cleared, next round becomes the new baseline");
            ui_set_status("Baseline cleared. BOOT: scan new baseline");
            break;
        case '\r':
        case '\n':
            break;
        default:
            print_help();
        }
    }
}
