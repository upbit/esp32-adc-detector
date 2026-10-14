#include "starchart.h"

#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f

float chart_wrap180(float a)
{
    a = fmodf(a + 180.0f, 360.0f);
    if (a < 0) a += 360.0f;
    return a - 180.0f;
}

int chart_channel_frequency(int c)
{
    if (c >= 1 && c <= 13) return 2407 + c * 5;
    if (c == 14) return 2484;
    if ((c >= 36 && c <= 64 && c % 4 == 0) ||
        (c >= 100 && c <= 144 && c % 4 == 0) ||
        (c >= 149 && c <= 177 && (c - 149) % 4 == 0)) return 5000 + c * 5;
    return 0;
}

void chart_screen_point(float sample, float display, float r, float *x, float *y)
{
    float a = chart_wrap180(sample - display) * PI / 180.0f;
    *x = CHART_CENTER_X + r * sinf(a);
    *y = CHART_CENTER_Y - r * cosf(a);
}

float chart_synthetic_rssi(float heading)
{
    float d = chart_wrap180(heading - CHART_SIM_PEAK_DEG);
    return -80.0f + 30.0f * expf(-d * d / (2.0f * 25.0f * 25.0f));
}

bool chart_target_beacon(const uint8_t *f, size_t length, const uint8_t bssid[6])
{
    /* 24-byte management header, 12-byte fixed Beacon body, 4-byte FCS. */
    return length >= 40 && f[0] == 0x80 && !(f[1] & 0x03) &&
           !memcmp(f + 10, bssid, 6) && !memcmp(f + 16, bssid, 6);
}

static float median(const float *values, unsigned n)
{
    float sorted[CHART_WINDOW];
    memcpy(sorted, values, n * sizeof(float));
    for (unsigned i = 1; i < n; ++i) {
        float v = sorted[i];
        unsigned j = i;
        while (j && sorted[j - 1] > v) {
            sorted[j] = sorted[j - 1];
            --j;
        }
        sorted[j] = v;
    }
    return n % 2 ? sorted[n / 2] : (sorted[n / 2 - 1] + sorted[n / 2]) * 0.5f;
}

void chart_clear(chart_model_t *m, int64_t now)
{
    memset(m->bins, 0, sizeof(m->bins));
    memset(m->trend, 0, sizeof(m->trend));
    m->peak_bin = -1;
    m->coverage = 0;
    m->recent_used = m->recent_next = m->trend_used = m->trend_next = 0;
    m->current_rssi = CHART_RSSI_MIN;
    m->last_signal_us = 0;
    m->epoch_us = now;
    m->samples = m->sync_rejected = 0;
    m->max_event_age_us = 0;
}

void chart_zero(chart_model_t *m, int64_t now)
{
    chart_clear(m, now);
    m->heading = m->display_heading = 0;
    m->pose_used = m->pose_next = 0;
    chart_record_pose(m, now, 0, 0);
}

void chart_init(chart_model_t *m, chart_mode_t mode, int64_t now)
{
    memset(m, 0, sizeof(*m));
    m->mode = mode;
    m->pose_status = mode == UI_SIM ? CHART_READY : CHART_CALIBRATING;
    m->target_status = mode == LIVE_WIFI ? CHART_TARGET_SEARCH : CHART_TARGET_LOCKED;
    const char *name = mode == LIVE_WIFI ? CHART_TARGET_SSID : CHART_SIM_NAME;
    memcpy(m->source, name, strlen(name) < 32 ? strlen(name) : 32);
    m->frequency_mhz = mode == LIVE_WIFI ? 0 : 2437;
    chart_zero(m, now);
}

void chart_record_pose(chart_model_t *m, int64_t now, float heading, float display)
{
    m->heading = heading;
    m->display_heading = display;
    if (m->pose_used) {
        unsigned last = (m->pose_next + CHART_POSE_HISTORY - 1) % CHART_POSE_HISTORY;
        if (now <= m->poses[last].time_us) return;
    }
    m->poses[m->pose_next] = (chart_pose_t){now, heading};
    m->pose_next = (m->pose_next + 1) % CHART_POSE_HISTORY;
    if (m->pose_used < CHART_POSE_HISTORY) ++m->pose_used;
}

bool chart_pose_at(const chart_model_t *m, int64_t at, float *heading)
{
    if (!m->pose_used || at < m->epoch_us) return false;
    unsigned first = (m->pose_next + CHART_POSE_HISTORY - m->pose_used) % CHART_POSE_HISTORY;
    chart_pose_t previous = m->poses[first];
    if (at < previous.time_us) return false;
    if (at == previous.time_us) { *heading = previous.heading; return true; }
    for (unsigned i = 1; i < m->pose_used; ++i) {
        chart_pose_t p = m->poses[(first + i) % CHART_POSE_HISTORY];
        if (at <= p.time_us) {
            int64_t dt = p.time_us - previous.time_us;
            if (dt > CHART_MAX_POSE_GAP_US) return false;
            float fraction = (float)(at - previous.time_us) / (float)dt;
            *heading = previous.heading + fraction * (p.heading - previous.heading);
            return true;
        }
        previous = p;
    }
    return false; /* Caller waits for the following IMU observation, never extrapolates. */
}

bool chart_record_signal(chart_model_t *m, chart_signal_event_t e, int64_t now)
{
    float heading;
    if (m->pose_status != CHART_READY || e.synthetic != (m->mode != LIVE_WIFI) ||
        e.rssi >= 0 || e.rssi < -127 ||
        e.time_us > now || !chart_pose_at(m, e.time_us, &heading)) {
        ++m->sync_rejected;
        return false;
    }
    float positive = chart_wrap180(heading);
    if (positive < 0) positive += 360.0f;
    int index = (int)floorf((positive + 5.0f) / 10.0f) % CHART_BINS;
    chart_bin_t *b = &m->bins[index];
    if (!b->count) ++m->coverage;
    if (b->count != UINT32_MAX) ++b->count;
    b->values[b->next] = e.rssi;
    b->angles[b->next] = chart_wrap180(heading - index * 10.0f);
    b->next = (b->next + 1) % CHART_WINDOW;
    if (b->used < CHART_WINDOW) ++b->used;
    b->rssi = median(b->values, b->used);
    b->heading = index * 10.0f;
    for (unsigned i = 0; i < b->used; ++i) b->heading += b->angles[i] / b->used;
    b->updated_us = e.time_us;
    if (b->count >= CHART_PEAK_MIN_SAMPLES && b->used >= CHART_PEAK_MIN_SAMPLES &&
        (m->peak_bin < 0 || m->peak_bin == index ||
         b->rssi > m->bins[m->peak_bin].rssi + CHART_PEAK_HYSTERESIS_DB)) m->peak_bin = index;

    m->recent[m->recent_next] = e.rssi;
    m->recent_next = (m->recent_next + 1) % CHART_WINDOW;
    if (m->recent_used < CHART_WINDOW) ++m->recent_used;
    float value = median(m->recent, m->recent_used);
    m->current_rssi = m->last_signal_us ? m->current_rssi * 0.65f + value * 0.35f : value;
    m->last_signal_us = e.time_us;
    m->trend[m->trend_next] = e;
    m->trend_next = (m->trend_next + 1) % CHART_TREND_LENGTH;
    if (m->trend_used < CHART_TREND_LENGTH) ++m->trend_used;
    ++m->samples;
    if (now - e.time_us > m->max_event_age_us) m->max_event_age_us = now - e.time_us;
    return true;
}

void chart_integrator_reset(chart_integrator_t *i)
{
    memset(i, 0, sizeof(*i));
}

bool chart_integrate(chart_integrator_t *i, int64_t now,
                     const float gyro[3], const float accel[3], float *tilt)
{
    float norm = sqrtf(accel[0]*accel[0] + accel[1]*accel[1] + accel[2]*accel[2]);
    float ratio = norm > 0.01f ? fabsf(accel[CHART_IMU_LEVEL_AXIS]) / norm : 0;
    *tilt = acosf(fminf(1, ratio)) * 180.0f / PI;
    int64_t delta_us = i->last_us ? now - i->last_us : 0;
    i->last_us = now;
    bool still = fabsf(norm - 1) < CHART_STILL_ACCEL_G && *tilt < CHART_TILT_DEG;
    for (unsigned a = 0; a < 3; ++a) still &= fabsf(gyro[a]) < CHART_STILL_GYRO_DPS;
    if (!i->calibrated) {
        if (!still || delta_us > CHART_MAX_POSE_GAP_US || delta_us < 0) {
            i->still_start_us = 0;
            i->still_count = 0;
            memset(i->sums, 0, sizeof(i->sums));
            memset(i->squares, 0, sizeof(i->squares));
            return false;
        }
        if (!i->still_start_us) i->still_start_us = now;
        ++i->still_count;
        for (unsigned a = 0; a < 3; ++a) {
            i->sums[a] += gyro[a];
            i->squares[a] += gyro[a] * gyro[a];
        }
        if (now - i->still_start_us >= CHART_CALIBRATION_US) {
            bool stable = true;
            for (unsigned a = 0; a < 3; ++a) {
                double mean = i->sums[a] / i->still_count;
                stable &= i->squares[a] / i->still_count - mean * mean < 0.04;
                i->bias[a] = (float)mean;
            }
            if (stable) i->calibrated = true;
            else {
                i->still_start_us = 0;
                i->still_count = 0;
                memset(i->sums, 0, sizeof(i->sums));
                memset(i->squares, 0, sizeof(i->squares));
            }
        }
        return i->calibrated;
    }
    /* A stalled acquisition cannot safely integrate the unseen motion. */
    if (delta_us <= 0 || delta_us > CHART_MAX_POSE_GAP_US) {
        chart_integrator_reset(i);
        i->last_us = now;
        return false;
    }
    float dt = delta_us / 1000000.0f;
    i->heading += (gyro[CHART_IMU_YAW_AXIS] - i->bias[CHART_IMU_YAW_AXIS]) *
                  CHART_IMU_YAW_SIGN * dt;
    float alpha = 1.0f - expf(-dt / CHART_DISPLAY_TAU_S);
    i->display_heading += alpha * (i->heading - i->display_heading);
    return true;
}
