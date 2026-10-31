#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ina226.h"

// Manual OCV display anchors, not protection/charger thresholds.
// Only 0-10% / 95-100% change; see docs/battery-ocv-draft.md before calibrating.
#ifndef BATTERY_EMPTY_VOLTAGE_V
#define BATTERY_EMPTY_VOLTAGE_V 3.00f
#endif
#ifndef BATTERY_FULL_VOLTAGE_V
#define BATTERY_FULL_VOLTAGE_V 4.20f
#endif
#define BATTERY_CURRENT_THRESHOLD_A 0.010f
#define BATTERY_CONFIRM_US INT64_C(60000000)

typedef enum {
    BATTERY_IDLE,
    BATTERY_CHARGING,
    BATTERY_DISCHARGING,
} battery_state_t;

typedef struct {
    bool valid;
    ina226_sample_t sample;
    battery_state_t state;
    int percent;
    double session_wh;
    double session_mah;
    int64_t session_start_us;
    int64_t session_end_us;
} battery_reading_t;

typedef struct {
    battery_reading_t reading;
    battery_state_t candidate;
    int64_t candidate_since_us;
    double candidate_wh;
    double candidate_mah;
    float filtered_percent;
    bool have_previous;
    ina226_sample_t previous;
    int64_t previous_us;
} battery_monitor_t;

void battery_init(battery_monitor_t *monitor);
// Pure voltage estimate, no capacity or automatic full-charge learning.
float battery_voltage_percent(float voltage_v);
// One monotonic timestamp per sample; NULL breaks continuity without clearing totals.
void battery_update(battery_monitor_t *monitor, const ina226_sample_t *sample, int64_t now_us);
