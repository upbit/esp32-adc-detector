#include <math.h>
#include <stddef.h>

#include "battery.h"

// Sampling is nominally 1 Hz. A longer gap cannot establish continuous current.
#define MAX_SAMPLE_GAP_US INT64_C(2500000)
#define SOC_FILTER_SECONDS 30.0

_Static_assert(BATTERY_EMPTY_VOLTAGE_V < 3.20f, "Empty-voltage anchor must be below the 10% knot");
_Static_assert(BATTERY_FULL_VOLTAGE_V > 4.14f, "Full-voltage anchor must exceed the 95% knot");
_Static_assert(BATTERY_SYSTEM_FULL_VOLTAGE_V > 3.20f && BATTERY_SYSTEM_FULL_VOLTAGE_V <= BATTERY_FULL_VOLTAGE_V,
               "System-full anchor must lie inside the cell curve");

static battery_state_t direction(float current_a)
{
    if (current_a > BATTERY_CURRENT_THRESHOLD_A) return BATTERY_CHARGING;
    if (current_a < -BATTERY_CURRENT_THRESHOLD_A) return BATTERY_DISCHARGING;
    return BATTERY_IDLE;
}

float battery_voltage_percent(float voltage_v)
{
    // TI OCV example at 10-90%, engineering endpoints / 95% knot.
    // Not a measured curve for this cell; calibrate locally, never stretch all knots.
    static const struct { float voltage; float percent; } curve[] = {
        {BATTERY_EMPTY_VOLTAGE_V, 0}, {3.20f, 10}, {3.38f, 20},
        {3.53f, 30}, {3.61f, 40}, {3.72f, 50}, {3.82f, 60},
        {3.90f, 70}, {3.99f, 80}, {4.07f, 90}, {4.14f, 95},
        {BATTERY_FULL_VOLTAGE_V, 100},
    };
    if (voltage_v <= curve[0].voltage) return 0;
    for (size_t i = 1; i < sizeof(curve) / sizeof(curve[0]); i++) {
        if (voltage_v <= curve[i].voltage) {
            float fraction = (voltage_v - curve[i - 1].voltage) /
                             (curve[i].voltage - curve[i - 1].voltage);
            return curve[i - 1].percent + fraction * (curve[i].percent - curve[i - 1].percent);
        }
    }
    return 100;
}

// The MPPT stops charging below cell-full, so rescale to its rest voltage.
// Capped at 99%: system-full is not cell-full. The empty anchor is already 0%.
static float system_percent(float voltage_v)
{
    float percent = battery_voltage_percent(voltage_v) * 100 /
                    battery_voltage_percent(BATTERY_SYSTEM_FULL_VOLTAGE_V);
    return percent > 99 ? 99 : percent;
}

void battery_init(battery_monitor_t *monitor)
{
    *monitor = (battery_monitor_t){0};
}

void battery_update(battery_monitor_t *monitor, const ina226_sample_t *sample, int64_t now_us)
{
    battery_reading_t *reading = &monitor->reading;
    if (!sample || !isfinite(sample->voltage_v) || !isfinite(sample->current_a)) {
        reading->valid = false;
        monitor->have_previous = false;
        monitor->candidate = reading->state;
        return;
    }

    int64_t elapsed_us = now_us - monitor->previous_us;
    bool continuous = monitor->have_previous && elapsed_us > 0 && elapsed_us <= MAX_SAMPLE_GAP_US;
    battery_state_t raw = direction(sample->current_a);
    float target_percent = system_percent(sample->voltage_v);
    if (!continuous) {
        monitor->filtered_percent = target_percent;
    } else {
        double seconds = elapsed_us / 1000000.0;
        monitor->filtered_percent += (target_percent - monitor->filtered_percent) *
                                    (1 - exp(-seconds / SOC_FILTER_SECONDS));
    }
    reading->percent = (int)lroundf(monitor->filtered_percent);

    if (!continuous || raw != monitor->candidate) {
        monitor->candidate = raw;
        monitor->candidate_since_us = now_us;
    }

    if (raw != reading->state && now_us - monitor->candidate_since_us >= BATTERY_CONFIRM_US) {
        reading->state = raw;
    }
    reading->valid = true;
    reading->sample = *sample;
    monitor->previous_us = now_us;
    monitor->have_previous = true;
}
