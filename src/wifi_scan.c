#include <string.h>

#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "devices.h"
#include "wifi_scan.h"

#define DWELL_MS 250
#define PASSES 2
#define MAX_CHANNEL 13

#define HDR_LEN 24         // 802.11 管理帧 / 数据帧头
#define BEACON_FIXED_LEN 12  // timestamp + interval + capability
#define FCS_LEN 4

static device_t *update(dev_type_t type, const uint8_t *mac, int8_t rssi, uint8_t channel)
{
    device_t *d = devices_update(type, mac, rssi);
    if (d) {
        d->random = mac[0] & 0x02;
        if (!d->channel) {
            d->channel = channel;
        }
    }
    return d;
}

static void parse_ies(device_t *d, const uint8_t *ie, int len)
{
    while (len >= 2 && ie[1] + 2 <= len) {
        if (ie[0] == 0) {  // SSID
            devices_set_name(d, ie + 2, ie[1]);
        } else if (ie[0] == 3 && ie[1] == 1) {  // DS 参数：AP 实际信道
            d->channel = ie[2];
        }
        len -= ie[1] + 2;
        ie += ie[1] + 2;
    }
}

static void rx_cb(void *buf, wifi_promiscuous_pkt_type_t type)
{
    const wifi_promiscuous_pkt_t *pkt = buf;
    const uint8_t *f = pkt->payload;
    int len = pkt->rx_ctrl.sig_len - FCS_LEN;
    if (len < HDR_LEN) {
        return;
    }
    int8_t rssi = pkt->rx_ctrl.rssi;
    uint8_t ch = pkt->rx_ctrl.channel;
    uint8_t subtype = f[0] >> 4;
    const uint8_t *addr1 = f + 4, *addr2 = f + 10;
    device_t *d;

    if (type == WIFI_PKT_MGMT) {
        if (subtype == 8 || subtype == 5) {  // Beacon / Probe Response
            d = update(DEV_WIFI_AP, addr2, rssi, ch);
            if (d) {
                parse_ies(d, f + HDR_LEN + BEACON_FIXED_LEN, len - HDR_LEN - BEACON_FIXED_LEN);
            }
        } else if (subtype == 4) {  // Probe Request
            update(DEV_WIFI_STA, addr2, rssi, ch);
        }
    } else if (type == WIFI_PKT_DATA) {
        bool to_ds = f[1] & 0x01, from_ds = f[1] & 0x02;
        if (to_ds && !from_ds) {  // STA -> AP：addr1 = BSSID，addr2 = STA
            d = update(DEV_WIFI_STA, addr2, rssi, ch);
            if (d) {
                memcpy(d->ap, addr1, 6);
            }
        } else if (from_ds && !to_ds) {  // AP -> STA：addr2 = BSSID
            update(DEV_WIFI_AP, addr2, rssi, ch);
        }
    }
}

void wifi_scan_init(void)
{
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_NULL));  // 不建 STA/AP 接口，只收不发
    ESP_ERROR_CHECK(esp_wifi_set_country_code("CN", false));  // 信道 1–13

    wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA,
    };
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_filter(&filter));
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous_rx_cb(rx_cb));
}

void wifi_scan_run(void)
{
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(true));
    for (int pass = 0; pass < PASSES; pass++) {
        for (int ch = 1; ch <= MAX_CHANNEL; ch++) {
            ESP_ERROR_CHECK(esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE));
            vTaskDelay(pdMS_TO_TICKS(DWELL_MS));
        }
    }
    ESP_ERROR_CHECK(esp_wifi_set_promiscuous(false));
    // 停掉 Wi-Fi，和 BLE 分时
    ESP_ERROR_CHECK(esp_wifi_stop());
}
