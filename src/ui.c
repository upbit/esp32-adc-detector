#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ui.h"

LV_FONT_DECLARE(font_ui_text_16);
LV_FONT_DECLARE(font_ui_actions_24);
LV_FONT_DECLARE(font_ui_numbers_36);
LV_FONT_DECLARE(font_ui_soc_88);
LV_FONT_DECLARE(font_ui_soc_64);

#define WHITE 0xF5F6F7
#define MUTED 0xA6ABAF
#define BLUE 0x26A8F7
#define PINK 0xFF738D
#define GREEN 0x35D85A
#define TREND_POINTS 48
#define HEADER_RIGHT_MARGIN 44

typedef enum { READING, CHARGING, IDLE, DISCHARGING, UNAVAILABLE } ui_state_t;

static ui_state_t state = READING;
static lv_obj_t *status_dot, *status_label, *current_label, *current_unit;
static lv_obj_t *voltage_label, *voltage_unit, *power_label, *battery, *trend;
static lv_obj_t *time_labels[4];
static lv_obj_t *soc_label, *soc_unit;
static battery_reading_t shown_reading;
static bool show_mah;
static lv_obj_t *main_screen, *portal_screen, *wifi_icon, *clock_label;
static lv_obj_t *hotspot_label, *portal_url_label, *countdown_label;
static lv_obj_t *portal_details, *network_actions, *sync_button;
static bool network_menu;
static bool sync_enabled;
static wifi_view_t shown_wifi;
static int64_t first_wifi_tap;
static bool have_wifi_tap;

// Trend history remains reference data until the history/time stage.
static const uint8_t sample_trends[3][TREND_POINTS] = {
    {60,57,54,51,47,44,41,37,33,29,26,23,21,20,19,20,
     21,23,25,28,31,34,37,40,43,45,47,49,51,53,54,55,
     54,53,51,50,52,55,58,60,61,62,64,66,68,70,71,72},
    {51,49,47,45,43,40,37,35,33,34,36,39,42,46,49,53,
     57,60,64,67,69,68,66,64,63,65,68,72,75,78,79,77,
     74,72,72,72,72,72,72,72,72,72,72,72,72,72,72,72},
    {52,50,48,46,43,41,38,35,34,35,38,42,46,50,54,58,
     62,66,69,71,70,68,65,64,65,68,72,76,79,82,84,85,
     83,81,79,77,75,73,71,69,68,66,65,63,62,60,58,56},
};

static lv_obj_t *label(lv_obj_t *parent, int x, int y, const char *text,
                       const lv_font_t *font, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(parent);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_text_letter_space(obj, 0, 0);
    lv_label_set_text(obj, text);
    return obj;
}

static lv_obj_t *surface(lv_obj_t *parent, int x, int y, int width, int height)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    return obj;
}

static lv_obj_t *network_button(lv_obj_t *parent, int y, const char *text,
                                const char *symbol, bool primary)
{
    lv_obj_t *button = surface(parent, 0, y, 280, primary ? 64 : 56);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(button, lv_color_hex(primary ? BLUE : 0x171B19), 0);
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_t *caption = label(button, 0, 0, text, &font_ui_actions_24, WHITE);
    lv_obj_align(caption, LV_ALIGN_CENTER, 18, 0);
    lv_obj_t *icon = label(button, 0, 0, symbol, &lv_font_montserrat_28, WHITE);
    lv_obj_align_to(icon, caption, LV_ALIGN_OUT_LEFT_MID, -12, 0);
    return button;
}

static void rect(lv_layer_t *layer, int x, int y, int width, int height,
                 uint32_t color, int radius, int border)
{
    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(color);
    dsc.radius = radius;
    dsc.border_width = border;
    dsc.border_color = lv_color_hex(0xBBC5D0);
    lv_area_t area = {x, y, x + width - 1, y + height - 1};
    lv_draw_rect(layer, &dsc, &area);
}

static void triangle(lv_layer_t *layer, int x1, int y1, int x2, int y2,
                     int x3, int y3, uint32_t color)
{
    lv_draw_triangle_dsc_t dsc;
    lv_draw_triangle_dsc_init(&dsc);
    dsc.color = lv_color_hex(color);
    dsc.p[0] = (lv_point_precise_t){x1, y1};
    dsc.p[1] = (lv_point_precise_t){x2, y2};
    dsc.p[2] = (lv_point_precise_t){x3, y3};
    lv_draw_triangle(layer, &dsc);
}

static void draw_wifi(lv_event_t *event)
{
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(event), &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    lv_draw_arc_dsc_t arc;
    lv_draw_arc_dsc_init(&arc);
    arc.center = (lv_point_t){area.x1 + 13, area.y1 + 21};
    arc.start_angle = 225;
    arc.end_angle = 315;
    arc.width = 2;
    arc.rounded = true;
    arc.color = lv_color_hex(shown_wifi.connected ? WHITE : MUTED);
    for (int radius = 8; radius <= 17; radius += 5) {
        arc.radius = radius;
        lv_draw_arc(layer, &arc);
    }
    rect(layer, area.x1 + 12, area.y1 + 20, 3, 3, shown_wifi.connected ? WHITE : MUTED, 2, 0);
    if (!shown_wifi.connected) {
        lv_draw_line_dsc_t line;
        lv_draw_line_dsc_init(&line);
        line.p1 = (lv_point_precise_t){area.x1 + 2, area.y1 + 2};
        line.p2 = (lv_point_precise_t){area.x1 + 24, area.y1 + 24};
        line.color = lv_color_black();
        line.width = 5;
        lv_draw_line(layer, &line);
        line.color = lv_color_hex(MUTED);
        line.width = 2;
        lv_draw_line(layer, &line);
    }
}

void ui_show_network(const wifi_view_t *wifi, const clock_view_t *clock)
{
    bool screen_changed = wifi->portal != shown_wifi.portal;
    bool icon_changed = wifi->connected != shown_wifi.connected;
    bool countdown_changed = wifi->seconds_remaining != shown_wifi.seconds_remaining;
    if (wifi->portal && (screen_changed || strcmp(wifi->hotspot, shown_wifi.hotspot))) {
        lv_label_set_text(hotspot_label, wifi->hotspot);
    }
    shown_wifi = *wifi;
    if (screen_changed) {
        have_wifi_tap = false;
        if (wifi->portal) {
            network_menu = false;
            lv_obj_add_flag(network_actions, LV_OBJ_FLAG_HIDDEN);
            lv_obj_remove_flag(portal_details, LV_OBJ_FLAG_HIDDEN);
        }
        lv_screen_load(wifi->portal ? portal_screen : main_screen);
    }
    if (wifi->portal && (screen_changed || countdown_changed))
        lv_label_set_text_fmt(countdown_label, "%d 秒后关闭热点", wifi->seconds_remaining);
    if (icon_changed) lv_obj_invalidate(wifi_icon);
    if (clock->valid && !wifi->active) lv_obj_add_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_remove_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN);
    bool enabled = wifi->configured && !wifi->active;
    if (enabled != sync_enabled) {
        sync_enabled = enabled;
        lv_obj_set_style_opa(sync_button, enabled ? LV_OPA_COVER : LV_OPA_40, 0);
    }
    const char *text = clock->feedback == CLOCK_FAILED ? "失败" : clock->valid ? clock->text : "";
    if (strcmp(lv_label_get_text(clock_label), text)) {
        lv_label_set_text(clock_label, text);
        lv_obj_update_layout(clock_label);
        lv_obj_align(clock_label, LV_ALIGN_TOP_RIGHT, -HEADER_RIGHT_MARGIN, 31);
        lv_obj_set_x(wifi_icon, text[0] ? lv_obj_get_x(clock_label) - 8 - 26 :
                     368 - HEADER_RIGHT_MARGIN - 26);
    }
    lv_obj_set_style_text_color(clock_label, lv_color_hex(clock->feedback == CLOCK_SYNCED ? GREEN :
                               clock->feedback == CLOCK_FAILED ? PINK : WHITE), 0);
}

ui_action_t ui_handle_network_tap(int x, int y, int64_t now_us)
{
    if (network_menu) {
        bool close = x >= 284 && x < 334 && y >= 22 && y < 70;
        bool sync = x >= 44 && x < 324 && y >= 192 && y < 256 &&
                    shown_wifi.configured && !shown_wifi.active;
        bool hotspot = x >= 44 && x < 324 && y >= 276 && y < 332;
        if (close || sync || hotspot) {
            network_menu = false;
            lv_screen_load(main_screen);
        }
        return sync ? UI_ACTION_SYNC_TIME : hotspot ? UI_ACTION_OPEN_WIFI : UI_ACTION_NONE;
    }
    if (shown_wifi.portal) {
        if (x >= 284 && x < 334 && y >= 22 && y < 70) return UI_ACTION_CANCEL_WIFI;
        return UI_ACTION_NONE;
    }
    if (x < 214 || x >= 328 || y < 18 || y >= 65) {
        have_wifi_tap = false;
        return UI_ACTION_NONE;
    }
    if (have_wifi_tap && now_us >= first_wifi_tap && now_us - first_wifi_tap <= 400000) {
        have_wifi_tap = false;
        network_menu = true;
        lv_obj_add_flag(portal_details, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(network_actions, LV_OBJ_FLAG_HIDDEN);
        lv_screen_load(portal_screen);
        return UI_ACTION_NONE;
    }
    have_wifi_tap = true;
    first_wifi_tap = now_us;
    return UI_ACTION_NONE;
}

static void draw_battery(lv_event_t *event)
{
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(event), &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    int x = area.x1, y = area.y1;
    rect(layer, x + 141, y + 18, 7, 20, 0xBBC5D0, 2, 0);
    rect(layer, x, y + 2, 142, 54, 0x17212B, 8, 3);
    int fill_percent = shown_reading.valid ? shown_reading.percent : 0;
    if (fill_percent >= 90) fill_percent = 100; // Coarse full icon, not a full-charge claim.
    int fill_width = 132 * fill_percent / 100;
    if (fill_width) rect(layer, x + 5, y + 7, fill_width, 44, GREEN, 3, 0);
    for (int offset = 13; offset < fill_width; offset += 13) {
        rect(layer, x + 5 + offset, y + 8, 1, 42, 0x179A3D, 0, 0);
    }
    if (state == CHARGING) {
        triangle(layer, x + 70, y + 14, x + 54, y + 32, x + 68, y + 32, WHITE);
        triangle(layer, x + 61, y + 45, x + 78, y + 26, x + 64, y + 26, WHITE);
    } else if (state == DISCHARGING) {
        rect(layer, x + 149, y + 25, 6, 8, PINK, 0, 0);
        triangle(layer, x + 155, y + 20, x + 155, y + 38, x + 164, y + 29, PINK);
    }
}

static void draw_trend(lv_event_t *event)
{
    lv_area_t area;
    lv_obj_get_coords(lv_event_get_target_obj(event), &area);
    lv_layer_t *layer = lv_event_get_layer(event);
    int x = area.x1, y = area.y1;
    unsigned example = state == CHARGING ? 0 : state == DISCHARGING ? 2 : 1;
    const uint8_t *values = sample_trends[example];
    int shade_start = example == 0 ? 150 : 79;
    int shade_width = example == 0 ? 55 : 70;
    rect(layer, x + shade_start, y, shade_width, 96, 0x102C19, 0, 0);
    for (int row = 0; row <= 2; row++) rect(layer, x, y + row * 48, 282, 1, 0x242A27, 0, 0);
    for (int col = 0; col <= 4; col++) rect(layer, x + col * 70, y, 1, 96, 0x202522, 0, 0);
    for (int i = 0; i < TREND_POINTS; i++) {
        int height = values[i] * 94 / 100;
        uint32_t color = example != 0 ? GREEN : values[i] <= 25 ? PINK : values[i] <= 40 ? 0xE7CE4D : GREEN;
        rect(layer, x + i * 282 / TREND_POINTS, y + 96 - height, 3, height, color, 0, 0);
    }
    // One centered bolt, separated from the interval line by a 4 px clearance.
    int start = x + shade_start, center = start + shade_width / 2;
    rect(layer, start, y + 106, center - 11 - start, 2, GREEN, 0, 0);
    rect(layer, center + 11, y + 106, start + shade_width - center - 11, 2, GREEN, 0, 0);
    triangle(layer, center + 4, y + 98, center - 7, y + 109, center + 2, y + 109, GREEN);
    triangle(layer, center - 4, y + 115, center + 7, y + 104, center - 2, y + 104, GREEN);
}

static void align_units(void)
{
    lv_obj_update_layout(current_label);
    lv_obj_align_to(current_unit, current_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
    lv_obj_align_to(voltage_unit, voltage_label, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -4);
}

void ui_show_battery(const battery_reading_t *reading)
{
    bool soc_changed = reading->valid != shown_reading.valid || reading->percent != shown_reading.percent;
    shown_reading = *reading;
    const ina226_sample_t *sample = &reading->sample;
    ui_state_t next = !reading->valid ? UNAVAILABLE : reading->state == BATTERY_CHARGING ? CHARGING :
                      reading->state == BATTERY_DISCHARGING ? DISCHARGING : IDLE;
    uint32_t color = next == CHARGING ? BLUE : next == DISCHARGING ? PINK : WHITE;
    uint32_t current_color = !reading->valid ? WHITE :
                             sample->current_a > BATTERY_CURRENT_THRESHOLD_A ? BLUE :
                             sample->current_a < -BATTERY_CURRENT_THRESHOLD_A ? PINK : WHITE;
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(next == UNAVAILABLE ? PINK : color), 0);
    lv_obj_set_style_text_color(status_label, lv_color_hex(next == DISCHARGING || next == UNAVAILABLE ? PINK : WHITE), 0);
    lv_obj_set_style_text_color(current_label, lv_color_hex(current_color), 0);
    lv_obj_set_style_text_color(current_unit, lv_color_hex(current_color), 0);
    lv_label_set_text(status_label, next == CHARGING ? "充电中" : next == DISCHARGING ? "放电中" :
                                   next == IDLE ? "待机中" : "读取失败");
    if (!reading->valid) {
        lv_label_set_text(current_label, "--");
        lv_label_set_text(voltage_label, "--");
        lv_label_set_text(soc_label, "--");
        lv_obj_set_style_text_font(soc_label, &font_ui_soc_88, 0);
        lv_obj_set_y(soc_label, 68);
    } else {
        int milliamps = fabsf(sample->current_a) <= BATTERY_CURRENT_THRESHOLD_A ? 0 :
                       (int)lroundf(sample->current_a * 1000.0f);
        char text[48];
        snprintf(text, sizeof(text), milliamps > 0 ? "+%d" : "%d", milliamps);
        lv_label_set_text(current_label, text);
        snprintf(text, sizeof(text), "%.2f", (double)sample->voltage_v);
        lv_label_set_text(voltage_label, text);
        lv_obj_set_style_text_font(soc_label, reading->percent == 100 ? &font_ui_soc_64 : &font_ui_soc_88, 0);
        lv_obj_set_y(soc_label, 68 + (reading->percent == 100 ?
                     font_ui_soc_88.line_height - font_ui_soc_64.line_height : 0));
        snprintf(text, sizeof(text), "%d", reading->percent);
        lv_label_set_text(soc_label, text);
    }
    if (next == IDLE || next == UNAVAILABLE) {
        lv_obj_add_flag(power_label, LV_OBJ_FLAG_HIDDEN);
    } else {
        char text[48];
        snprintf(text, sizeof(text), "%s %.2f %s", next == CHARGING ? "本次充入" : "本次输出",
                 show_mah ? reading->session_mah : reading->session_wh, show_mah ? "mAh" : "Wh");
        lv_label_set_text(power_label, text);
        lv_obj_remove_flag(power_label, LV_OBJ_FLAG_HIDDEN);
    }
    align_units();
    if (soc_changed) lv_obj_invalidate(battery);
    if (next != state) {
        state = next;
        const char *times[] = {"00:00", "06:00", "12:00", "18:00"};
        for (int i = 0; i < 4; i++) lv_label_set_text(time_labels[i], times[(i + (next == CHARGING ? 3 : 0)) % 4]);
        lv_obj_invalidate(battery);
        lv_obj_invalidate(trend);
    }
}

void ui_handle_tap(int x, int y)
{
    if (shown_wifi.portal || network_menu) return;
    if (lv_obj_has_flag(power_label, LV_OBJ_FLAG_HIDDEN)) return;
    if (x < 26 || x >= 343 || y < 235 || y >= 270) return;
    show_mah = !show_mah;
    ui_show_battery(&shown_reading);
}

void ui_init(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    main_screen = screen;
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    state = READING;
    shown_reading = (battery_reading_t){0};
    show_mah = false;
    shown_wifi = (wifi_view_t){0};
    have_wifi_tap = false;
    network_menu = false;
    sync_enabled = false;

    // Keep corner-adjacent content inside the panel's larger physical cutouts.
    status_dot = surface(screen, 44, 32, 15, 15);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(MUTED), 0);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    status_label = label(screen, 67, 31, "读取中", &font_ui_text_16, WHITE);
    wifi_icon = surface(screen, 368 - HEADER_RIGHT_MARGIN - 26, 26, 26, 26);
    lv_obj_add_event_cb(wifi_icon, draw_wifi, LV_EVENT_DRAW_MAIN, NULL);
    clock_label = label(screen, 0, 31, "", &font_ui_text_16, WHITE);
    lv_obj_align(clock_label, LV_ALIGN_TOP_RIGHT, -HEADER_RIGHT_MARGIN, 31);

    soc_label = label(screen, 24, 68, "--", &font_ui_soc_88, WHITE);
    lv_obj_set_width(soc_label, 126);
    soc_unit = label(screen, 154, 108, "%", &lv_font_montserrat_28, WHITE);
    battery = surface(screen, 195, 76, 166, 58);
    lv_obj_add_event_cb(battery, draw_battery, LV_EVENT_DRAW_MAIN, NULL);

    label(screen, 26, 172, "电池电流", &font_ui_text_16, MUTED);
    label(screen, 230, 172, "电池电压", &font_ui_text_16, MUTED);
    current_label = label(screen, 26, 198, "--", &font_ui_numbers_36, WHITE);
    current_unit = label(screen, 0, 0, "mA", &lv_font_montserrat_14, WHITE);
    voltage_label = label(screen, 230, 198, "--", &font_ui_numbers_36, WHITE);
    voltage_unit = label(screen, 0, 0, "V", &lv_font_montserrat_14, WHITE);
    power_label = label(screen, 26, 243, "", &font_ui_text_16, MUTED);
    lv_obj_add_flag(power_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_t *divider = surface(screen, 198, 170, 1, 68);
    lv_obj_set_style_bg_color(divider, lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_opa(divider, LV_OPA_COVER, 0);

    label(screen, 26, 274, "电量趋势", &font_ui_text_16, WHITE);
    lv_obj_t *period = label(screen, 0, 274, "过去24小时", &font_ui_text_16, MUTED);
    lv_obj_align(period, LV_ALIGN_TOP_RIGHT, -25, 274);
    trend = surface(screen, 26, 300, 283, 116);
    lv_obj_add_event_cb(trend, draw_trend, LV_EVENT_DRAW_MAIN, NULL);
    label(screen, 316, 296, "100%", &lv_font_montserrat_14, MUTED);
    label(screen, 316, 344, "50%", &lv_font_montserrat_14, MUTED);
    label(screen, 316, 392, "0%", &lv_font_montserrat_14, MUTED);
    const char *times[] = {"00:00", "06:00", "12:00", "18:00"};
    for (int i = 0; i < 4; i++) time_labels[i] = label(screen, 50 + i * 67, 419, times[i], &lv_font_montserrat_14, MUTED);
    align_units();

    portal_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(portal_screen, lv_color_black(), 0);
    lv_obj_set_style_pad_all(portal_screen, 0, 0);
    lv_obj_remove_flag(portal_screen, LV_OBJ_FLAG_SCROLLABLE);
    label(portal_screen, 44, 35, "Wi-Fi 配网", &font_ui_text_16, WHITE);
    label(portal_screen, 299, 31, LV_SYMBOL_CLOSE, &lv_font_montserrat_28, MUTED);
    lv_obj_t *rule = surface(portal_screen, 36, 76, 296, 1);
    lv_obj_set_style_bg_color(rule, lv_color_hex(0x242424), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    portal_details = surface(portal_screen, 0, 88, 368, 320);
    label(portal_details, 44, 28, "热点名称", &font_ui_text_16, MUTED);
    hotspot_label = label(portal_details, 44, 61, "", &lv_font_montserrat_36, WHITE);
    lv_label_set_long_mode(hotspot_label, LV_LABEL_LONG_CLIP);
    label(portal_details, 44, 158, "配网页面", &font_ui_text_16, MUTED);
    portal_url_label = label(portal_details, 44, 191, "http://192.168.4.1", &lv_font_montserrat_32, WHITE);
    lv_label_set_long_mode(portal_url_label, LV_LABEL_LONG_CLIP);
    countdown_label = label(portal_details, 44, 266, "", &font_ui_text_16, MUTED);
    network_actions = surface(portal_screen, 44, 192, 280, 152);
    sync_button = network_button(network_actions, 0, "时间同步", LV_SYMBOL_REFRESH, true);
    lv_obj_set_style_opa(sync_button, LV_OPA_40, 0);
    network_button(network_actions, 84, "重新开启热点", LV_SYMBOL_WIFI, false);
    lv_obj_add_flag(network_actions, LV_OBJ_FLAG_HIDDEN);
}
