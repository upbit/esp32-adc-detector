#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "battery.h"

static void near(double actual, double expected)
{
    assert(fabs(actual - expected) < 0.00001);
}

static void sample(battery_monitor_t *monitor, double seconds, float voltage, float current)
{
    ina226_sample_t value = {.voltage_v = voltage, .current_a = current};
    battery_update(monitor, &value, (int64_t)(seconds * 1000000));
}

static void run(battery_monitor_t *monitor, int start, int end, float current)
{
    for (int i = start; i <= end; i++) sample(monitor, i, 4.0f, current);
}

static int shown_percent(float voltage)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    sample(&monitor, 0, voltage, 0); // First sample is unsmoothed.
    return monitor.reading.percent;
}

static void test_system_percent(void)
{
    // 4.10 V rest voltage after CN3791 termination is ~92% of the cell curve.
    assert(fabsf(battery_voltage_percent(BATTERY_SYSTEM_FULL_VOLTAGE_V) - 92.142857f) < 0.0001f);
    assert(shown_percent(0) == 0);
    assert(shown_percent(BATTERY_EMPTY_VOLTAGE_V) == 0);
    assert(shown_percent(3.30f) == 17);
    assert(shown_percent(3.72f) == 54);
    assert(shown_percent(4.01f) == 90);
    assert(shown_percent(4.05f) == 95);
    assert(shown_percent(4.08f) == 98);
    for (int mv = 4090; mv <= 4110; mv++) assert(shown_percent(mv / 1000.0f) == 99);
    assert(shown_percent(BATTERY_FULL_VOLTAGE_V) == 99); // Never 100%.
    assert(shown_percent(5) == 99);
    int previous = 0;
    for (int mv = 0; mv <= 5000; mv++) {
        int percent = shown_percent(mv / 1000.0f);
        assert(percent >= previous && percent <= 99);
        previous = percent;
    }
}

static void test_curve(void)
{
    near(battery_voltage_percent(0), 0);
    near(battery_voltage_percent(BATTERY_EMPTY_VOLTAGE_V), 0);
    // These fixed knots must not move when either manual endpoint changes.
    static const struct { float voltage; float percent; } knots[] = {
        {3.20f, 10}, {3.38f, 20}, {3.53f, 30}, {3.61f, 40}, {3.72f, 50},
        {3.82f, 60}, {3.90f, 70}, {3.99f, 80}, {4.07f, 90}, {4.14f, 95},
    };
    for (size_t i = 0; i < sizeof(knots) / sizeof(knots[0]); i++) {
        near(battery_voltage_percent(knots[i].voltage), knots[i].percent);
    }
    assert(fabsf(battery_voltage_percent(3.30f) - 15.55556f) < 0.0001f);
    assert(fabsf(battery_voltage_percent(3.60f) - 38.75f) < 0.0001f);
    assert(fabsf(battery_voltage_percent(3.80f) - 58.0f) < 0.0001f);
    assert(fabsf(battery_voltage_percent(4.00f) - 81.25f) < 0.0001f);
    assert(fabsf(battery_voltage_percent(4.06f) - 88.75f) < 0.0001f);
    float low_midpoint = (BATTERY_EMPTY_VOLTAGE_V + 3.20f) / 2;
    float high_midpoint = (4.14f + BATTERY_FULL_VOLTAGE_V) / 2;
    assert(fabsf(battery_voltage_percent(low_midpoint) - 5) < 0.0001f);
    assert(fabsf(battery_voltage_percent(high_midpoint) - 97.5f) < 0.0001f);
    near(battery_voltage_percent(BATTERY_FULL_VOLTAGE_V), 100);
    near(battery_voltage_percent(5), 100);
    float previous = 0;
    for (int mv = 0; mv <= 5000; mv++) {
        float percent = battery_voltage_percent(mv / 1000.0f);
        assert(percent >= previous && percent >= 0 && percent <= 100);
        previous = percent;
    }

    battery_monitor_t monitor;
    battery_init(&monitor);
    assert(!monitor.reading.valid);
    sample(&monitor, 0, 3.72f, 0);
    assert(monitor.reading.percent == 54);
    sample(&monitor, 1, 4.0f, 0);
    assert(monitor.reading.percent == 55); // One-second smoothing, not a jump to 88%.
    battery_update(&monitor, NULL, 2000000);
    assert(!monitor.reading.valid);
    sample(&monitor, 3, 0, 0);
    assert(monitor.reading.valid && monitor.reading.percent == 0);
}

static void test_thresholds_and_confirmation(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    run(&monitor, 0, 65, BATTERY_CURRENT_THRESHOLD_A);
    assert(monitor.reading.state == BATTERY_IDLE);
    run(&monitor, 66, 131, -BATTERY_CURRENT_THRESHOLD_A);
    assert(monitor.reading.state == BATTERY_IDLE);

    run(&monitor, 132, 191, 0.011f);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 192, 4, 0.011f);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.candidate_since_us == 132000000);

    run(&monitor, 193, 252, 0.005f);
    assert(monitor.reading.state == BATTERY_CHARGING);
    sample(&monitor, 253, 4, 0);
    assert(monitor.reading.state == BATTERY_IDLE);
    assert(monitor.candidate_since_us == 193000000);
}

static void test_reversals_and_idle(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    run(&monitor, 0, 60, 1.0f);
    assert(monitor.reading.state == BATTERY_CHARGING);

    run(&monitor, 61, 70, 0);
    assert(monitor.reading.state == BATTERY_CHARGING);
    sample(&monitor, 71, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.candidate == BATTERY_CHARGING && monitor.candidate_since_us == 71000000);

    run(&monitor, 72, 131, -1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    sample(&monitor, 132, 4, -1);
    assert(monitor.reading.state == BATTERY_DISCHARGING);
    assert(monitor.candidate_since_us == 72000000);
    run(&monitor, 133, 192, 0);
    assert(monitor.reading.state == BATTERY_DISCHARGING);
    sample(&monitor, 193, 4, 0);
    assert(monitor.reading.state == BATTERY_IDLE);
    run(&monitor, 194, 254, 2);
    assert(monitor.reading.state == BATTERY_CHARGING);
    battery_init(&monitor);
    assert(!monitor.reading.valid && monitor.reading.state == BATTERY_IDLE);
}

static void test_missing_samples(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    run(&monitor, 0, 30, 1);
    battery_update(&monitor, NULL, 31000000);
    run(&monitor, 32, 91, 1);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 92, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.candidate_since_us == 32000000);
    battery_update(&monitor, NULL, 93000000);
    assert(!monitor.reading.valid && !monitor.have_previous);
    sample(&monitor, 1000, 4, 1);
    assert(monitor.reading.valid && monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.candidate_since_us == 1000000000);

    run(&monitor, 1002, 1050, 0);
    sample(&monitor, 1100, 4, 0); // An unsampled gap resets the 60-second confirmation.
    run(&monitor, 1101, 1159, 0);
    assert(monitor.reading.state == BATTERY_CHARGING);
    sample(&monitor, 1160, 4, 0);
    assert(monitor.reading.state == BATTERY_IDLE);

    battery_init(&monitor);
    run(&monitor, 0, 59, 1);
    sample(&monitor, 60, 4, NAN);
    assert(!monitor.reading.valid);
    run(&monitor, 61, 120, 1);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 121, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);

    battery_init(&monitor);
    run(&monitor, 0, 40, 1);
    run(&monitor, 41, 50, -1);
    run(&monitor, 51, 110, 1);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 111, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.candidate_since_us == 51000000);
    sample(&monitor, 112, INFINITY, 1);
    assert(!monitor.reading.valid && !monitor.have_previous);
    sample(&monitor, 113, 4, 1);
    assert(monitor.reading.valid && monitor.reading.state == BATTERY_CHARGING);
}

static void test_timing_and_low_frequency(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    for (int tick = 0; tick < 40; tick++) sample(&monitor, tick * 1.5, 4, 1);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 60, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);

    battery_init(&monitor);
    run(&monitor, 0, 59, 1);
    sample(&monitor, 61.5, 4, 1); // Exactly 2.5 seconds still establishes continuity.
    assert(monitor.reading.state == BATTERY_CHARGING);
    battery_init(&monitor);
    run(&monitor, 0, 59, 1);
    ina226_sample_t value = {.voltage_v = 4, .current_a = 1};
    battery_update(&monitor, &value, INT64_C(61500001));
    assert(monitor.reading.state == BATTERY_IDLE);
    assert(monitor.candidate_since_us == 61500001);

    battery_update(&monitor, &value, INT64_C(61500001)); // Duplicate timestamps reset evidence.
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 60, 4, 1);
    run(&monitor, 61, 119, 1);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 120, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);

    battery_init(&monitor);
    for (int i = 0; i <= 6; i++) {
        sample(&monitor, i * 600, 4, 1);
        assert(monitor.reading.valid && monitor.reading.state == BATTERY_IDLE);
        assert(monitor.reading.percent == shown_percent(4));
    }
}

int main(void)
{
    test_curve();
    test_system_percent();
    test_thresholds_and_confirmation();
    test_reversals_and_idle();
    test_missing_samples();
    test_timing_and_low_frequency();
    puts("PASS: voltage curve, 4.10 V system 99% cap, smoothing, 10 mA boundaries, 60 s confirmation, reversals, missing samples and sparse sampling");
    return 0;
}
