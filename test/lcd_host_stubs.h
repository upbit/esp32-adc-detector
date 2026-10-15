#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "starchart.h"

typedef int esp_err_t;
typedef void *QueueHandle_t;
#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 1
#define ESP_ERR_INVALID_STATE 2
#define ESP_ERR_NO_MEM 3
#define portMAX_DELAY (-1)
#define pdMS_TO_TICKS(ms) (ms)
#define ESP_LOGI(...) ((void)0)
#define ESP_LOGW(...) ((void)0)
#define GPIO_MODE_OUTPUT 1
#define SPI2_HOST 2
#define SPI_DMA_CH_AUTO 1
#define SPI_TRANS_USE_TXDATA 1
#define SPI_TRANS_CS_KEEP_ACTIVE 2
#define SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL 4

typedef struct {
    size_t length;
    const void *tx_buffer;
    uint32_t override_freq_hz,flags;
    uint8_t tx_data[4];
} spi_transaction_t;
typedef void *spi_device_handle_t;
typedef struct { uint64_t pin_bit_mask; int mode; } gpio_config_t;
typedef struct { int mosi_io_num,miso_io_num,sclk_io_num,quadwp_io_num,quadhd_io_num,max_transfer_sz; } spi_bus_config_t;
typedef struct { int clock_speed_hz,mode,spics_io_num,queue_size; } spi_device_interface_config_t;

int64_t esp_timer_get_time(void);
int gpio_set_level(int pin,int value);
esp_err_t gpio_config(const gpio_config_t *config);
void vTaskDelay(int ticks);
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t *config,int dma);
esp_err_t spi_bus_add_device(int host,const spi_device_interface_config_t *config,spi_device_handle_t *handle);
esp_err_t spi_bus_get_max_transaction_len(int host,size_t *limit);
esp_err_t spi_device_get_actual_freq(spi_device_handle_t handle,int *khz);
esp_err_t spi_device_acquire_bus(spi_device_handle_t handle,int ticks);
void spi_device_release_bus(spi_device_handle_t handle);
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t *t);
esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t *t,int ticks);
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,spi_transaction_t **t,int ticks);
