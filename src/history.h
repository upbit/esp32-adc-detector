#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HISTORY_WINDOW_SECONDS 1800
#define HISTORY_WINDOWS 336
#define HISTORY_BARS 48

typedef enum { HISTORY_IDLE, HISTORY_CHARGING, HISTORY_DISCHARGING } history_state_t;
typedef struct {
    int64_t timestamp;
    float percent;
    history_state_t state;
    bool valid;
    uint32_t expected_interval_seconds; // External cadence, not a sleep or sampling command.
} history_sample_t;

typedef struct {
    uint32_t start;
    uint32_t percent_sum; // Hundredths of a percent, before averaging.
    uint32_t samples;
    uint32_t attempts;
    uint32_t charging;
    uint32_t discharging;
    uint32_t covered_seconds; // Coverage at the declared cadence, not continuous measurement.
} history_window_t;

// Plain data only. An external owner may retain this across sleep; no RTC dependency.
typedef struct {
    history_window_t current;
    history_window_t pending;
    uint32_t last_sample;
    uint32_t coverage_until;
    uint32_t version;
    uint32_t checksum;
} history_work_t;

typedef struct {
    bool valid;
    bool partial;
    uint8_t percent;
    uint8_t charging_percent; // Sample-state proportion, not elapsed charging time.
    uint8_t valid_windows;
    uint32_t samples;
} history_bar_t;

typedef struct {
    bool time_valid;
    int64_t start;
    int64_t end; // End of the current half-hour; the last column is in progress.
    uint32_t seconds_per_bar;
    history_bar_t bars[HISTORY_BARS];
} history_view_t;

// All calls are serialized by the external owner; no tasks, clocks or sampling here.
bool history_init(void);
// Moves a completed window to pending, without writing Flash. Invalid time is ignored.
bool history_advance(int64_t timestamp);
void history_add(const history_sample_t *sample);
bool history_commit(void); // Only the pending, completed window is written.
void history_reset_time(void); // Discard the unfinished window after a clock discontinuity.
void history_query(int64_t timestamp, bool seven_days, history_view_t *view);
void history_export(history_work_t *work);
bool history_restore(const history_work_t *work);
