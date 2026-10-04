#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "host/ble_hs.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"

#include "ble_scan.h"
#include "devices.h"

#define SCAN_MS 5000
#define SCAN_ITVL 160  // 100 ms，单位 0.625 ms

static const char *TAG = "ble";
static SemaphoreHandle_t synced;

static int gap_cb(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_DISC: {
        const struct ble_gap_disc_desc *disc = &event->disc;
        uint8_t mac[6];
        for (int i = 0; i < 6; i++) {
            mac[i] = disc->addr.val[5 - i];  // NimBLE 地址是小端
        }
        device_t *d = devices_update(DEV_BLE, mac, disc->rssi);
        if (!d) {
            break;
        }
        d->random = disc->addr.type != BLE_ADDR_PUBLIC;
        struct ble_hs_adv_fields fields;
        if (ble_hs_adv_parse_fields(&fields, disc->data, disc->length_data) == 0) {
            devices_set_name(d, fields.name, fields.name_len);
        }
        break;
    }
    }
    return 0;
}

static void on_sync(void)
{
    xSemaphoreGive(synced);
}

static void host_task(void *param)
{
    nimble_port_run();
    nimble_port_freertos_deinit();
}

void ble_scan_init(void)
{
    synced = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(nimble_port_init());
    ble_hs_cfg.sync_cb = on_sync;
    nimble_port_freertos_init(host_task);
    xSemaphoreTake(synced, portMAX_DELAY);  // 等 host 与 controller 同步
}

void ble_scan_run(void)
{
    struct ble_gap_disc_params params = {
        .itvl = SCAN_ITVL,
        .window = SCAN_ITVL,  // 窗口等于间隔：连续扫描
        .passive = 1,         // 不发 scan request
    };
    // 只开 Observer 时 NimBLE 的 host 定时器不处理 GAP 超时，扫描不会自己结束，
    // 所以用 FOREVER 开始，到时间后自己取消
    int rc = ble_gap_disc(BLE_OWN_ADDR_PUBLIC, BLE_HS_FOREVER, &params, gap_cb, NULL);
    if (rc) {
        ESP_LOGE(TAG, "ble_gap_disc failed: %d", rc);
        return;
    }
    vTaskDelay(pdMS_TO_TICKS(SCAN_MS));
    ble_gap_disc_cancel();
}
