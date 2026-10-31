#include <math.h>
#include <stdio.h>

#include "ui.h"

LV_FONT_DECLARE(font_ui_text_16);
LV_FONT_DECLARE(font_ui_numbers_36);
LV_FONT_DECLARE(font_ui_soc_88);
LV_FONT_DECLARE(font_ui_soc_64);

#define WHITE 0xF5F6F7
#define MUTED 0xA6ABAF
#define BLUE 0x26A8F7
#define PINK 0xFF738D
#define GREEN 0x35D85A
#define TREND_POINTS 48

typedef enum { READING, CHARGING, IDLE, DISCHARGING, UNAVAILABLE } ui_state_t;

static ui_state_t state = READING;
static lv_obj_t *status_dot, *status_label, *current_label, *current_unit;
static lv_obj_t *voltage_label, *voltage_unit, *power_label, *battery, *trend;
static lv_obj_t *time_labels[4];
static lv_obj_t *soc_label, *soc_unit;
static battery_reading_t shown_reading;
static bool show_mah;

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
    if (lv_obj_has_flag(power_label, LV_OBJ_FLAG_HIDDEN)) return;
    if (x < 26 || x >= 343 || y < 235 || y >= 270) return;
    show_mah = !show_mah;
    ui_show_battery(&shown_reading);
}

void ui_init(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    state = READING;
    shown_reading = (battery_reading_t){0};
    show_mah = false;

    // Keep corner-adjacent content inside the panel's larger physical cutouts.
    status_dot = surface(screen, 44, 32, 15, 15);
    lv_obj_set_style_bg_opa(status_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(status_dot, lv_color_hex(MUTED), 0);
    lv_obj_set_style_radius(status_dot, LV_RADIUS_CIRCLE, 0);
    status_label = label(screen, 67, 31, "读取中", &font_ui_text_16, WHITE);

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
}
