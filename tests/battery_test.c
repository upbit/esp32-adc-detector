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
    assert(monitor.reading.percent == 50);
    sample(&monitor, 1, 4.0f, 0);
    assert(monitor.reading.percent == 51); // One-second smoothing, not a jump to 81%.
    battery_update(&monitor, NULL, 2000000);
    assert(!monitor.reading.valid);
    sample(&monitor, 3, 0, 0);
    assert(monitor.reading.valid && monitor.reading.percent == 0);
    battery_init(&monitor);
    sample(&monitor, 0, BATTERY_FULL_VOLTAGE_V - 0.0001f, 0);
    assert(monitor.reading.percent == 99);
    sample(&monitor, 1, BATTERY_FULL_VOLTAGE_V, 0);
    assert(monitor.reading.percent == 100);
}

static void test_thresholds_and_confirmation(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    run(&monitor, 0, 65, BATTERY_CURRENT_THRESHOLD_A);
    assert(monitor.reading.state == BATTERY_IDLE);
    run(&monitor, 66, 131, -BATTERY_CURRENT_THRESHOLD_A);
    assert(monitor.reading.state == BATTERY_IDLE);
    near(monitor.reading.session_mah, 0);

    run(&monitor, 132, 191, 0.011f);
    assert(monitor.reading.state == BATTERY_IDLE);
    sample(&monitor, 192, 4, 0.011f);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.reading.session_start_us == 132000000);
    near(monitor.reading.session_mah, 11.0 * 60 / 3600);

    double mah = monitor.reading.session_mah;
    run(&monitor, 193, 252, 0.005f);
    assert(monitor.reading.state == BATTERY_CHARGING);
    near(monitor.reading.session_mah, mah); // Pause immediately, even before idle confirmation.
    sample(&monitor, 253, 4, 0);
    assert(monitor.reading.state == BATTERY_IDLE);
    assert(monitor.reading.session_end_us == 193000000);
    near(monitor.reading.session_mah, mah);
}

static void test_integration_and_sessions(void)
{
    battery_monitor_t monitor;
    battery_init(&monitor);
    run(&monitor, 0, 3600, 1.0f);
    assert(monitor.reading.state == BATTERY_CHARGING);
    near(monitor.reading.session_mah, 1000);
    near(monitor.reading.session_wh, 4);
    double mah = monitor.reading.session_mah;

    run(&monitor, 3601, 3610, 0);
    near(monitor.reading.session_mah, mah);
    sample(&monitor, 3611, 4, 1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.reading.session_start_us == 0);
    near(monitor.reading.session_mah, mah);
    sample(&monitor, 3612, 4, 1);
    near(monitor.reading.session_mah, mah + 1000.0 / 3600);

    run(&monitor, 3613, 3672, -1);
    assert(monitor.reading.state == BATTERY_CHARGING);
    sample(&monitor, 3673, 4, -1);
    assert(monitor.reading.state == BATTERY_DISCHARGING);
    assert(monitor.reading.session_start_us == 3613000000);
    near(monitor.reading.session_mah, 1000.0 * 60 / 3600);
    near(monitor.reading.session_wh, 4.0 * 60 / 3600);
    run(&monitor, 3674, 3734, 0);
    assert(monitor.reading.state == BATTERY_IDLE);
    run(&monitor, 3735, 3795, 2);
    assert(monitor.reading.state == BATTERY_CHARGING);
    assert(monitor.reading.session_start_us == 3735000000);
    near(monitor.reading.session_mah, 2000.0 * 60 / 3600);
    near(monitor.reading.session_wh, 8.0 * 60 / 3600);

    // Trapezoidal integration of actual power and elapsed time, not nominal 1-second ticks.
    sample(&monitor, 3796.5, 4.2f, 1);
    near(monitor.reading.session_mah, 2000.0 * 60 / 3600 + 1500 * 1.5 / 3600);
    near(monitor.reading.session_wh, 8.0 * 60 / 3600 + (8.0 + 4.2) / 2 * 1.5 / 3600);
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
    assert(monitor.reading.session_start_us == 32000000);
    double mah = monitor.reading.session_mah;
    battery_update(&monitor, NULL, 93000000);
    sample(&monitor, 1000, 4, 1);
    near(monitor.reading.session_mah, mah);
    sample(&monitor, 1001, 4, 1);
    near(monitor.reading.session_mah, mah + 1000.0 / 3600);

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
    near(monitor.reading.session_mah, 1000.0 * 60 / 3600);
}

int main(void)
{
    test_curve();
    test_thresholds_and_confirmation();
    test_integration_and_sessions();
    test_missing_samples();
    puts("PASS: voltage curve, smoothing, 10 mA boundaries, 60 s confirmation, Wh/mAh, reversals and missing samples");
    return 0;
}
