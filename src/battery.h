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
// Rest voltage where the CN3791 MPPT ends charging; shown as 99%, never 100%.
#define BATTERY_SYSTEM_FULL_VOLTAGE_V 4.10f
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
} battery_reading_t;

typedef struct {
    battery_reading_t reading;
    battery_state_t candidate;
    int64_t candidate_since_us;
    float filtered_percent;
    bool have_previous;
    int64_t previous_us;
} battery_monitor_t;

void battery_init(battery_monitor_t *monitor);
// Pure cell voltage estimate, no capacity or automatic full-charge learning.
// reading.percent rescales it so BATTERY_SYSTEM_FULL_VOLTAGE_V is the system's full.
float battery_voltage_percent(float voltage_v);
// One monotonic timestamp per sample; NULL breaks smoothing/confirmation continuity.
void battery_update(battery_monitor_t *monitor, const ina226_sample_t *sample, int64_t now_us);
