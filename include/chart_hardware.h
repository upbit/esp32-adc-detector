#pragma once
#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "starchart.h"

esp_err_t chart_lcd_init(void);
esp_err_t chart_lcd_tile(int y,int rows,uint16_t *pixels);
esp_err_t chart_imu_init(void);
esp_err_t chart_imu_read(float gyro[3],float accel[3]);
esp_err_t chart_wifi_start(QueueHandle_t queue,char source[33],int *frequency);
void chart_wifi_pause(void);
uint32_t chart_wifi_dropped(void);
