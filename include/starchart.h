#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "starchart_config.h"

typedef enum { UI_SIM, IMU_SIM_RSSI, LIVE_WIFI } chart_mode_t;
typedef enum { CHART_READY, CHART_CALIBRATING, CHART_IMU_ERROR } chart_pose_status_t;
typedef enum { CHART_TARGET_SEARCH, CHART_TARGET_LOCKED, CHART_TARGET_MISSING,
               CHART_WIFI_ERROR } chart_target_status_t;

typedef struct {
    uint32_t count;
    uint8_t used, next;
    float values[CHART_WINDOW], angles[CHART_WINDOW];
    float rssi, heading;
    int64_t updated_us;
} chart_bin_t;

typedef struct { int64_t time_us; float heading; } chart_pose_t;
typedef struct { int64_t time_us; int8_t rssi; bool synthetic; } chart_signal_event_t;

typedef struct {
    chart_mode_t mode;
    chart_pose_status_t pose_status;
    chart_target_status_t target_status;
    char source[33];
    int frequency_mhz;
    float heading, display_heading, tilt_deg;
    chart_bin_t bins[CHART_BINS];
    int peak_bin, coverage;
    float current_rssi, recent[CHART_WINDOW];
    unsigned recent_used, recent_next;
    chart_signal_event_t trend[CHART_TREND_LENGTH];
    unsigned trend_used, trend_next;
    int64_t last_signal_us, epoch_us;
    uint32_t samples, sync_rejected;
    int64_t max_event_age_us;
    chart_pose_t poses[CHART_POSE_HISTORY];
    unsigned pose_used, pose_next;
} chart_model_t;

typedef struct {
    bool calibrated;
    int64_t last_us, still_start_us;
    unsigned still_count;
    double sums[3], squares[3];
    float bias[3], heading, display_heading;
} chart_integrator_t;

float chart_wrap180(float degrees);
int chart_channel_frequency(int channel);
void chart_screen_point(float sample_heading, float display_heading, float radius,
                        float *x, float *y);
float chart_synthetic_rssi(float heading);
bool chart_target_beacon(const uint8_t *frame, size_t length, const uint8_t bssid[6]);
void chart_init(chart_model_t *model, chart_mode_t mode, int64_t now);
void chart_clear(chart_model_t *model, int64_t now);
void chart_zero(chart_model_t *model, int64_t now);
void chart_record_pose(chart_model_t *model, int64_t now, float heading, float display);
bool chart_pose_at(const chart_model_t *model, int64_t at, float *heading);
bool chart_record_signal(chart_model_t *model, chart_signal_event_t event, int64_t now);
void chart_integrator_reset(chart_integrator_t *integrator);
bool chart_integrate(chart_integrator_t *integrator, int64_t now,
                     const float gyro[3], const float accel[3], float *tilt);

/* Pure RGB565 renderer shared by firmware and the host simulation. */
void chart_render_init(void);
void chart_render_tile(const chart_model_t *snapshot, int64_t now, int y, int rows,
                       uint16_t *rgb565);
void chart_render_test_tile(int y, int rows, uint16_t *rgb565);
