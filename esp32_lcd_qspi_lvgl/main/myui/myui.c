#include "myui.h"
#include "ui.h"
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include "esp_heap_caps.h"
//#include "gif1.c"

static const char *TAG = "myui";

//gif显示
void gif_show()
{
	LV_IMG_DECLARE(gif2);
	lv_obj_t* img1 = lv_gif_create(ui_Container5);
	lv_gif_set_src(img1, &gif2);
	lv_obj_align(img1, LV_ALIGN_CENTER, 0, 0);
	
	
	LV_IMG_DECLARE(gif5);
	lv_obj_t* img2 = lv_gif_create(ui_Container6);
	lv_gif_set_src(img2, &gif5);
	lv_obj_align(img2, LV_ALIGN_CENTER, 0, 0);
}
#include "esp_log.h"
//设置温度
void set_temp(lv_timer_t *timer)
{
	static int temp_value=0;
	static char str[24]={0};
	
	temp_value = temp_value>=100? 0:temp_value+5;
	
	sprintf(str, "%d", temp_value);
	lv_label_set_text(ui_temp_label, str);
	lv_bar_set_value(ui_Bar1, temp_value, LV_ANIM_ON);

}

//设置湿度
void set_humi(lv_timer_t *timer)
{
	static int humi_value=0;
	static char str[24]={0};
	
	humi_value = humi_value>=100? 0:humi_value+2;
	
	sprintf(str, "%d", humi_value);
	lv_label_set_text(ui_humi_label, str);
	lv_bar_set_value(ui_Bar2, humi_value, LV_ANIM_ON);
}
	
static const lv_img_dsc_t * ui_imgset_tq[7] = {&ui_img_tq1_png, &ui_img_tq2_png, &ui_img_tq3_png, &ui_img_tq4_png, &ui_img_tq5_png, &ui_img_tq6_png, &ui_img_tq7_png}; //图片数组
static lv_obj_t *weather_img[7];
//天气    图片预加载
void weather_img_show()
{
	int i=0;
	
	for(i=0;i<7;i++)
	{
		weather_img[i] = lv_img_create(ui_weather_container1);
		lv_img_set_src(weather_img[i], ui_imgset_tq[i]);
		lv_obj_set_width(weather_img[i], LV_SIZE_CONTENT);   /// 50
		lv_obj_set_height(weather_img[i], LV_SIZE_CONTENT);    /// 50
		lv_obj_set_x(weather_img[i], 0);
		lv_obj_set_y(weather_img[i], -16);
		lv_obj_set_align(weather_img[i], LV_ALIGN_CENTER);
		lv_obj_add_flag(weather_img[i], LV_OBJ_FLAG_ADV_HITTEST || LV_OBJ_FLAG_HIDDEN);     /// Flags		
		
	}

	lv_obj_clear_flag(ui_Image10, LV_OBJ_FLAG_SCROLLABLE);      /// Flags
}

//天气显示
void weather_show(lv_timer_t *timer)
{
	static int i=0;
	static char *weather_buff[7] = {"晴", "多云", "阴", "阵雨", "雨", "雪", "雾"};

	if(i==0)
	{
		lv_obj_clear_flag(weather_img[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(weather_img[6], LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_clear_flag(weather_img[i], LV_OBJ_FLAG_HIDDEN);
		lv_obj_add_flag(weather_img[i-1], LV_OBJ_FLAG_HIDDEN);
	}

	lv_label_set_text(ui_weather_label1, weather_buff[i]);
	i = i>=6 ? 0:i+1;
	
}

//初始化
void my_ui_init()
{

	gif_show();	//gif显示
	
	lv_timer_t *temp_up = lv_timer_create(set_temp, 500, NULL);	//设置温度
	
	lv_timer_t *humi_up = lv_timer_create(set_humi, 500, NULL);	//设置湿度
	
	weather_img_show();	//天气图片预加载
	lv_timer_t *weather_up = lv_timer_create(weather_show, 1000, NULL);	//设置湿度

}


#define COL_CYAN    0x00E5FF
#define COL_GREEN   0x00FF88
#define COL_WHITE   0xFFFFFF
#define COL_GRAY    0xAAAAAA
#define COL_DIM     0x888888
#define COL_DARK    0x1A1A1A
#define COL_RED     0xFF5555
#define COL_NAV_OFF 0x666666
#define COL_BLACK   0x000000
#define COL_HEART   0xFF3B5C
#define COL_AMBER   0xFFB74D
#define COL_VIOLET  0xB388FF

static lv_obj_t *pages[4];
static lv_obj_t *nav_labels[4];
static lv_obj_t *nav_icons[4];

static lv_obj_t *lbl_big;
static lv_obj_t *lbl_avg;
static lv_obj_t *lbl_max;
#define CHART_W    356
#define CHART_H    104
#define MAX_POINTS 60
static lv_obj_t *cv;
static float chart_pts[MAX_POINTS];
static float chart_cur, chart_tgt, chart_t;
static lv_obj_t *lbl_temp;
static lv_obj_t *lbl_humi;
static lv_obj_t *lbl_pres;

static lv_obj_t *lbl_unit;
static lv_obj_t *lbl_bright;
static lv_obj_t *slider_bright;
static lv_obj_t *sw_beeper;
static lv_obj_t *sw_sleep;

static lv_obj_t *clean_obj(lv_obj_t *parent)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_remove_style_all(obj);
    return obj;
}

static lv_obj_t *make_page(lv_obj_t *parent)
{
    lv_obj_t *page = clean_obj(parent);
    lv_obj_set_size(page, lv_pct(100), lv_pct(100));
    return page;
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, uint32_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    if (font) {
        lv_obj_set_style_text_font(label, font, 0);
    }
    return label;
}

static lv_obj_t *title_bar(lv_obj_t *parent, const char *title, const char *sub)
{
    lv_obj_t *bar = clean_obj(parent);
    lv_obj_set_width(bar, lv_pct(100));
    lv_obj_set_height(bar, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(bar, 6, 0);
    lv_obj_set_style_border_side(bar, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(bar, 1, 0);
    lv_obj_set_style_border_color(bar, lv_color_hex(COL_DARK), 0);
    make_label(bar, title, COL_WHITE, &lv_font_montserrat_14);
    make_label(bar, sub, COL_DIM, NULL);
    return bar;
}

static int cur_tab;

static void switch_tab(int idx)
{
    cur_tab = idx;
    for (int i = 0; i < 4; i++) {
        uint32_t nav_col = (i == idx) ? COL_CYAN : COL_NAV_OFF;
        lv_obj_set_style_text_color(nav_icons[i], lv_color_hex(nav_col), 0);
        lv_obj_set_style_text_color(nav_labels[i], lv_color_hex(nav_col), 0);
        if (i == idx) {
            lv_obj_remove_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(pages[i], LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void nav_event_cb(lv_event_t *e)
{
    switch_tab((int)(intptr_t)lv_event_get_user_data(e));
}

static lv_obj_tree_walk_res_t bubble_gesture_cb(lv_obj_t *obj, void *ud)
{
    if (obj != (lv_obj_t *)ud) {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
    }
    return LV_OBJ_TREE_WALK_NEXT;
}

static void gesture_event_cb(lv_event_t *e)
{
    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir == LV_DIR_LEFT) {
        switch_tab((cur_tab + 1) % 4);
    } else if (dir == LV_DIR_RIGHT) {
        switch_tab((cur_tab + 3) % 4);
    }
}

static void chart_redraw(void)
{
    lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
    lv_layer_t layer;
    lv_canvas_init_layer(cv, &layer);

    lv_draw_line_dsc_t ld;
    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_white();
    ld.opa = 13;
    ld.width = 1;
    for (int i = 1; i < 4; i++) {
        float y = (float)CHART_H * i / 4.0f;
        ld.p1.x = 0; ld.p1.y = y;
        ld.p2.x = CHART_W - 1; ld.p2.y = y;
        lv_draw_line(&layer, &ld);
    }

    float step = (float)CHART_W / (MAX_POINTS - 1);

    lv_draw_triangle_dsc_t td;
    lv_draw_triangle_dsc_init(&td);
    lv_draw_rect_dsc_t rd;
    lv_draw_rect_dsc_init(&rd);
    td.bg_grad.dir = LV_GRAD_DIR_VER;
    rd.bg_grad.dir = LV_GRAD_DIR_VER;
    td.bg_grad.stops[0].color = lv_color_hex(COL_HEART);
    td.bg_grad.stops[1].color = lv_color_hex(COL_HEART);
    rd.bg_grad.stops[0].color = lv_color_hex(COL_HEART);
    rd.bg_grad.stops[1].color = lv_color_hex(COL_HEART);
    td.bg_grad.stops[0].frac = 0;
    td.bg_grad.stops[1].frac = 255;
    rd.bg_grad.stops[0].frac = 0;
    rd.bg_grad.stops[1].frac = 255;
    for (int i = 0; i < MAX_POINTS - 1; i++) {
        float x1 = i * step, x2 = x1 + step;
        float y1 = chart_pts[i], y2 = chart_pts[i + 1];
        int32_t f_up = (int32_t)(LV_MIN(y1, y2) * 255 / CHART_H);
        int32_t f_lo = (int32_t)(LV_MAX(y1, y2) * 255 / CHART_H);
        td.bg_grad.stops[0].opa = 255 - f_up;
        td.bg_grad.stops[1].opa = 255 - f_lo;
        td.p[0].x = (int32_t)x1; td.p[0].y = (int32_t)y1;
        td.p[1].x = (int32_t)x2; td.p[1].y = (int32_t)y2;
        td.p[2].x = (int32_t)(y1 < y2 ? x1 : x2);
        td.p[2].y = (int32_t)LV_MAX(y1, y2);
        lv_draw_triangle(&layer, &td);

        rd.bg_grad.stops[0].opa = 255 - f_lo;
        rd.bg_grad.stops[1].opa = 0;
        lv_area_t ra = {
            .x1 = (int32_t)x1, .x2 = (int32_t)x2,
            .y1 = (int32_t)LV_MAX(y1, y2), .y2 = CHART_H - 1,
        };
        lv_draw_rect(&layer, &rd, &ra);
    }

    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(COL_HEART);
    ld.opa = 50;
    ld.width = 7;
    ld.round_start = 1;
    ld.round_end = 1;
    for (int i = 0; i < MAX_POINTS - 1; i++) {
        ld.p1.x = i * step; ld.p1.y = chart_pts[i];
        ld.p2.x = (i + 1) * step; ld.p2.y = chart_pts[i + 1];
        lv_draw_line(&layer, &ld);
    }

    lv_draw_line_dsc_init(&ld);
    ld.color = lv_color_hex(COL_HEART);
    ld.width = 3;
    for (int i = 0; i < MAX_POINTS - 1; i++) {
        float x2 = (i + 1) * step;
        float f = x2 / (CHART_W * 0.2f);
        if (f > 1.0f) f = 1.0f;
        ld.opa = (lv_opa_t)(f * 255.0f);
        ld.p1.x = i * step; ld.p1.y = chart_pts[i];
        ld.p2.x = x2; ld.p2.y = chart_pts[i + 1];
        lv_draw_line(&layer, &ld);
    }

    lv_canvas_finish_layer(cv, &layer);
    lv_obj_invalidate(cv);
}

static void chart_timer_cb(lv_timer_t *t)
{
    chart_t += 0.05f;
    float half = CHART_H / 2.0f;
    chart_tgt = (sinf(chart_t) * 0.5f + sinf(chart_t * 0.3f) * 0.5f) * (CHART_H * 0.3f) + half;
    chart_tgt += ((rand() % 100) / 100.0f - 0.5f) * 2.0f;
    float lo = CHART_H * 0.1f, hi = CHART_H * 0.9f;
    if (chart_tgt < lo) chart_tgt = lo;
    if (chart_tgt > hi) chart_tgt = hi;

    chart_cur += (chart_tgt - chart_cur) * 0.08f;

    memmove(&chart_pts[0], &chart_pts[1], (MAX_POINTS - 1) * sizeof(float));
    chart_pts[MAX_POINTS - 1] = chart_cur;

    chart_redraw();
}

static void build_measure_page(lv_obj_t *parent)
{
    lv_obj_t *page = make_page(parent);
    pages[0] = page;
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(page, 6, 0);
    lv_obj_set_style_pad_bottom(page, 4, 0);

    lv_obj_t *tags = clean_obj(page);
    lv_obj_set_width(tags, lv_pct(100));
    lv_obj_set_height(tags, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tags, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(tags, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);

    lv_obj_t *pill_auto = clean_obj(tags);
    lv_obj_set_size(pill_auto, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pill_auto, 12, 0);
    lv_obj_set_style_pad_ver(pill_auto, 4, 0);
    lv_obj_set_style_radius(pill_auto, 14, 0);
    lv_obj_set_style_bg_color(pill_auto, lv_color_hex(COL_GREEN), 0);
    lv_obj_set_style_margin_right(pill_auto, 6, 0);
    make_label(pill_auto, "AUTO", COL_BLACK, &lv_font_montserrat_14);

    lv_obj_t *pill_hold = clean_obj(tags);
    lv_obj_set_size(pill_hold, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_pad_hor(pill_hold, 12, 0);
    lv_obj_set_style_pad_ver(pill_hold, 4, 0);
    lv_obj_set_style_radius(pill_hold, 14, 0);
    lv_obj_set_style_border_width(pill_hold, 1, 0);
    lv_obj_set_style_border_color(pill_hold, lv_color_hex(COL_VIOLET), 0);
    make_label(pill_hold, "HOLD", COL_VIOLET, &lv_font_montserrat_14);

    lv_obj_t *ic;
    ic = make_label(tags, LV_SYMBOL_WIFI, COL_CYAN, NULL);
    lv_obj_set_style_margin_left(ic, 14, 0);
    ic = make_label(tags, LV_SYMBOL_BLUETOOTH, COL_VIOLET, NULL);
    lv_obj_set_style_margin_left(ic, 8, 0);
    ic = make_label(tags, LV_SYMBOL_BATTERY_FULL " 95%", COL_GREEN, NULL);
    lv_obj_set_style_margin_left(ic, 8, 0);

    lv_obj_t *num_row = clean_obj(page);
    lv_obj_set_width(num_row, lv_pct(100));
    lv_obj_set_height(num_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(num_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(num_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lbl_big = make_label(num_row, "12.34", COL_WHITE, &lv_font_montserrat_48);
    make_label(num_row, " m/s", COL_GRAY, &lv_font_montserrat_18);

    lv_obj_t *stats = clean_obj(page);
    lv_obj_set_width(stats, lv_pct(100));
    lv_obj_set_height(stats, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(stats, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(stats, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    make_label(stats, "VEL", COL_WHITE, NULL);
    lbl_avg = make_label(stats, "AVG 12.28", COL_AMBER, NULL);
    lbl_max = make_label(stats, "MAX 12.87", COL_HEART, NULL);

    cv = lv_canvas_create(page);
    void *cbuf = heap_caps_malloc(CHART_W * CHART_H * 2, MALLOC_CAP_INTERNAL);
    if (cbuf) {
        lv_canvas_set_buffer(cv, cbuf, CHART_W, CHART_H, LV_COLOR_FORMAT_RGB565);
        lv_obj_set_width(cv, lv_pct(100));
        lv_obj_set_flex_grow(cv, 1);
        lv_obj_set_style_bg_color(cv, lv_color_hex(COL_HEART), 0);
        lv_obj_set_style_bg_opa(cv, 8, 0);
        lv_obj_set_style_border_color(cv, lv_color_hex(COL_DARK), 0);
        lv_obj_set_style_border_width(cv, 1, 0);
        lv_obj_set_style_radius(cv, 8, 0);
        lv_obj_set_style_pad_all(cv, 6, 0);
        lv_canvas_fill_bg(cv, lv_color_black(), LV_OPA_COVER);
        for (int i = 0; i < MAX_POINTS; i++) {
            chart_pts[i] = CHART_H / 2.0f + sinf(i * 0.2f) * (CHART_H * 0.2f);
        }
        chart_cur = chart_tgt = CHART_H / 2.0f;
        lv_timer_create(chart_timer_cb, 100, NULL);
    }

    lv_obj_t *mets = clean_obj(page);
    lv_obj_set_width(mets, lv_pct(100));
    lv_obj_set_height(mets, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(mets, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(mets, 6, 0);

    static const char *met_names[3] = {LV_SYMBOL_UP " TEMP", LV_SYMBOL_TINT " RH", LV_SYMBOL_BARS " PRES"};
    lv_obj_t *met_vals[3];
    static const uint32_t met_cols[3] = {COL_AMBER, COL_CYAN, COL_VIOLET};
    for (int i = 0; i < 3; i++) {
        lv_obj_t *card = clean_obj(mets);
        lv_obj_set_flex_grow(card, 1);
        lv_obj_set_height(card, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_pad_all(card, 6, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(COL_DARK), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_radius(card, 8, 0);
        make_label(card, met_names[i], met_cols[i], NULL);
        met_vals[i] = make_label(card, "--", COL_WHITE, &lv_font_montserrat_18);
    }
    lbl_temp = met_vals[0];
    lbl_humi = met_vals[1];
    lbl_pres = met_vals[2];
}

static void build_record_page(lv_obj_t *parent)
{
    lv_obj_t *page = make_page(parent);
    pages[1] = page;
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

    title_bar(page, "HISTORY", "12 REC");

    static const char *dates[4] = {"07/12 14:30", "07/12 13:15", "07/12 11:00", "07/12 09:30"};
    static const char *vals[4] = {"12.34", "11.89", "10.50", "9.21"};
    static const char *maxs[4] = {"12.87", "12.45", "11.10", "9.80"};

    for (int i = 0; i < 4; i++) {
        lv_obj_t *row = lv_button_create(page);
        lv_obj_set_width(row, lv_pct(100));
        lv_obj_set_height(row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(row, 0, 0);
        lv_obj_set_style_radius(row, 0, 0);
        lv_obj_set_style_pad_ver(row, 10, 0);
        lv_obj_set_style_pad_hor(row, 4, 0);
        lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(row, 1, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(COL_DARK), 0);

        lv_obj_t *left = clean_obj(row);
        lv_obj_set_flex_grow(left, 1);
        lv_obj_set_height(left, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(left, LV_FLEX_FLOW_COLUMN);
        make_label(left, dates[i], COL_DIM, NULL);
        lv_obj_t *val_row = clean_obj(left);
        lv_obj_set_width(val_row, LV_SIZE_CONTENT);
        lv_obj_set_height(val_row, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(val_row, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(val_row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
        make_label(val_row, vals[i], COL_WHITE, &lv_font_montserrat_18);
        make_label(val_row, " m/s", COL_CYAN, NULL);

        lv_obj_t *right = clean_obj(row);
        lv_obj_set_height(right, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(right, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(right, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
        make_label(right, "MAX", COL_HEART, NULL);
        make_label(right, maxs[i], COL_WHITE, &lv_font_montserrat_18);
    }
}

static lv_obj_t *setting_row(lv_obj_t *parent, const char *text)
{
    lv_obj_t *row = lv_button_create(parent);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_pad_ver(row, 14, 0);
    lv_obj_set_style_pad_hor(row, 4, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(COL_DARK), 0);
    make_label(row, text, COL_WHITE, &lv_font_montserrat_14);
    return row;
}

static void unit_event_cb(lv_event_t *e)
{
    const char *t = lv_label_get_text(lbl_unit);
    if (strcmp(t, "m/s") == 0) {
        lv_label_set_text(lbl_unit, "km/h");
    } else if (strcmp(t, "km/h") == 0) {
        lv_label_set_text(lbl_unit, "mph");
    } else {
        lv_label_set_text(lbl_unit, "m/s");
    }
}

extern void lcd_set_brightness(uint8_t percent);

static void bright_slider_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int v = lv_slider_get_value(slider);
    lv_label_set_text_fmt(lbl_bright, "%d%%", v);
    lcd_set_brightness((uint8_t)v);
}

static void build_setup_page(lv_obj_t *parent)
{
    lv_obj_t *page = make_page(parent);
    pages[2] = page;
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

    title_bar(page, "SETTINGS", "TAP TO CHANGE");

    lv_obj_t *row1 = setting_row(page, "Wind Unit");
    lbl_unit = make_label(row1, "m/s", COL_AMBER, &lv_font_montserrat_18);
    lv_obj_add_event_cb(row1, unit_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *row2 = setting_row(page, "Brightness");
    lbl_bright = make_label(row2, "80%", COL_VIOLET, &lv_font_montserrat_18);

    slider_bright = lv_slider_create(page);
    lv_obj_set_width(slider_bright, lv_pct(82));
    lv_obj_set_height(slider_bright, 10);
    lv_slider_set_range(slider_bright, 5, 100);
    lv_slider_set_value(slider_bright, 80, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider_bright, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider_bright, lv_color_hex(COL_VIOLET), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider_bright, lv_color_white(), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider_bright, 8, LV_PART_KNOB);
    lv_obj_add_event_cb(slider_bright, bright_slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lcd_set_brightness(80);

    lv_obj_t *row3 = setting_row(page, "Beeper");
    sw_beeper = lv_switch_create(row3);
    lv_obj_set_size(sw_beeper, 40, 20);
    lv_obj_set_style_bg_color(sw_beeper, lv_color_hex(COL_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_add_state(sw_beeper, LV_STATE_CHECKED);

    lv_obj_t *row4 = setting_row(page, "Auto Sleep");
    sw_sleep = lv_switch_create(row4);
    lv_obj_set_size(sw_sleep, 40, 20);
    lv_obj_set_style_bg_color(sw_sleep, lv_color_hex(COL_GREEN), LV_PART_INDICATOR | LV_STATE_CHECKED);
}

static void reset_event_cb(lv_event_t *e)
{
    lv_label_set_text(lbl_unit, "m/s");
    lv_slider_set_value(slider_bright, 80, LV_ANIM_OFF);
    lv_label_set_text(lbl_bright, "80%");
    lcd_set_brightness(80);
    lv_obj_add_state(sw_beeper, LV_STATE_CHECKED);
    lv_obj_remove_state(sw_sleep, LV_STATE_CHECKED);
}

static void build_more_page(lv_obj_t *parent)
{
    lv_obj_t *page = make_page(parent);
    pages[3] = page;
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

    title_bar(page, "DEVICE INFO", "MORE");

    lv_obj_t *card = clean_obj(page);
    lv_obj_set_width(card, lv_pct(100));
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, 8, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_DARK), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_pad_row(card, 10, 0);

    static const char *keys[4] = {"Model", "Firmware", "Serial", "Battery"};
    static const char *vals[4] = {"WindMaster Pro", "V2.0.1", "SN20260905", "3.82 V"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *r = clean_obj(card);
        lv_obj_set_width(r, lv_pct(100));
        lv_obj_set_height(r, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
        lv_obj_set_flex_align(r, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        make_label(r, keys[i], COL_DIM, NULL);
        make_label(r, vals[i], COL_WHITE, NULL);
    }

    lv_obj_t *btn = lv_button_create(page);
    lv_obj_set_width(btn, lv_pct(100));
    lv_obj_set_height(btn, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_bg_opa(btn, 50, 0);
    lv_obj_set_style_border_color(btn, lv_color_hex(COL_RED), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_pad_ver(btn, 12, 0);
    lv_obj_t *rl = make_label(btn, LV_SYMBOL_TRASH "  RESET SETTINGS", COL_RED, &lv_font_montserrat_14);
    lv_obj_center(rl);
    lv_obj_add_event_cb(btn, reset_event_cb, LV_EVENT_CLICKED, NULL);
}

static void build_header(lv_obj_t *parent)
{
    lv_obj_t *header = clean_obj(parent);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_bottom(header, 10, 0);
    make_label(header, "10:30", COL_GRAY, NULL);
}

static void build_nav(lv_obj_t *parent)
{
    lv_obj_t *nav = clean_obj(parent);
    lv_obj_set_width(nav, lv_pct(100));
    lv_obj_set_height(nav, 82);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_AROUND, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_top(nav, 8, 0);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(nav, 1, 0);
    lv_obj_set_style_border_color(nav, lv_color_hex(COL_DARK), 0);

    static const char *icons[4] = {LV_SYMBOL_GPS, LV_SYMBOL_LIST, LV_SYMBOL_SETTINGS, LV_SYMBOL_EYE_OPEN};
    static const char *names[4] = {"MEASURE", "RECORD", "SETUP", "MORE"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t *btn = lv_button_create(nav);
        lv_obj_set_size(btn, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(btn, LV_FLEX_FLOW_COLUMN);
        lv_obj_set_flex_align(btn, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
        lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
        lv_obj_set_style_shadow_width(btn, 0, 0);
        lv_obj_set_style_radius(btn, 0, 0);
        lv_obj_set_style_pad_all(btn, 0, 0);
        nav_icons[i] = make_label(btn, icons[i], COL_NAV_OFF, &lv_font_montserrat_18);
        nav_labels[i] = make_label(btn, names[i], COL_NAV_OFF, NULL);
        lv_obj_add_event_cb(btn, nav_event_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }
}

static void measure_timer_cb(lv_timer_t *t)
{
    static float vel = 12.34f;
    static float avg = 12.28f;
    static float vmax = 12.87f;
    static int n = 1;

    vel += (rand() % 100 - 48) / 40.0f;
    if (vel < 9.0f) vel = 9.0f;
    if (vel > 18.0f) vel = 18.0f;
    avg = (avg * n + vel) / (n + 1);
    n++;
    if (vel > vmax) vmax = vel;

    lv_label_set_text_fmt(lbl_big, "%d.%02d", (int)vel, (int)(vel * 100) % 100);
    lv_label_set_text_fmt(lbl_avg, "AVG %d.%02d", (int)avg, (int)(avg * 100) % 100);
    lv_label_set_text_fmt(lbl_max, "MAX %d.%02d", (int)vmax, (int)(vmax * 100) % 100);
}

static void env_timer_cb(lv_timer_t *t)
{
    static float temp = 25.6f;
    static float humi = 45.2f;
    static float pres = 1013.2f;

    temp += (rand() % 10 - 5) / 10.0f;
    humi += (rand() % 10 - 5) / 10.0f;
    pres += (rand() % 10 - 5) / 10.0f;
    if (temp < 20.0f) temp = 20.0f;
    if (temp > 30.0f) temp = 30.0f;
    if (humi < 35.0f) humi = 35.0f;
    if (humi > 60.0f) humi = 60.0f;

    lv_label_set_text_fmt(lbl_temp, "%d.%d C", (int)temp, (int)(temp * 10) % 10);
    lv_label_set_text_fmt(lbl_humi, "%d.%d %%", (int)humi, (int)(humi * 10) % 10);
    lv_label_set_text_fmt(lbl_pres, "%d.%d hPa", (int)pres, (int)(pres * 10) % 10);
}

void wind_ui_init(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    build_header(scr);

    lv_obj_t *content = clean_obj(scr);
    lv_obj_set_width(content, lv_pct(100));
    lv_obj_set_flex_grow(content, 1);

    build_measure_page(content);
    build_record_page(content);
    build_setup_page(content);
    build_more_page(content);

    for (int i = 1; i < 4; i++) {
        lv_obj_set_style_pad_top(pages[i], 50, 0);
    }

    build_nav(scr);

    switch_tab(0);

    lv_obj_tree_walk(scr, bubble_gesture_cb, scr);
    lv_obj_remove_flag(slider_bright, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(scr, gesture_event_cb, LV_EVENT_GESTURE, NULL);

    lv_screen_load(scr);

    lv_timer_create(measure_timer_cb, 500, NULL);
    lv_timer_create(env_timer_cb, 2000, NULL);
}

/* ===================== 智能手环 UI ===================== */
#define BAND_PTS      48

static lv_obj_t *band_chart;
static lv_chart_series_t *band_ser;
static lv_obj_t *band_lbl_hr;
static lv_obj_t *band_lbl_spo2;
static lv_obj_t *band_lbl_temp;
static lv_obj_t *band_lbl_humi;
static lv_obj_t *band_heart;
static float band_bpm = 74.0f;

//折线图渐变面积+按下数值提示 (与 lv_demo_widgets 的 chart_event_cb 完全一致)
static void band_chart_draw_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t *obj = lv_event_get_target(e);

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_RELEASED) {
        lv_obj_invalidate(obj);
    }
    else if (code == LV_EVENT_DRAW_TASK_ADDED) {
        lv_draw_task_t *draw_task = lv_event_get_param(e);
        lv_draw_dsc_base_t *base_dsc = lv_draw_task_get_draw_dsc(draw_task);

        lv_draw_line_dsc_t *line_dsc = lv_draw_task_get_line_dsc(draw_task);
        if (base_dsc->part == LV_PART_ITEMS && line_dsc) {

    lv_area_t obj_coords;
    lv_obj_get_coords(obj, &obj_coords);
    const lv_chart_series_t *ser = lv_chart_get_series_next(obj, NULL);

    lv_draw_triangle_dsc_t tri_dsc;
    lv_draw_triangle_dsc_init(&tri_dsc);
    tri_dsc.p[0].x = (int32_t)line_dsc->p1.x;
    tri_dsc.p[0].y = (int32_t)line_dsc->p1.y;
    tri_dsc.p[1].x = (int32_t)line_dsc->p2.x;
    tri_dsc.p[1].y = (int32_t)line_dsc->p2.y;
    tri_dsc.p[2].x = (int32_t)(line_dsc->p1.y < line_dsc->p2.y ? line_dsc->p1.x : line_dsc->p2.x);
    tri_dsc.p[2].y = (int32_t)LV_MAX(line_dsc->p1.y, line_dsc->p2.y);
    tri_dsc.bg_grad.dir = LV_GRAD_DIR_VER;

    int32_t full_h = lv_obj_get_height(obj);
    int32_t fract_uppter = (int32_t)(LV_MIN(line_dsc->p1.y, line_dsc->p2.y) - obj_coords.y1) * 255 / full_h;
    int32_t fract_lower = (int32_t)(LV_MAX(line_dsc->p1.y, line_dsc->p2.y) - obj_coords.y1) * 255 / full_h;
    tri_dsc.bg_grad.stops[0].color = lv_chart_get_series_color(obj, ser);
    tri_dsc.bg_grad.stops[0].opa = 255 - fract_uppter;
    tri_dsc.bg_grad.stops[0].frac = 0;
    tri_dsc.bg_grad.stops[1].color = lv_chart_get_series_color(obj, ser);
    tri_dsc.bg_grad.stops[1].opa = 255 - fract_lower;
    tri_dsc.bg_grad.stops[1].frac = 255;

    lv_draw_triangle(base_dsc->layer, &tri_dsc);

    lv_draw_rect_dsc_t rect_dsc;
    lv_draw_rect_dsc_init(&rect_dsc);
    rect_dsc.bg_grad.dir = LV_GRAD_DIR_VER;
    rect_dsc.bg_grad.stops[0].color = lv_chart_get_series_color(obj, ser);
    rect_dsc.bg_grad.stops[0].frac = 0;
    rect_dsc.bg_grad.stops[0].opa = 255 - fract_lower;
    rect_dsc.bg_grad.stops[1].color = lv_chart_get_series_color(obj, ser);
    rect_dsc.bg_grad.stops[1].frac = 255;
    rect_dsc.bg_grad.stops[1].opa = 0;

    lv_area_t rect_area;
    rect_area.x1 = (int32_t)line_dsc->p1.x;
    rect_area.x2 = (int32_t)line_dsc->p2.x;
    rect_area.y1 = (int32_t)LV_MAX(line_dsc->p1.y, line_dsc->p2.y);
    rect_area.y2 = obj_coords.y2;
    lv_draw_rect(base_dsc->layer, &rect_dsc, &rect_area);

        }

        bool add_value = false;
        if (base_dsc->part == LV_PART_INDICATOR && lv_chart_get_pressed_point(obj) == base_dsc->id2) {
            lv_draw_rect_dsc_t outline_dsc;
            lv_draw_rect_dsc_init(&outline_dsc);
            outline_dsc.bg_opa = LV_OPA_TRANSP;
            outline_dsc.outline_color = lv_color_white();
            outline_dsc.outline_width = 2;
            outline_dsc.radius = LV_RADIUS_CIRCLE;
            lv_area_t draw_task_area;
            lv_draw_task_get_area(draw_task, &draw_task_area);
            lv_draw_rect(base_dsc->layer, &outline_dsc, &draw_task_area);
            add_value = true;
        }

        if (add_value) {
            const lv_chart_series_t *ser = lv_chart_get_series_next(obj, NULL);
            char buf[8];
            lv_snprintf(buf, sizeof(buf), "%" LV_PRId32, lv_chart_get_y_array(obj, (lv_chart_series_t *)ser)[base_dsc->id2]);

            lv_point_t text_size;
            lv_text_get_size(&text_size, buf, NULL, 0, 0, LV_COORD_MAX, LV_TEXT_FLAG_NONE);

            lv_area_t txt_area;
            lv_area_t draw_task_area;
            lv_draw_task_get_area(draw_task, &draw_task_area);
            txt_area.y2 = draw_task_area.y1 - LV_DPX(15);
            txt_area.y1 = txt_area.y2 - text_size.y;
            txt_area.x1 = draw_task_area.x1 + (lv_area_get_width(&draw_task_area) - text_size.x) / 2;
            txt_area.x2 = txt_area.x1 + text_size.x;

            lv_area_t bg_area;
            bg_area.x1 = txt_area.x1 - LV_DPX(8);
            bg_area.x2 = txt_area.x2 + LV_DPX(8);
            bg_area.y1 = txt_area.y1 - LV_DPX(8);
            bg_area.y2 = txt_area.y2 + LV_DPX(8);

            lv_draw_rect_dsc_t rect_dsc;
            lv_draw_rect_dsc_init(&rect_dsc);
            rect_dsc.bg_color = lv_chart_get_series_color(obj, ser);
            rect_dsc.radius = LV_DPX(5);
            lv_draw_rect(base_dsc->layer, &rect_dsc, &bg_area);

            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_white();
            label_dsc.text = buf;
            label_dsc.text_local = true;
            lv_draw_label(base_dsc->layer, &label_dsc, &txt_area);
        }
    }
}

//折线图动态数据更新
static void band_chart_timer_cb(lv_timer_t *t)
{
    static int32_t val = 60;
    val += lv_rand(-8, 8);
    if (val < 15) val = 15;
    if (val > 92) val = 92;
    lv_chart_set_next_value(band_chart, band_ser, val);
    lv_obj_invalidate(band_chart);
}

//心率数值更新
static void band_hr_timer_cb(lv_timer_t *t)
{
    band_bpm += (rand() % 7 - 3) * 0.7f;
    if (band_bpm < 62.0f) band_bpm = 62.0f;
    if (band_bpm > 96.0f) band_bpm = 96.0f;
    lv_label_set_text_fmt(band_lbl_hr, "%d", (int)band_bpm);
}

//血氧/温湿度更新
static void band_env_timer_cb(lv_timer_t *t)
{
    static float spo2 = 98.2f;
    static float temp = 36.6f;
    static float humi = 45.0f;

    spo2 += (rand() % 5 - 2) * 0.2f;
    temp += (rand() % 5 - 2) * 0.1f;
    humi += (rand() % 5 - 2) * 0.3f;
    if (spo2 < 95.0f) spo2 = 95.0f;
    if (spo2 > 100.0f) spo2 = 100.0f;
    if (temp < 36.2f) temp = 36.2f;
    if (temp > 37.2f) temp = 37.2f;
    if (humi < 38.0f) humi = 38.0f;
    if (humi > 58.0f) humi = 58.0f;

    lv_label_set_text_fmt(band_lbl_spo2, "%d%%", (int)spo2);
    lv_label_set_text_fmt(band_lbl_temp, "%d.%d C", (int)temp, (int)(temp * 10) % 10);
    lv_label_set_text_fmt(band_lbl_humi, "%d%%", (int)humi);
}

//心形脉动动画
static void band_heart_opa_cb(void *var, int32_t v)
{
    lv_obj_set_style_text_opa((lv_obj_t *)var, (lv_opa_t)v, 0);
}

//底部指标卡片
static void band_metric_card(lv_obj_t *parent, const char *icon, const char *name,
                              uint32_t accent, const char *unit, lv_obj_t **val_out)
{
    lv_obj_t *card = clean_obj(parent);
    lv_obj_set_flex_grow(card, 1);
    lv_obj_set_height(card, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(card, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(card, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_set_style_pad_row(card, 2, 0);
    lv_obj_set_style_bg_color(card, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(card, 8, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(COL_DARK), 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_radius(card, 10, 0);

    lv_obj_t *head = clean_obj(card);
    lv_obj_set_width(head, LV_SIZE_CONTENT);
    lv_obj_set_height(head, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(head, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(head, 4, 0);
    make_label(head, icon, accent, NULL);
    make_label(head, name, accent, NULL);

    char init[16];
    lv_snprintf(init, sizeof(init), "-- %s", unit);
    *val_out = make_label(card, init, COL_WHITE, &lv_font_montserrat_18);
}

//智能手环UI入口
void band_ui_init(void)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
    lv_obj_set_style_pad_all(scr, 0, 0);
    lv_obj_set_style_pad_hor(scr, 10, 0);
    lv_obj_set_flex_flow(scr, LV_FLEX_FLOW_COLUMN);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    //顶部状态栏
    lv_obj_t *header = clean_obj(scr);
    lv_obj_set_width(header, lv_pct(100));
    lv_obj_set_height(header, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(header, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(header, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(header, 8, 0);
    make_label(header, "12:38", COL_GRAY, NULL);
    lv_obj_t *hright = clean_obj(header);
    lv_obj_set_width(hright, LV_SIZE_CONTENT);
    lv_obj_set_height(hright, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hright, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hright, 8, 0);
    lv_obj_set_style_pad_right(hright, 50, 0);
    make_label(hright, LV_SYMBOL_WIFI, COL_CYAN, NULL);
    make_label(hright, LV_SYMBOL_BLUETOOTH, COL_CYAN, NULL);
    make_label(hright, LV_SYMBOL_BATTERY_FULL " 95%", COL_CYAN, NULL);

    //心率显示区
    lv_obj_t *hr_row = clean_obj(scr);
    lv_obj_set_width(hr_row, lv_pct(100));
    lv_obj_set_height(hr_row, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(hr_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hr_row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_ver(hr_row, 6, 0);
    band_heart = make_label(hr_row, LV_SYMBOL_CHARGE, COL_HEART, &lv_font_montserrat_18);
    band_lbl_hr = make_label(hr_row, "74", COL_WHITE, &lv_font_montserrat_48);
    make_label(hr_row, " BPM", COL_RED, &lv_font_montserrat_18);

    //心形脉动动画
    lv_anim_t a;
    lv_anim_init(&a);
    a.var = band_heart;
    a.exec_cb = band_heart_opa_cb;
    a.duration = 600;
    a.repeat_delay = 200;
    a.start_value = 255;
    a.end_value = 70;
    lv_anim_start(&a);

    //中间折线图 (黑色背景、无Y轴、X轴与屏幕等宽、动态滚动)
    lv_obj_t *chart_cont = lv_obj_create(scr);
    lv_obj_remove_style(chart_cont, NULL, LV_PART_MAIN);
    lv_obj_set_style_bg_color(chart_cont, lv_color_black(), 0);
    lv_obj_set_width(chart_cont, lv_pct(100));
    lv_obj_set_flex_grow(chart_cont, 1);

    static const int32_t col_dsc[] = {LV_GRID_FR(1), LV_GRID_TEMPLATE_LAST};
    static const int32_t row_dsc[] = {LV_GRID_CONTENT, LV_GRID_FR(1), 40, LV_GRID_TEMPLATE_LAST};
    lv_obj_set_grid_dsc_array(chart_cont, col_dsc, row_dsc);
    lv_obj_set_style_pad_column(chart_cont, 0, 0);
    lv_obj_set_style_pad_row(chart_cont, 0, 0);

    lv_obj_t *label = lv_label_create(chart_cont);
    lv_label_set_text(label, "Heart Rate");
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_18, 0);
    lv_obj_set_grid_cell(label, LV_GRID_ALIGN_START, 0, 1, LV_GRID_ALIGN_STRETCH, 0, 1);

    band_chart = lv_chart_create(chart_cont);
    lv_obj_add_flag(band_chart, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
    lv_chart_set_div_line_count(band_chart, 0, 12);
    lv_chart_set_point_count(band_chart, 12);
    lv_chart_set_range(band_chart, LV_CHART_AXIS_PRIMARY_Y, 0, 100);
    lv_chart_set_update_mode(band_chart, LV_CHART_UPDATE_MODE_SHIFT);
    lv_obj_set_grid_cell(band_chart, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_STRETCH, 1, 1);
    lv_obj_set_width(band_chart, lv_pct(100));
    lv_obj_set_style_border_width(band_chart, 0, 0);
    lv_obj_set_style_bg_opa(band_chart, LV_OPA_TRANSP, 0);
    lv_obj_set_style_radius(band_chart, 0, 0);
    lv_obj_add_event_cb(band_chart, band_chart_draw_cb, LV_EVENT_ALL, NULL);

    static const char *chart_texts[] = {"-55s", "-50s", "-45s", "-40s", "-35s", "-30s", "-25s", "-20s", "-15s", "-10s", "-5s", "now", NULL};
    lv_obj_t *scale_hor = lv_scale_create(chart_cont);
    lv_scale_set_mode(scale_hor, LV_SCALE_MODE_HORIZONTAL_BOTTOM);
    lv_obj_set_grid_cell(scale_hor, LV_GRID_ALIGN_STRETCH, 0, 1, LV_GRID_ALIGN_START, 2, 1);
    lv_scale_set_total_tick_count(scale_hor, 12);
    lv_scale_set_major_tick_every(scale_hor, 1);
    lv_scale_set_text_src(scale_hor, chart_texts);
    lv_obj_set_width(scale_hor, lv_pct(100));
    lv_obj_set_height(scale_hor, 40);
    lv_obj_set_style_text_color(scale_hor, lv_color_hex(COL_DIM), 0);
    lv_obj_set_style_pad_hor(scale_hor, lv_obj_get_style_pad_left(band_chart, 0), 0);

    band_ser = lv_chart_add_series(band_chart, lv_color_hex(COL_HEART), LV_CHART_AXIS_PRIMARY_Y);
    for (int i = 0; i < 12; i++) {
        lv_chart_set_next_value(band_chart, band_ser, lv_rand(20, 85));
    }

    //底部三卡片
    lv_obj_t *cards = clean_obj(scr);
    lv_obj_set_width(cards, lv_pct(100));
    lv_obj_set_height(cards, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(cards, 6, 0);
    lv_obj_set_style_pad_ver(cards, 10, 0);
    band_metric_card(cards, LV_SYMBOL_TINT, "SpO2", COL_CYAN, "%", &band_lbl_spo2);
    band_metric_card(cards, LV_SYMBOL_EYE_OPEN, "TEMP", 0xFFB74D, "C", &band_lbl_temp);
    band_metric_card(cards, LV_SYMBOL_DRIVE, "RH", COL_GREEN, "%", &band_lbl_humi);

    lv_screen_load(scr);

    lv_timer_create(band_chart_timer_cb, 500, NULL);
    lv_timer_create(band_hr_timer_cb, 1000, NULL);
    lv_timer_create(band_env_timer_cb, 2000, NULL);
}
