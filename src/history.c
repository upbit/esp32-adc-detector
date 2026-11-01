#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "history.h"

#define FORMAT_VERSION 1

typedef struct {
    history_window_t window;
    uint32_t version;
} stored_window_t;

_Static_assert(sizeof(stored_window_t) == 32, "Update format version if the stored layout changes");
_Static_assert(sizeof(history_work_t) == 72, "Working snapshot must remain small and padding-free");

static history_window_t windows[HISTORY_WINDOWS];
static history_work_t work;
static nvs_handle_t handle;
static bool opened;
static uint32_t latest_committed;

static bool valid_time(int64_t timestamp)
{
    return timestamp >= INT64_C(1704067200) && timestamp < INT64_C(4102444800);
}

static bool valid_window(const history_window_t *window)
{
    return valid_time(window->start) && window->start % HISTORY_WINDOW_SECONDS == 0 &&
           window->samples <= window->attempts && window->attempts <= HISTORY_WINDOW_SECONDS &&
           window->percent_sum <= window->samples * 10000 &&
           window->covered_seconds <= HISTORY_WINDOW_SECONDS &&
           window->charging <= window->samples &&
           window->discharging <= window->samples - window->charging;
}

static unsigned slot(uint32_t start)
{
    return (start / HISTORY_WINDOW_SECONDS) % HISTORY_WINDOWS;
}

static void key_for(unsigned index, char key[8])
{
    snprintf(key, 8, "w%03u", index);
}

static uint32_t checksum(const history_work_t *value)
{
    const unsigned char *bytes = (const unsigned char *)value;
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < offsetof(history_work_t, checksum); i++)
        hash = (hash ^ bytes[i]) * UINT32_C(16777619);
    return hash;
}

bool history_init(void)
{
    if (opened) nvs_close(handle);
    opened = false;
    memset(windows, 0, sizeof(windows));
    memset(&work, 0, sizeof(work));
    latest_committed = 0;
    esp_err_t err = nvs_flash_init_partition("history");
    if (err == ESP_OK) err = nvs_open_from_partition("history", "trend", NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGW("history", "Storage unavailable: %s; existing data not erased", esp_err_to_name(err));
        return false;
    }
    opened = true;
    unsigned recovered = 0;
    for (unsigned i = 0; i < HISTORY_WINDOWS; i++) {
        char key[8];
        key_for(i, key);
        stored_window_t record;
        size_t size = sizeof(record);
        err = nvs_get_blob(handle, key, &record, &size);
        if (err == ESP_ERR_NVS_NOT_FOUND) continue;
        if (err != ESP_OK) {
            ESP_LOGW("history", "Read %s failed: %s", key, esp_err_to_name(err));
            if (err == ESP_ERR_NVS_INVALID_LENGTH) continue;
            nvs_close(handle);
            opened = false;
            return false;
        }
        if (size != sizeof(record) || record.version != FORMAT_VERSION ||
            !valid_window(&record.window) || !record.window.samples || slot(record.window.start) != i) {
            ESP_LOGW("history", "Ignoring invalid record %s", key);
            continue;
        }
        windows[i] = record.window;
        if (record.window.start > latest_committed) latest_committed = record.window.start;
        recovered++;
    }
    ESP_LOGI("history", "Independent history partition: %u half-hour records restored", recovered);
    return true;
}

bool history_advance(int64_t timestamp)
{
    if (!valid_time(timestamp)) return false;
    uint32_t start = (uint32_t)(timestamp / HISTORY_WINDOW_SECONDS * HISTORY_WINDOW_SECONDS);
    if (start <= latest_committed) return false; // Never overwrite a committed window after time rollback.
    if (start == work.current.start) return true;
    if (work.current.start && start < work.current.start) return false;
    if (work.current.samples) {
        if (work.pending.start) return false; // Keep uncommitted data intact on storage failure.
        work.pending = work.current;
    }
    work.current = (history_window_t){.start = start};
    work.last_sample = 0;
    work.coverage_until = 0;
    return true;
}

void history_add(const history_sample_t *sample)
{
    if (!valid_time(sample->timestamp) || sample->timestamp < work.current.start ||
        sample->timestamp >= (int64_t)work.current.start + HISTORY_WINDOW_SECONDS ||
        sample->timestamp <= work.last_sample || !sample->expected_interval_seconds ||
        sample->expected_interval_seconds > HISTORY_WINDOW_SECONDS) return;
    work.last_sample = (uint32_t)sample->timestamp;
    work.current.attempts++;
    if (!sample->valid || !isfinite(sample->percent) || sample->percent < 0 || sample->percent > 100 ||
        sample->state < HISTORY_IDLE || sample->state > HISTORY_DISCHARGING) return;
    work.current.percent_sum += (uint32_t)lroundf(sample->percent * 100);
    work.current.samples++;
    work.current.charging += sample->state == HISTORY_CHARGING;
    work.current.discharging += sample->state == HISTORY_DISCHARGING;
    uint32_t end = (uint32_t)sample->timestamp + sample->expected_interval_seconds;
    if (end > work.current.start + HISTORY_WINDOW_SECONDS) end = work.current.start + HISTORY_WINDOW_SECONDS;
    uint32_t begin = sample->timestamp > work.coverage_until ? (uint32_t)sample->timestamp : work.coverage_until;
    if (end > begin) work.current.covered_seconds += end - begin;
    if (end > work.coverage_until) work.coverage_until = end;
}

bool history_commit(void)
{
    if (!work.pending.start) return true;
    if (!opened) return false;
    char key[8];
    key_for(slot(work.pending.start), key);
    stored_window_t record = {.window = work.pending, .version = FORMAT_VERSION};
    esp_err_t err = nvs_set_blob(handle, key, &record, sizeof(record));
    if (err == ESP_OK) err = nvs_commit(handle);
    if (err != ESP_OK) {
        ESP_LOGW("history", "Commit failed: %s; pending window retained", esp_err_to_name(err));
        return false;
    }
    windows[slot(work.pending.start)] = work.pending;
    latest_committed = work.pending.start;
    ESP_LOGI("history", "Saved UTC %lu: %lu valid samples, SOC~%.2f%%",
             (unsigned long)work.pending.start, (unsigned long)work.pending.samples,
             (double)work.pending.percent_sum / (work.pending.samples * 100.0));
    work.pending = (history_window_t){0};
    return true;
}

void history_reset_time(void)
{
    work.current = (history_window_t){0};
    work.last_sample = 0;
    work.coverage_until = 0;
}

static const history_window_t *find_window(uint32_t start)
{
    if (start == work.current.start) return &work.current;
    if (start == work.pending.start) return &work.pending;
    const history_window_t *window = &windows[slot(start)];
    return window->start == start ? window : NULL;
}

void history_query(int64_t timestamp, bool seven_days, history_view_t *view)
{
    memset(view, 0, sizeof(*view));
    unsigned group = seven_days ? 7 : 1;
    view->seconds_per_bar = group * HISTORY_WINDOW_SECONDS;
    if (!valid_time(timestamp)) return;
    view->time_valid = true;
    view->end = timestamp / HISTORY_WINDOW_SECONDS * HISTORY_WINDOW_SECONDS + HISTORY_WINDOW_SECONDS;
    view->start = view->end - HISTORY_BARS * view->seconds_per_bar;
    for (unsigned col = 0; col < HISTORY_BARS; col++) {
        history_bar_t *bar = &view->bars[col];
        uint32_t sum = 0, charging = 0;
        for (unsigned i = 0; i < group; i++) {
            uint32_t start = (uint32_t)(view->start + col * view->seconds_per_bar + i * HISTORY_WINDOW_SECONDS);
            const history_window_t *window = find_window(start);
            if (!window || !window->samples) continue;
            bar->valid_windows++;
            bar->samples += window->samples;
            sum += window->percent_sum;
            charging += window->charging;
            bar->partial |= window->attempts != window->samples ||
                            window->covered_seconds < HISTORY_WINDOW_SECONDS || start == work.current.start;
        }
        bar->valid = bar->samples != 0;
        if (!bar->valid) continue;
        bar->partial |= bar->valid_windows != group;
        bar->percent = (uint8_t)((sum + bar->samples * 50) / (bar->samples * 100));
        bar->charging_percent = (uint8_t)((charging * 100 + bar->samples / 2) / bar->samples);
    }
}

void history_export(history_work_t *value)
{
    *value = work;
    value->version = FORMAT_VERSION;
    value->checksum = checksum(value);
}

bool history_restore(const history_work_t *value)
{
    if (value->version != FORMAT_VERSION || value->checksum != checksum(value) ||
        (value->current.start && !valid_window(&value->current)) ||
        (value->pending.start && (!valid_window(&value->pending) || !value->pending.samples)) ||
        (value->pending.start && value->current.start && value->pending.start >= value->current.start) ||
        (value->last_sample && (!value->current.start || value->last_sample < value->current.start ||
         value->last_sample >= value->current.start + HISTORY_WINDOW_SECONDS)) ||
        (value->coverage_until && (!value->current.start || value->coverage_until <= value->current.start ||
         value->coverage_until > value->current.start + HISTORY_WINDOW_SECONDS))) return false;
    work = *value;
    if (work.pending.start <= latest_committed) work.pending = (history_window_t){0};
    if (work.current.start <= latest_committed) history_reset_time();
    return true;
}
