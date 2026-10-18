#include <stdint.h>

#include "esp_log.h"

#include "board.h"
#include "display.h"
#include "ui.h"

static lv_obj_t *mode_label, *fps_label, *details_label, *motion_bar, *blocks[6];
static lv_display_t *test_display;
static bool full_mode;
static uint32_t mode_start;
static bool settle_sample;
static const uint32_t colors[] = {0xFF3030, 0x30FF60, 0x3080FF, 0xFFFF30, 0x30FFFF, 0xFF30FF};

static lv_obj_t *rectangle(int x, int y, int width, int height, uint32_t color)
{
    lv_obj_t *obj = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, width, height);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *label(int x, int y, const char *text, uint32_t color)
{
    lv_obj_t *obj = lv_label_create(lv_screen_active());
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_text(obj, text);
    return obj;
}

static void animate(lv_timer_t *timer)
{
    uint32_t now = lv_tick_get();
    if (now - mode_start >= 15000) {
        full_mode = !full_mode;
        mode_start = now;
        settle_sample = true; // Exclude transition/mixed-mode samples.
        lv_label_set_text(mode_label, full_mode ? "FULL / whole-screen traffic" : "PARTIAL / moving regions");
        ESP_LOGI("benchmark", "mode=%s; first measurement interval is settling",
                 full_mode ? "FULL" : "PARTIAL");
    }
    // Time-based motion keeps speed constant when the rendering rate changes.
    int phase = (now / 4) % (2 * (LCD_WIDTH - 40));
    int x = phase <= LCD_WIDTH - 40 ? phase : 2 * (LCD_WIDTH - 40) - phase;
    lv_obj_set_x(motion_bar, x + 6);
    for (int i = 0; i < 6; i++) {
        int offset = (now / 6 + i * 45) % 240;
        int bx = i % 2 ? 310 - offset : 18 + offset;
        lv_obj_set_pos(blocks[i], bx, 166 + i * 26);
    }
    if (full_mode) lv_obj_invalidate(lv_display_get_screen_active(test_display));
}

static void update_stats(lv_timer_t *timer)
{
    display_stats_t stats = display_take_stats();
    if (settle_sample) {
        settle_sample = false;
        lv_label_set_text(fps_label, "Settling...");
        return;
    }
    lv_label_set_text_fmt(fps_label, "%.1f FPS", (double)stats.fps);
    lv_label_set_text_fmt(details_label, "%.2f MB/s | full eq %.1f FPS\nFrame avg %.1f / max %.1f ms",
                         (double)stats.megabytes_per_second, (double)stats.full_frame_fps,
                         (double)stats.average_ms, (double)stats.maximum_ms);
    ESP_LOGI("benchmark", "%s: completed=%.1f FPS, payload=%.2f MB/s, full_equiv=%.1f FPS, frame_avg=%.1f ms, frame_max=%.1f ms",
             full_mode ? "FULL" : "PARTIAL", (double)stats.fps, (double)stats.megabytes_per_second,
             (double)stats.full_frame_fps, (double)stats.average_ms, (double)stats.maximum_ms);
}

void ui_init(lv_display_t *display)
{
    test_display = display;
    lv_obj_t *screen = lv_display_get_screen_active(display);
    lv_obj_set_style_bg_color(screen, lv_color_black(), 0);
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    lv_obj_set_style_border_width(screen, 2, 0);
    lv_obj_set_style_border_color(screen, lv_color_hex(0x3D5265), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_t *title = label(14, 10, "", 0x72D9FF);
    lv_label_set_text_fmt(title, "AMOLED / QSPI %d MHz", LCD_QSPI_HZ / 1000000);
    mode_label = label(14, 34, "PARTIAL / moving regions", 0xFFFFFF);
    fps_label = label(14, 56, "Measuring...", 0x5AFFA4);
    lv_obj_set_style_text_font(fps_label, &lv_font_montserrat_24, 0);
    details_label = label(14, 88, "DMA-completed frame rate", 0xCDD7E0);
    label(14, 134, "Motion / tearing check", 0x7B93A8);
    for (int i = 0; i < 6; i++) blocks[i] = rectangle(18, 166 + i * 26, 40, 14, colors[i]);
    motion_bar = rectangle(6, 158, 5, 164, 0xFFFFFF);
    label(14, 330, "R       G       B       Y       C       M", 0x7B93A8);
    for (int i = 0; i < 6; i++) rectangle(14 + i * 56, 352, 56, 28, colors[i]);
    label(14, 386, "Gray ramp / edge border", 0x7B93A8);
    for (int i = 0; i < 16; i++) rectangle(14 + i * 21, 408, 21, 12, (i * 17) * 0x010101);
    label(14, 428, "368 x 448 | auto mode: 15s | no TE sync", 0x7B93A8);
    mode_start = lv_tick_get();
    settle_sample = true;
    lv_timer_create(animate, 16, NULL);
    lv_timer_create(update_stats, 1000, NULL);
}
