#define CHART_LCD_HOST_TEST
#include "../src/chart_hardware.c"
#include "../src/main.c"
#include <assert.h>

static int64_t now_us;
static int button_level=1,dc,actual_hz;
static bool acquired,cs_active;
static unsigned command_count,transaction_count,queued_count,fail_queue_at,fail_result_at,results,result_count;
static unsigned fail_transmit_at,fail_poll_at,poll_count;
static uint8_t commands[16],payload[320*128*2];
static size_t payload_size;
static struct {
    spi_transaction_t *original,copy;
    uint8_t bytes[320*32*2];
} queue[2];
static uint16_t pixels[320*64],expected[320*128];

int64_t esp_timer_get_time(void) { return ++now_us; }
int gpio_get_level(int pin) { assert(pin==CHART_LCD_BUTTON); return button_level; }
int gpio_set_level(int pin,int value) { assert(pin==CHART_LCD_DC); dc=value; return 0; }
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
esp_err_t gpio_set_drive_capability(int pin,gpio_drive_cap_t capability)
{
    assert(pin==CHART_LCD_MOSI || pin==CHART_LCD_SCK);
    assert(capability>=0 && capability<=3);
    return ESP_OK;
}
void vTaskDelay(int ticks) { now_us+=ticks*1000; }
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t *config,int dma)
{
    assert(host==SPI2_HOST && dma==SPI_DMA_CH_AUTO && config->max_transfer_sz==10240);
    return ESP_OK;
}
esp_err_t spi_bus_add_device(int host,const spi_device_interface_config_t *config,spi_device_handle_t *handle)
{
    assert(host==SPI2_HOST && config->queue_size==2);
    actual_hz=config->clock_speed_hz; *handle=(void *)1; return ESP_OK;
}
esp_err_t spi_bus_get_max_transaction_len(int host,size_t *limit)
{
    assert(host==SPI2_HOST); *limit=12279; return ESP_OK;
}
esp_err_t spi_device_get_actual_freq(spi_device_handle_t handle,int *khz)
{
    assert(handle); *khz=actual_hz/1000; return ESP_OK;
}
esp_err_t spi_device_acquire_bus(spi_device_handle_t handle,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && !acquired && !queued_count);
    acquired=true; return ESP_OK;
}
void spi_device_release_bus(spi_device_handle_t handle)
{
    assert(handle && acquired && !queued_count); acquired=false;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t *t)
{
    assert(handle && !queued_count && (t->flags&SPI_TRANS_USE_TXDATA));
    if (++poll_count==fail_poll_at) return ESP_ERR_INVALID_STATE;
    assert(t->override_freq_hz==CHART_LCD_SPI_HZ);
    actual_hz=t->override_freq_hz;
    if (!dc) { assert(command_count<sizeof(commands)); commands[command_count++]=t->tx_data[0]; }
    now_us+=5; return ESP_OK;
}
static void transfer(const spi_transaction_t *t)
{
    assert(dc==1 && t->override_freq_hz==CHART_LCD_FAST_SPI_HZ);
    assert(t->length%32==0 && t->length/8<=12276);
    size_t n=t->length/8;
    assert(payload_size+n<=sizeof(payload));
    memcpy(payload+payload_size,t->tx_buffer,n); payload_size+=n;
    if (t->flags&SPI_TRANS_CS_KEEP_ACTIVE) assert(acquired);
    cs_active=(t->flags&SPI_TRANS_CS_KEEP_ACTIVE)!=0;
    /* Baseline drops CS at each half; a window only at the final byte. */
    if (lcd_window.open) assert(cs_active==(payload_size<sizeof(payload)));
    actual_hz=t->override_freq_hz; ++transaction_count; now_us+=n/10;
}
esp_err_t spi_device_transmit(spi_device_handle_t handle,spi_transaction_t *t)
{
    assert(handle && !queued_count);
    if (transaction_count+1==fail_transmit_at) return ESP_ERR_INVALID_STATE;
    transfer(t); return ESP_OK;
}
esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t *t,int ticks)
{
    assert(handle && acquired && ticks==portMAX_DELAY && queued_count<2);
    if (++results==fail_queue_at) return ESP_ERR_NO_MEM;
    for (unsigned i=0;i<queued_count;++i) {
        assert(queue[i].original!=t);
        assert(queue[i].copy.tx_buffer!=t->tx_buffer);
    }
    queue[queued_count].original=t; queue[queued_count].copy=*t;
    assert(t->length/8<=sizeof(queue[0].bytes));
    memcpy(queue[queued_count].bytes,t->tx_buffer,t->length/8);
    ++queued_count; return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,spi_transaction_t **t,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && queued_count);
    assert(memcmp(queue[0].original,&queue[0].copy,sizeof(spi_transaction_t))==0);
    assert(memcmp(queue[0].copy.tx_buffer,queue[0].bytes,queue[0].copy.length/8)==0);
    transfer(&queue[0].copy); *t=queue[0].original;
    queue[0]=queue[1]; --queued_count;
    return ++result_count==fail_result_at ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static void reset(void)
{
    assert(!queued_count && !acquired);
    memset(&lcd_window,0,sizeof(lcd_window));
    command_count=transaction_count=queued_count=results=result_count=0;
    fail_queue_at=fail_result_at=fail_transmit_at=fail_poll_at=poll_count=0;
    payload_size=0; cs_active=false; actual_hz=CHART_LCD_FAST_SPI_HZ;
}

static esp_err_t run_frame(unsigned s,int64_t elapsed,unsigned frame,int64_t *draw,int64_t *spi,
                           chart_lcd_timing_t *timing)
{
    return refresh_strategies[s].o2 ?
        refresh_frame_o2(&refresh_strategies[s],pixels,elapsed,frame,draw,spi,timing) :
        refresh_frame(&refresh_strategies[s],pixels,elapsed,frame,draw,spi,timing);
}

static void frames(void)
{
    for (unsigned s=0;s<sizeof(refresh_strategies)/sizeof(*refresh_strategies);++s)
        for (unsigned frame=0;frame<2;++frame)
            for (int bar=0;bar<320;++bar) {
                reset();
                int64_t draw=0,spi=0;
                chart_lcd_timing_t t={0};
                chart_render_refresh_band_wire(96,128,expected,bar*6250LL,frame);
                assert(run_frame(s,bar*6250LL,frame,&draw,&spi,&t)==ESP_OK);
                assert(payload_size==sizeof(payload) && !cs_active && !acquired && !queued_count);
                assert(memcmp(payload,expected,sizeof(payload))==0);
                assert(transaction_count==8 && command_count==(refresh_strategies[s].window ? 3 : 6));
                for (unsigned i=0;i<command_count;i+=3)
                    assert(commands[i]==0x2a && commands[i+1]==0x2b && commands[i+2]==0x2c);
                assert(t.command_khz==40000 && t.pixel_khz==80000 && draw>0 && spi>0);
            }
    puts("PASS: adapted demo positions/markers match; queried limit, CS and DMA ownership");
}

static void failures(void)
{
    for (unsigned fail=1;fail<=5;++fail) {
        reset(); fail_poll_at=fail;
        int64_t draw=0,spi=0; chart_lcd_timing_t t={0};
        assert(run_frame(3,0,0,&draw,&spi,&t)==ESP_ERR_INVALID_STATE);
        assert(!queued_count && !acquired && !lcd_window.open && lcd_window.failed);
    }
    for (unsigned fail=1;fail<=8;++fail) {
        reset(); fail_queue_at=fail;
        int64_t draw=0,spi=0; chart_lcd_timing_t t={0};
        assert(run_frame(3,0,0,&draw,&spi,&t)==ESP_ERR_NO_MEM);
        assert(!queued_count && !acquired && !lcd_window.open && lcd_window.failed);
    }
    for (unsigned s=0;s<2;++s) for (unsigned fail=1;fail<=5;++fail) {
        reset(); fail_poll_at=fail;
        int64_t draw=0,spi=0; chart_lcd_timing_t t={0};
        assert(run_frame(s,0,0,&draw,&spi,&t)==ESP_ERR_INVALID_STATE);
        assert(!queued_count && !acquired && !lcd_window.open && lcd_window.failed);
    }
    for (unsigned s=0;s<2;++s) for (unsigned fail=1;fail<=8;++fail) {
        reset(); fail_queue_at=fail;
        int64_t draw=0,spi=0; chart_lcd_timing_t t={0};
        assert(run_frame(s,0,0,&draw,&spi,&t)==ESP_ERR_NO_MEM);
        assert(!queued_count && !acquired && !lcd_window.open && lcd_window.failed);
        assert(chart_lcd_window_begin(0,96,320,128)==ESP_ERR_INVALID_STATE);
    }
    for (unsigned s=0;s<2;++s) for (unsigned fail=1;fail<=8;++fail) {
        reset(); fail_result_at=fail;
        int64_t draw=0,spi=0; chart_lcd_timing_t t={0};
        assert(run_frame(s,0,0,&draw,&spi,&t)==ESP_ERR_INVALID_STATE);
        assert(!queued_count && !acquired && !lcd_window.open && lcd_window.failed);
    }
    reset();
    assert(chart_lcd_window_begin(0,96,320,128)==ESP_OK);
    assert(chart_lcd_tile(96,32,pixels)==ESP_ERR_INVALID_STATE);
    assert(chart_lcd_window_submit(pixels,32,true)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,64,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,16,false)==ESP_OK);
    assert(chart_lcd_window_submit(pixels,16,false)==ESP_ERR_INVALID_STATE);
    assert(chart_lcd_window_end(&((chart_lcd_timing_t){0}))==ESP_ERR_INVALID_STATE);
    assert(!queued_count && !acquired);
    puts("PASS: rejected submissions preserve ownership; failed partial windows drain and stop");
}

static void button(void)
{
    button_level=1; assert(!refresh_button_pressed(0));
    button_level=0; assert(!refresh_button_pressed(1000));
    button_level=1; assert(!refresh_button_pressed(2000));
    button_level=0; assert(!refresh_button_pressed(3000));
    assert(!refresh_button_pressed(32999)); assert(refresh_button_pressed(33000));
    assert(!refresh_button_pressed(2000000));
    button_level=1; assert(!refresh_button_pressed(2001000));
    assert(!refresh_button_pressed(2031000));
    button_level=0; assert(!refresh_button_pressed(2040000));
    assert(refresh_button_pressed(2070000));
    unsigned index=0;
    const unsigned order[]={15,10,13,14};
    assert(refresh_strategies[0].id==14 && !refresh_strategies[0].o2);
    assert(refresh_strategies[1].id==15 && refresh_strategies[1].o2);
    for (unsigned i=0;i<8;++i) {
        int64_t at=3000000+(int64_t)i*200000;
        button_level=1; assert(!refresh_button_pressed(at));
        assert(!refresh_button_pressed(at+30000));
        button_level=0; assert(!refresh_button_pressed(at+60000));
        if (refresh_button_pressed(at+90000)) index=(index+1)%4;
        assert(refresh_strategies[index].id==order[i%4]);
    }
    puts("PASS: BOOT bounce, 30 ms threshold, long hold and release/rearm");
}

int main(void)
{
    /* Exercise both swap helpers in this translation unit as well. */
    uint16_t word=0x1234; lcd_swap_baseline(&word,1); lcd_swap_optimized(&word,1); assert(word==0x1234);
    assert(chart_lcd_init()==ESP_OK && lcd_dma_max==12276);
    chart_lcd_fast_pixels(true);
    frames(); failures(); button(); return 0;
}
