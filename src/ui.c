#include <stdint.h>

#include "esp_log.h"

#include "board.h"
#include "display.h"
#include "radar.h"
#include "ui.h"

static lv_obj_t *tier_label, *theory_label, *detail_label, *fps_label, *touch_label;
static touch_status_t shown_touch_status = TOUCH_STATUS_OFF;

// Centred lines keep text clear of the panel's rounded corners.
static lv_obj_t *label(int y, const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(lv_screen_active());
    lv_obj_align(obj, LV_ALIGN_TOP_MID, 0, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    return obj;
}

void ui_set_tier(radar_tier_t tier)
{
    int percent = radar_tier_percent(tier);
    // Ideal 4-line QSPI payload: LCD_QSPI_HZ / 2 bytes per second, 2 bytes per pixel.
    int theory = (int)(LCD_QSPI_HZ / 2.0 / (percent / 100.0 * LCD_WIDTH * LCD_HEIGHT * 2));
    lv_label_set_text_fmt(tier_label, "%s %d%%  RGB565", tier == RADAR_TIER_FULL ? "Circle" : "Trail",
                          percent);
    lv_label_set_text_fmt(theory_label, "Bus limit %d FPS  |  QSPI %d MHz  |  FPS at right", theory,
                          LCD_QSPI_HZ / 1000000);
    lv_label_set_text(fps_label, "--");
    lv_label_set_text(detail_label, "Settling...");
}

void ui_show_stats(const display_stats_t *display, const radar_stats_t *radar)
{
    // LVGL's built-in printf has no %f; format fixed-point by hand.
    int mb = (int)(display->megabytes_per_second * 100), draw = (int)(radar->render_ms * 100);
    lv_label_set_text_fmt(fps_label, "%d", (int)(display->fps + 0.5f));
    lv_label_set_text_fmt(detail_label, "%d.%02d MB/s | bus %d%% | draw %d.%02d ms", mb / 100, mb % 100,
                          (int)(display->bus_percent + 0.5f), draw / 100, draw % 100);
}

void ui_show_touch(touch_status_t status)
{
    static bool error_pending;
    static uint32_t last_error_ms;
    if (status == TOUCH_STATUS_ERROR) {
        last_error_ms = lv_tick_get();
        error_pending = true;
    } else if (error_pending && lv_tick_elaps(last_error_ms) >= 500) {
        error_pending = false;
    }
    // Keep brief errors readable; a valid press still takes effect immediately.
    if (status == TOUCH_STATUS_UP && error_pending) status = TOUCH_STATUS_ERROR;
    if (status == shown_touch_status) return;
    shown_touch_status = status;
    const char *text = status == TOUCH_STATUS_DOWN ? "ON" : status == TOUCH_STATUS_UP ? "UP" :
                       status == TOUCH_STATUS_ERROR ? "ERR" : "OFF";
    uint32_t color = status == TOUCH_STATUS_DOWN ? 0x5AFFA4 : status == TOUCH_STATUS_ERROR ? 0xFF6B6B :
                     0x9FB3C4;
    lv_label_set_text(touch_label, text);
    lv_obj_set_style_text_color(touch_label, lv_color_hex(color), 0);
}

void ui_init(lv_display_t *display)
{
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    // Text stays above y=88, where the radar circle starts.
    tier_label = label(10, "", 0xFFFFFF);
    lv_obj_set_style_text_font(tier_label, &lv_font_montserrat_24, 0);
    theory_label = label(40, "", 0x9FB3C4);
    detail_label = label(56, "", 0x9FB3C4);
    label(72, "Swipe down: next | up: prev | BOOT: next", 0x5C7080);
    // Fixed box right of the circle's lower edge (circle reaches x=282 at y=410)
    // and inside the rounded corner, so LVGL never repaints radar pixels.
    fps_label = lv_label_create(screen);
    lv_obj_set_pos(fps_label, 286, 410);
    lv_obj_set_width(fps_label, 44);
    lv_obj_set_style_text_align(fps_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x5AFFA4), 0);
    lv_label_set_text(fps_label, "--");
    // Mirror the FPS box on the left, clear of both radar and rounded corner.
    touch_label = lv_label_create(screen);
    lv_obj_set_pos(touch_label, 38, 410);
    lv_obj_set_width(touch_label, 44);
    lv_obj_set_style_text_color(touch_label, lv_color_hex(0x9FB3C4), 0);
    lv_label_set_text(touch_label, "OFF");
}
