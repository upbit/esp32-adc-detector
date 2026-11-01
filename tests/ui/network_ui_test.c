#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include UI_SOURCE
#include "src/misc/lv_text_private.h"

#define WIDTH 368
#define HEIGHT 448
static uint16_t pixels[WIDTH * HEIGHT], buffer[WIDTH * 48];
static lv_display_t *display;

static void flush(lv_display_t *disp, const lv_area_t *area, uint8_t *source)
{
    uint16_t *p = (uint16_t *)source;
    for (int y = area->y1; y <= area->y2; y++)
        for (int x = area->x1; x <= area->x2; x++) pixels[y * WIDTH + x] = *p++;
    lv_display_flush_ready(disp);
}

static void check_labels(lv_obj_t *screen)
{
    lv_obj_update_layout(screen);
    for (unsigned i = 0; i < lv_obj_get_child_count(screen); i++) {
        lv_obj_t *obj = lv_obj_get_child(screen, i);
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) continue;
        if (!lv_obj_check_type(obj, &lv_label_class)) { check_labels(obj); continue; }
        lv_area_t area; lv_obj_get_coords(obj, &area);
        assert(area.x1 >= 0 && area.x2 < WIDTH && area.y1 >= 0 && area.y2 < HEIGHT);
        lv_area_t parent; lv_obj_get_coords(screen, &parent);
        assert(area.x1 >= parent.x1 && area.x2 <= parent.x2 && area.y1 >= parent.y1 && area.y2 <= parent.y2);
        const char *text = lv_label_get_text(obj);
        assert(!strstr(text, "Wh") && !strstr(text, "mAh"));
        const lv_font_t *font = lv_obj_get_style_text_font(obj, 0);
        for (uint32_t index = 0; text[index];) {
            uint32_t cp = lv_text_encoded_next(text, &index);
            lv_font_glyph_dsc_t glyph; assert(lv_font_get_glyph_dsc(font, &glyph, cp, 0));
        }
    }
}

static void screenshot(const char *name)
{
    lv_refr_now(display);
    check_labels(lv_screen_active());
    if (lv_screen_active() == main_screen) {
        assert(lv_obj_get_y(current_label) == 182 && lv_obj_get_y(voltage_label) == 182);
        assert(lv_obj_get_y(period_label) == 254);
        assert(lv_obj_get_y(trend) == 280 && lv_obj_get_height(trend) == 136);
        assert(lv_obj_get_y(current_unit) + lv_obj_get_height(current_unit) == 204);
        assert(lv_obj_get_y(voltage_unit) + lv_obj_get_height(voltage_unit) == 204);
        for (int y = 154; y < 222; y++) assert(pixels[y * WIDTH + 198] == 0x2124);
        for (int i = 0; i < 4; i++) assert(lv_obj_get_y(time_labels[i]) == 419);
        for (int y = 222; y < 254; y++)
            for (int x = 26; x < 343; x++) assert(pixels[y * WIDTH + x] == 0);
    }
    uint16_t saved[WIDTH * HEIGHT]; memcpy(saved, pixels, sizeof(saved));
    lv_obj_invalidate(lv_screen_active()); lv_refr_now(display);
    assert(!memcmp(saved, pixels, sizeof(saved)));
    char path[100]; snprintf(path, sizeof(path), "%s.ppm", name);
    FILE *file = fopen(path, "wb"); assert(file);
    fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (unsigned i = 0; i < WIDTH * HEIGHT; i++) {
        uint16_t p = pixels[i];
        unsigned char rgb[] = {(p >> 11) * 255 / 31, ((p >> 5) & 63) * 255 / 63, (p & 31) * 255 / 31};
        assert(fwrite(rgb, 1, 3, file) == 3);
    }
    assert(!fclose(file));
}

static void test_battery_display(void)
{
    battery_reading_t reading = {.valid = true, .percent = 99, .state = BATTERY_IDLE,
        .sample = {.voltage_v = 4.10f, .current_a = 1.0f}};
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(status_label), "待机中"));
    assert(lv_color_eq(lv_obj_get_style_text_color(current_label, 0), lv_color_hex(BLUE)));
    screenshot("battery-charging-candidate");
    reading.state = BATTERY_CHARGING;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(status_label), "充电中"));
    screenshot("battery-charging-no-total");

    reading.sample.current_a = BATTERY_CURRENT_THRESHOLD_A;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(current_label), "0"));
    assert(lv_color_eq(lv_obj_get_style_text_color(current_unit, 0), lv_color_hex(WHITE)));
    reading.sample.current_a = -BATTERY_CURRENT_THRESHOLD_A;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(current_label), "0"));
    reading.sample.current_a = -0.2f;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(status_label), "充电中"));
    assert(lv_color_eq(lv_obj_get_style_text_color(current_label, 0), lv_color_hex(PINK)));
    screenshot("battery-discharging-candidate");

    reading.state = BATTERY_DISCHARGING;
    reading.sample.current_a = -3.278f;
    reading.percent = 54;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(current_label), "-3278"));
    screenshot("battery-discharging-no-total");
    reading.state = BATTERY_IDLE;
    reading.sample.current_a = 0;
    reading.percent = 0;
    ui_show_battery(&reading);
    screenshot("battery-idle-empty");
    reading.percent = 100; // Keep three-digit layout robust even though the model caps at 99.
    ui_show_battery(&reading);
    screenshot("battery-three-digit-layout");
    reading.valid = false;
    ui_show_battery(&reading);
    assert(!strcmp(lv_label_get_text(current_label), "--"));
    assert(!strcmp(lv_label_get_text(voltage_label), "--"));
    screenshot("battery-read-failed");
    reading.valid = true;
    reading.percent = 99;
    ui_show_battery(&reading);
    assert(ui_handle_network_tap(40, 250, 0) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(40, 250, 100000) == UI_ACTION_NONE);
    assert(!network_menu && lv_screen_active() == main_screen);
    screenshot("battery-recovered-no-total");
}

static uint16_t rgb565(uint32_t color)
{
    return (uint16_t)(((color >> 19) << 11) | (((color >> 10) & 63) << 5) | ((color >> 3) & 31));
}

static void test_history_display(void)
{
    history_view_t day = {.time_valid = true, .start = INT64_C(1761998400),
                          .end = INT64_C(1762084800), .seconds_per_bar = 1800};
    history_view_t week = {.time_valid = true, .start = INT64_C(1761480000),
                           .end = INT64_C(1762084800), .seconds_per_bar = 12600};
    ui_show_history(&day, &week);
    screenshot("history-empty-with-time");
    assert(!seven_days && !strcmp(lv_label_get_text(time_labels[0]), "20:00"));
    assert(!strcmp(lv_label_get_text(time_labels[1]), "02:00"));
    for (int i = 0; i < HISTORY_BARS; i++) {
        int x = 26 + i * 282 / HISTORY_BARS + 1;
        assert(pixels[310 * WIDTH + x] == rgb565(TREND_BACKGROUND));
        assert(pixels[380 * WIDTH + x] == rgb565(TREND_BACKGROUND));
    }
    for (int i = 0; i < HISTORY_BARS; i++) {
        if (i >= 16 && i < 28) continue;
        day.bars[i] = (history_bar_t){.valid = true, .percent = (uint8_t)(10 + i), .samples = 100,
                                     .valid_windows = 1, .charging_percent = i >= 30 && i < 40 ? 100 : 0};
        week.bars[i] = (history_bar_t){.valid = true, .percent = (uint8_t)(95 - i), .samples = 700,
                                      .valid_windows = 7, .charging_percent = i >= 5 && i < 20 ? 70 : 0};
    }
    day.bars[47].partial = true;
    day.bars[0].percent = 0;
    ui_show_history(&day, &week);
    screenshot("history-day-gaps");
    // Bar heights use the expanded plot, not the old 96 px scale.
    int bar_x = 26 + 30 * 282 / HISTORY_BARS + 1;
    int bar_top = TREND_BASELINE_Y - day.bars[30].percent * (TREND_HEIGHT - 2) / 100;
    assert(pixels[bar_top * WIDTH + bar_x] == rgb565(0xE7CE4D));
    assert(pixels[(bar_top - 1) * WIDTH + bar_x] == rgb565(0x16371E));
    assert(pixels[280 * WIDTH + 150] == rgb565(0x3B3B3B));
    assert(pixels[338 * WIDTH + 150] == rgb565(0x3B3B3B));
    assert(pixels[396 * WIDTH + 150] == rgb565(0x3B3B3B));
    assert(pixels[337 * WIDTH + 150] == rgb565(TREND_BACKGROUND));
    assert(pixels[395 * WIDTH + 27] == rgb565(PINK)); // Real 0% is a colored baseline marker.
    assert(pixels[380 * WIDTH + 150] == rgb565(TREND_BACKGROUND)); // Missing columns stay gray.
    assert(pixels[395 * WIDTH + 303] == rgb565(0x278742)); // Current/partial data is dimmed.
    assert(ui_handle_history_tap(100, 330, 0) == false && !seven_days);
    assert(ui_handle_history_tap(100, 330, 400000) && seven_days);
    screenshot("history-week-gaps");
    assert(!strcmp(lv_label_get_text(period_label), "过去7天"));
    assert(!strcmp(lv_label_get_text(time_labels[0]), "10/26"));
    assert(!ui_handle_history_tap(100, 330, 500000));
    assert(!ui_handle_history_tap(100, 330, 900001) && seven_days);
    assert(ui_handle_history_tap(100, 330, 1000000) && !seven_days);
    assert(!strcmp(lv_label_get_text(period_label), "过去24小时"));
    assert(!ui_handle_history_tap(100, 330, 1100000));
    assert(!ui_handle_history_tap(309, 330, 1100001));
    assert(!ui_handle_history_tap(100, 330, 1100002) && !seven_days);
    assert(!ui_handle_history_tap(100, 416, 1100003));
    assert(!ui_handle_history_tap(100, 330, 1100004) && !seven_days);
    assert(!ui_handle_history_tap(100, 330, 1000000) && !seven_days); // Backwards monotonic input.
    assert(!ui_handle_history_tap(100, 279, 1100010));
    assert(!ui_handle_history_tap(100, 280, 1100011) && !seven_days);
    assert(ui_handle_history_tap(100, 280, 1100012) && seven_days);
    assert(!ui_handle_history_tap(100, 280, 1100013));
    assert(ui_handle_history_tap(100, 280, 1100014) && !seven_days);

    wifi_view_t wifi = {.configured = true};
    clock_view_t clock = {0};
    ui_show_network(&wifi, &clock);
    ui_handle_network_tap(312, 38, 1200000);
    ui_handle_network_tap(312, 38, 1300000);
    assert(network_menu);
    assert(!ui_handle_history_tap(100, 330, 1300001));
    assert(!ui_handle_history_tap(100, 330, 1300002) && !seven_days);
    day.bars[47].percent = 90;
    ui_show_history(&day, &week); // Update the hidden dashboard without switching screens.
    assert(lv_screen_active() == portal_screen);
    ui_handle_network_tap(300, 40, 1300003);
    assert(!network_menu && lv_screen_active() == main_screen);
    assert(!ui_handle_history_tap(100, 330, 1300004) && !seven_days);
    assert(ui_handle_history_tap(100, 330, 1300005) && seven_days);
    screenshot("history-week-after-actions");
    ui_handle_history_tap(100, 330, 1400000);
    ui_handle_history_tap(100, 330, 1500000);
    assert(!seven_days);
    memset(&day, 0, sizeof(day));
    memset(&week, 0, sizeof(week));
    ui_show_history(&day, &week);
    screenshot("history-time-unavailable");
    for (int i = 0; i < 4; i++) assert(!lv_label_get_text(time_labels[i])[0]);
}

int main(void)
{
    lv_init(); display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush); ui_init(display);
    wifi_view_t wifi = {0}; clock_view_t clock = {0};
    ui_show_network(&wifi, &clock); assert(!lv_label_get_text(clock_label)[0]);
    screenshot("wifi-off");
    test_battery_display();
    test_history_display();
    ui_show_network(&wifi, &clock);
    lv_area_t icon_area, clock_area;
    lv_obj_get_coords(wifi_icon, &icon_area);
    assert(icon_area.x2 == WIDTH - HEADER_RIGHT_MARGIN - 1);
    assert(ui_handle_network_tap(312, 38, 0) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(312, 38, 1) == UI_ACTION_NONE && network_menu);
    screenshot("wifi-actions-unconfigured");
    assert(ui_handle_network_tap(184, 224, 2) == UI_ACTION_NONE && network_menu);
    assert(ui_handle_network_tap(300, 40, 3) == UI_ACTION_NONE && !network_menu);
    assert(ui_handle_network_tap(245, 38, 100) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(245, 38, 400100) == UI_ACTION_NONE && network_menu);
    assert(ui_handle_network_tap(300, 40, 400101) == UI_ACTION_NONE && !network_menu);
    assert(ui_handle_network_tap(245, 38, 500000) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(245, 38, 900001) == UI_ACTION_NONE && !network_menu);
    assert(ui_handle_network_tap(40, 250, 950000) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(245, 38, 960000) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(245, 38, 960001) == UI_ACTION_NONE && network_menu);
    assert(ui_handle_network_tap(300, 40, 960002) == UI_ACTION_NONE && !network_menu);
    clock.valid = true; strcpy(clock.text, "23:59"); clock.feedback = CLOCK_SYNCED;
    wifi.connected = wifi.active = true; wifi.configured = true; ui_show_network(&wifi, &clock);
    assert(!lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    assert(lv_color_eq(lv_obj_get_style_text_color(clock_label, 0), lv_color_hex(GREEN)));
    screenshot("wifi-synced");
    lv_obj_get_coords(wifi_icon, &icon_area);
    lv_obj_get_coords(clock_label, &clock_area);
    assert(clock_area.x2 == WIDTH - HEADER_RIGHT_MARGIN - 1);
    assert(clock_area.x1 - icon_area.x2 == 9);
    wifi.connected = wifi.active = false; clock.feedback = CLOCK_NORMAL; ui_show_network(&wifi, &clock);
    assert(lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    assert(!strcmp(lv_label_get_text(clock_label), "23:59")); screenshot("wifi-off-with-time");
    clock.feedback = CLOCK_FAILED; ui_show_network(&wifi, &clock);
    assert(!strcmp(lv_label_get_text(clock_label), "失败")); screenshot("wifi-time-failed");
    assert(lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    clock.valid = false; clock.feedback = CLOCK_NORMAL;
    ui_show_network(&wifi, &clock); screenshot("wifi-time-hidden-again");
    lv_obj_get_coords(wifi_icon, &icon_area);
    assert(icon_area.x2 == WIDTH - HEADER_RIGHT_MARGIN - 1);
    assert(!lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    clock.valid = true;
    clock.feedback = CLOCK_NORMAL;
    ui_show_network(&wifi, &clock);
    assert(lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    assert(ui_handle_network_tap(312, 38, 970000) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(312, 38, 970100) == UI_ACTION_NONE && network_menu);
    screenshot("wifi-actions-configured");
    lv_area_t button_area; lv_obj_get_coords(sync_button, &button_area);
    assert(button_area.x1 == 44 && button_area.x2 == 323 && button_area.y1 == 192 && button_area.y2 == 255);
    battery_reading_t reading = {.valid = true, .percent = 99, .state = BATTERY_CHARGING,
        .sample = {.voltage_v = 4.10f, .current_a = 1.0f}};
    ui_show_battery(&reading);
    assert(ui_handle_network_tap(40, 250, 970150) == UI_ACTION_NONE && network_menu);
    assert(ui_handle_network_tap(184, 224, 970200) == UI_ACTION_SYNC_TIME && !network_menu);
    assert(lv_screen_active() == main_screen);
    wifi.active = true; ui_show_network(&wifi, &clock); screenshot("wifi-manual-connecting");
    assert(!lv_obj_has_flag(wifi_icon, LV_OBJ_FLAG_HIDDEN));
    assert(ui_handle_network_tap(312, 38, 980000) == UI_ACTION_NONE);
    assert(ui_handle_network_tap(312, 38, 980100) == UI_ACTION_NONE && network_menu);
    screenshot("wifi-actions-busy");
    assert(ui_handle_network_tap(184, 224, 980200) == UI_ACTION_NONE && network_menu);
    wifi.active = false; ui_show_network(&wifi, &clock);
    assert(ui_handle_network_tap(184, 304, 980300) == UI_ACTION_OPEN_WIFI && !network_menu);
    wifi.portal = wifi.active = true; wifi.seconds_remaining = 300;
    strcpy(wifi.hotspot, "Solar-WWWW"); // Wider than any real hexadecimal suffix.
    ui_show_network(&wifi, &clock); assert(lv_screen_active() == portal_screen);
    lv_obj_update_layout(portal_screen);
    assert(lv_obj_get_width(hotspot_label) <= WIDTH - 88);
    assert(lv_obj_get_width(portal_url_label) <= WIDTH - 88);
    assert(lv_obj_get_height(hotspot_label) == lv_font_montserrat_36.line_height);
    assert(lv_obj_get_height(portal_url_label) == lv_font_montserrat_32.line_height);
    strcpy(wifi.hotspot, "Solar-2F9C");
    ui_show_network(&wifi, &clock);
    screenshot("wifi-hotspot");
    assert(ui_handle_network_tap(300, 40, 1000000) == UI_ACTION_CANCEL_WIFI);
    assert(ui_handle_network_tap(245, 100, 1000001) == UI_ACTION_NONE);
    ui_show_battery(&reading);
    assert(ui_handle_network_tap(40, 250, 1000002) == UI_ACTION_NONE);
    assert(!ui_handle_history_tap(100, 330, 1000002));
    assert(!ui_handle_history_tap(100, 330, 1000003) && !seven_days);
    wifi.seconds_remaining = 1; ui_show_network(&wifi, &clock); screenshot("wifi-hotspot-last-second");
    wifi.portal = wifi.active = false; ui_show_network(&wifi, &clock); assert(lv_screen_active() == main_screen);
    assert(ui_handle_network_tap(40, 250, 1000003) == UI_ACTION_NONE);
    screenshot("wifi-back-to-main");
    for (int i = 0; i < 100; i++) {
        int64_t now = 2000000 + i * 1000000LL;
        ui_handle_network_tap(312, 38, now);
        ui_handle_network_tap(312, 38, now + 100000);
        assert(network_menu && lv_screen_active() == portal_screen);
        for (int poll = 0; poll < 10; poll++) {
            ui_show_network(&wifi, &clock);
            lv_refr_now(display);
        }
        assert(sync_enabled && lv_obj_get_style_opa(sync_button, 0) == LV_OPA_COVER);
        ui_handle_network_tap(300, 40, now + 200000);
        assert(!network_menu && lv_screen_active() == main_screen);
        lv_refr_now(display);
    }
    screenshot("wifi-after-100-menu-cycles");
    puts("PASS: real-history views, gray gaps/zero, 400 ms double taps, hidden-screen updates, battery colors, Wi-Fi actions, glyphs, bounds and redraw");
}
