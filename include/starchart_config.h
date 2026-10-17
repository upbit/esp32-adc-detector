#pragma once

/* Confirmed wiring from README.md. Panel settings below require module checks. */
/* GPIO7 is the SPI2 IOMUX MOSI pad on the C5; with SCK=6 and CS=10 the whole
 * bus leaves through the IOMUX instead of the GPIO matrix, which removes the
 * skew that corrupted 80 MHz pixels (photo-measured 1-bit shift, 2026-10-15). */
#define CHART_LCD_MOSI 7
#define CHART_LCD_SCK 6
#define CHART_LCD_CS 10
#define CHART_LCD_DC 9
#define CHART_LCD_BL (-1) /* Do not drive GPIO0 until the BL circuit is confirmed. */
#define CHART_LCD_BL_ACTIVE 1
#define CHART_LCD_SPI_HZ 40000000 /* Initialization, commands and baseline pixels. */
#define CHART_LCD_FAST_SPI_HZ 80000000 /* Experimental pixel-only clock; verify optically. */
#define CHART_LCD_X_OFFSET 0 /* Unverified: measure using the boot test pattern. */
#define CHART_LCD_Y_OFFSET 0 /* Unverified: not an assumed 320x480 stretch. */
#define CHART_LCD_ROTATION 0 /* 0, 90, 180, 270; verify offsets at each rotation. */
#define CHART_LCD_MIRROR_X 1 /* Correct the observed left/right panel mirroring. */
#define CHART_LCD_BGR 1
#define CHART_LCD_INVERT 1
#define CHART_LCD_TEST_MS 5000 /* Hold the bright boot pattern for inspection. */
#ifndef CHART_LCD_BOOT_DIAGNOSTIC
#define CHART_LCD_BOOT_DIAGNOSTIC 0 /* 1 shows the colour/boundary check before the chart. */
#endif
#define CHART_LCD_PIPE_ROWS 32 /* Maximum rows in each fixed DMA pixel region. */
#define CHART_LCD_BUFFER_PIXELS (320 * CHART_LCD_PIPE_ROWS)
#define CHART_LCD_BUFFER_BYTES (CHART_LCD_BUFFER_PIXELS * 2)
#define CHART_LCD_PIXEL_BYTES (CHART_LCD_BUFFER_BYTES * 2) /* Two regions, 40 KiB total. */
#define CHART_TILE_ROWS 16
/* At 80 MHz the panel latches one bit period late, so the pixel stream arrives
 * rotated left by one bit (measured from photographs, 2026-10-15; 40 MHz needs
 * none). Prepending this many dummy bits per block cancels it. The panel runs
 * far outside the ST7796 66 ns write spec here, so the amount may drift with
 * temperature, supply or panel batch -- re-check with the ruler below. */
#ifndef CHART_LCD_SHIFT_BITS
#define CHART_LCD_SHIFT_BITS 1
#endif
#ifndef CHART_LCD_SHIFT_DIAGNOSTIC
#define CHART_LCD_SHIFT_DIAGNOSTIC 0 /* 1 overlays the bit ruler and swatches. */
#endif
#ifndef CHART_RENDER_BENCHMARK
#define CHART_RENDER_BENCHMARK 0 /* 1 reports a one-shot draw-cost breakdown at boot. */
#endif
#if CHART_LCD_SHIFT_DIAGNOSTIC
/* Amber is the only palette entry with a saturated red channel and live G/B
 * bits, so it is the one colour that exposes a shifted stream; the other four
 * act as controls. The band covers the bottom status rows while diagnosing. */
#define CHART_DIAG_SWATCH_Y 306
#define CHART_DIAG_SWATCH_ROWS 14
#define CHART_DIAG_SWATCH_W 48
/* Bit ruler: 16 blocks of 20 px, block k holding only bit k. A stream shifted
 * by n bits slides the pattern n blocks left. The scale rows use 0x0000/0xffff,
 * the two words a rotation cannot change, giving a trustworthy spatial scale. */
#define CHART_DIAG_RULER_Y 284
#define CHART_DIAG_RULER_ROWS 22
#define CHART_DIAG_RULER_SCALE 5
#define CHART_DIAG_RULER_W 20
#endif
#define CHART_DIRTY_CELL 16
#define CHART_DIRTY_COLS (320 / CHART_DIRTY_CELL)
#define CHART_DIRTY_ROWS (320 / CHART_DIRTY_CELL)
#define CHART_DIRTY_BYTES ((CHART_DIRTY_COLS * CHART_DIRTY_ROWS) / 8)

/* Shared layout: the renderer and chart_screen_point must agree on the centre. */
#define CHART_CENTER_X 160
#define CHART_CENTER_Y 156
#define CHART_OUTER_R 114 /* Outer ring; ticks and glow extend a few pixels further. */
#define CHART_SAMPLE_R 103 /* Fixed sample radius; RSSI never moves points radially. */
/* Text sizes confirmed on hardware 2026-10-15: ticks up one step, labels up two. */
#define CHART_FONT_NAME 18
#define CHART_FONT_RSSI 24
#define CHART_FONT_ANGLE 18
#define CHART_FONT_FREQ 14 /* 2437 MHz under the name; one step up from the ticks. */
#define CHART_FONT_TICK 12 /* -90/+90/180 dial ticks. */
#define CHART_FONT_LABEL 14 /* dBm, 模拟, 天线: deliberately larger than the ticks. */
#define CHART_FONT_STATUS 14 /* Bottom status line. */
#define CHART_FONT_TURN 14 /* The 转角 prefix; its number uses CHART_FONT_ANGLE. */
/* Noto Sans SC hhea ascender is 1.16 em; used to align different sizes on one baseline. */
#define CHART_ASCENT(size) (((size)*116+50)/100)

#define CHART_IMU_SDA 4
#define CHART_IMU_SCL 5
#define CHART_IMU_ADDRESS 0x68
#define CHART_IMU_I2C_HZ 100000
#define CHART_IMU_HZ 100
#define CHART_IMU_GYRO_FS 0 /* 0=250, 1=500, 2=1000, 3=2000 degrees/s. */
#define CHART_IMU_DLPF 3 /* MPU6050: ~42 Hz gyro bandwidth. */
#define CHART_IMU_YAW_SIGN (-1) /* Clockwise from above is positive; bench-verified with the chip face up. */
#define CHART_IMU_LEVEL_AXIS 2 /* Only used for the tilt readout and the level gate. */
#define CHART_CALIBRATION_US 3000000
#define CHART_STILL_GYRO_DPS 3.0f /* Deviation from the window mean; handheld tremor is ~1-2 dps. */
#define CHART_STILL_ACCEL_G 0.04f
#define CHART_TILT_DEG 20.0f
#define CHART_DISPLAY_TAU_S 0.10f
#define CHART_MAX_POSE_GAP_US 100000

/* Power-on calibration acceptance gate. `psidot = dot(gyro - bias, a_hat)` is
 * exact at any tilt, so the yaw axis comes from measured gravity rather than a
 * configured mount guess; a_hat is the unit gravity vector in the body frame.
 * The gate rejects a calibration instead of trusting it; failure repeats the
 * on-screen guidance (see CHART_MAX_GATE_RETRIES) and never falls back to
 * simulation. */
#define CHART_CAL_MIN_SAMPLES 30 /* Reject a window that barely started. */
#define CHART_CAL_MAX_BIAS_DPS 5.0f /* A still MPU6050 is well under 1 dps. */
#define CHART_CAL_MAX_VAR_DPS2 1.5f /* Per-axis gyro variance; a steady hand measured 0.5-1.0 dps^2. */
#define CHART_CAL_MIN_ACCEL_G 0.85f
#define CHART_CAL_MAX_ACCEL_G 1.15f
#define CHART_CAL_MIN_UP_G 0.9f /* |a| after normalising; keeps a_hat identifiable. */
#define CHART_MAX_GATE_RETRIES 8 /* Recoverable failures before the fatal red screen. */

#define CHART_DEFAULT_MODE 1 /* 1=IMU_SIM_RSSI, 2=LIVE_WIFI; 0=UI_SIM is host-only. */
#ifndef CHART_TARGET_SSID
#define CHART_TARGET_SSID "" /* Fill before LIVE_WIFI; empty is deliberately invalid. */
#endif
#define CHART_TARGET_BSSID "" /* Optional aa:bb:cc:dd:ee:ff; locks one transmitter. */
#define CHART_WIFI_COUNTRY "CN" /* Set to the actual operating jurisdiction. */
#define CHART_WIFI_QUEUE_LENGTH 64
#define CHART_SIM_NAME "CAM_02"
#define CHART_SIM_PEAK_DEG 0.0f /* At the calibrated start: the amber peak marks the original heading for drift checks. */
#define CHART_SIM_SPEED_DPS 15.0f
#define CHART_SIM_PERIOD_US 100000
#define CHART_SIM_NOISE_DB 0.0f /* Deterministic by default; optional +/- noise. */

#define CHART_BINS 36
#define CHART_WINDOW 5
#define CHART_POSE_HISTORY 256
#define CHART_TREND_LENGTH 44
#define CHART_PEAK_MIN_SAMPLES 3
#define CHART_PEAK_HYSTERESIS_DB 0.5f
#define CHART_RSSI_MIN (-90.0f)
#define CHART_RSSI_MAX (-40.0f)
#define CHART_FRESH_US 2000000
#define CHART_DARK_US 8000000
#define CHART_HISTORY_US 20000000
#define CHART_SIGNAL_TIMEOUT_US 2000000
