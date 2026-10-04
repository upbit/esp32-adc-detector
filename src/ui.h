#pragma once

// RLCD + LVGL：顶部状态栏、统计行、设备表
void ui_init(void);
void ui_set_status(const char *fmt, ...);
// 显示上一次 devices_end_round 的结果，当前页的设备类型
void ui_show_report(void);
// 翻页：先翻完当前类型的各页，再切到下一类型（AP → STA → BLE → AP）
void ui_next_page(void);
