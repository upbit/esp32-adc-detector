#include "starchart.h"
#include "starchart_font.h"
#include <assert.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static chart_model_t model;
static int64_t clock_us;

static void sample(float angle,int rssi)
{
    clock_us+=10000;
    chart_record_pose(&model,clock_us,angle,angle);
    assert(chart_record_signal(&model,(chart_signal_event_t){clock_us,rssi,true},clock_us));
}

static void geometry(void)
{
    float x,y;
    const float r=CHART_SAMPLE_R,cx=CHART_CENTER_X,cy=CHART_CENTER_Y;
    chart_screen_point(0,0,r,&x,&y);
    assert(fabsf(x-cx)<0.001f && fabsf(y-(cy-r))<0.001f);
    chart_screen_point(0,30,r,&x,&y);
    assert(fabsf(x-(cx-r*0.5f))<0.001f && fabsf(y-(cy-r*cosf(3.14159265f/6)))<0.001f);
    chart_screen_point(30,30,r,&x,&y);
    assert(fabsf(x-cx)<0.001f && fabsf(y-(cy-r))<0.001f);
    float x2,y2;
    chart_screen_point(60,179.9f,r,&x,&y);
    chart_screen_point(60,-179.9f,r,&x2,&y2);
    assert(hypotf(x-x2,y-y2)<0.4f);
    assert(chart_wrap180(361)==1 && chart_wrap180(-181)==179);
    assert(chart_channel_frequency(6)==2437 && chart_channel_frequency(14)==2484);
    assert(chart_channel_frequency(36)==5180 && chart_channel_frequency(177)==5885);
    assert(chart_channel_frequency(35)==0 && chart_channel_frequency(15)==0);
    assert(fabsf(chart_synthetic_rssi(60)+50)<0.001f);
    assert(fabsf(chart_synthetic_rssi(420)+50)<0.001f);
    puts("PASS: opposite rotation, top alignment, wrap boundary, channel frequencies, fixed synthetic peak");
}

static void buckets(void)
{
    clock_us=1000000;
    chart_init(&model,UI_SIM,clock_us);
    sample(0,-65); sample(0,-65); sample(0,-65);
    assert(model.coverage==1 && model.peak_bin==0);
    sample(60,-40);
    assert(model.peak_bin==0);
    sample(60,-80); sample(60,-80);
    assert(model.peak_bin==0 && model.bins[6].rssi==-80);
    sample(60,-50); sample(60,-50); sample(60,-50);
    assert(model.peak_bin==6 && model.coverage==2);
    int64_t timestamp=model.bins[6].updated_us;
    float saved=model.bins[6].rssi;
    uint32_t samples=model.samples;
    chart_record_pose(&model,clock_us+10000,90,89);
    assert(model.bins[6].updated_us==timestamp && model.bins[6].rssi==saved && model.samples==samples);
    assert(!chart_record_signal(&model,(chart_signal_event_t){clock_us+10000,-30,false},clock_us+10000));
    chart_zero(&model,clock_us+20000);
    assert(model.coverage==0 && model.peak_bin==-1 && model.samples==0 && model.trend_used==0);
    assert(!chart_record_signal(&model,(chart_signal_event_t){clock_us,-60,true},clock_us+20000));
    puts("PASS: actual coverage, median rejects one-packet spike, confirmed peak, no invented samples, atomic reset");
}

static void synchronization(void)
{
    chart_init(&model,UI_SIM,1000000);
    chart_record_pose(&model,1010000,359,358);
    chart_record_pose(&model,1020000,361,360);
    float heading;
    assert(chart_pose_at(&model,1015000,&heading) && fabsf(heading-360)<0.001f);
    assert(!chart_pose_at(&model,1021000,&heading));
    assert(!chart_pose_at(&model,999999,&heading));
    assert(chart_record_signal(&model,(chart_signal_event_t){1015000,-61,true},1020000));
    assert(model.coverage==1 && model.bins[0].count==1 && fabsf(model.bins[0].heading)<0.001f);
    chart_record_pose(&model,1300000,380,379);
    assert(!chart_pose_at(&model,1200000,&heading));
    for (int j=0;j<1000;++j) chart_record_pose(&model,1310000+j*10000,380+j,379+j);
    assert(model.pose_used==CHART_POSE_HISTORY);
    assert(!chart_pose_at(&model,1015000,&heading));
    puts("PASS: event-time interpolation on unwrapped yaw, no extrapolation, gap rejection, bounded pose history");
}

static void calibration(void)
{
    chart_integrator_t i;
    chart_integrator_reset(&i);
    float accel[]={0,0,1},gyro[]={0,0,5},tilt;
    for (int j=0;j<400;++j) assert(!chart_integrate(&i,1000000+j*10000,gyro,accel,&tilt));
    assert(!i.calibrated);
    gyro[2]=0.1f;
    for (int j=0;j<260;++j) chart_integrate(&i,5000000+j*10000,gyro,accel,&tilt);
    assert(i.calibrated && fabsf(i.bias[2]-0.1f)<0.001f);
    float before=i.heading;
    gyro[2]=0.3f;
    for (int j=0;j<100;++j) assert(chart_integrate(&i,7600000+j*10000,gyro,accel,&tilt));
    assert(fabsf((i.heading-before)-0.2f*CHART_IMU_YAW_SIGN)<0.001f);
    assert(!chart_integrate(&i,10000000,gyro,accel,&tilt) && !i.calibrated);
    puts("PASS: motion delays calibration, static bias estimate, slow rotation retained, stalled IMU forces recalibration");
}

static uint16_t frame[320*320],tile[320*CHART_TILE_ROWS];
static void boot_pattern(void)
{
    chart_render_init();
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        chart_render_test_tile(y,CHART_TILE_ROWS,tile);
        memcpy(frame+y*320,tile,sizeof(tile));
    }
    const uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    for (int i=0;i<4;++i) {
        assert(frame[100*320+40+i*80]==colors[i]);
        assert(frame[250*320+40+i*80]==colors[i]);
    }
    assert(frame[160]==0 && frame[319*320+160]==0);
    assert(frame[100*320]==0 && frame[100*320+319]==0);
    puts("PASS: full-height RGB/white boot pattern and four visible boundaries");
}

static void rendering(void)
{
    chart_init(&model,UI_SIM,1000000);
    clock_us=1000000;
    sample(0,-60); sample(0,-60); sample(0,-60);
    sample(90,-70);
    chart_render_init();
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        chart_render_tile(&model,clock_us,y,CHART_TILE_ROWS,tile);
        memcpy(frame+y*320,tile,sizeof(tile));
    }
    uint16_t background=frame[319*320+319];
    /* Sample ring sits at a fixed radius above the centre; the centre carries the dot. */
    const int top_y=CHART_CENTER_Y-CHART_SAMPLE_R;
    assert(frame[top_y*320+CHART_CENTER_X]!=background);
    const int gap_x=(int)(CHART_CENTER_X+CHART_SAMPLE_R*0.7071f);
    const int gap_y=(int)(CHART_CENTER_Y-CHART_SAMPLE_R*0.7071f);
    assert(frame[gap_y*320+gap_x]==background); /* Missing +45 degree bin stays empty. */
    /* Origin is a ring with a dark core, so check the ring itself, not the centre. */
    assert(frame[CHART_CENTER_Y*320+CHART_CENTER_X-4]!=background);
    assert(frame[(CHART_CENTER_Y-4)*320+CHART_CENTER_X]!=background);
    uint32_t count=model.samples;
    float rssi=model.bins[0].rssi;
    const int fade_band=top_y-top_y%CHART_TILE_ROWS;
    chart_render_tile(&model,clock_us+25000000,fade_band,CHART_TILE_ROWS,tile);
    assert(memcmp(frame+fade_band*320,tile,sizeof(tile))!=0);
    assert(model.samples==count && model.bins[0].rssi==rssi);
    puts("PASS: native 320x320 raster, antenna and samples visible, unscanned gap blank, age fade does not mutate RSSI");
}

static void render_tiles(void)
{
    chart_init(&model,UI_SIM,1000000);
    clock_us=1000000;
    for (int angle=0;angle<360;angle+=10)
        for (int j=0;j<3;++j) sample(angle,-50-angle%40);
    for (int turn=0;turn<4;++turn) {
        model.display_heading=turn*90+0.37f;
        int64_t now=clock_us+turn*9000000LL;
        chart_render_tile(&model,now,0,320,frame);
        for (int y=0;y<320;y+=CHART_TILE_ROWS) {
            chart_render_tile(&model,now,y,CHART_TILE_ROWS,tile);
            assert(memcmp(frame+y*320,tile,sizeof(tile))==0);
        }
    }
    uint16_t before[320*CHART_TILE_ROWS];
    chart_render_tile(&model,clock_us,288,CHART_TILE_ROWS,tile);
    memcpy(before,tile,sizeof(tile));
    chart_render_test_overlay(288,CHART_TILE_ROWS,tile,"T02 40MHz 10.0FPS",0);
    assert(memcmp(before,tile,11*320*sizeof(*tile))==0);
    assert(tile[11*320]!=before[11*320]);
    puts("PASS: full-circle/faded/fractional-angle tiled raster matches full frame; test footer stays in its band");
}

static uint16_t rect_pixels[320*320+16];

static void check_rect(int64_t now,int x,int y,int width,int rows)
{
    const uint16_t guard=0x5aa5;
    for (unsigned i=0;i<sizeof(rect_pixels)/sizeof(*rect_pixels);++i) rect_pixels[i]=guard;
    chart_render_rect(&model,now,x,y,width,rows,rect_pixels+8);
    for (int py=0;py<rows;++py)
        assert(memcmp(rect_pixels+8+py*width,frame+(y+py)*320+x,width*sizeof(*frame))==0);
    for (int i=0;i<8;++i) assert(rect_pixels[i]==guard);
    for (unsigned i=8+width*rows;i<sizeof(rect_pixels)/sizeof(*rect_pixels);++i)
        assert(rect_pixels[i]==guard);
}

static void render_rects(void)
{
    assert(sizeof(chart_dirty_mask_t)==50);
    chart_init(&model,UI_SIM,1000000);
    clock_us=1000000;
    for (int angle=0;angle<360;angle+=10)
        for (int j=0;j<3;++j) sample(angle,-40-angle%50);
    const struct { int x,y,w,h; } rects[]={
        {0,0,320,320},{0,0,1,1},{319,319,1,1},{319,0,1,320},{0,319,320,1},
        {46,42,229,229},{53,48,213,217},{0,276,320,44},{9,7,131,51},
        {222,1,97,59},{154,31,13,126},{159,155,3,3},{14,146,31,17},
        {270,146,49,19},{101,206,117,23},{63,293,193,27},{7,58,17,1},
    };
    const float headings[]={0,30.37f,60,-179.9f,179.9f,-90.25f,270.5f,720.37f};
    const int64_t ages[]={0,CHART_FRESH_US,CHART_FRESH_US+1,CHART_DARK_US,
                          CHART_DARK_US+1,CHART_HISTORY_US,CHART_HISTORY_US+1,25000000};
    const chart_pose_status_t poses[]={CHART_READY,CHART_READY,CHART_CALIBRATING,CHART_IMU_ERROR,
                                       CHART_READY,CHART_READY,CHART_READY,CHART_READY};
    const chart_target_status_t targets[]={CHART_TARGET_LOCKED,CHART_TARGET_SEARCH,CHART_TARGET_LOCKED,
        CHART_TARGET_LOCKED,CHART_TARGET_LOCKED,CHART_WIFI_ERROR,CHART_TARGET_MISSING,CHART_TARGET_LOCKED};
    uint32_t random=0x12345678;
    for (unsigned c=0;c<sizeof(headings)/sizeof(*headings);++c) {
        model.heading=model.display_heading=headings[c];
        model.pose_status=poses[c]; model.target_status=targets[c];
        model.mode=c%2 ? LIVE_WIFI : UI_SIM;
        model.tilt_deg=c==4 ? 45 : 0;
        model.current_rssi=c%2 ? -104 : -7;
        snprintf(model.source,sizeof(model.source),"%s",c%2 ? "HomeOffice_Camera_Backyard_5G" : "CAM_02?");
        if (c==6) { model.source[0]=0; model.frequency_mhz=0; model.last_signal_us=0; }
        /* Keep both sparse disconnected points and the last-to-first wrap connection. */
        if (c==7) for (int i=1;i<CHART_BINS-1;++i)
            if (i%3) model.bins[i].count=0;
        int64_t now=clock_us+ages[c];
        chart_model_t before=model;
        chart_render_tile(&model,now,0,320,frame);
        for (unsigned r=0;r<sizeof(rects)/sizeof(*rects);++r)
            check_rect(now,rects[r].x,rects[r].y,rects[r].w,rects[r].h);
        /* Reconstruct all screen pixels using tight, independently rendered rectangles. */
        const int blocks[]={16,32};
        for (unsigned b=0;b<sizeof(blocks)/sizeof(*blocks);++b)
            for (int y=0;y<320;y+=blocks[b])
                for (int x=0;x<320;x+=47) {
                    int width=320-x<47 ? 320-x : 47;
                    check_rect(now,x,y,width,blocks[b]);
                }
        for (int r=0;r<64;++r) {
            int values[4];
            for (int i=0;i<4;++i) { random=random*1664525u+1013904223u; values[i]=(int)(random>>16); }
            int x=values[0]%320,y=values[1]%320;
            check_rect(now,x,y,1+values[2]%(320-x),1+values[3]%(320-y));
        }
        assert(memcmp(&before,&model,sizeof(model))==0);
    }
    chart_clear(&model,clock_us+26000000);
    chart_model_t cleared=model;
    chart_render_tile(&model,clock_us+26000000,0,320,frame);
    for (unsigned r=0;r<sizeof(rects)/sizeof(*rects);++r)
        check_rect(clock_us+26000000,rects[r].x,rects[r].y,rects[r].w,rects[r].h);
    assert(memcmp(&cleared,&model,sizeof(model))==0);
    /* An invalid rectangle is rejected, not cropped with a different output stride. */
    const struct { int x,y,w,h; } invalid[]={
        {-1,0,16,16},{0,-1,16,16},{320,0,1,1},{0,320,1,1},
        {319,319,2,1},{319,319,1,2},{0,0,321,320},{0,0,320,321},
        {0,0,0,1},{0,0,1,0},{0,0,-1,1},{0,0,1,-1},
        {INT_MIN,0,1,1},{0,INT_MAX,1,1},{0,0,INT_MAX,1},{0,0,1,INT_MAX},
        {1,1,INT_MAX,INT_MAX},{0,0,INT_MIN,INT_MIN},
    };
    for (unsigned i=0;i<sizeof(rect_pixels)/sizeof(*rect_pixels);++i) rect_pixels[i]=0x5aa5;
    for (unsigned r=0;r<sizeof(invalid)/sizeof(*invalid);++r) {
        chart_render_rect(&model,clock_us,invalid[r].x,invalid[r].y,invalid[r].w,invalid[r].h,rect_pixels+8);
        for (unsigned i=0;i<sizeof(rect_pixels)/sizeof(*rect_pixels);++i) assert(rect_pixels[i]==0x5aa5);
    }
    /* The diagnostic/overlay entries must restore their full-width context after a rect. */
    chart_render_test_tile(96,CHART_TILE_ROWS,tile);
    memcpy(frame,tile,sizeof(tile));
    chart_render_rect(&model,clock_us,159,155,3,3,rect_pixels+8);
    chart_render_test_tile(96,CHART_TILE_ROWS,tile);
    assert(memcmp(frame,tile,sizeof(tile))==0);
    puts("PASS: guarded tight rectangles, 16/32-row reconstruction, random crops, wrap/fade/status/clear cases, invalid no-write and read-only snapshots");
}

static void glyph_coverage(void)
{
    /* Every drawable glyph must carry ink: a blank box means the bake clipped it. */
    for (unsigned j=0;j<sizeof(chart_glyphs)/sizeof(*chart_glyphs);++j) {
        const chart_glyph_t *g=&chart_glyphs[j];
        if (g->code==' ') continue;
        unsigned ink=0;
        for (unsigned p=0;p<(unsigned)g->width*g->height;++p) {
            unsigned v=chart_font_data[g->offset+p/2];
            if (p%2 ? v&15 : v>>4) ++ink;
        }
        assert(ink>0);
    }
    /* Digits are monospaced per size so a changing reading never shifts neighbours. */
    for (unsigned j=0;j<sizeof(chart_glyphs)/sizeof(*chart_glyphs);++j) {
        const chart_glyph_t *g=&chart_glyphs[j];
        if (g->code<'0' || g->code>'9') continue;
        const chart_glyph_t *zero=NULL;
        for (unsigned k=0;k<sizeof(chart_glyphs)/sizeof(*chart_glyphs);++k)
            if (chart_glyphs[k].code=='0' && chart_glyphs[k].size==g->size) zero=&chart_glyphs[k];
        assert(zero && g->width==zero->width);
    }
    /* The underscore regression: CAM_02 lost its bar when the box clipped descenders. */
    for (unsigned j=0;j<sizeof(chart_glyphs)/sizeof(*chart_glyphs);++j) {
        const chart_glyph_t *g=&chart_glyphs[j];
        if (g->code!='_') continue;
        unsigned ink=0;
        for (unsigned p=0;p<(unsigned)g->width*g->height;++p) {
            unsigned v=chart_font_data[g->offset+p/2];
            if (p%2 ? v&15 : v>>4) ++ink;
        }
        assert(ink>=g->width);
    }
    puts("PASS: every glyph inked, digits monospaced per size, underscore bar present");
}

static void beacon_filter(void)
{
    uint8_t bssid[]={0x02,0x01,0x02,0x03,0x04,0x05},f[40]={0x80};
    memcpy(f+10,bssid,6); memcpy(f+16,bssid,6);
    assert(chart_target_beacon(f,sizeof(f),bssid));
    for (unsigned n=0;n<40;++n) assert(!chart_target_beacon(f,n,bssid));
    f[10]^=1; assert(!chart_target_beacon(f,40,bssid)); f[10]^=1;
    f[16]^=1; assert(!chart_target_beacon(f,40,bssid)); f[16]^=1;
    f[0]=0x50; assert(!chart_target_beacon(f,40,bssid)); f[0]=0x80;
    f[1]=1; assert(!chart_target_beacon(f,40,bssid));
    puts("PASS: Beacon length/type/DS flags and transmitter+BSSID filtering");
}

static void refresh_pattern(void)
{
    static uint16_t block[320*CHART_LCD_REFRESH_ROWS],later[320*320];
    static uint16_t wire[320*CHART_LCD_REFRESH_ROWS+2];
    const int first=CHART_LCD_REFRESH_Y,end=first+CHART_LCD_REFRESH_HEIGHT;
    chart_render_refresh_tile(0,320,frame,"10 WIRE 80M MAX",123.4f,0,0);
    const int sizes[]={16,32,64};
    for (unsigned s=0;s<sizeof(sizes)/sizeof(*sizes);++s) {
        for (int y=0;y<320;y+=sizes[s]) {
            int rows=320-y<sizes[s] ? 320-y : sizes[s];
            chart_render_refresh_tile(y,rows,block,"10 WIRE 80M MAX",123.4f,0,0);
            assert(memcmp(frame+y*320,block,rows*320*sizeof(*block))==0);
        }
    }
    chart_render_refresh_tile(0,320,later,"10 WIRE 80M MAX",123.4f,1000000,1);
    assert(memcmp(frame,later,first*320*sizeof(*frame))==0);
    assert(memcmp(frame+end*320,later+end*320,(320-end)*320*sizeof(*frame))==0);
    for (int group=0;group<2;++group) {
        int y=first+group*64;
        assert(memcmp(frame+y*320,later+y*320,64*320*sizeof(*frame))!=0);
        assert(frame[y*320+80]==0xf800 && frame[(y+16)*320+80]==0x07e0);
        assert(frame[(y+32)*320+80]==0x001f && frame[(y+48)*320+80]==0xffff);
        assert(frame[(y+20)*320+300]!=later[(y+20)*320+300]);
    }
    assert(frame[first*320+8]==0 && later[first*320+168]==0);
    assert(frame[(first+64)*320+168]==0 && later[(first+64)*320+8]==0);
    for (int bar=0;bar<320;++bar) {
        for (unsigned f=0;f<2;++f) {
            chart_render_refresh_tile(first,CHART_LCD_REFRESH_HEIGHT,later,"10 WIRE 80M MAX",120,bar*6250LL,f);
            for (int y=first;y<end;y+=64) {
                chart_render_refresh_band(y,64,block,bar*6250LL,f);
                assert(memcmp(later+(y-first)*320,block,64*320*sizeof(*block))==0);
                wire[0]=wire[320*64+1]=0x5aa5;
                chart_render_refresh_band_wire(y,64,wire+1,bar*6250LL,f);
                const unsigned char *bytes=(const unsigned char *)(wire+1);
                for (int i=0;i<320*64;++i) {
                    assert(bytes[i*2]==(unsigned char)(block[i]>>8));
                    assert(bytes[i*2+1]==(unsigned char)block[i]);
                }
                assert(wire[0]==0x5aa5 && wire[320*64+1]==0x5aa5);
            }
        }
    }
    for (int y=first;y<end;y+=7) {
        int rows=end-y<7 ? end-y : 7;
        chart_render_refresh_band(y,rows,block,319*6250LL,1);
        chart_render_refresh_tile(y,rows,later,"10 WIRE 80M MAX",120,319*6250LL,1);
        assert(memcmp(later,block,rows*320*sizeof(*block))==0);
        wire[0]=wire[rows*320+1]=0x5aa5;
        chart_render_refresh_band_wire(y,rows,wire+1,319*6250LL,1);
        const unsigned char *bytes=(const unsigned char *)(wire+1);
        for (int i=0;i<rows*320;++i) {
            assert(bytes[i*2]==(unsigned char)(block[i]>>8));
            assert(bytes[i*2+1]==(unsigned char)block[i]);
        }
        assert(wire[0]==0x5aa5 && wire[rows*320+1]==0x5aa5);
    }
    for (int y=first;y<end;++y) {
        int rows=end-y<64 ? end-y : 64;
        chart_render_refresh_band(y,rows,block,317*6250LL,1);
        wire[0]=wire[rows*320+1]=0x5aa5;
        chart_render_refresh_band_wire(y,rows,wire+1,317*6250LL,1);
        const unsigned char *bytes=(const unsigned char *)(wire+1);
        for (int i=0;i<rows*320;++i) {
            assert(bytes[i*2]==(unsigned char)(block[i]>>8));
            assert(bytes[i*2+1]==(unsigned char)block[i]);
        }
        assert(wire[0]==0x5aa5 && wire[rows*320+1]==0x5aa5);
    }
    puts("PASS: dual motion and guarded wire bytes match all positions, markers and offsets");
}

static void endurance(void)
{
    chart_init(&model,UI_SIM,1000000);
    for (int step=1;step<=60000;++step) {
        int64_t now=1000000+(int64_t)step*10000;
        float heading=step*0.15f;
        chart_record_pose(&model,now,heading,heading-1.5f);
        if (step%10==0) assert(chart_record_signal(&model,
            (chart_signal_event_t){now,(int8_t)lroundf(chart_synthetic_rssi(heading)),true},now));
    }
    assert(model.samples==6000 && model.coverage==36);
    assert(model.pose_used==CHART_POSE_HISTORY && model.trend_used==CHART_TREND_LENGTH);
    for (int b=0;b<36;++b) assert(model.bins[b].used<=CHART_WINDOW);
    puts("PASS: 600 seconds of simulated timestamps, 6000 packets, fixed-size histories and 36/36 coverage");
}

int main(void)
{
    geometry(); buckets(); synchronization(); calibration(); boot_pattern(); rendering(); render_tiles(); render_rects(); glyph_coverage(); refresh_pattern(); beacon_filter(); endurance();
    printf("All host tests passed. model=%zu bytes, tile=%zu bytes\n",sizeof(model),sizeof(tile));
    return 0;
}
