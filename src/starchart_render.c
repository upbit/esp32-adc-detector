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
/* Q6 distances for discs up to CHART_DISC_MAX_R px; widest halo is (4+7)+6 wide. */
#define CHART_DISC_MAX_R 12
#define CHART_DISTANCE_LUT ((CHART_DISC_MAX_R+1)*(CHART_DISC_MAX_R+1)*64)
static uint16_t distance_lut[CHART_DISTANCE_LUT];
static uint16_t *tile;
static int tile_x, tile_y, tile_width, tile_rows;

static void set_tile(uint16_t *buffer,int x,int y,int width,int rows)
{
    tile=buffer; tile_x=x; tile_y=y; tile_width=width; tile_rows=rows;
}

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
    if (x < tile_x || x >= tile_x + tile_width || y < tile_y || y >= tile_y + tile_rows) return;
    uint16_t *p = &tile[(y-tile_y)*tile_width+(x-tile_x)];
    if (alpha >= 15) { *p = pack(c); return; }
    unsigned r=((*p >> 11) & 31)*255/31, g=((*p >> 5) & 63)*255/63, b=(*p & 31)*255/31;
    r=(r*(15-alpha)+c.r*alpha)/15;
    g=(g*(15-alpha)+c.g*alpha)/15;
    b=(b*(15-alpha)+c.b*alpha)/15;
    *p=(r>>3)<<11 | (g>>2)<<5 | (b>>3);
}

static void disc(float x, float y, float radius, color_t c)
{
    if (x+radius+1 < tile_x || x-radius-1 >= tile_x+tile_width ||
        y+radius+1 < tile_y || y-radius-1 >= tile_y+tile_rows) return;
    /* The LUT bounds the radius; clamp rather than read past it. */
    if (radius>CHART_DISC_MAX_R) radius=CHART_DISC_MAX_R;
    int cx=(int)lroundf(x*64),cy=(int)lroundf(y*64),edge=(int)lroundf((radius+0.5f)*64);
    int left=(int)floorf(x-radius),right=(int)ceilf(x+radius);
    int top=(int)floorf(y-radius),bottom=(int)ceilf(y+radius);
    if (left<tile_x) left=tile_x;
    if (right>=tile_x+tile_width) right=tile_x+tile_width-1;
    if (top<tile_y) top=tile_y;
    if (bottom>=tile_y+tile_rows) bottom=tile_y+tile_rows-1;
    for (int py=top;py<=bottom;++py) {
        int dy=py*64-cy;
        for (int px=left;px<=right;++px) {
            int dx=px*64-cx,squared=dx*dx+dy*dy;
            if (squared>=edge*edge) continue;
            unsigned coverage=edge-distance_lut[(unsigned)squared>>6];
            pixel(px,py,c,coverage>=64 ? 15 : coverage*15/64);
        }
    }
}

static void line(float x1, float y1, float x2, float y2, float width, color_t c)
{
    if (fmaxf(x1,x2)+width < tile_x || fminf(x1,x2)-width >= tile_x+tile_width ||
        fmaxf(y1,y2)+width < tile_y || fminf(y1,y2)-width >= tile_y+tile_rows) return;
    float dx=x2-x1, dy=y2-y1;
    int steps = (int)ceilf(fmaxf(fabsf(dx),fabsf(dy)));
    if (!steps) { disc(x1,y1,width*0.5f,c); return; }
    int first=0,last=steps;
    /* Clip step indices, not endpoints: retain the full-frame disc centres and order. */
    if (dx!=0) {
        float a=(tile_x-width-1-x1)*steps/dx,b=(tile_x+tile_width+width+1-x1)*steps/dx;
        if (a>b) { float swap=a; a=b; b=swap; }
        if (a>first) first=(int)ceilf(a);
        if (b<last) last=(int)floorf(b);
    }
    if (dy!=0) {
        float a=(tile_y-width-1-y1)*steps/dy,b=(tile_y+tile_rows+width+1-y1)*steps/dy;
        if (a>b) { float swap=a; a=b; b=swap; }
        if (a>first) first=(int)ceilf(a);
        if (b<last) last=(int)floorf(b);
    }
    for (int j=first; j<=last; ++j) disc(x1+dx*j/steps,y1+dy*j/steps,width*0.5f,c);
}

static void polar(float angle, float radius, float *x, float *y)
{
    int index = (int)lroundf(angle*2) % 720;
    if (index < 0) index += 720;
    *x = CHART_CENTER_X+radius*sine[index];
    *y = CHART_CENTER_Y-radius*cosine[index];
}

static bool arc_in_tile(float start,float end,float r,float padding)
{
    if (end-start>=180)
        return CHART_CENTER_X+r+padding>=tile_x && CHART_CENTER_X-r-padding<tile_x+tile_width &&
               CHART_CENTER_Y+r+padding>=tile_y && CHART_CENTER_Y-r-padding<tile_y+tile_rows;
    float x1,x2,y1,y2;
    polar(start,r,&x1,&y1); polar(end,r,&x2,&y2);
    float left=fminf(x1,x2),right=fmaxf(x1,x2);
    float top=fminf(y1,y2),bottom=fmaxf(y1,y2);
    int half=(int)floorf(start/180)+1;
    if (half*180.0f<=end) {
        if (half%2) bottom=CHART_CENTER_Y+r;
        else top=CHART_CENTER_Y-r;
    }
    half=(int)floorf((start-90)/180)+1;
    if (90+half*180.0f<=end) {
        if (half%2) left=CHART_CENTER_X-r;
        else right=CHART_CENTER_X+r;
    }
    return right+padding>=tile_x && left-padding<tile_x+tile_width &&
           bottom+padding>=tile_y && top-padding<tile_y+tile_rows;
}

static void arc(float start, float end, float r, float width, color_t c)
{
    if (!arc_in_tile(start,end,r,width+1)) return;
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

/* `ellipsis` marks the one caller (the source name) that may be shortened with "...". */
static void text_limited(const char *s, int x, int y, int size, color_t c, int limit, bool ellipsis)
{
    uint32_t code;
    while ((code=next_code(&s))) {
        const chart_glyph_t *g=glyph(code,size);
        if (!g) continue;
        if (x+g->width > limit || (ellipsis && *s && code!='.' &&
            x+g->width+text_width("...",size)>limit)) {
            if (ellipsis && code!='.' && limit-x>=text_width("...",size))
                text_limited("...",x,y,size,c,limit,false);
            return;
        }
        /* Glyph rows are trimmed to inked extent; `top` restores the shared baseline. */
        int top=y+g->top;
        if (x+g->width>tile_x && x<tile_x+tile_width &&
            top+g->height>tile_y && top<tile_y+tile_rows) {
            int from_x=tile_x>x ? tile_x-x : 0,from_y=tile_y>top ? tile_y-top : 0;
            int to_x=tile_x+tile_width-x,to_y=tile_y+tile_rows-top;
            if (to_x>g->width) to_x=g->width;
            if (to_y>g->height) to_y=g->height;
            for (int py=from_y; py<to_y; ++py) {
                for (int px=from_x; px<to_x; ++px) {
                    unsigned pos=py*g->width+px;
                    unsigned v=chart_font_data[g->offset+pos/2];
                    unsigned a=pos%2 ? v&15 : v>>4;
                    if (a) pixel(x+px,top+py,c,a);
                }
            }
        }
        x+=g->width;
    }
}

static void text(const char *s, int x, int y, int size, color_t c, int limit)
{
    text_limited(s,x,y,size,c,limit,false);
}

/* Ink bottom of a size's reference glyph, for optical baseline alignment. */
static int ink_bottom(int size)
{
    const chart_glyph_t *g=glyph('0',size);
    if (!g) g=glyph('B',size);
    if (!g) return size;
    int bottom=-1;
    for (int py=0; py<g->height; ++py)
        for (int px=0; px<g->width; ++px) {
            unsigned pos=py*g->width+px;
            unsigned v=chart_font_data[g->offset+pos/2];
            if ((pos%2 ? v&15 : v>>4) && py>bottom) bottom=py;
        }
    return g->top+(bottom<0 ? g->height-1 : bottom);
}

static void centered(const char *s,int y,int size,color_t c)
{
    text(s,CHART_CENTER_X-text_width(s,size)/2,y,size,c,320);
}

/* Ink centre of a size's digit box, so mixed-size runs can align optically. */
static int ink_centre(int size)
{
    const chart_glyph_t *g=glyph('0',size);
    if (!g) g=glyph('A',size);
    if (!g) return size/2;
    int top=g->height,bottom=-1;
    for (int py=0; py<g->height; ++py)
        for (int px=0; px<g->width; ++px) {
            unsigned pos=py*g->width+px;
            unsigned v=chart_font_data[g->offset+pos/2];
            if (pos%2 ? v&15 : v>>4) {
                if (py<top) top=py;
                if (py>bottom) bottom=py;
            }
        }
    if (bottom<0) return g->top+g->height/2;
    return g->top+(top+bottom)/2;
}

void chart_render_init(void)
{
    for (unsigned j=0;j<CHART_DISTANCE_LUT;++j) distance_lut[j]=(uint16_t)sqrtf(j*64.0f);
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
    /* Strength reads as ribbon thickness, matching the design's banded scale. */
    float width=4+7*strength;
    if (now-b->updated_us>CHART_DARK_US) {
        float fade=fminf(1,(float)(now-b->updated_us-CHART_DARK_US)/(CHART_HISTORY_US-CHART_DARK_US));
        width += (1.5f-width)*fade;
    }
    return width;
}

void chart_render_rect(const chart_model_t *m,int64_t now,int x,int y,int width,int rows,uint16_t *buffer)
{
    if (x<0 || x>=320 || y<0 || y>=320 || width<=0 || width>320-x || rows<=0 || rows>320-y) return;
    set_tile(buffer,x,y,width,rows);
    for (int j=0; j<width*rows; ++j) tile[j]=pack(bg);
    /* Inner grid rings scale with the outer ring: 0.26 and 0.47 and 0.68 of R. */
    for (int r=29;r<=86;r+=r==29 ? 28 : 29)
        for (int a=0;a<360;a+=8) arc(a,a+3,r,0.8f,grid);
    line(CHART_CENTER_X-CHART_OUTER_R+6,CHART_CENTER_Y,
         CHART_CENTER_X+CHART_OUTER_R-6,CHART_CENTER_Y,0.7f,grid);
    line(CHART_CENTER_X,CHART_CENTER_Y-CHART_OUTER_R+6,
         CHART_CENTER_X,CHART_CENTER_Y+CHART_OUTER_R-6,0.7f,grid);
    arc(0,360,CHART_OUTER_R,0.9f,mix(grid,muted,0.25f));
    for (int a=0;a<360;a+=10) {
        float x1,y1,x2,y2;
        polar(a,CHART_OUTER_R-2,&x1,&y1);
        polar(a,a%30 ? CHART_OUTER_R+1 : CHART_OUTER_R+4,&x2,&y2);
        line(x1,y1,x2,y2,0.9f,mix(grid,muted,0.35f));
    }
    /* Fixed antenna cone: a solid wedge fading outward, not a radar sweep. */
    {
        const float half_angle=11.0f, reach=CHART_SAMPLE_R-6.0f;
        int top=CHART_CENTER_Y-(int)reach-1;
        if (top<tile_y) top=tile_y;
        int bottom=CHART_CENTER_Y;
        if (bottom>=tile_y+tile_rows) bottom=tile_y+tile_rows-1;
        const float slope=tanf(half_angle*3.14159265f/180.0f);
        for (int py=top;py<=bottom;++py) {
            float dy=(float)(CHART_CENTER_Y-py);
            if (dy<0 || dy>reach) continue;
            float spread=dy*slope;
            /* Fade with distance so the wedge reads as emission, not a filled triangle. */
            float strength=1.0f-dy/reach;
            strength*=strength;
            int from=(int)floorf(CHART_CENTER_X-spread)-1,to=(int)ceilf(CHART_CENTER_X+spread)+1;
            if (from<tile_x) from=tile_x;
            if (to>=tile_x+tile_width) to=tile_x+tile_width-1;
            for (int px=from;px<=to;++px) {
                float dx=fabsf((float)px-CHART_CENTER_X);
                /* Soften both the outer edge and the centre so no hard step shows. */
                float edge=fminf(1.0f,fmaxf(0.0f,spread+0.5f-dx));
                float alpha=15.0f*0.42f*strength*edge;
                unsigned a=(unsigned)(alpha+0.5f);
                if (a) pixel(px,py,cyan,a>15 ? 15 : a);
            }
        }
    }
    for (int i=0;i<CHART_BINS;++i) {
        const chart_bin_t *b=&m->bins[i],*next=&m->bins[(i+1)%CHART_BINS];
        if (!b->count) continue;
        float angle=chart_wrap180(b->heading-m->display_heading);
        float span=next->count ? chart_wrap180(next->heading-b->heading) : 0;
        if (span<0 || span>20) span=0;
        if (!arc_in_tile(angle,angle+span,CHART_SAMPLE_R,9)) continue;
        color_t c=bin_color(b,now);
        float width=bin_width(b,now);
        if (span>0) {
            color_t next_color=bin_color(next,now);
            float next_width=bin_width(next,now);
            for (float t=0;t<span;t+=1) {
                if (!arc_in_tile(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,9)) continue;
                color_t part=mix(c,next_color,t/span);
                float w=width+(next_width-width)*t/span;
                /* Outer halo, inner halo, then a bright core: a ribbon, not a hairline. */
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w+6,mix(bg,part,0.14f));
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w+3,mix(bg,part,0.30f));
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w,part);
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w*0.42f,mix(part,white,0.28f));
            }
        }
        float px,py; polar(angle,CHART_SAMPLE_R,&px,&py);
        disc(px,py,now-b->updated_us<=CHART_FRESH_US ? 1.8f : 0.9f,c);
    }
    if (m->peak_bin>=0) {
        const chart_bin_t *b=&m->bins[m->peak_bin];
        float a=chart_wrap180(b->heading-m->display_heading);
        color_t c=now-b->updated_us>CHART_DARK_US ? history : amber;
        arc(a-6,a+6,CHART_SAMPLE_R,12,mix(bg,c,0.18f));
        arc(a-6,a+6,CHART_SAMPLE_R,7,c);
        float px,py; polar(a,CHART_SAMPLE_R,&px,&py); disc(px,py,2.6f,mix(c,white,0.5f));
    }
    /* Antenna marker sits just outside the sample ring, straddling the outer ring. */
    line(CHART_CENTER_X,CHART_CENTER_Y,CHART_CENTER_X,CHART_CENTER_Y-CHART_SAMPLE_R-4,1,cyan);
    /* Origin reads as a ring with a dark core, matching the design's hollow hub. */
    disc(CHART_CENTER_X,CHART_CENTER_Y,4.5f,cyan);
    disc(CHART_CENTER_X,CHART_CENTER_Y,2.2f,bg);
    for (int py=CHART_CENTER_Y-CHART_OUTER_R-10;py<=CHART_CENTER_Y-CHART_OUTER_R+1;++py) {
        int half=(py-(CHART_CENTER_Y-CHART_OUTER_R-10))/2;
        for (int px=CHART_CENTER_X-half;px<=CHART_CENTER_X+half;++px) pixel(px,py,cyan,15);
    }
    /* Name must stop clear of the centred 天线 marker, whatever that label measures. */
    int antenna_left=CHART_CENTER_X-text_width("天线",CHART_FONT_LABEL)/2;
    text_limited(m->source[0] ? m->source : "--",12,8,CHART_FONT_NAME,white,antenna_left-8,true);
    char label[64];
    if (m->frequency_mhz) snprintf(label,sizeof(label),"%d MHz",m->frequency_mhz);
    else snprintf(label,sizeof(label),"-- MHz");
    text(label,12,32,CHART_FONT_FREQ,muted,132);
    bool stale=!m->last_signal_us || now-m->last_signal_us>CHART_SIGNAL_TIMEOUT_US;
    if (m->last_signal_us) snprintf(label,sizeof(label),"%d",(int)lroundf(m->current_rssi));
    else snprintf(label,sizeof(label),"--");
    text(label,278-text_width(label,CHART_FONT_RSSI),4,CHART_FONT_RSSI,stale ? history : cyan,278);
    /* dBm sits on the reading's baseline, not its box. */
    text("dBm",281,4+ink_bottom(CHART_FONT_RSSI)-ink_bottom(CHART_FONT_LABEL),
         CHART_FONT_LABEL,muted,319);
    if (m->mode!=LIVE_WIFI) text("模拟",12,54,CHART_FONT_LABEL,amber,80);
    centered("天线",4,CHART_FONT_LABEL,cyan);
    text("-90°",10,CHART_CENTER_Y-11,CHART_FONT_TICK,muted,48);
    text("+90°",272,CHART_CENTER_Y-11,CHART_FONT_TICK,muted,319);
    int angle=(int)lroundf(chart_wrap180(m->heading));
    if (angle==180) angle=-180;
    snprintf(label,sizeof(label),"%+04d°",angle);
    /* Turn angle stays inside the dial; the prefix centres on the number's ink. */
    int w=text_width("转角 ",CHART_FONT_TURN)+text_width(label,CHART_FONT_ANGLE);
    int left=CHART_CENTER_X-w/2;
    text("转角 ",left,206+ink_centre(CHART_FONT_ANGLE)-ink_centre(CHART_FONT_TURN),
         CHART_FONT_TURN,muted,320);
    text(label,left+text_width("转角 ",CHART_FONT_TURN),206,CHART_FONT_ANGLE,cyan,320);
    /* Trend sparkline: clipped left so it never touches the dial's tick ring. */
    const float trend_right=306.0f, trend_base=57.0f, trend_height=12.0f;
    /* At the sparkline's lowest row the ring reaches furthest right; clear it there. */
    float ring_dy=(float)(CHART_CENTER_Y-trend_base);
    float ring_edge=(float)CHART_OUTER_R+4;
    float ring_x=ring_dy<ring_edge
        ? CHART_CENTER_X+sqrtf(ring_edge*ring_edge-ring_dy*ring_dy) : (float)CHART_CENTER_X;
    const float trend_left=ring_x+5;
    unsigned first=(m->trend_next+CHART_TREND_LENGTH-m->trend_used)%CHART_TREND_LENGTH;
    for (unsigned i=1;i<m->trend_used;++i) {
        chart_signal_event_t a=m->trend[(first+i-1)%CHART_TREND_LENGTH];
        chart_signal_event_t b=m->trend[(first+i)%CHART_TREND_LENGTH];
        if (b.time_us-a.time_us>350000) continue;
        float x1=trend_right-(now-a.time_us)/50000.0f, x2=trend_right-(now-b.time_us)/50000.0f;
        if (x2<trend_left) continue; /* Older than the window; already scrolled out. */
        float v1=fmaxf(0,fminf(1,(a.rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
        float v2=fmaxf(0,fminf(1,(b.rssi-CHART_RSSI_MIN)/(CHART_RSSI_MAX-CHART_RSSI_MIN)));
        float y1=trend_base-v1*trend_height,y2=trend_base-v2*trend_height;
        if (x1<trend_left) { /* Trim the entering segment instead of dropping it. */
            y1+=(y2-y1)*(trend_left-x1)/(x2-x1);
            x1=trend_left;
        }
        line(x1,y1,x2,y2,1,stale ? history : dark);
    }
    /* Bottom: `180°` clears the ticks, the coverage bar sits between the two lines. */
    centered("180°",276,CHART_FONT_TICK,muted);
    line(67,295,253,295,3,grid);
    if (m->coverage) line(67,295,67+186.0f*m->coverage/CHART_BINS,295,3,cyan);
    const char *status="慢速转动设备";
    if (m->pose_status==CHART_IMU_ERROR) status="IMU 错误";
    else if (m->pose_status==CHART_CALIBRATING) status="静置校准";
    else if (m->tilt_deg>CHART_TILT_DEG) status="保持水平";
    else if (m->target_status==CHART_WIFI_ERROR) status="Wi-Fi 错误";
    else if (m->target_status==CHART_TARGET_MISSING) status="未发现信号";
    else if (stale || m->target_status==CHART_TARGET_SEARCH) status="等待信号";
    centered(status,297,CHART_FONT_STATUS,muted);
}

void chart_render_tile(const chart_model_t *m,int64_t now,int y,int rows,uint16_t *buffer)
{
    chart_render_rect(m,now,0,y,320,rows,buffer);
}

/* Temporary 80 MHz colour experiment; removed with the demo in P5. Amber is the
 * only palette colour with a saturated red channel and live G/B bits, so the
 * cyan/white/muted/grid neighbours act as its controls. */
static const color_t *diag_swatch(int index)
{
    static const color_t *const swatches[CHART_DIAG_SWATCHES]=
        {&amber,&cyan,&white,&muted,&grid};
    if (index<0 || index>=CHART_DIAG_SWATCHES) return NULL;
    return swatches[index];
}

uint16_t chart_diag_swatch_word(int index)
{
    const color_t *c=diag_swatch(index);
    return c ? pack(*c) : 0;
}

const char *chart_diag_swatch_name(int index)
{
    static const char *const names[CHART_DIAG_SWATCHES]=
        {"amber","cyan","white","muted","grid"};
    if (index<0 || index>=CHART_DIAG_SWATCHES) return "?";
    return names[index];
}

/* Writes a raw RGB565 word, bypassing colour_t so single-bit values survive. */
static void raw_pixel(int x,int y,uint16_t word)
{
    if (x<tile_x || x>=tile_x+tile_width || y<tile_y || y>=tile_y+tile_rows) return;
    tile[(y-tile_y)*tile_width+(x-tile_x)]=word;
}

/* Bit ruler: block k carries only bit k, so a stream shifted by n bits shows
 * the pattern slid n blocks left. Reading it needs counting, not colour
 * judgement, which a camera's white balance cannot distort. */
void chart_render_diag_ruler(int y,int rows,uint16_t *buffer)
{
    if (y<0 || y>=320 || rows<=0 || rows>320-y) return;
    const int top=CHART_DIAG_RULER_Y,bottom=top+CHART_DIAG_RULER_ROWS;
    if (y>=bottom || y+rows<=top) return;
    set_tile(buffer,0,y,320,rows);
    int from=y>top ? y : top,to=y+rows<bottom ? y+rows : bottom;
    for (int py=from;py<to;++py)
        for (int i=0;i<16;++i) {
            /* 0x0000/0xffff are rotation fixed points: the scale stays valid
             * however far the stream has shifted. */
            uint16_t word=py<top+CHART_DIAG_RULER_SCALE ? (i%2 ? 0xffff : 0x0000)
                                                        : (uint16_t)(1u<<i);
            for (int x=i*CHART_DIAG_RULER_W;x<(i+1)*CHART_DIAG_RULER_W;++x)
                raw_pixel(x,py,word);
        }
}

/* Overlays the swatch band onto whichever tile covers it; other rows untouched. */
void chart_render_diag_swatches(int y,int rows,uint16_t *buffer,const char *label)
{
    if (y<0 || y>=320 || rows<=0 || rows>320-y) return;
    const int top=CHART_DIAG_SWATCH_Y,bottom=top+CHART_DIAG_SWATCH_ROWS;
    if (y>=bottom || y+rows<=top) return;
    set_tile(buffer,0,y,320,rows);
    int from=y>top ? y : top,to=y+rows<bottom ? y+rows : bottom;
    for (int py=from;py<to;++py) {
        for (int i=0;i<CHART_DIAG_SWATCHES;++i) {
            const color_t *c=diag_swatch(i);
            for (int x=i*CHART_DIAG_SWATCH_W;x<(i+1)*CHART_DIAG_SWATCH_W;++x)
                pixel(x,py,*c,15);
        }
        /* Keep the label area flat so no swatch is tinted by text pixels. */
        for (int x=CHART_DIAG_SWATCHES*CHART_DIAG_SWATCH_W;x<320;++x) pixel(x,py,bg,15);
    }
    if (label) text(label,CHART_DIAG_SWATCHES*CHART_DIAG_SWATCH_W+3,
                    top+CHART_DIAG_SWATCH_ROWS-2-ink_bottom(CHART_FONT_TICK),
                    CHART_FONT_TICK,white,320);
}

void chart_render_test_tile(int y,int rows,uint16_t *buffer)
{
    set_tile(buffer,0,y,320,rows);
    color_t colors[]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    color_t black={0,0,0};
    for (int py=y;py<y+rows;++py)
        for (int x=0;x<320;++x) pixel(x,py,colors[x/80],15);
    line(0,0,319,0,1,black); line(0,319,319,319,1,black);
    line(0,0,0,319,1,black); line(319,0,319,319,1,black);
    text("TL",3,57,20,black,320); text("TR",287,57,20,black,320);
    text("BL",3,288,20,black,320); text("BR",287,288,20,black,320);
    centered("320 x 320",145,20,white);
    centered("天线",174,14,cyan);
}

void chart_render_test_overlay(int y,int rows,uint16_t *buffer,const char *label,unsigned frame)
{
    set_tile(buffer,0,y,320,rows);
    for (int py=y<299 ? 299 : y;py<y+rows;++py)
        for (int x=0;x<320;++x) pixel(x,py,bg,15);
    int x=(frame*6)%318;
    for (int dx=0;dx<3;++dx) pixel(x+dx,299,cyan,15);
    centered(label,302,14,cyan);
}

void chart_render_refresh_tile(int y,int rows,uint16_t *buffer,const char *strategy,
                              float fps,int64_t elapsed_us,unsigned frame)
{
    set_tile(buffer,0,y,320,rows);
    color_t black={0,0,0}, light={220,220,220};
    color_t colors[]={{255,0,0},{0,255,0},{0,0,255},{255,255,255}};
    int position=(int)((elapsed_us/6250)%320); /* Same 160 px/s motion in every strategy. */
    for (int py=y;py<y+rows;++py) {
        int group=(py-CHART_LCD_REFRESH_Y)/64;
        int local=(py-CHART_LCD_REFRESH_Y)%64;
        int bar=group ? (160-position+320)%320 : position;
        for (int x=0;x<320;++x) {
            color_t c=black;
            if (py>=CHART_LCD_REFRESH_Y && py<CHART_LCD_REFRESH_Y+CHART_LCD_REFRESH_HEIGHT) {
                c=colors[local/16];
                if ((x-bar+320)%320<24) c=black;
                if (x>=288 && local>=16 && local<48) c=(frame+group)%2 ? light : black;
            }
            if (x==0 || x==319 || py==0 || py==319) c=light;
            pixel(x,py,c,15);
        }
    }
    centered("LCD REFRESH",8,20,white);
    centered(strategy,36,20,white);
    char label[40];
    snprintf(label,sizeof(label),"CMD %d MHz  RGB565",CHART_LCD_SPI_HZ/1000000);
    centered(label,68,14,light);
    snprintf(label,sizeof(label),"%.1f FPS",fps);
    centered(label,240,20,white);
    centered("320 x 128",288,14,light);
}

static uint16_t wire_word(uint16_t rgb565)
{
    uint8_t bytes[]={(uint8_t)(rgb565>>8),(uint8_t)rgb565};
    uint16_t word;
    memcpy(&word,bytes,sizeof(word));
    return word;
}

static void refresh_band(int y,int rows,uint16_t *buffer,int64_t elapsed_us,unsigned frame,bool wire)
{
    uint16_t colors[]={0xf800,0x07e0,0x001f,0xffff};
    uint16_t light=pack((color_t){220,220,220});
    if (wire) {
        for (unsigned i=0;i<sizeof(colors)/sizeof(*colors);++i) colors[i]=wire_word(colors[i]);
        light=wire_word(light);
    }
    int position=(int)((elapsed_us/6250)%320);
    for (int py=y;py<y+rows;++py) {
        uint16_t *row=buffer+(py-y)*320;
        if (py>y && py%16) { memcpy(row,row-320,320*sizeof(*row)); continue; }
        int group=(py-CHART_LCD_REFRESH_Y)/64,local=(py-CHART_LCD_REFRESH_Y)%64;
        int bar=group ? (160-position+320)%320 : position;
        int end=bar+24<320 ? bar+24 : 320;
        uint16_t color=colors[local/16];
        for (int x=0;x<320;++x) row[x]=color;
        for (int x=bar;x<end;++x) row[x]=0;
        for (int x=0;x<bar+24-320;++x) row[x]=0;
        if (local>=16 && local<48)
            for (int x=288;x<320;++x) row[x]=(frame+group)%2 ? light : 0;
        row[0]=row[319]=light;
    }
}

void chart_render_refresh_band(int y,int rows,uint16_t *buffer,int64_t elapsed_us,unsigned frame)
{
    refresh_band(y,rows,buffer,elapsed_us,frame,false);
}

void chart_render_refresh_band_wire(int y,int rows,uint16_t *buffer,int64_t elapsed_us,unsigned frame)
{
    refresh_band(y,rows,buffer,elapsed_us,frame,true);
}
