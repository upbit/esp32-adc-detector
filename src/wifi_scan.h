#pragma once

void wifi_scan_init(void);
// 混杂模式跳频扫描，结果写入设备表；阻塞约 6.5 s
void wifi_scan_run(void);
