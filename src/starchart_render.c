#include "starchart.h"
#include "starchart_font.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

typedef struct { unsigned char r, g, b; } color_t;
static const color_t bg = {4,18,30}, grid = {27,49,66}, white = {231,242,248},
    muted = {154,181,201}, cyan = {37,223,237}, dark = {20,97,115},
    amber = {255,196,90}, history = {100,114,131};
static float sine[720], cosine[720];
static uint16_t *tile;
static int tile_y, tile_rows;

static color_t mix(color_t a, color_t b, float t)
{
    t = fmaxf(0, fminf(1, t));
    return (color_t){a.r+(b.r-a.r)*t, a.g+(b.g-a.g)*t, a.b+(b.b-a.b)*t};
}

static uint16_t pack(color_t c)
{
    return ((c.r >> 3) << 11) | ((c.g >> 2) << 5) | (c.b >> 3);
}

static void pixel(int x, int y, color_t c, unsigned alpha)
{
    if ((unsigned)x >= 320 || y < tile_y || y >= tile_y + tile_rows) return;
    uint16_t *p = &tile[(y-tile_y)*320+x];
    if (alpha >= 15) { *p = pack(c); return; }
    color_t old = {((*p >> 11) & 31)*255/31, ((*p >> 5) & 63)*255/63, (*p & 31)*255/31};
    *p = pack(mix(old, c, alpha / 15.0f));
}

static void disc(float x, float y, float radius, color_t c)
{
    if (y+radius+1 < tile_y || y-radius-1 >= tile_y+tile_rows) return;
    for (int py = (int)floorf(y-radius); py <= (int)ceilf(y+radius); ++py) {
        if (py < tile_y || py >= tile_y+tile_rows) continue;
        for (int px = (int)floorf(x-radius); px <= (int)ceilf(x+radius); ++px) {
            float dx = px-x, dy = py-y;
            float coverage = radius+0.5f-sqrtf(dx*dx+dy*dy);
            if (coverage > 0) pixel(px, py, c, (unsigned)(fminf(1,coverage)*15));
        }
    }
}

static void line(float x1, float y1, float x2, float y2, float width, color_t c)
{
    if (fmaxf(y1,y2)+width < tile_y || fminf(y1,y2)-width >= tile_y+tile_rows) return;
    float dx=x2-x1, dy=y2-y1;
    int steps = (int)ceilf(fmaxf(fabsf(dx),fabsf(dy)));
    if (!steps) { disc(x1,y1,width*0.5f,c); return; }
    for (int j=0; j<=steps; ++j) disc(x1+dx*j/steps,y1+dy*j/steps,width*0.5f,c);
}

static void polar(float angle, float radius, float *x, float *y)
{
    int index = (int)lroundf(angle*2) % 720;
    if (index < 0) index += 720;
    *x = 160+radius*sine[index];
    *y = 170-radius*cosine[index];
}

static void arc(float start, float end, float r, float width, color_t c)
{
    float x1,y1,x2,y2;
    polar(start,r,&x1,&y1);
    for (float a=start+1; a < end+1; a+=1) {
        polar(fminf(a,end),r,&x2,&y2);
        line(x1,y1,x2,y2,width,c);
        x1=x2; y1=y2;
    }
}

static uint32_t next_code(const char **text)
{
    const unsigned char *s=(const unsigned char *)*text;
    if (!*s) return 0;
    uint32_t c=*s++;
    int n=0;
    if (c>=0xf0) { c &= 7; n=3; }
    else if (c>=0xe0) { c &= 15; n=2; }
    else if (c>=0xc0) { c &= 31; n=1; }
    for (int j=0; j<n; ++j) {
        if ((*s & 0xc0)!=0x80) { *text=(const char *)s; return '?'; }
        c=(c<<6)|(*s++ & 63);
    }
    *text=(const char *)s;
    return c;
}

static const chart_glyph_t *glyph(uint32_t c, int size)
{
    for (unsigned j=0; j<sizeof(chart_glyphs)/sizeof(*chart_glyphs); ++j)
        if (chart_glyphs[j].code==c && chart_glyphs[j].size==size) return &chart_glyphs[j];
    if (c != '?') return glyph('?',size);
    return NULL;
}

static int text_width(const char *s, int size)
{
    int w=0;
    uint32_t c;
    while ((c=next_code(&s))) { const chart_glyph_t *g=glyph(c,size); w += g ? g->width : 8; }
    return w;
}

static void text(const char *s, int x, int y, int size, color_t c, int limit)
{
    uint32_t code;
    while ((code=next_code(&s))) {
        const chart_glyph_t *g=glyph(code,size);
        if (!g) continue;
        if (x+g->width > limit || (limit==185 && *s && code!='.' &&
            x+g->width+text_width("...",size)>limit)) {
            if (code!='.' && limit-x>=text_width("...",size)) text("...",x,y,size,c,limit);
            return;
        }
        if (y+g->height>=tile_y && y<tile_y+tile_rows) {
            for (int py=0; py<g->height; ++py) {
                if (y+py<tile_y || y+py>=tile_y+tile_rows) continue;
                for (int px=0; px<g->width; ++px) {
                    unsigned pos=py*g->width+px;
                    unsigned v=chart_font_data[g->offset+pos/2];
                    unsigned a=pos%2 ? v&15 : v>>4;
                    if (a) pixel(x+px,y+py,c,a);
                }
            }
        }
        x+=g->width;
    }
}

static void centered(const char *s,int y,int size,color_t c)
{
    text(s,160-text_width(s,size)/2,y,size,c,320);
}

void chart_render_init(void)
{
    for (int j=0; j<720; ++j) {
        sine[j]=sinf(j*3.14159265358979323846f/360);
        cosine[j]=cosf(j*3.14159265358979323846f/360);
    }
}

static color_t bin_color(const chart_bin_t *b,int64_t now)
{
    int64_t age=now-b->updated_us;
    float strength=fmaxf(0,fminf(1,(b->rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
    color_t fresh=mix(dark,cyan,0.35f+0.65f*strength);
    if (age<=CHART_FRESH_US) return fresh;
    if (age<=CHART_DARK_US) return mix(fresh,dark,(float)(age-CHART_FRESH_US)/(CHART_DARK_US-CHART_FRESH_US));
    if (age<=CHART_HISTORY_US) return mix(dark,history,(float)(age-CHART_DARK_US)/(CHART_HISTORY_US-CHART_DARK_US));
    return mix(bg,history,0.45f);
}

static float bin_width(const chart_bin_t *b,int64_t now)
{
    float strength=fmaxf(0,fminf(1,(b->rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
    float width=2+5*strength;
    if (now-b->updated_us>CHART_DARK_US) {
        float fade=fminf(1,(float)(now-b->updated_us-CHART_DARK_US)/(CHART_HISTORY_US-CHART_DARK_US));
        width += (1-width)*fade;
    }
    return width;
}

void chart_render_tile(const chart_model_t *m,int64_t now,int y,int rows,uint16_t *buffer)
{
    tile=buffer; tile_y=y; tile_rows=rows;
    for (int j=0; j<320*rows; ++j) tile[j]=pack(bg);
    for (int r=25;r<=74;r+=r==25 ? 24 : 25)
        for (int a=0;a<360;a+=8) arc(a,a+3,r,0.8f,grid);
    line(62,170,258,170,0.7f,grid);
    line(160,72,160,268,0.7f,grid);
    arc(0,360,98,0.9f,mix(grid,muted,0.25f));
    for (int a=0;a<360;a+=10) {
        float x1,y1,x2,y2;
        polar(a,96,&x1,&y1); polar(a,a%30 ? 99 : 102,&x2,&y2);
        line(x1,y1,x2,y2,0.9f,mix(grid,muted,0.35f));
    }
    /* Fixed antenna cone and ray, never an independent radar sweep. */
    for (int a=-10;a<=10;a+=2) {
        float x1,y1,x2,y2;
        polar(a,12,&x1,&y1); polar(a,91,&x2,&y2);
        line(x1,y1,x2,y2,2,mix(bg,dark,0.22f*(1-fabsf((float)a)/12)));
    }
    for (int i=0;i<CHART_BINS;++i) {
        const chart_bin_t *b=&m->bins[i],*next=&m->bins[(i+1)%CHART_BINS];
        if (!b->count) continue;
        float angle=chart_wrap180(b->heading-m->display_heading);
        color_t c=bin_color(b,now);
        float width=bin_width(b,now);
        if (next->count) {
            float span=chart_wrap180(next->heading-b->heading);
            if (span>0 && span<=20) {
                for (float t=0;t<span;t+=1) {
                    color_t part=mix(c,bin_color(next,now),t/span);
                    float w=width+(bin_width(next,now)-width)*t/span;
                    arc(angle+t,angle+fminf(t+1,span),88,w+4,mix(bg,part,0.08f));
                    arc(angle+t,angle+fminf(t+1,span),88,w+2,mix(bg,part,0.16f));
                    arc(angle+t,angle+fminf(t+1,span),88,w,part);
                }
            }
        }
        float px,py; polar(angle,88,&px,&py);
        disc(px,py,now-b->updated_us<=CHART_FRESH_US ? 1.8f : 0.9f,c);
    }
    if (m->peak_bin>=0) {
        const chart_bin_t *b=&m->bins[m->peak_bin];
        float a=chart_wrap180(b->heading-m->display_heading);
        color_t c=now-b->updated_us>CHART_DARK_US ? history : amber;
        arc(a-6,a+6,88,8,mix(bg,c,0.15f));
        arc(a-6,a+6,88,4,c);
        float px,py; polar(a,88,&px,&py); disc(px,py,2,mix(c,white,0.4f));
    }
    line(160,170,160,78,1,cyan);
    disc(160,170,3,cyan);
    for (int py=64;py<=75;++py) {
        int half=(py-64)/2;
        for (int px=160-half;px<=160+half;++px) pixel(px,py,cyan,15);
    }
    text(m->source[0] ? m->source : "--",12,10,20,white,185);
    char label[64];
    if (m->frequency_mhz) snprintf(label,sizeof(label),"%d MHz",m->frequency_mhz);
    else snprintf(label,sizeof(label),"-- MHz");
    text(label,12,38,14,muted,132);
    bool stale=!m->last_signal_us || now-m->last_signal_us>CHART_SIGNAL_TIMEOUT_US;
    if (m->last_signal_us) snprintf(label,sizeof(label),"%d",(int)lroundf(m->current_rssi));
    else snprintf(label,sizeof(label),"--");
    text(label,278-text_width(label,28),7,28,stale ? history : cyan,278);
    text("dBm",281,24,14,muted,318);
    if (m->mode!=LIVE_WIFI) text("模拟",12,57,14,amber,70);
    centered("天线",47,14,cyan);
    text("-90°",12,162,14,muted,59);
    text("+90°",268,162,14,muted,319);
    centered("180°",270,14,muted);
    int angle=(int)lroundf(chart_wrap180(m->heading));
    if (angle==180) angle=-180;
    snprintf(label,sizeof(label),"%+04d°",angle);
    int w=text_width("转角 ",14)+text_width(label,20);
    int left=160-w/2;
    text("转角 ",left,231,14,muted,320);
    text(label,left+text_width("转角 ",14),227,20,cyan,320);
    unsigned first=(m->trend_next+CHART_TREND_LENGTH-m->trend_used)%CHART_TREND_LENGTH;
    for (unsigned i=1;i<m->trend_used;++i) {
        chart_signal_event_t a=m->trend[(first+i-1)%CHART_TREND_LENGTH];
        chart_signal_event_t b=m->trend[(first+i)%CHART_TREND_LENGTH];
        if (b.time_us-a.time_us>350000 || now-a.time_us>4400000) continue;
        float x1=306-(now-a.time_us)/50000.0f, x2=306-(now-b.time_us)/50000.0f;
        float v1=fmaxf(0,fminf(1,(a.rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
        float v2=fmaxf(0,fminf(1,(b.rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
        line(x1,57-v1*12,x2,57-v2*12,1,stale ? history : dark);
    }
    line(67,294,253,294,3,grid);
    if (m->coverage) line(67,294,67+186.0f*m->coverage/CHART_BINS,294,3,cyan);
    const char *status="慢速转动设备";
    if (m->pose_status==CHART_IMU_ERROR) status="IMU 错误";
    else if (m->pose_status==CHART_CALIBRATING) status="静置校准";
    else if (m->tilt_deg>CHART_TILT_DEG) status="保持水平";
    else if (m->target_status==CHART_WIFI_ERROR) status="Wi-Fi 错误";
    else if (m->target_status==CHART_TARGET_MISSING) status="未发现信号";
    else if (stale || m->target_status==CHART_TARGET_SEARCH) status="等待信号";
    centered(status,302,14,muted);
}

void chart_render_test_tile(int y,int rows,uint16_t *buffer)
{
    tile=buffer; tile_y=y; tile_rows=rows;
    color_t colors[]={{255,0,0},{0,255,0},{0,0,255},white};
    for (int py=y;py<y+rows;++py)
        for (int x=0;x<320;++x) pixel(x,py,py<52 ? colors[x/80] : bg,15);
    line(0,0,319,0,1,white); line(0,319,319,319,1,white);
    line(0,0,0,319,1,white); line(319,0,319,319,1,white);
    text("TL",3,57,20,cyan,320); text("TR",287,57,20,cyan,320);
    text("BL",3,288,20,amber,320); text("BR",287,288,20,amber,320);
    centered("320 x 320",145,20,white);
    centered("天线",174,14,cyan);
}
