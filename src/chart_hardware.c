#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Keep the pixel swap at O2 without changing the other hardware drivers. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((optimize("O2")))
#endif
static __attribute__((noinline)) void lcd_swap_optimized(uint16_t *pixels,size_t count)
{
    for (size_t i=0;i<count;++i) pixels[i]=(pixels[i]<<8)|(pixels[i]>>8);
}

#ifndef CHART_LCD_SWAP_HOST_TEST

/* At the fast clock the panel starts sampling one bit period late, so every
 * uniform block comes back rotated left by one bit. Prepending one dummy bit
 * absorbs that late first sample and realigns the rest.
 *
 * A window keeps CS asserted across its blocks, so the panel sees ONE
 * continuous bit stream: the dummy bit belongs to the window, not to each
 * block. Shifting blocks independently would push the stream a further 7 bits
 * at every boundary and band the image. `carry` therefore threads the bits
 * displaced out of one block into the next, and only the final block emits the
 * trailing byte. Operates on wire-order bytes, after the RGB565 swap.
 * Returns the bits carried out of this block. */
static __attribute__((noinline)) unsigned lcd_shift_stream(uint8_t *bytes,size_t count,
                                                           unsigned bits,unsigned carry,bool last)
{
    for (size_t i=0;i<count;++i) {
        unsigned v=bytes[i];
        bytes[i]=(uint8_t)((carry<<(8-bits))|(v>>bits));
        carry=v&((1u<<bits)-1);
    }
    if (last) bytes[count]=(uint8_t)(carry<<(8-bits));
    return carry;
}

#include "chart_hardware.h"

#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#ifndef CHART_LCD_HOST_TEST
#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "driver/spi_master.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "soc/spi_pins.h"
#endif

static spi_device_handle_t lcd;
static size_t lcd_dma_max;
static uint32_t lcd_pixel_hz=CHART_LCD_SPI_HZ;
#ifndef CHART_LCD_HOST_TEST
static i2c_master_dev_handle_t imu;
static QueueHandle_t signal_queue;
static uint8_t locked_bssid[6];
static atomic_bool listening;
static atomic_uint dropped;
static bool wifi_initialized;
#endif

static struct {
    bool open,last,failed;
    int width;
    unsigned carry; /* Bits displaced out of the previous block in this window. */
    size_t remaining;
    unsigned pending,next;
    spi_transaction_t tx[2];
    chart_lcd_timing_t timing;
    int64_t first_submit;
} lcd_window;

#define TRY(call) do { esp_err_t e=(call); if (e!=ESP_OK) return e; } while (0)

static esp_err_t lcd_send(int dc,const void *data,size_t length)
{
    if (!length) return ESP_OK;
    gpio_set_level(CHART_LCD_DC,dc);
    /* All initialization and window commands have at most four parameter bytes. */
    spi_transaction_t t={.length=length*8,.flags=SPI_TRANS_USE_TXDATA,
        .override_freq_hz=CHART_LCD_SPI_HZ};
    memcpy(t.tx_data,data,length);
    return spi_device_polling_transmit(lcd,&t);
}

static esp_err_t lcd_command(uint8_t command,const uint8_t *data,size_t n)
{
    TRY(lcd_send(0,&command,1));
    return lcd_send(1,data,n);
}

esp_err_t chart_lcd_init(void)
{
    ESP_LOGI("lcd","SPI MOSI=%d SCK=%d CS=%d DC=%d Hz=%d",
        CHART_LCD_MOSI,CHART_LCD_SCK,CHART_LCD_CS,CHART_LCD_DC,CHART_LCD_SPI_HZ);
    if (CHART_LCD_BL<0)
        ESP_LOGW("lcd","BL is not driven; backlight circuit and polarity must be verified externally");
    gpio_config_t io={.pin_bit_mask=1ULL<<CHART_LCD_DC,.mode=GPIO_MODE_OUTPUT};
    if (CHART_LCD_BL>=0) io.pin_bit_mask|=1ULL<<(CHART_LCD_BL & 63);
    TRY(gpio_config(&io));
    if (CHART_LCD_BL>=0) gpio_set_level(CHART_LCD_BL,!CHART_LCD_BL_ACTIVE);
    spi_bus_config_t bus={.mosi_io_num=CHART_LCD_MOSI,.miso_io_num=-1,
        .sclk_io_num=CHART_LCD_SCK,.quadwp_io_num=-1,.quadhd_io_num=-1,
        .max_transfer_sz=320*CHART_TILE_ROWS*2};
    TRY(spi_bus_initialize(SPI2_HOST,&bus,SPI_DMA_CH_AUTO));
    spi_device_interface_config_t device={
        .clock_speed_hz=CHART_LCD_SPI_HZ,
        .mode=0,.spics_io_num=CHART_LCD_CS,.queue_size=2};
    TRY(spi_bus_add_device(SPI2_HOST,&device,&lcd));
    TRY(spi_bus_get_max_transaction_len(SPI2_HOST,&lcd_dma_max));
    lcd_dma_max&=~(size_t)3; /* Retain the validated limit; individual TX lengths may be odd words. */
    /* Conservative ST7796 DCS bring-up. Vendor power/gamma table is not verified. */
    TRY(lcd_command(0x01,NULL,0));
    int actual_khz=0;
    TRY(spi_device_get_actual_freq(lcd,&actual_khz));
    ESP_LOGI("lcd","actual initialization/command SPI clock: %d kHz",actual_khz);
    vTaskDelay(pdMS_TO_TICKS(150));
    TRY(lcd_command(0x11,NULL,0)); vTaskDelay(pdMS_TO_TICKS(120));
    uint8_t format=0x55;
    TRY(lcd_command(0x3a,&format,1));
    uint8_t orientation=CHART_LCD_ROTATION==90 ? 0x60 :
        CHART_LCD_ROTATION==180 ? 0xc0 : CHART_LCD_ROTATION==270 ? 0xa0 : 0x00;
    if (CHART_LCD_MIRROR_X) orientation^=(orientation & 0x20) ? 0x80 : 0x40;
    if (CHART_LCD_BGR) orientation|=0x08;
    TRY(lcd_command(0x36,&orientation,1));
    TRY(lcd_command(CHART_LCD_INVERT ? 0x21 : 0x20,NULL,0));
    TRY(lcd_command(0x29,NULL,0)); vTaskDelay(pdMS_TO_TICKS(20));
    if (CHART_LCD_BL>=0) gpio_set_level(CHART_LCD_BL,CHART_LCD_BL_ACTIVE);
    ESP_LOGI("lcd","pixel transfer: rectangular DMA queue=2; region=%d bytes; transaction limit=%u bytes; full-width block=%u rows",
        CHART_LCD_BUFFER_BYTES,(unsigned)lcd_dma_max,(unsigned)(lcd_dma_max/(320*2)));
    /* SPI2 reaches the IOMUX only when every pin matches; one mismatch routes
     * the whole bus (including SCK) through the GPIO matrix and adds skew. */
    ESP_LOGW("lcd","SPI2 IOMUX pins are MOSI=%d SCK=%d CS=%d; configured MOSI=%d SCK=%d CS=%d -> %s",
        SPI2_IOMUX_PIN_NUM_MOSI,SPI2_IOMUX_PIN_NUM_CLK,SPI2_IOMUX_PIN_NUM_CS,
        CHART_LCD_MOSI,CHART_LCD_SCK,CHART_LCD_CS,
        (CHART_LCD_MOSI==SPI2_IOMUX_PIN_NUM_MOSI && CHART_LCD_SCK==SPI2_IOMUX_PIN_NUM_CLK)
            ? "IOMUX" : "GPIO matrix (extra skew at 80 MHz)");
    ESP_LOGW("lcd","ST7796 DCS candidate; window offset %d,%d BGR=%d rotation=%d mirror_x=%d MADCTL=0x%02x: verify on module",
        CHART_LCD_X_OFFSET,CHART_LCD_Y_OFFSET,CHART_LCD_BGR,CHART_LCD_ROTATION,
        CHART_LCD_MIRROR_X,orientation);
    return ESP_OK;
}

void chart_lcd_fast_pixels(bool enabled)
{
    if (lcd_window.open || lcd_window.failed) return;
    lcd_pixel_hz=enabled ? CHART_LCD_FAST_SPI_HZ : CHART_LCD_SPI_HZ;
}

esp_err_t chart_lcd_window_begin(int x,int y,int width,int rows)
{
    if (lcd_window.open || lcd_window.failed) return ESP_ERR_INVALID_STATE;
    if (x<0 || y<0 || x>=320 || y>=320 || width<=0 || rows<=0 ||
        width>320-x || rows>320-y) return ESP_ERR_INVALID_ARG;
    esp_err_t err=spi_device_acquire_bus(lcd,portMAX_DELAY);
    if (err!=ESP_OK) { lcd_window.failed=true; return err; }
    memset(&lcd_window,0,sizeof(lcd_window));
    lcd_window.open=true;
    lcd_window.width=width;
    lcd_window.remaining=(size_t)width*rows*2;
    int x1=CHART_LCD_X_OFFSET+x,x2=x1+width-1,y1=CHART_LCD_Y_OFFSET+y,y2=y1+rows-1;
    uint8_t xs[]={x1>>8,x1&255,x2>>8,x2&255},ys[]={y1>>8,y1&255,y2>>8,y2&255};
    int64_t started=esp_timer_get_time();
    err=lcd_command(0x2a,xs,sizeof(xs));
    if (err==ESP_OK) err=lcd_command(0x2b,ys,sizeof(ys));
    if (err==ESP_OK) err=lcd_command(0x2c,NULL,0);
    if (err==ESP_OK) err=spi_device_get_actual_freq(lcd,&lcd_window.timing.command_khz);
    lcd_window.timing.command_us=esp_timer_get_time()-started;
    gpio_set_level(CHART_LCD_DC,1);
    if (err!=ESP_OK) {
        lcd_window.failed=true;
        lcd_window.open=false;
        spi_device_release_bus(lcd);
    }
    return err;
}

esp_err_t chart_lcd_window_submit(uint16_t *pixels,int rows,bool last)
{
    if (!lcd_window.open || lcd_window.failed || lcd_window.last || lcd_window.pending==2)
        return ESP_ERR_INVALID_STATE;
    if (!pixels || rows<=0 || rows>CHART_LCD_PIPE_ROWS) return ESP_ERR_INVALID_ARG;
    size_t length=(size_t)lcd_window.width*rows*2;
    /* Compensation appends a carry byte, so the wire is one byte longer than
     * the pixels. Reject a block that would not fit rather than silently
     * sending it uncompensated, which would corrupt every colour. */
    bool compensate=CHART_LCD_SHIFT_BITS && lcd_pixel_hz!=CHART_LCD_SPI_HZ;
    size_t extra=compensate && last ? 1 : 0; /* Only the final block carries it. */
    if (length+extra>lcd_dma_max || length+extra>CHART_LCD_BUFFER_BYTES ||
        length>lcd_window.remaining || last!=(length==lcd_window.remaining))
        return ESP_ERR_INVALID_ARG;
    for (unsigned i=0;i<lcd_window.pending;++i) {
        const spi_transaction_t *t=&lcd_window.tx[(lcd_window.next+2-lcd_window.pending+i)%2];
        uintptr_t a=(uintptr_t)pixels,b=(uintptr_t)t->tx_buffer;
        if (a<b+t->length/8 && b<a+length) return ESP_ERR_INVALID_STATE;
    }
    int64_t started=esp_timer_get_time();
    lcd_swap_optimized(pixels,length/2);
    /* One dummy bit per window, not per block: thread the displaced bits into
     * the next block so the panel sees a single continuous stream, and let only
     * the last block emit the trailing byte. Window bookkeeping keeps using the
     * true pixel length, not the wire one. */
    if (compensate)
        lcd_window.carry=lcd_shift_stream((uint8_t *)pixels,length,
                                          CHART_LCD_SHIFT_BITS,lcd_window.carry,last);
    size_t wire=length+extra;
    lcd_window.timing.swap_us+=esp_timer_get_time()-started;
    spi_transaction_t *t=&lcd_window.tx[lcd_window.next];
    *t=(spi_transaction_t){.length=wire*8,.tx_buffer=pixels,.override_freq_hz=lcd_pixel_hz,
        .flags=SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL | (last ? 0 : SPI_TRANS_CS_KEEP_ACTIVE)};
    if (!lcd_window.first_submit) lcd_window.first_submit=esp_timer_get_time();
    esp_err_t err=spi_device_queue_trans(lcd,t,portMAX_DELAY);
    if (err!=ESP_OK) { lcd_window.failed=true; return err; }
    lcd_window.next=(lcd_window.next+1)%2;
    ++lcd_window.pending;
    lcd_window.remaining-=length;
    lcd_window.last=last;
    return ESP_OK;
}

esp_err_t chart_lcd_window_wait(uint16_t **completed_pixels)
{
    if (completed_pixels) *completed_pixels=NULL;
    if (!lcd_window.open || !lcd_window.pending) return ESP_ERR_INVALID_STATE;
    spi_transaction_t *returned=NULL;
    spi_transaction_t *expected=&lcd_window.tx[(lcd_window.next+2-lcd_window.pending)%2];
    int64_t started=esp_timer_get_time();
    esp_err_t err=spi_device_get_trans_result(lcd,&returned,portMAX_DELAY);
    lcd_window.timing.wait_us+=esp_timer_get_time()-started;
    if (returned==expected) {
        --lcd_window.pending;
        if (completed_pixels) *completed_pixels=(uint16_t *)returned->tx_buffer;
    } else err=ESP_ERR_INVALID_STATE;
    if (err!=ESP_OK) lcd_window.failed=true;
    return err;
}

esp_err_t chart_lcd_window_end(chart_lcd_timing_t *timing)
{
    if (!lcd_window.open) return ESP_ERR_INVALID_STATE;
    esp_err_t err=lcd_window.failed || !lcd_window.last ? ESP_ERR_INVALID_STATE : ESP_OK;
    while (lcd_window.pending) {
        unsigned before=lcd_window.pending;
        esp_err_t result=chart_lcd_window_wait(NULL);
        if (result!=ESP_OK) err=result;
        if (before==lcd_window.pending) return result; /* Ownership is still unknown; retain bus and pixels. */
    }
    if (lcd_window.first_submit)
        lcd_window.timing.dma_us=esp_timer_get_time()-lcd_window.first_submit;
    if (err==ESP_OK) err=spi_device_get_actual_freq(lcd,&lcd_window.timing.pixel_khz);
    spi_device_release_bus(lcd);
    lcd_window.open=false;
    lcd_window.failed=err!=ESP_OK;
    if (timing) *timing=lcd_window.timing;
    return err;
}

#ifndef CHART_LCD_HOST_TEST
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

#endif
#endif
