#define CHART_LCD_HOST_TEST
#define CHART_LCD_REFRESH_TEST 0
#include "starchart.h"
#undef CHART_LCD_X_OFFSET
#undef CHART_LCD_Y_OFFSET
#define CHART_LCD_X_OFFSET 7
#define CHART_LCD_Y_OFFSET 11
#include "../src/chart_hardware.c"
#include <assert.h>
#include <limits.h>

static uint16_t pixels[CHART_LCD_PIXEL_BYTES/2],reference[320*320];
static uint8_t payload[320*320*2];
static struct {
    spi_transaction_t *original,copy;
    uint8_t bytes[CHART_LCD_BUFFER_BYTES];
} queue[2];
static unsigned pending,polls,submits,completions,transfers,releases;
static unsigned fail_poll,fail_submit,fail_completion;
static bool missing_result,wrong_result,fail_acquire,fail_frequency,acquired,cs;
static size_t payload_size,window_bytes;
static int dc,actual_hz;
static int64_t clock_us;
static uint8_t commands[3],addresses[2][4];
static unsigned command_count;

int64_t esp_timer_get_time(void) { return ++clock_us; }
int gpio_get_level(int pin) { (void)pin; return 1; }
int gpio_set_level(int pin,int value) { assert(pin==CHART_LCD_DC && !pending); dc=value; return 0; }
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
esp_err_t gpio_set_drive_capability(int pin,gpio_drive_cap_t capability)
{
    assert(pin==CHART_LCD_MOSI || pin==CHART_LCD_SCK);
    assert(capability>=0 && capability<=3);
    return ESP_OK;
}
void vTaskDelay(int ticks) { clock_us+=ticks*1000; }
esp_err_t spi_bus_initialize(int host,const spi_bus_config_t *config,int dma)
{
    assert(host==SPI2_HOST && dma==SPI_DMA_CH_AUTO && config->max_transfer_sz==10240);
    return ESP_OK;
}
esp_err_t spi_bus_add_device(int host,const spi_device_interface_config_t *config,spi_device_handle_t *handle)
{
    assert(host==SPI2_HOST && config->queue_size==2 && config->clock_speed_hz==CHART_LCD_SPI_HZ);
    actual_hz=config->clock_speed_hz; *handle=(void *)1; return ESP_OK;
}
esp_err_t spi_bus_get_max_transaction_len(int host,size_t *limit)
{
    assert(host==SPI2_HOST); *limit=12279; return ESP_OK;
}
esp_err_t spi_device_get_actual_freq(spi_device_handle_t handle,int *khz)
{
    assert(handle); if (fail_frequency) return ESP_ERR_INVALID_STATE;
    *khz=actual_hz/1000; return ESP_OK;
}
esp_err_t spi_device_acquire_bus(spi_device_handle_t handle,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && !pending && !acquired);
    if (fail_acquire) return ESP_ERR_INVALID_STATE;
    acquired=true; return ESP_OK;
}
void spi_device_release_bus(spi_device_handle_t handle)
{
    assert(handle && acquired && !pending); acquired=false; ++releases;
}
esp_err_t spi_device_polling_transmit(spi_device_handle_t handle,spi_transaction_t *t)
{
    assert(handle && !pending && (t->flags&SPI_TRANS_USE_TXDATA));
    assert(t->override_freq_hz==CHART_LCD_SPI_HZ);
    if (++polls==fail_poll) return ESP_ERR_INVALID_STATE;
    actual_hz=t->override_freq_hz;
    if (lcd_window.open) {
        assert(acquired && !cs);
        if (!dc) { assert(command_count<3); commands[command_count++]=t->tx_data[0]; }
        else { assert(command_count==1 || command_count==2); memcpy(addresses[command_count-1],t->tx_data,4); }
    }
    return ESP_OK;
}
esp_err_t spi_device_transmit(spi_device_handle_t handle,spi_transaction_t *t)
{
    (void)handle; (void)t; assert(false); return ESP_ERR_INVALID_STATE;
}
esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t *t,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && acquired && dc==1 && pending<2);
    assert(t->flags&SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
    assert(t->length%16==0 && t->length/8<=lcd_dma_max);
    assert(t->override_freq_hz==lcd_pixel_hz);
    /* C5 internal TX: no length alignment and no cache sync; no temporary copy. */
    uintptr_t p=(uintptr_t)t->tx_buffer,base=(uintptr_t)pixels;
    if (p<base || p+t->length/8>base+sizeof(pixels) || p%_Alignof(uint16_t))
        return ESP_ERR_INVALID_ARG;
    if (++submits==fail_submit) return ESP_ERR_NO_MEM;
    for (unsigned i=0;i<pending;++i) assert(queue[i].original!=t);
    queue[pending].original=t; queue[pending].copy=*t;
    memcpy(queue[pending].bytes,t->tx_buffer,t->length/8);
    ++pending; return ESP_OK;
}
esp_err_t spi_device_get_trans_result(spi_device_handle_t handle,spi_transaction_t **t,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && pending);
    assert(memcmp(queue[0].original,&queue[0].copy,sizeof(spi_transaction_t))==0);
    assert(memcmp(queue[0].copy.tx_buffer,queue[0].bytes,queue[0].copy.length/8)==0);
    ++completions;
    if (missing_result) return ESP_ERR_INVALID_STATE;
    if (wrong_result) { static spi_transaction_t foreign; *t=&foreign; return ESP_OK; }
    size_t n=queue[0].copy.length/8;
    assert(dc==1 && payload_size+n<=window_bytes);
    memcpy(payload+payload_size,queue[0].copy.tx_buffer,n); payload_size+=n;
    cs=(queue[0].copy.flags&SPI_TRANS_CS_KEEP_ACTIVE)!=0;
    assert(cs==(payload_size<window_bytes));
    actual_hz=queue[0].copy.override_freq_hz; ++transfers;
    *t=queue[0].original; queue[0]=queue[1]; --pending;
    return completions==fail_completion ? ESP_ERR_INVALID_STATE : ESP_OK;
}

static void reset(void)
{
    assert(!pending && !acquired);
    memset(&lcd_window,0,sizeof(lcd_window));
    polls=submits=completions=transfers=releases=command_count=0;
    fail_poll=fail_submit=fail_completion=0;
    missing_result=wrong_result=fail_acquire=fail_frequency=cs=false;
    payload_size=window_bytes=0; chart_lcd_fast_pixels(true);
}

static void rectangle(int x,int y,int width,int rows,bool fast)
{
    reset(); chart_lcd_fast_pixels(fast);
    window_bytes=(size_t)width*rows*2;
    for (int i=0;i<width*rows;++i) reference[i]=(uint16_t)(i*7919+0x1234);
    assert(chart_lcd_window_begin(x,y,width,rows)==ESP_OK);
    assert(command_count==3 && commands[0]==0x2a && commands[1]==0x2b && commands[2]==0x2c);
    int x1=x+CHART_LCD_X_OFFSET,y1=y+CHART_LCD_Y_OFFSET;
    uint8_t xs[]={x1>>8,x1&255,(x1+width-1)>>8,(x1+width-1)&255};
    uint8_t ys[]={y1>>8,y1&255,(y1+rows-1)>>8,(y1+rows-1)&255};
    assert(!memcmp(xs,addresses[0],4) && !memcmp(ys,addresses[1],4));
    chart_lcd_fast_pixels(!fast); assert(lcd_pixel_hz==(fast ? CHART_LCD_FAST_SPI_HZ : CHART_LCD_SPI_HZ));
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE);
    assert(chart_lcd_tile(0,1,pixels)==ESP_ERR_INVALID_STATE);
    unsigned part=0;
    int chunk=(int)(lcd_dma_max/(width*2));
    if (chunk>CHART_LCD_PIPE_ROWS) chunk=CHART_LCD_PIPE_ROWS;
    for (int row=0;row<rows;++part) {
        uint16_t *buffer=pixels+(part%2)*CHART_LCD_BUFFER_PIXELS;
        if (part>=2) {
            uint16_t *completed=NULL;
            assert(chart_lcd_window_wait(&completed)==ESP_OK && completed==buffer);
        }
        int n=rows-row<chunk ? rows-row : chunk;
        for (int i=0;i<CHART_LCD_BUFFER_PIXELS;++i) buffer[i]=0xa55a;
        memcpy(buffer,reference+row*width,(size_t)width*n*2);
        assert(chart_lcd_window_submit(buffer,n,row+n==rows)==ESP_OK);
        for (int i=0;i<width*n;++i) {
            uint16_t word=reference[row*width+i];
            assert(buffer[i]==(uint16_t)((word<<8)|(word>>8)));
        }
        for (int i=width*n;i<CHART_LCD_BUFFER_PIXELS;++i) assert(buffer[i]==0xa55a);
        row+=n;
    }
    chart_lcd_timing_t timing={0};
    assert(chart_lcd_window_end(&timing)==ESP_OK);
    assert(!acquired && !pending && !cs && releases==1 && payload_size==window_bytes);
    assert(timing.swap_us>0 && timing.command_us>0 && timing.dma_us>0 && timing.wait_us>0);
    assert(timing.command_khz==40000 && timing.pixel_khz==(fast ? 80000 : 40000));
    for (int i=0;i<width*rows;++i) {
        assert(payload[i*2]==reference[i]>>8 && payload[i*2+1]==(reference[i]&255));
    }
    /* The next window must be usable without resetting the driver. */
    payload_size=0; window_bytes=2; command_count=0;
    assert(chart_lcd_window_begin(319,319,1,1)==ESP_OK);
    pixels[0]=0xf800;
    assert(chart_lcd_window_submit(pixels,1,true)==ESP_OK && chart_lcd_window_end(NULL)==ESP_OK);
    assert(payload[0]==0xf8 && payload[1]==0);
}

static void invalid_inputs(void)
{
    const int cases[][4]={{-1,0,1,1},{0,-1,1,1},{320,0,1,1},{0,320,1,1},
        {0,0,0,1},{0,0,1,0},{319,0,2,1},{0,319,1,2},{0,0,INT_MAX,1},
        {0,0,1,INT_MAX},{INT_MIN,0,1,1},{0,INT_MIN,1,1},{0,0,-1,1},{0,0,1,-1}};
    reset();
    for (unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i)
        assert(chart_lcd_window_begin(cases[i][0],cases[i][1],cases[i][2],cases[i][3])==ESP_ERR_INVALID_ARG);
    assert(!polls && !acquired && !lcd_window.failed);
    uint16_t *completed=pixels;
    assert(chart_lcd_window_wait(&completed)==ESP_ERR_INVALID_STATE && !completed);
    assert(chart_lcd_window_begin(0,0,320,64)==ESP_OK); window_bytes=320*64*2;
    pixels[0]=0x1234;
    assert(chart_lcd_window_submit(NULL,1,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,0,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,INT_MAX,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,33,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,32,false)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,16,true)==ESP_ERR_INVALID_ARG && pixels[0]==0x1234);
    assert(chart_lcd_window_submit(pixels,16,false)==ESP_OK);
    assert(chart_lcd_window_submit(pixels+1,16,false)==ESP_ERR_INVALID_STATE);
    assert(chart_lcd_window_submit(pixels+CHART_LCD_BUFFER_PIXELS,16,false)==ESP_OK);
    assert(chart_lcd_window_submit(pixels,16,false)==ESP_ERR_INVALID_STATE);
    assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && !pending && !acquired);
}

static void failures(void)
{
    for (unsigned i=1;i<=5;++i) {
        reset(); fail_poll=i;
        assert(chart_lcd_window_begin(0,0,3,3)==ESP_ERR_INVALID_STATE);
        assert(!acquired && !pending && lcd_window.failed);
        assert(chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE);
    }
    reset(); fail_acquire=true;
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE && lcd_window.failed && !acquired);
    reset(); fail_frequency=true;
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE && !acquired);
    for (unsigned i=1;i<=2;++i) {
        reset(); window_bytes=6; fail_submit=i;
        assert(chart_lcd_window_begin(0,0,1,3)==ESP_OK);
        pixels[0]=0x1234;
        esp_err_t err=chart_lcd_window_submit(pixels,1,false);
        if (i==2) { assert(err==ESP_OK); pixels[CHART_LCD_BUFFER_PIXELS]=0x4321;
            err=chart_lcd_window_submit(pixels+CHART_LCD_BUFFER_PIXELS,2,true); }
        assert(err==ESP_ERR_NO_MEM);
        assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && !pending && !acquired);
        assert(pixels[i==1 ? 0 : CHART_LCD_BUFFER_PIXELS]==(i==1 ? 0x3412 : 0x2143));
        assert(lcd_window.failed && transfers==i-1);
    }
    for (unsigned kind=0;kind<3;++kind) {
        reset(); window_bytes=4;
        assert(chart_lcd_window_begin(0,0,1,2)==ESP_OK);
        assert(chart_lcd_window_submit(pixels,1,false)==ESP_OK);
        assert(chart_lcd_window_submit(pixels+CHART_LCD_BUFFER_PIXELS,1,true)==ESP_OK);
        missing_result=kind==0; wrong_result=kind==1; fail_completion=kind==2 ? 1 : 0;
        uint16_t *completed=pixels;
        assert(chart_lcd_window_wait(&completed)==ESP_ERR_INVALID_STATE);
        if (kind<2) {
            assert(!completed && pending==2 && lcd_window.pending==2);
            assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && acquired && !releases);
            assert(chart_lcd_window_submit(pixels,1,false)==ESP_ERR_INVALID_STATE);
            missing_result=wrong_result=false; /* Test-only repair to return retained descriptors. */
        } else assert(completed==pixels && pending==1);
        assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && !pending && !acquired);
        assert(lcd_window.failed && chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE);
    }
    reset(); window_bytes=2;
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_OK);
    uint16_t external=0x1234;
    assert(chart_lcd_window_submit(&external,1,true)==ESP_ERR_INVALID_ARG);
    assert(external==0x3412 && chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE);
    reset(); window_bytes=2;
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_OK);
    assert(chart_lcd_window_submit(pixels,2,true)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,1,true)==ESP_OK);
    assert(chart_lcd_window_submit(pixels+CHART_LCD_BUFFER_PIXELS,1,true)==ESP_ERR_INVALID_STATE);
    fail_frequency=true;
    assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && !pending && !acquired && lcd_window.failed);
}

int main(void)
{
    assert(sizeof(pixels)==40960 && (uint8_t *)(pixels+CHART_LCD_BUFFER_PIXELS)-(uint8_t *)pixels==20480);
    assert(chart_lcd_init()==ESP_OK && lcd_dma_max==12276);
    for (int width=1;width<=320;++width) rectangle(320-width,0,width,33,true);
    rectangle(0,0,320,320,true);
    rectangle(319,319,1,1,true);
    rectangle(0,0,1,320,false);
    rectangle(157,273,163,47,false);
    puts("PASS: production rectangles without demo flag; all widths, odd rows, 1x1, offsets, tight bytes, fixed regions, CS/DC and 40/80 MHz");
    invalid_inputs(); failures();
    puts("PASS: rectangle validation, descriptor ownership, single swap, command/queue/result faults, unknown completion retains bus and buffers");
    return 0;
}
