#include "driver/gpio.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "radar.h"
#include "ui.h"

#define BOOT_PIN GPIO_NUM_0

static radar_tier_t tier = RADAR_TIER_10;
static bool settling = true;

static void apply_tier(void)
{
    radar_set_tier(tier);
    ui_set_tier(tier);
    settling = true; // First stats window after a switch mixes both tiers.
    ESP_LOGI("benchmark", "tier: %d%%", radar_tier_percent(tier));
}

// Each press selects the next tier. Polled from the display loop; the 30 ms
// re-arm delay is longer than any contact bounce.
static void poll_boot(void)
{
    static int64_t released_at;
    static bool down;
    int64_t now = esp_timer_get_time();
    bool level_down = gpio_get_level(BOOT_PIN) == 0;
    if (level_down && !down && now - released_at > 30000) {
        down = true;
    } else if (!level_down && down) {
        down = false;
        released_at = now;
        tier = (tier + 1) % RADAR_TIER_COUNT;
        apply_tier();
    }
}

static void report(void)
{
    display_stats_t display = display_take_stats();
    radar_stats_t radar = radar_take_stats();
    if (settling) {
        settling = false;
        return;
    }
    ui_show_stats(&display, &radar);
    ESP_LOGI("benchmark", "%d%%: %.1f FPS, %.2f MB/s, bus %.0f%%, frame %.2f/%.2f ms, draw %.2f ms",
             radar_tier_percent(tier),
             (double)display.fps, (double)display.megabytes_per_second, (double)display.bus_percent,
             (double)display.average_ms, (double)display.maximum_ms, (double)radar.render_ms);
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
    gpio_config_t boot = {.pin_bit_mask = 1ULL << BOOT_PIN, .mode = GPIO_MODE_INPUT, .pull_up_en = 1};
    ESP_ERROR_CHECK(gpio_config(&boot));
    lv_display_t *display = display_init();
    ui_init(display);
    lv_refr_now(display); // Paint the black screen and text before the radar draws.
    radar_init();
    apply_tier();
    // Display task (core 0): radar bands, LVGL text and stats share one SPI queue.
    int64_t last_report = esp_timer_get_time();
    for (;;) {
        radar_pump(5);
        poll_boot();
        lv_timer_handler();
        int64_t now = esp_timer_get_time();
        if (now - last_report >= 1000000) {
            last_report = now;
            report();
        }
    }
}
