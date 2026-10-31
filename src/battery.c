#include <math.h>
#include <stddef.h>

#include "battery.h"

// Sampling is nominally 1 Hz. A longer gap cannot establish continuous current.
#define MAX_SAMPLE_GAP_US INT64_C(2500000)
#define SOC_FILTER_SECONDS 30.0

_Static_assert(BATTERY_EMPTY_VOLTAGE_V < 3.20f, "Empty-voltage anchor must be below the 10% knot");
_Static_assert(BATTERY_FULL_VOLTAGE_V > 4.14f, "Full-voltage anchor must exceed the 95% knot");

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
        monitor->candidate_wh = monitor->candidate_mah = 0;
        return;
    }

    int64_t elapsed_us = now_us - monitor->previous_us;
    bool continuous = monitor->have_previous && elapsed_us > 0 && elapsed_us <= MAX_SAMPLE_GAP_US;
    battery_state_t raw = direction(sample->current_a);
    float target_percent = battery_voltage_percent(sample->voltage_v);
    if (!continuous) {
        monitor->filtered_percent = target_percent;
    } else {
        double seconds = elapsed_us / 1000000.0;
        monitor->filtered_percent += (target_percent - monitor->filtered_percent) *
                                    (1 - exp(-seconds / SOC_FILTER_SECONDS));
    }
    reading->percent = (int)lroundf(monitor->filtered_percent);
    // Rounding must not claim 100% before the voltage curve actually reaches its anchor.
    if (reading->percent == 100 && target_percent < 100) reading->percent = 99;

    if (!continuous || raw != monitor->candidate) {
        monitor->candidate = raw;
        monitor->candidate_since_us = now_us;
        monitor->candidate_wh = monitor->candidate_mah = 0;
    }

    // Integrate only adjacent, valid, same-direction readings above threshold.
    // Never interpolate across an idle boundary, direction reversal or missing sample.
    double wh = 0, mah = 0;
    if (continuous && raw != BATTERY_IDLE && direction(monitor->previous.current_a) == raw) {
        double hours = elapsed_us / 3600000000.0;
        double current = fabs((double)sample->current_a);
        double previous_current = fabs((double)monitor->previous.current_a);
        mah = (current + previous_current) * 0.5 * 1000 * hours;
        wh = (sample->voltage_v * current + monitor->previous.voltage_v * previous_current) * 0.5 * hours;
    }
    if (raw == reading->state) {
        reading->session_wh += wh;
        reading->session_mah += mah;
    } else {
        monitor->candidate_wh += wh;
        monitor->candidate_mah += mah;
        if (now_us - monitor->candidate_since_us >= BATTERY_CONFIRM_US) {
            reading->state = raw;
            if (raw == BATTERY_IDLE) {
                reading->session_end_us = monitor->candidate_since_us;
            } else {
                reading->session_start_us = monitor->candidate_since_us;
                reading->session_end_us = 0;
                reading->session_wh = monitor->candidate_wh;
                reading->session_mah = monitor->candidate_mah;
            }
            monitor->candidate_wh = monitor->candidate_mah = 0;
        }
    }
    reading->valid = true;
    reading->sample = *sample;
    monitor->previous = *sample;
    monitor->previous_us = now_us;
    monitor->have_previous = true;
}
