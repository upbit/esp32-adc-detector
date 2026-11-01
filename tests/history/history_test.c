#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "history.h"
#include "nvs.h"

static unsigned char persisted[HISTORY_WINDOWS][64], staged[64];
static size_t sizes[HISTORY_WINDOWS], staged_size;
static unsigned staged_index, writes, commits;
static bool fail_init, fail_read, fail_write, fail_commit;
static uint32_t cadence;
static const int64_t start = INT64_C(1761998400); // 2025-11-01 12:00 UTC.

const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock error"; }
void test_log(const char *tag, const char *format, ...) { (void)tag; (void)format; }
esp_err_t nvs_flash_init_partition(const char *partition)
{
    assert(!strcmp(partition, "history"));
    return fail_init ? ESP_FAIL : ESP_OK;
}
esp_err_t nvs_open_from_partition(const char *partition, const char *name, int mode, nvs_handle_t *handle)
{
    assert(!strcmp(partition, "history") && !strcmp(name, "trend") && mode == NVS_READWRITE);
    *handle = 1;
    return ESP_OK;
}
void nvs_close(nvs_handle_t handle) { assert(handle == 1); staged_size = 0; }
static unsigned key_index(const char *key)
{
    unsigned index;
    assert(sscanf(key, "w%u", &index) == 1 && index < HISTORY_WINDOWS);
    return index;
}
esp_err_t nvs_get_blob(nvs_handle_t handle, const char *key, void *value, size_t *size)
{
    assert(handle == 1);
    if (fail_read) return ESP_FAIL;
    unsigned index = key_index(key);
    if (!sizes[index]) return ESP_ERR_NVS_NOT_FOUND;
    if (*size < sizes[index]) return ESP_ERR_NVS_INVALID_LENGTH;
    *size = sizes[index];
    memcpy(value, persisted[index], *size);
    return ESP_OK;
}
esp_err_t nvs_set_blob(nvs_handle_t handle, const char *key, const void *value, size_t size)
{
    assert(handle == 1 && size <= sizeof(staged));
    writes++;
    if (fail_write) return ESP_FAIL;
    staged_index = key_index(key);
    staged_size = size;
    memcpy(staged, value, size);
    return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t handle)
{
    assert(handle == 1);
    if (fail_commit) return ESP_FAIL;
    assert(staged_size);
    memcpy(persisted[staged_index], staged, staged_size);
    sizes[staged_index] = staged_size;
    staged_size = 0;
    commits++;
    return ESP_OK;
}

static void fresh(void)
{
    memset(persisted, 0, sizeof(persisted));
    memset(sizes, 0, sizeof(sizes));
    writes = commits = 0;
    fail_init = fail_read = fail_write = fail_commit = false;
    cadence = 600;
    assert(history_init());
}
static void sample(int64_t timestamp, float percent, history_state_t state, bool valid)
{
    assert(history_advance(timestamp));
    history_sample_t value = {.timestamp = timestamp, .percent = percent, .state = state, .valid = valid,
                             .expected_interval_seconds = cadence};
    history_add(&value);
}
static history_view_t query(int64_t timestamp, bool week)
{
    history_view_t view;
    history_query(timestamp, week, &view);
    return view;
}

static void test_three_points_and_boundary(void)
{
    fresh();
    sample(start, 30, HISTORY_CHARGING, true);
    sample(start + 600, 60, HISTORY_IDLE, true);
    sample(start + 1200, 90, HISTORY_CHARGING, true);
    assert(history_commit() && writes == 0); // Three points do not close the window early.
    history_view_t view = query(start + 1200, false);
    assert(view.bars[47].valid && view.bars[47].samples == 3 && view.bars[47].percent == 60);
    assert(view.bars[47].charging_percent == 67 && view.bars[47].partial);
    assert(view.start == start - 47 * 1800 && view.end == start + 1800);
    assert(history_advance(start + 1800));
    assert(writes == 0 && history_commit() && writes == 1 && commits == 1);
    sample(start + 1800, 12, HISTORY_DISCHARGING, true);
    view = query(start + 1800, false);
    assert(view.bars[46].samples == 3 && view.bars[46].percent == 60);
    assert(view.bars[47].samples == 1 && view.bars[47].percent == 12);
    assert(history_commit() && writes == 1);
    sample(start + 2400, 24, HISTORY_IDLE, true);
    sample(start + 3000, 36, HISTORY_IDLE, true);
    assert(history_advance(start + 3600) && history_commit());
    view = query(start + 3600, false);
    assert(view.bars[46].samples == 3 && view.bars[46].percent == 24);
    assert(writes == 2 && commits == 2); // Neither two-point nor four-point boundary accounting.

    assert(history_init()); // A cold restart restores completed windows, not the current one.
    view = query(start + 3600, false);
    assert(view.bars[45].percent == 60 && view.bars[46].percent == 24 && !view.bars[47].valid);
}

static void test_partial_missing_and_zero(void)
{
    fresh();
    sample(start + 600, 20, HISTORY_IDLE, true);
    sample(start + 1200, 40, HISTORY_IDLE, false);
    assert(history_advance(start + 1800) && history_commit());
    history_view_t view = query(start + 1800, false);
    assert(view.bars[46].valid && view.bars[46].samples == 1 && view.bars[46].percent == 20);
    assert(view.bars[46].partial);
    sample(start + 1800, 0, HISTORY_IDLE, true);
    history_view_t zero = query(start + 1800, false);
    assert(zero.bars[47].valid && zero.bars[47].percent == 0);
    assert(history_advance(start + 3600) && history_commit());
    sample(start + 3600, NAN, HISTORY_IDLE, true);
    sample(start + 3601, INFINITY, HISTORY_IDLE, true);
    sample(start + 3602, 101, HISTORY_IDLE, true);
    assert(history_advance(start + 5400) && history_commit());
    assert(writes == 2); // All-invalid windows need no Flash write.
    view = query(start + 5400, false);
    assert(!view.bars[46].valid && !view.bars[47].valid && view.bars[45].valid);
    assert(history_advance(start + 48 * 1800) && history_commit());
    view = query(start + 48 * 1800, false);
    assert(view.bars[0].valid && view.bars[0].percent == 0);
    for (unsigned i = 1; i < HISTORY_BARS; i++) assert(!view.bars[i].valid);
    view = query(0, false);
    assert(!view.time_valid);
    for (unsigned i = 0; i < HISTORY_BARS; i++) assert(!view.bars[i].valid);
    assert(!history_advance(0) && !history_advance(INT64_MAX));
}

static void test_partial_start_and_delayed_boundary(void)
{
    fresh();
    sample(start + 600, 40, HISTORY_IDLE, true);
    sample(start + 1200, 80, HISTORY_IDLE, true);
    assert(history_advance(start + 1800) && history_commit());
    sample(start + 1800, 30, HISTORY_IDLE, true);
    sample(start + 2400, 60, HISTORY_IDLE, true);
    sample(start + 3000, 90, HISTORY_IDLE, true);
    history_view_t view = query(start + 3000, false);
    assert(view.bars[46].samples == 2 && view.bars[46].percent == 60 && view.bars[46].partial);
    assert(view.bars[47].samples == 3 && view.bars[47].percent == 60);
    assert(history_advance(start + 3605) && history_commit()); // Five-second delayed wakeup.
    sample(start + 3605, 10, HISTORY_IDLE, true);
    view = query(start + 3605, false);
    assert(view.bars[45].samples == 2 && view.bars[46].samples == 3 && view.bars[47].samples == 1);
    assert(commits == 2);
}

static void test_dense_and_merged(void)
{
    fresh();
    cadence = 1;
    for (int i = 0; i < 1800; i++) sample(start + i, 40.25f, HISTORY_CHARGING, true);
    sample(start + 1799, 99, HISTORY_IDLE, true); // Duplicate second ignored.
    assert(writes == 0);
    assert(history_advance(start + 1800) && history_commit());
    sample(start + 1800, 80, HISTORY_IDLE, true);
    assert(history_advance(start + 3600) && history_commit());
    history_view_t view = query(start + 6 * 1800, true);
    assert(view.seconds_per_bar == 12600 && view.end - view.start == 7 * 86400);
    assert(view.bars[47].samples == 1801 && view.bars[47].percent == 40);
    assert(view.bars[47].charging_percent == 100 && view.bars[47].valid_windows == 2);
    assert(view.bars[47].partial); // Ignore empty windows rather than averaging their fake zeros.

    fresh();
    for (int i = 0; i < 7; i++) {
        for (int j = 0; j < 3; j++) sample(start + i * 1800 + j * 600, 10 + 10 * i, HISTORY_IDLE, true);
        assert(history_advance(start + (i + 1) * 1800) && history_commit());
    }
    view = query(start + 6 * 1800, true);
    assert(view.bars[47].valid_windows == 7 && !view.bars[47].partial && view.bars[47].percent == 40);
}

static void test_seven_day_rollover(void)
{
    fresh();
    for (int i = 0; i < HISTORY_WINDOWS * 3; i++) {
        sample(start + i * 1800, (float)(i % 100), HISTORY_IDLE, true);
        assert(history_advance(start + (i + 1) * 1800) && history_commit());
    }
    assert(commits == HISTORY_WINDOWS * 3);
    assert(history_init());
    int last = HISTORY_WINDOWS * 3 - 1;
    history_view_t view = query(start + last * 1800, true);
    for (int col = 0; col < HISTORY_BARS; col++) {
        unsigned sum = 0;
        for (int i = 0; i < 7; i++) sum += (last - HISTORY_WINDOWS + 1 + col * 7 + i) % 100;
        assert(view.bars[col].samples == 7 && view.bars[col].valid_windows == 7);
        assert(view.bars[col].percent == (sum + 3) / 7);
    }
    view = query(start, false);
    assert(!view.bars[47].valid); // Wrapped keys cannot masquerade as their old timestamps.
}

static void test_failures_and_snapshot(void)
{
    fresh();
    sample(start, 10, HISTORY_IDLE, true);
    assert(history_advance(start + 1800));
    fail_write = true;
    assert(!history_commit());
    sample(start + 1800, 20, HISTORY_CHARGING, true);
    assert(!history_advance(start + 3600)); // Never discard two still-uncommitted windows.
    history_work_t saved;
    history_export(&saved);
    assert(sizeof(saved) <= 80 && saved.pending.samples == 1 && saved.current.samples == 1);
    fail_write = false;
    fail_commit = true;
    assert(!history_commit());
    assert(commits == 0);
    assert(history_init()); // Simulate power loss before a transaction completes.
    history_view_t view = query(start + 1800, false);
    assert(!view.bars[46].valid && !view.bars[47].valid);
    assert(history_restore(&saved)); // Deep-sleep restoration keeps working data instead.
    history_work_t bad = saved;
    bad.current.percent_sum++;
    assert(!history_restore(&bad));
    fail_commit = false;
    assert(history_commit() && commits == 1);
    assert(history_init());
    assert(history_restore(&saved) && history_commit());
    assert(commits == 1); // RTC snapshot from just before the successful write is idempotent.
    assert(history_advance(start + 3600) && history_commit() && commits == 2);
    view = query(start + 3600, false);
    assert(view.bars[45].percent == 10 && view.bars[46].percent == 20);

    fail_init = true;
    assert(!history_init());
    fail_init = false;
    assert(history_init());
    view = query(start + 3600, false);
    assert(view.bars[45].percent == 10 && view.bars[46].percent == 20); // No erase on init failure.
    fail_read = true;
    assert(!history_init());
    fail_read = false;
    assert(history_init());
}

static void test_clock_changes_and_sleep_cycles(void)
{
    fresh();
    sample(start, 20, HISTORY_IDLE, true);
    history_reset_time(); // Correct time within the same bucket cannot mix the old samples.
    sample(start + 600, 80, HISTORY_IDLE, true);
    assert(history_advance(start + 1800) && history_commit());
    assert(!history_advance(start));
    history_reset_time();
    assert(!history_advance(start + 600));
    assert(history_advance(start + 10 * 1800));
    history_view_t view = query(start + 10 * 1800, false);
    assert(view.bars[37].percent == 80);
    for (int i = 38; i < HISTORY_BARS; i++) assert(!view.bars[i].valid);

    fresh();
    for (int i = 0; i < 9; i++) {
        assert(history_advance(start + i * 600) && history_commit());
        sample(start + i * 600, 10 + i, HISTORY_IDLE, true);
        history_work_t saved;
        history_export(&saved);
        assert(history_init() && history_restore(&saved));
    }
    assert(commits == 2); // Nine wakeups, not nine Flash commits.
    view = query(start + 8 * 600, false);
    assert(view.bars[45].samples == 3 && view.bars[45].percent == 11);
    assert(view.bars[46].samples == 3 && view.bars[46].percent == 14);
    assert(view.bars[47].samples == 3 && view.bars[47].percent == 17);
}

int main(void)
{
    assert(start % HISTORY_WINDOW_SECONDS == 0);
    test_three_points_and_boundary();
    test_partial_missing_and_zero();
    test_partial_start_and_delayed_boundary();
    test_dense_and_merged();
    test_seven_day_rollover();
    test_failures_and_snapshot();
    test_clock_changes_and_sleep_cycles();
    puts("PASS: independent partition, half-hour boundaries, three-point windows, gaps/zero, 48-bar merging, 7-day rollover, failures, time changes and working snapshots");
}
