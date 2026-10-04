#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef enum { DEV_WIFI_AP, DEV_WIFI_STA, DEV_BLE, DEV_TYPE_NUM } dev_type_t;

typedef struct {
    dev_type_t type;
    uint8_t mac[6];   // 高字节在前
    bool random;      // 随机 / 本地管理地址
    uint8_t channel;  // Wi-Fi 信道，BLE 为 0
    int8_t rssi;      // 本轮最强 RSSI
    uint32_t pkts;    // 本轮包数
    uint8_t ap[6];    // STA 所属 AP，全 0 表示未知
    char name[33];    // SSID 或 BLE 名称
    bool seen;        // 本轮扫到
    bool in_baseline;
} device_t;

// 开始新一轮：只保留基线条目，清零本轮统计
void devices_begin_round(void);
// 查找或新增设备并记录一个包；表满返回 NULL
device_t *devices_update(dev_type_t type, const uint8_t mac[6], int8_t rssi);
// 名称为空时写入，控制字符替换为 '?'
void devices_set_name(device_t *d, const uint8_t *name, int len);
// 打印本轮报告；还没有基线时把本轮存为基线
void devices_end_round(void);
void devices_clear_baseline(void);

// 以下给显示用，内容是上一次 devices_end_round 的结果
typedef struct {
    int round_no;
    int baseline_round;  // 0 表示那一轮就是基线
    int count[DEV_TYPE_NUM];
    int new_count, gone_count;
} devices_summary_t;

const devices_summary_t *devices_summary(void);
// "NEW" / "GONE" / ""
const char *devices_tag(const device_t *d);
// 只看一种类型：NEW 在前，其余按 RSSI 从强到弱，GONE 在最后。
// 从第 skip 条起取至多 max 条写入 out，返回取到的条数
int devices_sorted(dev_type_t type, int skip, const device_t **out, int max);
// 按 MAC 查 AP，给 STA 显示所属 AP 的 SSID；找不到返回 NULL
const device_t *devices_find_ap(const uint8_t mac[6]);
