#pragma once

void ble_scan_init(void);
// 被动扫描，结果写入设备表；阻塞约 5 s
void ble_scan_run(void);
