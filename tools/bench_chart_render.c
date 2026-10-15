#define _POSIX_C_SOURCE 200809L
#include "starchart.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static chart_model_t model;
static uint16_t pixels[320*320]; /* Host only, including the full-frame reference benchmark. */
static volatile unsigned checksum;

static double monotonic_us(void)
{
    struct timespec t;
    int result=clock_gettime(CLOCK_MONOTONIC,&t);
    assert(result==0);
    return t.tv_sec*1000000.0+t.tv_nsec/1000.0;
}

/* Geometry-only ring workload, not P4 dirty detection: include the widest ribbon halo. */
static bool ring_cell(int column,int row)
{
    int x0=column*16-CHART_CENTER_X,x1=x0+15;
    int y0=row*16-CHART_CENTER_Y,y1=y0+15;
    int near_x=x0>0 ? x0 : x1<0 ? -x1 : 0,near_y=y0>0 ? y0 : y1<0 ? -y1 : 0;
    int far_x=abs(x0)>abs(x1) ? abs(x0) : abs(x1),far_y=abs(y0)>abs(y1) ? abs(y0) : abs(y1);
    int inner=CHART_SAMPLE_R-10,outer=CHART_SAMPLE_R+10;
    return near_x*near_x+near_y*near_y<=outer*outer && far_x*far_x+far_y*far_y>=inner*inner;
}

static unsigned draw_workload(int workload,int64_t now,unsigned *rectangles)
{
    unsigned count=0;
    *rectangles=0;
    if (workload==3) {
        for (int row=0;row<20;++row) {
            for (int column=0;column<20;) {
                if (!ring_cell(column,row)) { ++column; continue; }
                int first=column++,end=column;
                while (column<20) {
                    if (ring_cell(column,row)) end=++column;
                    else if (column+1<20 && ring_cell(column+1,row)) { column+=2; end=column; }
                    else break;
                }
                int width=(end-first)*16;
                chart_render_rect(&model,now,first*16,row*16,width,16,pixels);
                count+=width*16; ++*rectangles;
            }
        }
    } else if (workload==4 || workload==5) {
        chart_render_rect(&model,now,0,workload==4 ? 0 : 288,320,32,pixels);
        count=320*32; *rectangles=1;
    } else {
        int rows=workload==0 ? 320 : workload==1 ? 16 : 32;
        for (int y=0;y<320;y+=rows) {
            chart_render_tile(&model,now,y,rows,pixels);
            count+=320*rows; ++*rectangles;
        }
    }
    return count;
}

static int compare(const void *a,const void *b)
{
    double x=*(const double *)a,y=*(const double *)b;
    return (x>y)-(x<y);
}

int main(void)
{
    chart_render_init();
    puts("Pure CPU host benchmark: -O2, no sanitizers; 3 warmups + 31 measured rounds.");
    puts("No SPI, model updates, initialization or checksums in timed intervals; not ESP32 FPS.");
    puts("Ring workload: R=103 +/-10 px, 16px cells, one empty cell merged per row.");
    const char *names[]={"full-reference","full-tiles16","full-tiles32","ring-rows16","header32","footer32"};
    const char *scenes[]={"partial12-fresh","full36-fresh","full36-history"};
    for (int scene=0;scene<3;++scene) {
        int64_t now=1000000;
        chart_init(&model,UI_SIM,now);
        int bins=scene==0 ? 12 : 36;
        for (int i=0;i<bins;++i)
            for (int j=0;j<3;++j) {
                now+=10000;
                float angle=i*10.0f;
                chart_record_pose(&model,now,angle,angle);
                assert(chart_record_signal(&model,(chart_signal_event_t){now,
                    (int8_t)lroundf(chart_synthetic_rssi(angle)),true},now));
            }
        model.heading=model.display_heading=30.37f;
        if (scene==2) now+=12000000;
        for (int workload=0;workload<6;++workload) {
            double timings[31];
            unsigned count=0,rectangles=0;
            for (int round=-3;round<31;++round) {
                double start=monotonic_us();
                count=draw_workload(workload,now,&rectangles);
                double elapsed=monotonic_us()-start;
                if (round>=0) timings[round]=elapsed;
                checksum+=pixels[0];
            }
            qsort(timings,31,sizeof(*timings),compare);
            printf("%-16s %-14s pixels=%6u rects=%2u min/median/p95=%.3f/%.3f/%.3f ms median=%.4f us/pixel\n",
                scenes[scene],names[workload],count,rectangles,
                timings[0]/1000,timings[15]/1000,timings[29]/1000,timings[15]/count);
        }
    }
    printf("checksum=%u\n",checksum);
    return 0;
}
