#include <assert.h>
#include <math.h>
#include <string.h>
#include <sys/param.h>

#include "esp_attr.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include "board.h"
#include "radar.h"

// Angles are in 1/4096 turn. Math convention: 0 = right, counter-clockwise up.
#define TURN 4096
#define SWEEP_US 2000000 // One clockwise revolution every 2 s, independent of FPS.
#define BAND_LINES 16
#define BAND_COUNT (2 * RADAR_RADIUS / BAND_LINES)
#define MAX_RECTS 4
#define MIN_GAP 24 // Columns; narrower gaps are sent rather than split (command cost).
#define BUFFER_COUNT 4
// Rects of a band share one buffer; even rounding can widen each rect by 2 columns.
#define BUFFER_BYTES ((2 * RADAR_RADIUS + 2 * MAX_RECTS) * BAND_LINES * 2)
#define CLASS_RING 0x1000
#define CLASS_OUTSIDE 0x2000

typedef struct {
    uint8_t *buffer;
    int16_t y0, y1;
    int16_t x1[MAX_RECTS], x2[MAX_RECTS];
    uint8_t count;
    bool frame_last;
} band_t;

static const int tier_percent[RADAR_TIER_COUNT] = {10, 25, 35, 50, 59};
// Quarter-circle table, entry = class | angle (0..1024) for pixel centre (ax+.5, ay+.5).
static uint16_t *table;
// Palette indexed by (class | age), big-endian RGB565.
static uint16_t *palette;
// Per row (0 = circle top): half chord and the angle range the row covers.
static float row_half_width[2 * RADAR_RADIUS], row_angle_lo[2 * RADAR_RADIUS], row_angle_hi[2 * RADAR_RADIUS];
static QueueHandle_t free_buffers, ready_bands;
static volatile radar_tier_t requested;
static portMUX_TYPE stats_lock = portMUX_INITIALIZER_UNLOCKED;
static int64_t render_us;
static uint32_t rendered_frames;

int radar_tier_percent(radar_tier_t tier)
{
    return tier_percent[tier];
}

static int trail_units(radar_tier_t tier)
{
    if (tier == RADAR_TIER_FULL) return TURN * 300 / 360; // Visual trail; whole circle is sent.
    double pixels = tier_percent[tier] / 100.0 * LCD_WIDTH * LCD_HEIGHT;
    return (int)(pixels / (M_PI * RADAR_RADIUS * RADAR_RADIUS) * TURN);
}

static void build_table(void)
{
    table = heap_caps_malloc(RADAR_RADIUS * RADAR_RADIUS * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    assert(table);
    for (int ay = 0; ay < RADAR_RADIUS; ay++) {
        for (int ax = 0; ax < RADAR_RADIUS; ax++) {
            float x = ax + 0.5f, y = ay + 0.5f, r = sqrtf(x * x + y * y);
            uint16_t angle = (uint16_t)lrintf(atan2f(y, x) * (TURN / 2) / (float)M_PI);
            uint16_t class = 0;
            if (r > RADAR_RADIUS) class = CLASS_OUTSIDE;
            else if (ax == 0 || ay == 0 || fabsf(r - 60) < 0.8f || fabsf(r - 118) < 0.8f ||
                     r > RADAR_RADIUS - 1.5f) class = CLASS_RING; // Crosshair and range rings.
            table[ay * RADAR_RADIUS + ax] = class | angle;
        }
    }
    for (int i = 0; i < 2 * RADAR_RADIUS; i++) {
        float y = RADAR_RADIUS - i - 0.5f, h = sqrtf(RADAR_RADIUS * RADAR_RADIUS - y * y);
        float left = atan2f(y, -h) * (TURN / 2) / (float)M_PI, right = atan2f(y, h) * (TURN / 2) / (float)M_PI;
        if (y < 0) { // Lower half: map (-pi, 0) to (TURN/2, TURN) so the range stays contiguous.
            left += TURN;
            right += TURN;
        }
        row_half_width[i] = h;
        row_angle_lo[i] = fminf(left, right);
        row_angle_hi[i] = fmaxf(left, right);
    }
}

typedef struct { uint8_t r, g, b; } rgb_t;

static rgb_t blend(rgb_t a, rgb_t b, float t)
{
    return (rgb_t){a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
}

static void build_palette(int trail)
{
    const rgb_t base = {16, 22, 18}, ring = {52, 66, 58}, beam = {150, 255, 170};
    const rgb_t green = {40, 210, 90}, gray = {72, 84, 76};
    for (int age = 0; age < TURN; age++) {
        float t = (float)age / trail;
        rgb_t c = age < 24 ? beam : t < 0.55f ? blend(green, gray, t / 0.55f)
                : t < 0.95f ? blend(gray, base, (t - 0.55f) / 0.4f) : base;
        rgb_t r = {MAX(c.r, ring.r), MAX(c.g, ring.g), MAX(c.b, ring.b)};
        const rgb_t colors[3] = {c, r, {0, 0, 0}};
        for (int class = 0; class < 3; class++) {
            rgb_t p = colors[class];
            palette[class << 12 | age] = __builtin_bswap16((p.r >> 3) << 11 | (p.g >> 2) << 5 | p.b >> 3);
        }
    }
}

// Column span [x1, x2) of row `row` (0 = circle top) whose angle lies in the wedge
// [start, start + length] (units, may wrap). Angle is monotonic along a row, so each
// non-wrapping part of the wedge maps to one span. Writes up to two spans.
static int row_spans(int row, int start, int length, int *x1, int *x2)
{
    float h = row_half_width[row], y = RADAR_RADIUS - row - 0.5f;
    if (length >= TURN) {
        x1[0] = -h - 1;
        x2[0] = h + 2;
        return 1;
    }
    const float scale = (float)M_PI / (TURN / 2);
    float lo_edge = row_angle_lo[row], hi_edge = row_angle_hi[row];
    int parts[2][2] = {{start, start + length}, {start - TURN, start + length - TURN}};
    int count = 0;
    for (int p = 0; p < 2; p++) {
        float lo = fmaxf(parts[p][0], lo_edge), hi = fminf(parts[p][1], hi_edge);
        if (lo >= hi) continue;
        // x = y / tan(angle); clamp keeps the near-horizontal centre rows finite.
        float xa = fmaxf(-h, fminf(h, y * cosf(lo * scale) / sinf(lo * scale)));
        float xb = fmaxf(-h, fminf(h, y * cosf(hi * scale) / sinf(hi * scale)));
        x1[count] = (int)floorf(fminf(xa, xb)) - 1;
        x2[count] = (int)ceilf(fmaxf(xa, xb)) + 2;
        count++;
    }
    return count;
}

// Collect the band's dirty columns, then merge into at most MAX_RECTS even-aligned
// rectangles, closing gaps narrower than MIN_GAP to save command overhead.
static void band_rects(band_t *band, int start, int length)
{
    static uint8_t cover[2 * RADAR_RADIUS];
    memset(cover, 0, sizeof(cover));
    int top = band->y0 - (RADAR_CENTER_Y - RADAR_RADIUS);
    for (int row = top; row < top + BAND_LINES; row++) {
        int x1[2], x2[2];
        int n = row_spans(row, start, length, x1, x2);
        for (int i = 0; i < n; i++) {
            int a = MAX(0, x1[i] + RADAR_RADIUS), b = MIN(2 * RADAR_RADIUS, x2[i] + RADAR_RADIUS);
            if (a < b) memset(cover + a, 1, b - a);
        }
    }
    band->count = 0;
    int x = 0;
    while (x < 2 * RADAR_RADIUS) {
        while (x < 2 * RADAR_RADIUS && !cover[x]) x++;
        if (x == 2 * RADAR_RADIUS) break;
        int begin = x;
        for (;;) {
            while (x < 2 * RADAR_RADIUS && cover[x]) x++;
            int gap = x;
            while (gap < 2 * RADAR_RADIUS && !cover[gap]) gap++;
            if (gap < 2 * RADAR_RADIUS && gap - x < MIN_GAP) x = gap;
            else break;
        }
        if (band->count == MAX_RECTS) { // Rare: extend the last rect instead.
            band->x2[MAX_RECTS - 1] = x + RADAR_CENTER_X - RADAR_RADIUS;
            continue;
        }
        band->x1[band->count] = begin + RADAR_CENTER_X - RADAR_RADIUS;
        band->x2[band->count] = x + RADAR_CENTER_X - RADAR_RADIUS;
        band->count++;
    }
    for (int i = 0; i < band->count; i++) { // Even bounds keep DMA chunks 4-byte aligned.
        band->x1[i] &= ~1;
        band->x2[i] = (band->x2[i] + 1) & ~1;
    }
}

// Age of a pixel = (angle - pointer) mod TURN, where angle = offset +/- table angle
// depending on the quadrant. Two loops per row: left and right of the centre column.
static size_t render_rect(void *out, int x1, int x2, int y0, int y1, int pointer)
{
    uint16_t *dst = out;
    for (int y = y0; y < y1; y++) {
        int dy = y - RADAR_CENTER_Y;
        bool up = dy < 0;
        const uint16_t *row = table + (up ? -dy - 1 : dy) * RADAR_RADIUS;
        int split = MIN(MAX(x1, RADAR_CENTER_X), x2);
        int off = TURN / 2 - pointer, sign = up ? -1 : 1;
        for (int x = x1; x < split; x++) {
            uint16_t e = row[RADAR_CENTER_X - 1 - x];
            *dst++ = palette[(e & 0x3000) | ((off + sign * (e & 0xFFF)) & 0xFFF)];
        }
        off = up ? -pointer : TURN - pointer;
        sign = -sign;
        for (int x = split; x < x2; x++) {
            uint16_t e = row[x - RADAR_CENTER_X];
            *dst++ = palette[(e & 0x3000) | ((off + sign * (e & 0xFFF)) & 0xFFF)];
        }
    }
    return (x2 - x1) * (y1 - y0) * 2;
}

static void render_task(void *arg)
{
    radar_tier_t tier = RADAR_TIER_COUNT; // Forces the first rebuild.
    int trail = 0, last_pointer = 0;
    band_t held = {0}; // Last non-empty band, sent once the next one exists.
    bool have_held = false;
    for (;;) {
        bool redraw_all = false;
        if (requested != tier) {
            tier = requested;
            trail = trail_units(tier);
            build_palette(trail);
            redraw_all = true;
        }
        int64_t now = esp_timer_get_time();
        int pointer = (int)(TURN - (now % SWEEP_US) * TURN / SWEEP_US) & (TURN - 1);
        int step = (last_pointer - pointer) & (TURN - 1);
        last_pointer = pointer;
        // Dirty wedge: the trail plus the sliver that just left it (back to base).
        int length = redraw_all || tier == RADAR_TIER_FULL ? TURN : trail + step + 16;
        int64_t busy = 0;
        for (int b = 0; b < BAND_COUNT; b++) {
            band_t band = {.y0 = RADAR_CENTER_Y - RADAR_RADIUS + b * BAND_LINES};
            band.y1 = band.y0 + BAND_LINES;
            int64_t t0 = esp_timer_get_time();
            band_rects(&band, pointer, length);
            if (!band.count) {
                busy += esp_timer_get_time() - t0;
                continue;
            }
            int64_t wait = esp_timer_get_time();
            xQueueReceive(free_buffers, &band.buffer, portMAX_DELAY);
            t0 += esp_timer_get_time() - wait; // Exclude time blocked on the bus.
            uint8_t *out = band.buffer;
            for (int i = 0; i < band.count; i++) {
                out += render_rect(out, band.x1[i], band.x2[i], band.y0, band.y1, pointer);
            }
            busy += esp_timer_get_time() - t0;
            if (have_held) xQueueSend(ready_bands, &held, portMAX_DELAY);
            held = band;
            have_held = true;
        }
        if (have_held) {
            held.frame_last = true;
            xQueueSend(ready_bands, &held, portMAX_DELAY);
            have_held = false;
        }
        portENTER_CRITICAL(&stats_lock);
        render_us += busy;
        rendered_frames++;
        portEXIT_CRITICAL(&stats_lock);
    }
}

static IRAM_ATTR void band_sent(void *buffer)
{
    BaseType_t woken = pdFALSE;
    xQueueSendFromISR(free_buffers, &buffer, &woken);
    if (woken) portYIELD_FROM_ISR();
}

void radar_pump(uint32_t timeout_ms)
{
    band_t band;
    TickType_t wait = pdMS_TO_TICKS(timeout_ms);
    // Stop at a frame boundary so the caller's LVGL work runs once per frame.
    while (xQueueReceive(ready_bands, &band, wait) == pdTRUE) {
        wait = 0;
        const uint8_t *pixels = band.buffer;
        for (int i = 0; i < band.count; i++) {
            bool last = i == band.count - 1;
            display_queue_band(band.x1[i], band.y0, band.x2[i], band.y1, pixels,
                               last && band.frame_last, last ? band_sent : NULL, band.buffer);
            pixels += (band.x2[i] - band.x1[i]) * (band.y1 - band.y0) * 2;
        }
        if (band.frame_last) break;
    }
}

void radar_set_tier(radar_tier_t tier)
{
    requested = tier;
}

radar_stats_t radar_take_stats(void)
{
    portENTER_CRITICAL(&stats_lock);
    radar_stats_t stats = {
        .render_ms = rendered_frames ? render_us / (rendered_frames * 1000.0f) : 0,
        .frames = rendered_frames,
    };
    render_us = 0;
    rendered_frames = 0;
    portEXIT_CRITICAL(&stats_lock);
    return stats;
}

void radar_init(void)
{
    build_table();
    palette = heap_caps_malloc(3 * TURN * sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    free_buffers = xQueueCreate(BUFFER_COUNT, sizeof(uint8_t *));
    ready_bands = xQueueCreate(BUFFER_COUNT, sizeof(band_t));
    assert(palette && free_buffers && ready_bands);
    for (int i = 0; i < BUFFER_COUNT; i++) {
        uint8_t *buffer = heap_caps_aligned_alloc(4, BUFFER_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        assert(buffer);
        xQueueSend(free_buffers, &buffer, 0);
    }
    requested = RADAR_TIER_10;
    xTaskCreatePinnedToCore(render_task, "radar", 4096, NULL, 5, NULL, 1);
    ESP_LOGI("radar", "Table %u B, palette %u B, %d x %u B band buffers, internal free %u B",
             (unsigned)(RADAR_RADIUS * RADAR_RADIUS * 2), (unsigned)(3 * TURN * 2), BUFFER_COUNT,
             (unsigned)BUFFER_BYTES, (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}
