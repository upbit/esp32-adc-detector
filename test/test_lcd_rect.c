#define CHART_LCD_HOST_TEST
#include "starchart.h"
#undef CHART_LCD_X_OFFSET
#undef CHART_LCD_Y_OFFSET
#define CHART_LCD_X_OFFSET 7
#define CHART_LCD_Y_OFFSET 11
#include "../src/chart_hardware.c"
#include "../src/main.c"
#include <assert.h>
#include <limits.h>

static uint16_t pixels[CHART_LCD_PIXEL_BYTES/2],reference[320*320];
static uint8_t payload[320*320*2];
static struct {
    spi_transaction_t *original,copy;
    uint8_t bytes[CHART_LCD_BUFFER_BYTES];
    int64_t end_us;
} queue[2];
static unsigned pending,polls,submits,completions,transfers,releases;
static unsigned fail_poll,fail_submit,fail_completion;
static bool missing_result,wrong_result,fail_acquire,fail_frequency,acquired,cs;
static size_t payload_size,window_bytes,wire_size;
static uint8_t wire[320*320*2+8];
static int dc,actual_hz;
static int64_t clock_us;
static uint8_t commands[3],addresses[2][4];
static unsigned command_count;
static bool virtual_mode;
static uint16_t screen[320*320],full_frame[320*320];
static unsigned idle_ticks,total_windows;
static unsigned timers_in_transit;
static int window_x,window_y,window_width,window_rows;

int64_t esp_timer_get_time(void)
{
    if (virtual_mode && pending && clock_us<queue[pending-1].end_us) ++timers_in_transit;
    return ++clock_us;
}
int gpio_set_level(int pin,int value) { assert(pin==CHART_LCD_DC && !pending); dc=value; return 0; }
esp_err_t gpio_config(const gpio_config_t *config) { (void)config; return ESP_OK; }
void vTaskDelay(int ticks) { clock_us+=ticks*1000; idle_ticks+=(unsigned)ticks; }
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
        if (virtual_mode && !dc && t->tx_data[0]==0x2a) {
            command_count=0; payload_size=0; wire_size=0; ++total_windows;
        }
        if (!dc) { assert(command_count<3); commands[command_count++]=t->tx_data[0]; }
        else { assert(command_count==1 || command_count==2); memcpy(addresses[command_count-1],t->tx_data,4); }
        if (virtual_mode && !dc && t->tx_data[0]==0x2c) {
            window_x=((addresses[0][0]<<8)|addresses[0][1])-CHART_LCD_X_OFFSET;
            window_y=((addresses[1][0]<<8)|addresses[1][1])-CHART_LCD_Y_OFFSET;
            window_width=((addresses[0][2]<<8)|addresses[0][3])-CHART_LCD_X_OFFSET-window_x+1;
            window_rows=((addresses[1][2]<<8)|addresses[1][3])-CHART_LCD_Y_OFFSET-window_y+1;
            window_bytes=(size_t)window_width*window_rows*2;
        }
    }
    return ESP_OK;
}
/* The firmware prepends CHART_LCD_SHIFT_BITS dummy bits at the fast clock to
 * cancel a panel that latches late, which makes the wire one byte longer. */
static bool actual_fast(void) { return lcd_pixel_hz!=CHART_LCD_SPI_HZ; }

static size_t shift_bytes(const spi_transaction_t *t)
{
    /* Only the window's final block carries the extra byte, so detect it by
     * the released CS rather than assuming every compensated block has one. */
    return CHART_LCD_SHIFT_BITS && t->override_freq_hz!=CHART_LCD_SPI_HZ &&
           !(t->flags&SPI_TRANS_CS_KEEP_ACTIVE) ? 1 : 0;
}

/* Model the panel: it starts sampling CHART_LCD_SHIFT_BITS bit periods late, so
 * recovering a pixel means reading 16 bits from that offset in the stream. */
static uint16_t panel_word(const uint8_t *bytes,size_t n,size_t index,unsigned late)
{
    uint16_t v=0;
    for (unsigned b=0;b<16;++b) {
        size_t bit=late+index*16+b;
        unsigned got=bit/8<n ? (bytes[bit/8]>>(7-bit%8))&1 : 0;
        v=(uint16_t)((v<<1)|got);
    }
    return v;
}

esp_err_t spi_device_queue_trans(spi_device_handle_t handle,spi_transaction_t *t,int ticks)
{
    assert(handle && ticks==portMAX_DELAY && acquired && dc==1 && pending<2);
    assert(t->flags&SPI_TRANS_DMA_BUFFER_ALIGN_MANUAL);
    /* Compensation appends one carry byte, so only the pixel part is whole. */
    assert((t->length/8-shift_bytes(t))%2==0 && t->length/8<=lcd_dma_max);
    assert(t->override_freq_hz==lcd_pixel_hz);
    /* C5 internal TX: no length alignment and no cache sync; no temporary copy. */
    uintptr_t p=(uintptr_t)t->tx_buffer,base=(uintptr_t)pixels;
    if (p<base || p+t->length/8>base+sizeof(pixels) || p%_Alignof(uint16_t))
        return ESP_ERR_INVALID_ARG;
    if (++submits==fail_submit) return ESP_ERR_NO_MEM;
    for (unsigned i=0;i<pending;++i) assert(queue[i].original!=t);
    queue[pending].original=t; queue[pending].copy=*t;
    memcpy(queue[pending].bytes,t->tx_buffer,t->length/8);
    int64_t start=pending && queue[pending-1].end_us>clock_us ? queue[pending-1].end_us : clock_us;
    queue[pending].end_us=start+(int64_t)(t->length/8)/10+1;
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
    size_t extra=shift_bytes(&queue[0].copy),payload_n=n-extra;
    if (virtual_mode && clock_us<queue[0].end_us) clock_us=queue[0].end_us;
    assert(dc==1 && payload_size+payload_n<=window_bytes);
    /* The window is one continuous stream across blocks, so accumulate the
     * wire bytes and decode from the window start, exactly as the panel does. */
    memcpy(wire+wire_size,queue[0].copy.tx_buffer,n); wire_size+=n;
    if (virtual_mode && !(queue[0].copy.flags&SPI_TRANS_CS_KEEP_ACTIVE)) {
        unsigned late=(CHART_LCD_SHIFT_BITS && actual_fast()) ? CHART_LCD_SHIFT_BITS : 0;
        for (size_t index=0;index<window_bytes/2;++index) {
            int x=window_x+(int)(index%window_width),y=window_y+(int)(index/window_width);
            assert(x>=0 && x<320 && y>=0 && y<320);
            screen[y*320+x]=panel_word(wire,wire_size,index,late);
        }
    }
    memcpy(payload+payload_size,queue[0].copy.tx_buffer,payload_n); payload_size+=payload_n;
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
    payload_size=wire_size=window_bytes=0; chart_lcd_fast_pixels(true);
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
    unsigned part=0;
    /* Compensation needs one spare byte in both the region and the queried
     * transaction limit, so block sizing must leave room for it. */
    size_t usable=lcd_dma_max-(CHART_LCD_SHIFT_BITS && fast ? 1 : 0);
    size_t room=CHART_LCD_BUFFER_BYTES-(CHART_LCD_SHIFT_BITS && fast ? 1 : 0);
    if (usable>room) usable=room;
    int chunk=(int)(usable/((size_t)width*2));
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
        /* A single block is not independently decodable once compensation
         * threads bits across the window, so correctness is checked on the
         * reassembled stream after window_end. Here only verify that nothing
         * past the block (plus its optional carry byte) was touched. */
        bool tail=(CHART_LCD_SHIFT_BITS && fast) && row+n==rows;
        for (int i=width*n+(tail ? 1 : 0);i<CHART_LCD_BUFFER_PIXELS;++i)
            assert(buffer[i]==0xa55a);
        row+=n;
    }
    chart_lcd_timing_t timing={0};
    assert(chart_lcd_window_end(&timing)==ESP_OK);
    assert(!acquired && !pending && !cs && releases==1 && payload_size==window_bytes);
    assert(timing.swap_us>0 && timing.command_us>0 && timing.dma_us>0 && timing.wait_us>0);
    assert(timing.command_khz==40000 && timing.pixel_khz==(fast ? 80000 : 40000));
    {   /* Decode the window exactly as the panel does: one stream, one dummy
         * bit for the whole window, and it must reproduce every pixel. */
        unsigned late=(CHART_LCD_SHIFT_BITS && fast) ? CHART_LCD_SHIFT_BITS : 0;
        assert(wire_size==(size_t)width*rows*2+(late ? 1 : 0));
        for (int i=0;i<width*rows;++i)
            assert(panel_word(wire,wire_size,(size_t)i,late)==reference[i]);
    }
    /* The next window must be usable without resetting the driver. */
    payload_size=0; wire_size=0; window_bytes=2; command_count=0;
    assert(chart_lcd_window_begin(319,319,1,1)==ESP_OK);
    pixels[0]=0xf800;
    assert(chart_lcd_window_submit(pixels,1,true)==ESP_OK && chart_lcd_window_end(NULL)==ESP_OK);
    {   /* A single compensated pixel still has to arrive intact. */
        unsigned late=(CHART_LCD_SHIFT_BITS && fast) ? CHART_LCD_SHIFT_BITS : 0;
        assert(panel_word((const uint8_t *)pixels,(size_t)2+(late ? 1 : 0),0,late)==0xf800);
    }
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
    /* Overlapping the in-flight block is refused on range, not pointer equality.
     * Checked at 40 MHz, where no carry byte is appended, so the interior
     * offset stays inside the region. */
    chart_lcd_fast_pixels(false);
    assert(chart_lcd_window_submit(pixels+1,16,false)==ESP_ERR_INVALID_STATE);
    chart_lcd_fast_pixels(true);
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
        /* The rejected buffer is already converted (and compensated); it must
         * not be retried, but the pixel it encodes still has to be the one we
         * handed in -- proving only one conversion happened. */
        {
            const uint8_t *b=(const uint8_t *)(i==1 ? pixels : pixels+CHART_LCD_BUFFER_PIXELS);
            unsigned late=CHART_LCD_SHIFT_BITS;
            assert(panel_word(b,(size_t)2*(i==1 ? 1 : 2)+1,0,late)==(i==1 ? 0x1234 : 0x4321));
        }
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
    /* Outside the DMA regions: converted, then refused by the driver. The
     * second word is the slack the interface requires for the carry byte. */
    uint16_t external[2]={0x1234,0};
    assert(chart_lcd_window_submit(external,1,true)==ESP_ERR_INVALID_ARG);
    assert(external[0]==0x3412 || panel_word((const uint8_t *)external,3,0,
           CHART_LCD_SHIFT_BITS)==0x1234);
    assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE);
    reset(); window_bytes=2;
    assert(chart_lcd_window_begin(0,0,1,1)==ESP_OK);
    assert(chart_lcd_window_submit(pixels,2,true)==ESP_ERR_INVALID_ARG);
    assert(chart_lcd_window_submit(pixels,1,true)==ESP_OK);
    assert(chart_lcd_window_submit(pixels+CHART_LCD_BUFFER_PIXELS,1,true)==ESP_ERR_INVALID_STATE);
    fail_frequency=true;
    assert(chart_lcd_window_end(NULL)==ESP_ERR_INVALID_STATE && !pending && !acquired && lcd_window.failed);
}

static void set_cell(chart_dirty_mask_t *mask,int row,int col)
{
    unsigned bit=(unsigned)(row*CHART_DIRTY_COLS+col);
    mask->bits[bit/8]|=(uint8_t)(1U<<(bit%8));
}

static void plans(void)
{
    chart_dirty_mask_t mask={0};
    set_cell(&mask,2,1); set_cell(&mask,2,3); set_cell(&mask,2,6);
    display_rect_iterator_t plan=display_rect_plan(&mask);
    display_rect_t rect;
    assert(!plan.bounded && display_next_rect(&plan,&rect));
    assert(rect.x==16 && rect.y==32 && rect.width==48 && rect.rows==16);
    assert(display_next_rect(&plan,&rect) && rect.x==96 && rect.width==16);
    assert(!display_next_rect(&plan,&rect));
    memset(&mask,0,sizeof(mask));
    set_cell(&mask,0,0); set_cell(&mask,1,0);
    plan=display_rect_plan(&mask);
    assert(plan.bounded && display_next_rect(&plan,&rect) && rect.width==16 && rect.rows==32);
    memset(mask.bits,255,sizeof(mask.bits));
    plan=display_rect_plan(&mask);
    assert(plan.bounded && display_next_rect(&plan,&rect) && rect.width==320 && rect.rows==320);
    memset(&mask,0,sizeof(mask));
    plan=display_rect_plan(&mask); assert(!display_next_rect(&plan,&rect));
}

static void check_update(displayed_state_t *displayed,chart_model_t *model,int64_t now)
{
    chart_model_t saved=*model;
    chart_render_rect(model,now,0,0,320,320,full_frame);
#if CHART_LCD_SHIFT_DIAGNOSTIC
    /* The reference must carry the same overlays the display path draws. */
    chart_render_diag_ruler(0,0,320,320,full_frame);
    chart_render_diag_swatches(0,0,320,320,full_frame,NULL);
#endif
    display_work_t work;
    assert(display_update(displayed,model,now,pixels,&work)==ESP_OK);
    for (int i=0;i<320*320;++i) {
        if (screen[i]!=full_frame[i]) {
            fprintf(stderr,"dirty mismatch x=%d y=%d now=%lld heading=%f\n",i%320,i/320,(long long)now,model->display_heading);
            assert(screen[i]==full_frame[i]);
        }
    }
    assert(!memcmp(model,&saved,sizeof(saved)) && !pending && !acquired && !cs);
    if (work.windows) {
        assert(displayed->valid && displayed->time==now && !memcmp(&displayed->snapshot,model,sizeof(*model)));
        assert(work.bytes && work.draw_us>0 && work.work_us>0);
    }
}

static void dirty_refresh(void)
{
    reset(); virtual_mode=true; chart_render_init();
    displayed_state_t displayed={0};
    chart_model_t model;
    int64_t now=1000000;
    chart_init(&model,UI_SIM,now);
    check_update(&displayed,&model,now);
    assert(total_windows==1 && transfers==20);
    assert(timers_in_transit>0);
    unsigned before=submits,ticks=idle_ticks;
    check_update(&displayed,&model,now+1000);
    assert(submits==before && idle_ticks==ticks+1);
    for (int i=0;i<CHART_BINS;++i) {
        model.bins[i]=(chart_bin_t){.count=3,.heading=(float)i*10,.rssi=-40-i,.updated_us=now};
    }
    model.coverage=36; model.peak_bin=6; model.last_signal_us=now; model.current_rssi=-61;
    model.target_status=CHART_TARGET_LOCKED;
    for (int i=0;i<CHART_TREND_LENGTH;++i)
        model.trend[i]=(chart_signal_event_t){now-2000000+i*45000,-90+i,true};
    model.trend_used=CHART_TREND_LENGTH; model.trend_next=0;
    check_update(&displayed,&model,now);
    const float angles[]={0,30.37f,60,-179.9f,179.9f,-180,180,720.4f,-541.2f};
    for (unsigned i=0;i<sizeof(angles)/sizeof(*angles);++i) {
        model.heading=model.display_heading=angles[i];
        now+=8333; check_update(&displayed,&model,now);
    }
    model.display_heading=0; model.heading=0;
    for (int i=0;i<200;++i) {
        now+=8333; model.heading+=0.125f; model.display_heading+=0.125f;
        if (i%7==0) {
            int bin=i%CHART_BINS;
            model.bins[bin].count=(i%14) ? 0 : 3;
            model.bins[bin].rssi=-90+(i%51);
            model.bins[bin].heading=bin*10+(i%3)*0.37f;
            model.bins[bin].updated_us=now;
            model.peak_bin=(i%14) ? -1 : bin;
            model.current_rssi=(i%21) ? -7 : -104;
        }
        check_update(&displayed,&model,now);
    }
    /* No packets: scrolling, freshness/color/width transitions and timeout. */
    const int64_t ages[]={CHART_FRESH_US,CHART_FRESH_US+1,3000000,CHART_DARK_US,
        CHART_DARK_US+1,12000000,CHART_HISTORY_US,CHART_HISTORY_US+1,30000000};
    int64_t base=now;
    for (unsigned i=0;i<sizeof(ages)/sizeof(*ages);++i) check_update(&displayed,&model,base+ages[i]);
    now=base+30000000;
    before=submits; ticks=idle_ticks;
    model.samples+=200; model.sync_rejected+=2; model.pose_next=5;
    model.bins[2].count+=100; model.bins[2].values[0]=123; model.recent[0]=22;
    check_update(&displayed,&model,now+10000);
    assert(submits==before && idle_ticks==ticks+1);
    const char *names[]={"HomeOffice_Camera_Backyard_5G","CAM_02","","bad\xffname"};
    for (unsigned i=0;i<sizeof(names)/sizeof(*names);++i) {
        snprintf(model.source,sizeof(model.source),"%s",names[i]); model.frequency_mhz=i ? 0 : 123456;
        check_update(&displayed,&model,++now);
    }
    model.pose_status=CHART_CALIBRATING; check_update(&displayed,&model,++now);
    model.pose_status=CHART_IMU_ERROR; check_update(&displayed,&model,++now);
    model.pose_status=CHART_READY; model.tilt_deg=25; check_update(&displayed,&model,++now);
    model.tilt_deg=0; model.target_status=CHART_WIFI_ERROR; check_update(&displayed,&model,++now);
    model.target_status=CHART_TARGET_MISSING; check_update(&displayed,&model,++now);
    model.target_status=CHART_TARGET_SEARCH; check_update(&displayed,&model,++now);
    model.target_status=CHART_TARGET_LOCKED; model.last_signal_us=now; check_update(&displayed,&model,++now);
    model.coverage=0; model.last_signal_us=0; model.peak_bin=-1; model.trend_used=0;
    memset(model.bins,0,sizeof(model.bins)); check_update(&displayed,&model,++now);
    chart_zero(&model,++now); check_update(&displayed,&model,now);
    chart_init(&model,LIVE_WIFI,++now); check_update(&displayed,&model,now);
    chart_init(&model,IMU_SIM_RSSI,++now); check_update(&displayed,&model,now);
    /* Separate small windows must prepare the next rect while the previous drains. */
    model.heading=60; model.current_rssi=-70; model.last_signal_us=now;
    display_work_t work;
    assert(display_update(&displayed,&model,++now,pixels,&work)==ESP_OK);
    assert(work.windows>1 && work.overlap_draw_us>0);
    chart_render_rect(&model,now,0,0,320,320,full_frame);
#if CHART_LCD_SHIFT_DIAGNOSTIC
    chart_render_diag_ruler(0,0,320,320,full_frame);
    chart_render_diag_swatches(0,0,320,320,full_frame,NULL);
#endif
    assert(!memcmp(screen,full_frame,sizeof(screen)));
    /* Deterministic changes independently perturb predecessor arcs and circular trend history. */
    uint32_t seed=0x14c5;
    for (unsigned step=0;step<300;++step) {
        seed=seed*1664525U+1013904223U;
        int bin=(int)(seed%CHART_BINS);
        now+=1000+(seed%60000);
        model.heading=(float)(seed%144000)/100-720;
        model.display_heading=model.heading+0.37f;
        model.bins[bin]=(chart_bin_t){.count=(seed&256) ? 3 : 0,
            .heading=bin*10+(float)(seed%900)/100,.rssi=-90+(float)(seed%51),
            .updated_us=now-(seed%25000000)};
        model.peak_bin=(seed&512) ? bin : -1;
        model.trend[model.trend_next]=(chart_signal_event_t){now,-90+(int)(seed%51),true};
        model.trend_next=(model.trend_next+1)%CHART_TREND_LENGTH;
        if (model.trend_used<CHART_TREND_LENGTH) ++model.trend_used;
        model.last_signal_us=(seed&1024) ? now : 0;
        model.current_rssi=-104+(float)(seed%98);
        model.coverage=(int)(seed%37);
        check_update(&displayed,&model,now);
    }
    virtual_mode=false;
    puts("PASS: dirty virtual LCD equals full reference through rotation/wrap, fade/timeout/scroll, sparse arcs, text/status/clear/reset/mode; clean rounds yield without pixels");
}

static void display_faults(void)
{
    for (unsigned fault=0;fault<3;++fault) {
        reset(); virtual_mode=true;
        displayed_state_t displayed={0},saved=displayed;
        chart_model_t model; chart_init(&model,UI_SIM,1000000);
        fail_submit=fault==0 ? 3 : 0;
        fail_poll=fault==1 ? 4 : 0;
        missing_result=fault==2;
        display_work_t work;
        assert(display_update(&displayed,&model,1000000,pixels,&work)!=ESP_OK);
        assert(!memcmp(&saved,&displayed,sizeof(saved)) && lcd_window.failed);
        if (fault==2) {
            assert(pending==2 && acquired); missing_result=false;
            assert(chart_lcd_window_end(NULL)!=ESP_OK);
        }
        assert(!pending && !acquired);
        virtual_mode=false;
    }
    reset(); virtual_mode=true;
    displayed_state_t displayed={0};
    chart_model_t model; chart_init(&model,UI_SIM,1000000);
    check_update(&displayed,&model,1000000);
    displayed_state_t saved=displayed;
    model.heading=90; snprintf(model.source,sizeof(model.source),"SHORT");
    fail_submit=submits+2;
    display_work_t work;
    assert(display_update(&displayed,&model,1010000,pixels,&work)!=ESP_OK);
    assert(!memcmp(&saved,&displayed,sizeof(saved)) && !pending && !acquired && lcd_window.failed);
    virtual_mode=false;
    puts("PASS: display failures do not commit baseline; drain accepted work, retain unknown ownership and stop");
}

#if CHART_LCD_BOOT_DIAGNOSTIC
static void boot_diagnostic(void)
{
    reset(); virtual_mode=true;
    unsigned before=total_windows;
    assert(display_boot_pattern(pixels)==ESP_OK);
    chart_render_test_tile(0,320,full_frame);
    assert(!memcmp(screen,full_frame,sizeof(screen)));
    assert(submits==20 && total_windows-before==20 && actual_hz==40000000);
    assert(!pending && !acquired && !cs);
    chart_lcd_fast_pixels(true);
    displayed_state_t displayed={0};
    chart_model_t model; chart_init(&model,UI_SIM,1000000);
    check_update(&displayed,&model,1000000);
    assert(actual_hz==80000000);
    for (unsigned block=0;block<20;++block) {
        for (unsigned fault=0;fault<3;++fault) {
            reset();
            fail_poll=fault==0 ? block*5+3 : 0;
            fail_submit=fault==1 ? block+1 : 0;
            fail_completion=fault==2 ? block+1 : 0;
            assert(display_boot_pattern(pixels)!=ESP_OK);
            assert(lcd_window.failed && !pending && !acquired);
            assert(submits<=block+1);
            assert(chart_lcd_window_begin(0,0,1,1)==ESP_ERR_INVALID_STATE);
        }
    }
    reset(); missing_result=true;
    assert(display_boot_pattern(pixels)!=ESP_OK);
    assert(submits==1 && pending==1 && acquired && lcd_window.failed);
    assert(!memcmp(queue[0].copy.tx_buffer,queue[0].bytes,queue[0].copy.length/8));
    missing_result=false;
    assert(chart_lcd_window_end(NULL)!=ESP_OK && !pending && !acquired);
    virtual_mode=false;
    puts("PASS: optional 40 MHz boot diagnostic matches reference then enters 80 MHz chart; every block fault stops, unknown ownership retained");
}
#endif

int main(void)
{
    assert(sizeof(pixels)==40960 && (uint8_t *)(pixels+CHART_LCD_BUFFER_PIXELS)-(uint8_t *)pixels==20480);
    assert(chart_lcd_init()==ESP_OK && lcd_dma_max==12276);
    for (int width=1;width<=320;++width) rectangle(320-width,0,width,33,true);
    rectangle(0,0,320,320,true);
    rectangle(319,319,1,1,true);
    rectangle(0,0,1,320,false);
    rectangle(157,273,163,47,false);
    puts("PASS: production rectangles; all widths, odd rows, 1x1, offsets, tight bytes, fixed regions, CS/DC and 40/80 MHz");
    invalid_inputs(); failures();
    puts("PASS: rectangle validation, descriptor ownership, single swap, command/queue/result faults, unknown completion retains bus and buffers");
    plans(); dirty_refresh(); display_faults();
#if CHART_LCD_BOOT_DIAGNOSTIC
    boot_diagnostic();
#endif
    return 0;
}
