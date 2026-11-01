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

int main(void)
{
    lv_init(); display = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, buffer, NULL, sizeof(buffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush); ui_init(display);
    wifi_view_t wifi = {0}; clock_view_t clock = {0};
    ui_show_network(&wifi, &clock); assert(!lv_label_get_text(clock_label)[0]);
    screenshot("wifi-off");
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
        .sample = {.voltage_v = 4.10f, .current_a = 1.0f}, .session_wh = 1.0};
    ui_show_battery(&reading); ui_handle_tap(40, 250); assert(!show_mah);
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
    ui_show_battery(&reading); assert(!show_mah); ui_handle_tap(40, 250); assert(!show_mah);
    wifi.seconds_remaining = 1; ui_show_network(&wifi, &clock); screenshot("wifi-hotspot-last-second");
    wifi.portal = wifi.active = false; ui_show_network(&wifi, &clock); assert(lv_screen_active() == main_screen);
    ui_handle_tap(40, 250); assert(show_mah); screenshot("wifi-back-to-main");
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
    puts("PASS: hidden Wi-Fi hit area, manual sync/reopen hotspot actions, disabled/busy controls, time feedback, glyphs, bounds and redraw");
}
