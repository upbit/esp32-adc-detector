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
typedef struct {
    int64_t swap_us,command_us,dma_us,wait_us;
    int command_khz,pixel_khz;
} chart_lcd_timing_t;
/* Call only between completed transfers; all commands remain at baseline clock. */
void chart_lcd_fast_pixels(bool enabled);
/* Single display-task owner. Close/drain before the next window or clock change. */
esp_err_t chart_lcd_window_begin(int x,int y,int width,int rows);
/* CPU-order RGB565; <=32 rows and within the queried transaction limit.
 * Converts once before queueing. Never retry a rejected block.
 * At the fast clock the conversion also appends one shift-compensation byte
 * after the pixels, so `pixels` must have CHART_LCD_SHIFT_BITS worth of slack:
 * pass a full CHART_LCD_BUFFER_BYTES region, not an interior offset. */
esp_err_t chart_lcd_window_submit(uint16_t *pixels,int rows,bool last);
esp_err_t chart_lcd_window_wait(uint16_t **completed_pixels);
/* Also call on failure to drain successful submissions; stop displaying on error. */
esp_err_t chart_lcd_window_end(chart_lcd_timing_t *timing);
esp_err_t chart_imu_init(void);
/* temp_c may be NULL; it is only reported by the serial imu dump. */
esp_err_t chart_imu_read(float gyro[3],float accel[3],float *temp_c);
esp_err_t chart_wifi_start(QueueHandle_t queue,char source[33],int *frequency);
void chart_wifi_pause(void);
uint32_t chart_wifi_dropped(void);
