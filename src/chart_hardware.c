#include "chart_hardware.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

static spi_device_handle_t lcd;
static i2c_master_dev_handle_t imu;
static QueueHandle_t signal_queue;
static uint8_t locked_bssid[6];
static atomic_bool listening;
static atomic_uint dropped;
static bool wifi_initialized;

#define TRY(call) do { esp_err_t e=(call); if (e!=ESP_OK) return e; } while (0)

static esp_err_t lcd_send(int dc,const void *data,size_t length)
{
    if (!length) return ESP_OK;
    gpio_set_level(CHART_LCD_DC,dc);
    spi_transaction_t t={.length=length*8,.tx_buffer=data};
    return spi_device_polling_transmit(lcd,&t);
}

static esp_err_t lcd_command(uint8_t command,const uint8_t *data,size_t n)
{
    TRY(lcd_send(0,&command,1));
    return lcd_send(1,data,n);
}

esp_err_t chart_lcd_init(void)
{
    gpio_config_t io={.pin_bit_mask=1ULL<<CHART_LCD_DC,.mode=GPIO_MODE_OUTPUT};
    if (CHART_LCD_BL>=0) io.pin_bit_mask|=1ULL<<(CHART_LCD_BL & 63);
    TRY(gpio_config(&io));
    if (CHART_LCD_BL>=0) gpio_set_level(CHART_LCD_BL,!CHART_LCD_BL_ACTIVE);
    spi_bus_config_t bus={.mosi_io_num=CHART_LCD_MOSI,.miso_io_num=-1,
        .sclk_io_num=CHART_LCD_SCK,.quadwp_io_num=-1,.quadhd_io_num=-1,
        .max_transfer_sz=320*CHART_TILE_ROWS*2};
    TRY(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device={.clock_speed_hz=CHART_LCD_SPI_HZ,
        .mode=0,.spics_io_num=CHART_LCD_CS,.queue_size=1};
    TRY(spi_bus_add_device(SPI2_HOST,&device,&lcd));
    /* Conservative ST7796 DCS bring-up. Vendor power/gamma table is not verified. */
    TRY(lcd_command(0x01,NULL,0)); vTaskDelay(pdMS_TO_TICKS(150));
    TRY(lcd_command(0x11,NULL,0)); vTaskDelay(pdMS_TO_TICKS(120));
    uint8_t format=0x55;
    TRY(lcd_command(0x3a,&format,1));
    uint8_t orientation=CHART_LCD_ROTATION==90 ? 0x60 :
        CHART_LCD_ROTATION==180 ? 0xc0 : CHART_LCD_ROTATION==270 ? 0xa0 : 0x00;
    if (CHART_LCD_BGR) orientation|=0x08;
    TRY(lcd_command(0x36,&orientation,1));
    TRY(lcd_command(CHART_LCD_INVERT ? 0x21 : 0x20,NULL,0));
    TRY(lcd_command(0x29,NULL,0)); vTaskDelay(pdMS_TO_TICKS(20));
    if (CHART_LCD_BL>=0) gpio_set_level(CHART_LCD_BL,CHART_LCD_BL_ACTIVE);
    ESP_LOGW("lcd","ST7796 DCS candidate; window offset %d,%d BGR=%d rotation=%d: verify on module",
        CHART_LCD_X_OFFSET,CHART_LCD_Y_OFFSET,CHART_LCD_BGR,CHART_LCD_ROTATION);
    return ESP_OK;
}

esp_err_t chart_lcd_tile(int y,int rows,uint16_t *pixels)
{
    int x1=CHART_LCD_X_OFFSET,x2=x1+319,y1=CHART_LCD_Y_OFFSET+y,y2=y1+rows-1;
    uint8_t xs[]={x1>>8,x1&255,x2>>8,x2&255}, ys[]={y1>>8,y1&255,y2>>8,y2&255};
    TRY(lcd_command(0x2a,xs,sizeof(xs)));
    TRY(lcd_command(0x2b,ys,sizeof(ys)));
    for (int i=0;i<320*rows;++i) pixels[i]=(pixels[i]<<8)|(pixels[i]>>8);
    TRY(lcd_command(0x2c,NULL,0));
    return lcd_send(1,pixels,320*rows*2);
}

static esp_err_t imu_write(uint8_t address,uint8_t value)
{
    uint8_t bytes[]={address,value};
    return i2c_master_transmit(imu,bytes,2,50);
}

esp_err_t chart_imu_init(void)
{
    if (!imu) {
        i2c_master_bus_handle_t bus;
        i2c_master_bus_config_t config={.i2c_port=I2C_NUM_0,.sda_io_num=CHART_IMU_SDA,
            .scl_io_num=CHART_IMU_SCL,.clk_source=I2C_CLK_SRC_DEFAULT,.glitch_ignore_cnt=7,
            .flags.enable_internal_pullup=1};
        TRY(i2c_new_master_bus(&config,&bus));
        i2c_device_config_t device={.dev_addr_length=I2C_ADDR_BIT_LEN_7,
            .device_address=CHART_IMU_ADDRESS,.scl_speed_hz=CHART_IMU_I2C_HZ};
        TRY(i2c_master_bus_add_device(bus,&device,&imu));
    }
    uint8_t reg=0x75,id=0;
    TRY(i2c_master_transmit_receive(imu,&reg,1,&id,1,50));
    if ((id & 0x7e)!=0x68) return ESP_ERR_NOT_FOUND;
    TRY(imu_write(0x6b,0x80)); vTaskDelay(pdMS_TO_TICKS(100));
    TRY(imu_write(0x6b,0x01));
    TRY(imu_write(0x1a,CHART_IMU_DLPF));
    TRY(imu_write(0x19,1000/CHART_IMU_HZ-1));
    TRY(imu_write(0x1b,CHART_IMU_GYRO_FS<<3));
    return imu_write(0x1c,0x00);
}

static int16_t signed_word(const uint8_t *p)
{
    return (int16_t)((uint16_t)p[0]<<8|p[1]);
}

esp_err_t chart_imu_read(float gyro[3],float accel[3])
{
    uint8_t reg=0x3b,bytes[14];
    if (!imu) return ESP_ERR_INVALID_STATE;
    TRY(i2c_master_transmit_receive(imu,&reg,1,bytes,sizeof(bytes),20));
    const float scales[]={131.0f,65.5f,32.8f,16.4f};
    for (int j=0;j<3;++j) {
        accel[j]=signed_word(bytes+j*2)/16384.0f;
        gyro[j]=signed_word(bytes+8+j*2)/scales[CHART_IMU_GYRO_FS];
    }
    return ESP_OK;
}

static void wifi_receive(void *buffer,wifi_promiscuous_pkt_type_t type)
{
    int64_t received=esp_timer_get_time();
    if (type!=WIFI_PKT_MGMT || !atomic_load(&listening)) return;
    const wifi_promiscuous_pkt_t *packet=buffer;
    const uint8_t *f=packet->payload;
    if (packet->rx_ctrl.rx_state ||
        !chart_target_beacon(f,packet->rx_ctrl.sig_len,locked_bssid)) return;
    chart_signal_event_t event={received,packet->rx_ctrl.rssi,false};
    if (xQueueSend(signal_queue,&event,0)!=pdTRUE) atomic_fetch_add(&dropped,1);
}

void chart_wifi_pause(void)
{
    atomic_store(&listening,false);
    if (wifi_initialized) esp_wifi_set_promiscuous(false);
}

uint32_t chart_wifi_dropped(void) { return atomic_load(&dropped); }

esp_err_t chart_wifi_start(QueueHandle_t queue,char source[33],int *frequency)
{
    chart_wifi_pause();
    signal_queue=queue;
    if (!CHART_TARGET_SSID[0] || strlen(CHART_TARGET_SSID)>32) return ESP_ERR_INVALID_ARG;
    uint8_t wanted[6];
    bool specified=CHART_TARGET_BSSID[0];
    if (specified) {
        unsigned b[6]; int consumed=0;
        if (sscanf(CHART_TARGET_BSSID,"%2x:%2x:%2x:%2x:%2x:%2x%n",
                   b,b+1,b+2,b+3,b+4,b+5,&consumed)!=6 ||
            CHART_TARGET_BSSID[consumed]) return ESP_ERR_INVALID_ARG;
        for (int j=0;j<6;++j) wanted[j]=b[j];
    }
    if (!wifi_initialized) {
        TRY(nvs_flash_init()); /* Do not erase the user's NVS implicitly. */
        TRY(esp_netif_init());
        TRY(esp_event_loop_create_default());
        wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();
        TRY(esp_wifi_init(&config));
        TRY(esp_wifi_set_storage(WIFI_STORAGE_RAM));
        TRY(esp_wifi_set_mode(WIFI_MODE_STA));
        TRY(esp_wifi_start());
        wifi_initialized=true;
    }
    TRY(esp_wifi_set_country_code(CHART_WIFI_COUNTRY,true));
    TRY(esp_wifi_set_band_mode(WIFI_BAND_MODE_AUTO));
    wifi_scan_config_t scan={.ssid=(uint8_t *)CHART_TARGET_SSID,
        .bssid=specified ? wanted : NULL,.scan_type=WIFI_SCAN_TYPE_PASSIVE};
    TRY(esp_wifi_scan_start(&scan,true));
    uint16_t count=0;
    TRY(esp_wifi_scan_get_ap_num(&count));
    wifi_ap_record_t best={0}; bool found=false;
    for (unsigned i=0;i<count;++i) {
        wifi_ap_record_t ap;
        esp_err_t err=esp_wifi_scan_get_ap_record(&ap);
        if (err!=ESP_OK) { esp_wifi_clear_ap_list(); return err; }
        ESP_LOGI("wifi","scan %s %02x:%02x:%02x:%02x:%02x:%02x CH%u RSSI=%d",
            (char *)ap.ssid,ap.bssid[0],ap.bssid[1],ap.bssid[2],ap.bssid[3],ap.bssid[4],ap.bssid[5],ap.primary,ap.rssi);
        if (!strcmp((char *)ap.ssid,CHART_TARGET_SSID) &&
            (!specified || !memcmp(ap.bssid,wanted,6)) && (!found || ap.rssi>best.rssi)) {
            best=ap; found=true;
        }
    }
    esp_wifi_clear_ap_list();
    if (!found) return ESP_ERR_NOT_FOUND;
    TRY(esp_wifi_set_band_mode(best.primary<=14 ? WIFI_BAND_MODE_2G_ONLY : WIFI_BAND_MODE_5G_ONLY));
    TRY(esp_wifi_set_channel(best.primary,WIFI_SECOND_CHAN_NONE));
    TRY(esp_wifi_set_ps(WIFI_PS_NONE));
    memcpy(locked_bssid,best.bssid,6);
    memcpy(source,best.ssid,32); source[32]=0;
    *frequency=chart_channel_frequency(best.primary);
    wifi_promiscuous_filter_t filter={.filter_mask=WIFI_PROMIS_FILTER_MASK_MGMT};
    TRY(esp_wifi_set_promiscuous_filter(&filter));
    TRY(esp_wifi_set_promiscuous_rx_cb(wifi_receive));
    atomic_store(&listening,true);
    esp_err_t result=esp_wifi_set_promiscuous(true);
    if (result!=ESP_OK) atomic_store(&listening,false);
    ESP_LOGI("wifi","LOCK %02x:%02x:%02x:%02x:%02x:%02x CH%u %d MHz; Beacon only; not associated",
        best.bssid[0],best.bssid[1],best.bssid[2],best.bssid[3],best.bssid[4],best.bssid[5],best.primary,*frequency);
    return result;
}
