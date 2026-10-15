#pragma once

/* Confirmed wiring from README.md. Panel settings below require module checks. */
#define CHART_LCD_MOSI 8
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
#define CHART_LCD_TEST_ID 4 /* 0 restores the normal status footer. */
#ifndef CHART_LCD_BOOT_DIAGNOSTIC
#define CHART_LCD_BOOT_DIAGNOSTIC 0 /* 1 shows the colour/boundary check before the chart. */
#endif
#ifndef CHART_LCD_REFRESH_TEST
#define CHART_LCD_REFRESH_TEST 1 /* 0 restores the existing star-chart tasks. */
#endif
#define CHART_LCD_REFRESH_ROWS 64 /* Benchmark maximum: 40960 bytes, not a full frame. */
#define CHART_LCD_PIPE_ROWS 32 /* Maximum rows in each fixed DMA pixel region. */
#define CHART_LCD_BUFFER_PIXELS (320 * CHART_LCD_PIPE_ROWS)
#define CHART_LCD_BUFFER_BYTES (CHART_LCD_BUFFER_PIXELS * 2)
#define CHART_LCD_PIXEL_BYTES (CHART_LCD_BUFFER_BYTES * 2) /* Two regions, 40 KiB total. */
#define CHART_LCD_BUTTON 28 /* DevKitC-1 BOOT, active low. */
#define CHART_LCD_BUTTON_DEBOUNCE_US 30000
#define CHART_LCD_REFRESH_Y 96
#define CHART_LCD_REFRESH_HEIGHT 128 /* Two independently animated 64-row bands. */
#define CHART_TILE_ROWS 16
#define CHART_DIRTY_CELL 16
#define CHART_DIRTY_COLS (320 / CHART_DIRTY_CELL)
#define CHART_DIRTY_ROWS (320 / CHART_DIRTY_CELL)
#define CHART_DIRTY_BYTES ((CHART_DIRTY_COLS * CHART_DIRTY_ROWS) / 8)
#define CHART_FPS 60 /* Above the full-frame bandwidth limit, for refresh testing. */

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

#define CHART_IMU_SDA 23
#define CHART_IMU_SCL 24
#define CHART_IMU_ADDRESS 0x68
#define CHART_IMU_I2C_HZ 100000
#define CHART_IMU_HZ 100
#define CHART_IMU_GYRO_FS 0 /* 0=250, 1=500, 2=1000, 3=2000 degrees/s. */
#define CHART_IMU_DLPF 3 /* MPU6050: ~42 Hz gyro bandwidth. */
#define CHART_IMU_YAW_AXIS 2 /* 0=X, 1=Y, 2=Z; verify installation. */
#define CHART_IMU_YAW_SIGN (-1) /* Clockwise from above is positive. Verify physically. */
#define CHART_IMU_LEVEL_AXIS 2
#define CHART_CALIBRATION_US 2500000
#define CHART_STILL_GYRO_DPS 1.5f
#define CHART_STILL_ACCEL_G 0.04f
#define CHART_TILT_DEG 20.0f
#define CHART_DISPLAY_TAU_S 0.10f
#define CHART_MAX_POSE_GAP_US 100000

#define CHART_DEFAULT_MODE 0 /* 0=UI_SIM, 1=IMU_SIM_RSSI, 2=LIVE_WIFI. */
#ifndef CHART_TARGET_SSID
#define CHART_TARGET_SSID "" /* Fill before LIVE_WIFI; empty is deliberately invalid. */
#endif
#define CHART_TARGET_BSSID "" /* Optional aa:bb:cc:dd:ee:ff; locks one transmitter. */
#define CHART_WIFI_COUNTRY "CN" /* Set to the actual operating jurisdiction. */
#define CHART_WIFI_QUEUE_LENGTH 64
#define CHART_SIM_NAME "CAM_02"
#define CHART_SIM_PEAK_DEG 60.0f
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
