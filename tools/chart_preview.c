#include "starchart.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static chart_model_t model;
static uint16_t tile[320*CHART_TILE_ROWS];

static void write_frame(const char *path,int64_t now)
{
    FILE *file=fopen(path,"wb");
    if (!file) { perror(path); exit(1); }
    fputs("P6\n320 320\n255\n",file);
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        chart_render_tile(&model,now,y,CHART_TILE_ROWS,tile);
        for (unsigned p=0;p<sizeof(tile)/sizeof(*tile);++p) {
            uint16_t v=tile[p];
            unsigned char rgb[]={((v>>11)&31)*255/31,((v>>5)&63)*255/63,(v&31)*255/31};
            fwrite(rgb,1,3,file);
        }
    }
    fclose(file);
}

int main(int argc,char **argv)
{
    const char *directory=argc>1 ? argv[1] : "/tmp/chart-preview";
    chart_init(&model,UI_SIM,1000000);
    chart_render_init();
    int64_t now=1000000;
    /* Scan a real synthetic path through the synthetic peak, then reverse with signal paused. */
    unsigned frame=0;
    for (int step=0;step<=150;++step) {
        now+=100000;
        float heading=step<=110 ? -40+step : 70-(step-110)*2;
        chart_record_pose(&model,now,heading,heading);
        if (step<=110) chart_record_signal(&model,
            (chart_signal_event_t){now,(int8_t)lroundf(chart_synthetic_rssi(heading)),true},now);
        if (step%5!=0) continue;
        char path[512];
        snprintf(path,sizeof(path),"%s/frame-%03u.ppm",directory,frame++);
        write_frame(path,now);
    }
    printf("%u native 320x320 frames; packets=%lu coverage=%d/36 stable peak=%.1f deg\n",
        frame,(unsigned long)model.samples,model.coverage,model.bins[model.peak_bin].heading);

    /* State and text stress pages for V04/V07; each writes one inspectable frame. */
    const int64_t base=now;
    struct { const char *name,*source; int angle,rssi; } cases[]={
        {"state-normal","CAM_02",60,-61},
        {"text-long","HomeOffice_Camera_Backyard_5G",-170,-104},
        {"text-short","A",0,-7},
        {"text-missing","",180,0},
    };
    for (unsigned c=0;c<sizeof(cases)/sizeof(*cases);++c) {
        snprintf(model.source,sizeof(model.source),"%s",cases[c].source);
        model.heading=model.display_heading=(float)cases[c].angle;
        model.current_rssi=(float)cases[c].rssi;
        /* Keep the reading fresh so the dBm value renders, not the stale dash. */
        model.last_signal_us=cases[c].rssi ? base : 0;
        char path[512];
        snprintf(path,sizeof(path),"%s/case-%s.ppm",directory,cases[c].name);
        write_frame(path,base);
    }
    snprintf(model.source,sizeof(model.source),"CAM_02");
    model.heading=model.display_heading=60;
    model.current_rssi=-61; model.last_signal_us=base;
    struct { const char *name; chart_pose_status_t pose; chart_target_status_t target; float tilt; chart_mode_t mode; } states[]={
        {"state-imu-error",CHART_IMU_ERROR,CHART_TARGET_LOCKED,0,UI_SIM},
        {"state-calibrating",CHART_CALIBRATING,CHART_TARGET_LOCKED,0,UI_SIM},
        {"state-tilted",CHART_READY,CHART_TARGET_LOCKED,45,UI_SIM},
        {"state-wifi-error",CHART_READY,CHART_WIFI_ERROR,0,LIVE_WIFI},
        {"state-missing",CHART_READY,CHART_TARGET_MISSING,0,LIVE_WIFI},
        {"state-search",CHART_READY,CHART_TARGET_SEARCH,0,LIVE_WIFI},
    };
    {   /* Calibration guidance: off-centre bubble, then centred with countdown. */
        model.pose_status=CHART_CALIBRATING; model.tilt_deg=0; model.mode=IMU_SIM_RSSI;
        model.fatal=CHART_FATAL_NONE; model.cal_reason=CHART_CAL_OK;
        model.level_x=0.34f; model.level_y=-0.22f; model.still_ms=0;
        char path[512];
        snprintf(path,sizeof(path),"%s/case-cal-bubble.ppm",directory);
        write_frame(path,base);
        model.level_x=0.02f; model.level_y=0.01f; model.still_ms=1800;
        snprintf(path,sizeof(path),"%s/case-cal-countdown.ppm",directory);
        write_frame(path,base);
        model.cal_reason=CHART_CAL_TILT; model.still_ms=0;
        snprintf(path,sizeof(path),"%s/case-cal-level.ppm",directory);
        write_frame(path,base);
        model.level_x=model.level_y=0; model.cal_reason=CHART_CAL_OK;
    }
    {   /* Unrecoverable: red background, English reason, layout retained. */
        model.pose_status=CHART_IMU_ERROR; model.fatal=CHART_FATAL_NO_IMU;
        model.gate_retries=0;
        char path[512];
        snprintf(path,sizeof(path),"%s/case-fatal-no-imu.ppm",directory);
        write_frame(path,base);
        model.fatal=CHART_FATAL_GATE; model.gate_retries=8;
        snprintf(path,sizeof(path),"%s/case-fatal-gate.ppm",directory);
        write_frame(path,base);
        model.fatal=CHART_FATAL_NONE; model.pose_status=CHART_READY;
    }
    for (unsigned s=0;s<sizeof(states)/sizeof(*states);++s) {
        model.pose_status=states[s].pose; model.target_status=states[s].target;
        model.tilt_deg=states[s].tilt; model.mode=states[s].mode;
        char path[512];
        snprintf(path,sizeof(path),"%s/case-%s.ppm",directory,states[s].name);
        write_frame(path,base);
    }
    printf("%u text cases and %u state pages\n",
        (unsigned)(sizeof(cases)/sizeof(*cases)),(unsigned)(sizeof(states)/sizeof(*states)));
    return 0;
}
