#pragma once

#include "esp_err.h"

// This module reads ~5.3% high. Multimeter at VOUT (checked on 3V3 = 3.333 V) vs raw:
// 4.09 V / 4.3056 V idle, 4.04 V / 4.2562 V at 203 mA; both ratios agree within meter resolution.
#define INA226_BUS_VOLTAGE_GAIN 0.9496f

typedef struct {
    float voltage_v; // VBUS (VOUT side) relative to GND, gain corrected; wide-range linearity pending.
    float current_a; // Zero-offset corrected battery current: charging positive, discharging negative.
} ina226_sample_t;

// Called once at startup. Failure leaves sampling disabled.
esp_err_t ina226_init(void);
// Main task only. On failure, sample is unchanged; no cached values are returned.
esp_err_t ina226_read(ina226_sample_t *sample);
