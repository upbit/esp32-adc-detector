#include <stdarg.h>
#include <stdio.h>

#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "devices.h"
#include "oui.h"
#include "rlcd.h"
#include "ui.h"

// 字体：小字用 Fusion Pixel 10，大字用 Ark Pixel 12（像素字体，1bpp 无失真）
LV_FONT_DECLARE(font_fusion10)
LV_FONT_DECLARE(font_ark12)

#define STATUS_H 16   // Ark 12
#define SUMMARY_H 14  // Fusion 10
#define ROW_H 12      // Fusion 10 行高 10 + 上下内边距各 1
#define MAX_ROWS ((RLCD_H - STATUS_H - SUMMARY_H) / ROW_H)  // 含表头
#define PAGE_ROWS (MAX_ROWS - 1)

static lv_obj_t *status_label, *summary_label, *table;
static dev_type_t page = DEV_WIFI_AP;
static int sub_page;  // 当前类型的第几页，从 0 开始

static const char *const type_short[DEV_TYPE_NUM] = {"AP", "STA", "BLE"};

static uint32_t tick_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px)
{
    rlcd_draw_l8(px);  // FULL 模式，px 总是整屏
    lv_display_flush_ready(disp);
}

static void lvgl_task(void *arg)
{
    for (;;) {
        uint32_t ms = lv_timer_handler();  // 内部自带 lv_lock
        vTaskDelay(pdMS_TO_TICKS(ms < 10 ? 10 : ms > 100 ? 100 : ms));
    }
}

static lv_obj_t *bar(int y, int h, bool inverted)
{
    lv_obj_t *l = lv_label_create(lv_screen_active());
    lv_obj_set_pos(l, 0, y);
    lv_obj_set_size(l, RLCD_W, h);
    lv_obj_set_style_pad_left(l, 4, 0);
    lv_obj_set_style_pad_top(l, 1, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_CLIP);
    if (inverted) {
        lv_obj_set_style_bg_color(l, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
        lv_obj_set_style_text_color(l, lv_color_white(), 0);
    }
    return l;
}

void ui_init(void)
{
    rlcd_init();
    lv_init();
    lv_tick_set_cb(tick_ms);

    lv_display_t *disp = lv_display_create(RLCD_W, RLCD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_L8);
    size_t buf_len = RLCD_W * RLCD_H;
    void *buf = heap_caps_aligned_alloc(LV_DRAW_BUF_ALIGN, buf_len, MALLOC_CAP_SPIRAM);
    assert(buf);
    lv_display_set_buffers(disp, buf, NULL, buf_len, LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_obj_set_style_text_font(lv_screen_active(), &font_fusion10, 0);  // 子控件继承
    status_label = bar(0, STATUS_H, true);
    lv_obj_set_style_text_font(status_label, &font_ark12, 0);
    summary_label = bar(STATUS_H, SUMMARY_H, false);
    lv_label_set_text(status_label, "Booting...");
    lv_label_set_text(summary_label, "");

    table = lv_table_create(lv_screen_active());
    lv_obj_set_pos(table, 0, STATUS_H + SUMMARY_H);
    lv_obj_set_size(table, RLCD_W, RLCD_H - STATUS_H - SUMMARY_H);
    lv_obj_set_style_pad_all(table, 0, 0);
    lv_obj_set_style_border_width(table, 0, 0);
    lv_obj_set_style_radius(table, 0, 0);
    lv_obj_set_style_pad_hor(table, 2, LV_PART_ITEMS);
    lv_obj_set_style_pad_ver(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_width(table, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_side(table, LV_BORDER_SIDE_TOP, LV_PART_ITEMS);
    lv_obj_set_style_border_color(table, lv_color_black(), LV_PART_ITEMS);
    lv_obj_remove_flag(table, LV_OBJ_FLAG_SCROLLABLE);

    static const int widths[] = {44, 28, 40, 76, 212};  // TAG CH RSSI VENDOR NAME
    lv_table_set_column_count(table, 5);
    for (int c = 0; c < 5; c++) {
        lv_table_set_column_width(table, c, widths[c]);
    }
    lv_table_set_row_count(table, 0);

    xTaskCreatePinnedToCore(lvgl_task, "lvgl", 8192, NULL, 2, NULL, 1);
}

void ui_set_status(const char *fmt, ...)
{
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    lv_lock();
    lv_label_set_text(status_label, buf);
    lv_unlock();
}

static void set_cell(int row, int col, const char *txt)
{
    lv_table_set_cell_value(table, row, col, txt);
    lv_table_set_cell_ctrl(table, row, col, LV_TABLE_CELL_CTRL_TEXT_CROP);  // 不换行
}

void ui_show_report(void)
{
    const devices_summary_t *s = devices_summary();
    int pages = s->count[page] ? (s->count[page] + PAGE_ROWS - 1) / PAGE_ROWS : 1;
    if (sub_page >= pages) {  // 新一轮设备变少时回到第 1 页
        sub_page = 0;
    }
    const device_t *rows[PAGE_ROWS];
    int n = devices_sorted(page, sub_page * PAGE_ROWS, rows, PAGE_ROWS);

    lv_lock();
    if (s->baseline_round) {
        lv_label_set_text_fmt(summary_label, "[%s %d/%d] %d devs  R%d vs base R%d  NEW %d  GONE %d", type_short[page],
                              sub_page + 1, pages, s->count[page], s->round_no, s->baseline_round, s->new_count,
                              s->gone_count);
    } else {
        lv_label_set_text_fmt(summary_label, "[%s %d/%d] %d devs  R%d (baseline)  AP %d  STA %d  BLE %d",
                              type_short[page], sub_page + 1, pages, s->count[page], s->round_no,
                              s->count[DEV_WIFI_AP], s->count[DEV_WIFI_STA], s->count[DEV_BLE]);
    }

    lv_table_set_row_count(table, n + 1);
    static const char *const header[] = {"", "CH", "RSSI", "VENDOR", ""};
    static const char *const name_header[DEV_TYPE_NUM] = {"SSID", "-> AP", "NAME"};
    for (int c = 0; c < 5; c++) {
        set_cell(0, c, c == 4 ? name_header[page] : header[c]);
    }
    for (int i = 0; i < n; i++) {
        const device_t *d = rows[i];
        char ch[4] = "", rssi[5] = "-", name[64];
        if (d->channel) {
            snprintf(ch, sizeof(ch), "%u", d->channel);
        }
        if (d->seen) {
            snprintf(rssi, sizeof(rssi), "%d", d->rssi);
        }
        const char *vendor = d->random ? "(random)" : oui_lookup(d->mac);

        // 名称列：AP 显示 SSID，STA 显示所属 AP 的 SSID，BLE 显示广播名；没有名称时显示 MAC
        const char *label = d->name;
        if (d->type == DEV_WIFI_STA) {
            const device_t *ap = devices_find_ap(d->ap);
            label = ap && ap->name[0] ? ap->name : "";
        }
        if (label[0]) {
            snprintf(name, sizeof(name), "%s", label);
        } else {
            snprintf(name, sizeof(name), "%02X:%02X:%02X:%02X:%02X:%02X", d->mac[0], d->mac[1], d->mac[2], d->mac[3],
                     d->mac[4], d->mac[5]);
        }

        set_cell(i + 1, 0, devices_tag(d));
        set_cell(i + 1, 1, ch);
        set_cell(i + 1, 2, rssi);
        set_cell(i + 1, 3, vendor ? vendor : "");
        set_cell(i + 1, 4, name);
    }
    lv_unlock();
}

void ui_next_page(void)
{
    // 先翻完当前类型的各页，再切到下一类型
    int count = devices_summary()->count[page];
    if ((sub_page + 1) * PAGE_ROWS < count) {
        sub_page++;
    } else {
        sub_page = 0;
        page = (page + 1) % DEV_TYPE_NUM;
    }
    ui_show_report();
}
