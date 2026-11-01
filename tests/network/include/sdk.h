#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/time.h>

typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1
#define ESP_ERR_NO_MEM 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NVS_NOT_FOUND 3
#define ESP_ERR_WIFI_NOT_STARTED 4
#define ESP_EVENT_ANY_ID -1
#define ESP_MAC_WIFI_SOFTAP 0
#define NVS_READWRITE 0
#define WIFI_MODE_STA 1
#define WIFI_MODE_APSTA 2
#define WIFI_STORAGE_RAM 0
#define WIFI_IF_STA 0
#define WIFI_IF_AP 1
#define WIFI_ALL_CHANNEL_SCAN 0
#define WIFI_AUTH_OPEN 0
#define WIFI_AUTH_WPA2_PSK 3
#define WIFI_EVENT_STA_CONNECTED 1
#define WIFI_EVENT_STA_DISCONNECTED 2
#define WIFI_EVENT_SCAN_DONE 3
#define IP_EVENT_STA_GOT_IP 1
#define pdTRUE 1
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(p) ((void)(p))
#define portEXIT_CRITICAL(p) ((void)(p))
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
typedef int portMUX_TYPE;
typedef int BaseType_t;
typedef unsigned nvs_handle_t;
typedef void *QueueHandle_t;
typedef const char *esp_event_base_t;
typedef void *esp_event_handler_instance_t;
typedef struct { int id; } esp_netif_t;
extern const char *WIFI_EVENT, *IP_EVENT;
typedef struct { uint8_t reason; } wifi_event_sta_disconnected_t;
typedef struct { uint32_t status; } wifi_event_sta_scan_done_t;
typedef struct { uint8_t ssid[33]; int8_t rssi; int authmode; } wifi_ap_record_t;
typedef struct { bool show_hidden; } wifi_scan_config_t;
typedef struct { int unused; } wifi_init_config_t;
#define WIFI_INIT_CONFIG_DEFAULT() ((wifi_init_config_t){0})
typedef union {
    struct { uint8_t ssid[32], password[64]; int scan_method; } sta;
    struct { uint8_t ssid[32], password[64]; uint8_t ssid_len, channel, max_connection; int authmode; } ap;
} wifi_config_t;
typedef struct {
    bool wait_for_sync;
    void (*sync_cb)(struct timeval *);
    int num_of_servers;
    const char *servers[3];
} esp_sntp_config_t;
#define ESP_SNTP_SERVER_LIST(...) {__VA_ARGS__}
#define ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(n, list) {.num_of_servers = (n), .servers = list}

const char *esp_err_to_name(esp_err_t err);
QueueHandle_t xQueueCreate(unsigned count, unsigned size);
int xQueueSend(QueueHandle_t queue, const void *item, unsigned timeout);
int xQueueReceive(QueueHandle_t queue, void *item, unsigned timeout);
void xQueueReset(QueueHandle_t queue);
void vQueueDelete(QueueHandle_t queue);
esp_err_t esp_netif_init(void);
esp_err_t esp_event_loop_create_default(void);
esp_netif_t *esp_netif_create_default_wifi_sta(void);
esp_netif_t *esp_netif_create_default_wifi_ap(void);
void esp_netif_destroy_default_wifi(void *netif);
esp_err_t esp_event_handler_instance_register(esp_event_base_t base, int32_t id,
    void (*callback)(void *, esp_event_base_t, int32_t, void *), void *arg, esp_event_handler_instance_t *instance);
esp_err_t esp_event_handler_instance_unregister(esp_event_base_t base, int32_t id, esp_event_handler_instance_t instance);
esp_err_t esp_wifi_init(const wifi_init_config_t *config);
esp_err_t esp_wifi_deinit(void);
esp_err_t esp_wifi_start(void);
esp_err_t esp_wifi_stop(void);
esp_err_t esp_wifi_set_storage(int storage);
esp_err_t esp_wifi_set_mode(int mode);
esp_err_t esp_wifi_set_country_code(const char *country, bool ieee80211d);
esp_err_t esp_wifi_set_config(int interface, wifi_config_t *config);
esp_err_t esp_wifi_connect(void);
esp_err_t esp_wifi_disconnect(void);
esp_err_t esp_wifi_scan_start(const wifi_scan_config_t *config, bool block);
esp_err_t esp_wifi_scan_stop(void);
esp_err_t esp_wifi_scan_get_ap_records(uint16_t *count, wifi_ap_record_t *records);
esp_err_t esp_wifi_clear_ap_list(void);
esp_err_t esp_read_mac(uint8_t *mac, int type);
void esp_fill_random(void *buffer, size_t length);
esp_err_t nvs_flash_init(void);
esp_err_t nvs_open(const char *name, int mode, nvs_handle_t *handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *length);
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t length);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t esp_netif_sntp_init(const esp_sntp_config_t *config);
void esp_netif_sntp_deinit(void);
