#pragma once
#ifdef CHART_LCD_HOST_TEST
#include "../test/lcd_host_stubs.h"
#else
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#endif
#include "starchart.h"

esp_err_t chart_lcd_init(void);
esp_err_t chart_lcd_tile(int y,int rows,uint16_t *pixels);
typedef struct {
    int64_t swap_us,command_us,dma_us,wait_us;
    int command_khz,pixel_khz;
} chart_lcd_timing_t;
typedef enum { CHART_LCD_WRITE_SWAP,CHART_LCD_WRITE_SWAP_O2,CHART_LCD_WRITE_WIRE } chart_lcd_write_mode_t;
/* WIRE pixels are already MSB-first and are not modified by transmission. */
esp_err_t chart_lcd_tile_profiled(int y,int rows,uint16_t *pixels,chart_lcd_write_mode_t mode,
                                 chart_lcd_timing_t *timing);
/* Call only between completed transfers; all commands remain at baseline clock. */
void chart_lcd_fast_pixels(bool enabled);
/* Temporary 80 MHz experiment only: raises the command clock so a frame can run
 * without switching clocks per window. Same call restrictions as fast_pixels. */
void chart_lcd_fast_commands(bool enabled);
/* Temporary 80 MHz experiment only: 0..3 pad drive for MOSI/SCK. */
esp_err_t chart_lcd_drive_strength(int capability);
/* Temporary 80 MHz experiment only: prepend `bits` (0..7) dummy bits to each
 * pixel block, compensating a panel that latches one bit period late. */
esp_err_t chart_lcd_shift_compensation(unsigned bits);
/* Temporary 80 MHz experiment only: clock the driver really programmed. */
esp_err_t chart_lcd_actual_pixel_khz(int *khz);
/* Single display-task owner. Close/drain before any tile write or mode change. */
esp_err_t chart_lcd_window_begin(int x,int y,int width,int rows);
/* CPU-order RGB565; <=32 rows and within the queried transaction limit.
 * Converts once before queueing. Never retry a rejected block. */
esp_err_t chart_lcd_window_submit(uint16_t *pixels,int rows,bool last);
esp_err_t chart_lcd_window_wait(uint16_t **completed_pixels);
/* Also call on failure to drain successful submissions; stop displaying on error. */
esp_err_t chart_lcd_window_end(chart_lcd_timing_t *timing);
esp_err_t chart_imu_init(void);
esp_err_t chart_imu_read(float gyro[3],float accel[3]);
esp_err_t chart_wifi_start(QueueHandle_t queue,char source[33],int *frequency);
void chart_wifi_pause(void);
uint32_t chart_wifi_dropped(void);
