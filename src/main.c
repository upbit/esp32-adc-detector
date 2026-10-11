#include <math.h>

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "led_strip.h"

#define LED_GPIO GPIO_NUM_27
#define BOOT_GPIO GPIO_NUM_28
#define FRAME_MS 10
#define DEBOUNCE_US 50000
#define PERIOD_US 2000000
#define MAX_BRIGHTNESS 48

void app_main(void)
{
    led_strip_handle_t led;
    led_strip_config_t led_cfg = {
        .strip_gpio_num = LED_GPIO,
        .max_leds = 1,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
    };
    led_strip_rmt_config_t rmt_cfg = {.resolution_hz = 10000000};
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&led_cfg, &rmt_cfg, &led));

    gpio_config_t boot_cfg = {
        .pin_bit_mask = 1ULL << BOOT_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&boot_cfg));

    uint32_t hue = 0, saturation = 0;
    int stable = 1, previous = 1;
    int64_t started_at = esp_timer_get_time(), changed_at = started_at;
    ESP_LOGI("led", "White breathing LED on GPIO27; BOOT on GPIO28");

    for (;;) {
        int64_t now = esp_timer_get_time();
        int level = gpio_get_level(BOOT_GPIO);
        if (level != previous) {
            previous = level;
            changed_at = now;
        }
        if (level != stable && now - changed_at >= DEBOUNCE_US) {
            stable = level;
            if (!stable) {
                hue = (hue + 1 + esp_random() % 359) % 360;
                saturation = 255;
                ESP_LOGI("led", "BOOT: hue=%lu", (unsigned long)hue);
            }
        }

        float phase = (now - started_at) % PERIOD_US / (float)PERIOD_US;
        float brightness = (1.0f + cosf(6.2831853f * phase)) * 0.5f;
        uint32_t value = lroundf(MAX_BRIGHTNESS * brightness * brightness);
        ESP_ERROR_CHECK(led_strip_set_pixel_hsv(led, 0, hue, saturation, value));
        ESP_ERROR_CHECK(led_strip_refresh(led));
        vTaskDelay(pdMS_TO_TICKS(FRAME_MS));
    }
}
