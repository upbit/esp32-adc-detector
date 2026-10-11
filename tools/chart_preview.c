#include "starchart.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static chart_model_t model;
static uint16_t tile[320*CHART_TILE_ROWS];

int main(int argc,char **argv)
{
    const char *directory=argc>1 ? argv[1] : "/tmp/chart-preview";
    chart_init(&model,UI_SIM,1000000);
    chart_render_init();
    int64_t now=1000000;
    /* Scan a real synthetic path through +60, then reverse with signal paused. */
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
        FILE *file=fopen(path,"wb");
        if (!file) { perror(path); return 1; }
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
    printf("%u native 320x320 frames; packets=%lu coverage=%d/36 stable peak=%.1f deg\n",
        frame,(unsigned long)model.samples,model.coverage,model.bins[model.peak_bin].heading);
    return 0;
}
