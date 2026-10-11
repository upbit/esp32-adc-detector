#include "starchart.h"
#include <assert.h>
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
    chart_screen_point(0,0,88,&x,&y);
    assert(fabsf(x-160)<0.001f && fabsf(y-82)<0.001f);
    chart_screen_point(0,30,88,&x,&y);
    assert(fabsf(x-116)<0.001f && fabsf(y-(170-88*cosf(3.14159265f/6)))<0.001f);
    chart_screen_point(30,30,88,&x,&y);
    assert(fabsf(x-160)<0.001f && fabsf(y-82)<0.001f);
    float x2,y2;
    chart_screen_point(60,179.9f,88,&x,&y);
    chart_screen_point(60,-179.9f,88,&x2,&y2);
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
    assert(frame[82*320+160]!=background);
    assert(frame[108*320+222]==background); /* Missing +45 degree bin stays empty. */
    assert(frame[170*320+160]!=background);
    uint32_t count=model.samples;
    float rssi=model.bins[0].rssi;
    chart_render_tile(&model,clock_us+25000000,80,CHART_TILE_ROWS,tile);
    assert(memcmp(frame+80*320,tile,sizeof(tile))!=0);
    assert(model.samples==count && model.bins[0].rssi==rssi);
    puts("PASS: native 320x320 raster, antenna and samples visible, unscanned gap blank, age fade does not mutate RSSI");
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
    geometry(); buckets(); synchronization(); calibration(); rendering(); beacon_filter(); endurance();
    printf("All host tests passed. model=%zu bytes, tile=%zu bytes\n",sizeof(model),sizeof(tile));
    return 0;
}
