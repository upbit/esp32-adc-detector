#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "starchart.h"

esp_err_t chart_lcd_init(void);
esp_err_t chart_lcd_tile(int y,int rows,uint16_t *pixels);
typedef struct {
    int64_t swap_us,command_us,dma_us;
    int command_khz,pixel_khz;
} chart_lcd_timing_t;
typedef enum { CHART_LCD_WRITE_SWAP,CHART_LCD_WRITE_SWAP_O2,CHART_LCD_WRITE_WIRE } chart_lcd_write_mode_t;
/* WIRE pixels are already MSB-first and are not modified by transmission. */
esp_err_t chart_lcd_tile_profiled(int y,int rows,uint16_t *pixels,chart_lcd_write_mode_t mode,
                                 chart_lcd_timing_t *timing);
/* Call only between completed transfers; all commands remain at baseline clock. */
void chart_lcd_fast_pixels(bool enabled);
esp_err_t chart_imu_init(void);
esp_err_t chart_imu_read(float gyro[3],float accel[3]);
esp_err_t chart_wifi_start(QueueHandle_t queue,char source[33],int *frequency);
void chart_wifi_pause(void);
uint32_t chart_wifi_dropped(void);
