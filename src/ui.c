#include <stdint.h>

#include "esp_log.h"

#include "board.h"
#include "display.h"
#include "radar.h"
#include "ui.h"

static lv_obj_t *tier_label, *theory_label, *detail_label, *fps_label;

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
    label(72, "BOOT: next tier", 0x5C7080);
    // Fixed box right of the circle's lower edge (circle reaches x=282 at y=410)
    // and inside the rounded corner, so LVGL never repaints radar pixels.
    fps_label = lv_label_create(screen);
    lv_obj_set_pos(fps_label, 286, 410);
    lv_obj_set_width(fps_label, 44);
    lv_obj_set_style_text_align(fps_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(fps_label, lv_color_hex(0x5AFFA4), 0);
    lv_label_set_text(fps_label, "--");
}
