#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_psram.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "display.h"
#include "ui.h"

void app_main(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);
    uint32_t flash_size;
    ESP_ERROR_CHECK(esp_flash_get_size(NULL, &flash_size));
    ESP_LOGI("main", "ESP32-S3 rev %d, %d cores; flash=%lu MB, PSRAM=%u MB",
             chip.revision, chip.cores, (unsigned long)(flash_size / (1024 * 1024)),
             (unsigned)(esp_psram_get_size() / (1024 * 1024)));
    ESP_LOGI("main", "Internal free heap=%u bytes; AMOLED benchmark, radio/touch disabled",
             (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
    lv_display_t *display = display_init();
    ui_init(display);
    for (;;) {
        uint32_t delay = lv_timer_handler();
        // 1 kHz RTOS tick keeps a 16 ms animation from rounding down to 10 ms.
        vTaskDelay(pdMS_TO_TICKS(delay < 1 ? 1 : delay > 10 ? 10 : delay));
    }
}
