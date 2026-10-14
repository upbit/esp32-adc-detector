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
/* Q6 distances for discs up to 8 px; the largest current radius is 5.5 px. */
static uint16_t distance_lut[4096];
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
    unsigned r=((*p >> 11) & 31)*255/31, g=((*p >> 5) & 63)*255/63, b=(*p & 31)*255/31;
    r=(r*(15-alpha)+c.r*alpha)/15;
    g=(g*(15-alpha)+c.g*alpha)/15;
    b=(b*(15-alpha)+c.b*alpha)/15;
    *p=(r>>3)<<11 | (g>>2)<<5 | (b>>3);
}

static void disc(float x, float y, float radius, color_t c)
{
    if (y+radius+1 < tile_y || y-radius-1 >= tile_y+tile_rows) return;
    int cx=(int)lroundf(x*64),cy=(int)lroundf(y*64),edge=(int)lroundf((radius+0.5f)*64);
    int left=(int)floorf(x-radius),right=(int)ceilf(x+radius);
    int top=(int)floorf(y-radius),bottom=(int)ceilf(y+radius);
    if (left<0) left=0;
    if (right>319) right=319;
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
    if (fmaxf(y1,y2)+width < tile_y || fminf(y1,y2)-width >= tile_y+tile_rows) return;
    float dx=x2-x1, dy=y2-y1;
    int steps = (int)ceilf(fmaxf(fabsf(dx),fabsf(dy)));
    if (!steps) { disc(x1,y1,width*0.5f,c); return; }
    int first=0,last=steps;
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
        return CHART_CENTER_Y+r+padding>=tile_y && CHART_CENTER_Y-r-padding<tile_y+tile_rows;
    float x,y1,y2;
    polar(start,r,&x,&y1); polar(end,r,&x,&y2);
    float top=fminf(y1,y2),bottom=fmaxf(y1,y2);
    int half=(int)floorf(start/180)+1;
    if (half*180.0f<=end) {
        if (half%2) bottom=CHART_CENTER_Y+r;
        else top=CHART_CENTER_Y-r;
    }
    return bottom+padding>=tile_y && top-padding<tile_y+tile_rows;
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
        /* Glyph rows are trimmed to inked extent; `top` restores the shared baseline. */
        int top=y+g->top;
        if (top+g->height>=tile_y && top<tile_y+tile_rows) {
            for (int py=0; py<g->height; ++py) {
                if (top+py<tile_y || top+py>=tile_y+tile_rows) continue;
                for (int px=0; px<g->width; ++px) {
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

static void centered(const char *s,int y,int size,color_t c)
{
    text(s,CHART_CENTER_X-text_width(s,size)/2,y,size,c,320);
}

void chart_render_init(void)
{
    for (unsigned j=0;j<4096;++j) distance_lut[j]=(uint16_t)sqrtf(j*64.0f);
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
    /* Fixed antenna cone and ray, never an independent radar sweep. */
    for (int a=-10;a<=10;a+=2) {
        float x1,y1,x2,y2;
        polar(a,14,&x1,&y1); polar(a,CHART_SAMPLE_R-7,&x2,&y2);
        line(x1,y1,x2,y2,2,mix(bg,dark,0.22f*(1-fabsf((float)a)/12)));
    }
    for (int i=0;i<CHART_BINS;++i) {
        const chart_bin_t *b=&m->bins[i],*next=&m->bins[(i+1)%CHART_BINS];
        if (!b->count) continue;
        float angle=chart_wrap180(b->heading-m->display_heading);
        float span=next->count ? chart_wrap180(next->heading-b->heading) : 0;
        if (span<0 || span>20) span=0;
        if (!arc_in_tile(angle,angle+span,CHART_SAMPLE_R,7)) continue;
        color_t c=bin_color(b,now);
        float width=bin_width(b,now);
        if (span>0) {
            color_t next_color=bin_color(next,now);
            float next_width=bin_width(next,now);
            for (float t=0;t<span;t+=1) {
                if (!arc_in_tile(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,7)) continue;
                color_t part=mix(c,next_color,t/span);
                float w=width+(next_width-width)*t/span;
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w+4,mix(bg,part,0.08f));
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w+2,mix(bg,part,0.16f));
                arc(angle+t,angle+fminf(t+1,span),CHART_SAMPLE_R,w,part);
            }
        }
        float px,py; polar(angle,CHART_SAMPLE_R,&px,&py);
        disc(px,py,now-b->updated_us<=CHART_FRESH_US ? 1.8f : 0.9f,c);
    }
    if (m->peak_bin>=0) {
        const chart_bin_t *b=&m->bins[m->peak_bin];
        float a=chart_wrap180(b->heading-m->display_heading);
        color_t c=now-b->updated_us>CHART_DARK_US ? history : amber;
        arc(a-6,a+6,CHART_SAMPLE_R,8,mix(bg,c,0.15f));
        arc(a-6,a+6,CHART_SAMPLE_R,4,c);
        float px,py; polar(a,CHART_SAMPLE_R,&px,&py); disc(px,py,2,mix(c,white,0.4f));
    }
    /* Antenna marker sits just outside the sample ring, straddling the outer ring. */
    line(CHART_CENTER_X,CHART_CENTER_Y,CHART_CENTER_X,CHART_CENTER_Y-CHART_SAMPLE_R-4,1,cyan);
    disc(CHART_CENTER_X,CHART_CENTER_Y,3,cyan);
    for (int py=CHART_CENTER_Y-CHART_OUTER_R-10;py<=CHART_CENTER_Y-CHART_OUTER_R+1;++py) {
        int half=(py-(CHART_CENTER_Y-CHART_OUTER_R-10))/2;
        for (int px=CHART_CENTER_X-half;px<=CHART_CENTER_X+half;++px) pixel(px,py,cyan,15);
    }
    text(m->source[0] ? m->source : "--",12,8,CHART_FONT_NAME,white,185);
    char label[64];
    if (m->frequency_mhz) snprintf(label,sizeof(label),"%d MHz",m->frequency_mhz);
    else snprintf(label,sizeof(label),"-- MHz");
    text(label,12,32,CHART_FONT_FREQ,muted,132);
    bool stale=!m->last_signal_us || now-m->last_signal_us>CHART_SIGNAL_TIMEOUT_US;
    if (m->last_signal_us) snprintf(label,sizeof(label),"%d",(int)lroundf(m->current_rssi));
    else snprintf(label,sizeof(label),"--");
    text(label,278-text_width(label,CHART_FONT_RSSI),4,CHART_FONT_RSSI,stale ? history : cyan,278);
    text("dBm",281,22,CHART_FONT_SMALL,muted,318);
    if (m->mode!=LIVE_WIFI) text("模拟",12,52,CHART_FONT_SMALL,amber,70);
    centered("天线",6,CHART_FONT_SMALL,cyan);
    text("-90°",12,CHART_CENTER_Y-10,CHART_FONT_SMALL,muted,44);
    text("+90°",276,CHART_CENTER_Y-10,CHART_FONT_SMALL,muted,319);
    int angle=(int)lroundf(chart_wrap180(m->heading));
    if (angle==180) angle=-180;
    snprintf(label,sizeof(label),"%+04d°",angle);
    /* Turn angle stays inside the dial; baselines align via the 1.16em ascent. */
    int w=text_width("转角 ",CHART_FONT_SMALL)+text_width(label,CHART_FONT_ANGLE);
    int left=CHART_CENTER_X-w/2;
    text("转角 ",left,206+CHART_ASCENT(CHART_FONT_ANGLE)-CHART_ASCENT(CHART_FONT_SMALL),
         CHART_FONT_SMALL,muted,320);
    text(label,left+text_width("转角 ",CHART_FONT_SMALL),206,CHART_FONT_ANGLE,cyan,320);
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
    /* Bottom: `180°` outside the dial, then the coverage bar, then one status line. */
    centered("180°",CHART_CENTER_Y+CHART_OUTER_R+6,CHART_FONT_SMALL,muted);
    line(67,294,253,294,3,grid);
    if (m->coverage) line(67,294,67+186.0f*m->coverage/CHART_BINS,294,3,cyan);
    const char *status="慢速转动设备";
    if (m->pose_status==CHART_IMU_ERROR) status="IMU 错误";
    else if (m->pose_status==CHART_CALIBRATING) status="静置校准";
    else if (m->tilt_deg>CHART_TILT_DEG) status="保持水平";
    else if (m->target_status==CHART_WIFI_ERROR) status="Wi-Fi 错误";
    else if (m->target_status==CHART_TARGET_MISSING) status="未发现信号";
    else if (stale || m->target_status==CHART_TARGET_SEARCH) status="等待信号";
    centered(status,303,CHART_FONT_SMALL,muted);
}

void chart_render_test_tile(int y,int rows,uint16_t *buffer)
{
    tile=buffer; tile_y=y; tile_rows=rows;
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
    tile=buffer; tile_y=y; tile_rows=rows;
    for (int py=y<299 ? 299 : y;py<y+rows;++py)
        for (int x=0;x<320;++x) pixel(x,py,bg,15);
    int x=(frame*6)%318;
    for (int dx=0;dx<3;++dx) pixel(x+dx,299,cyan,15);
    centered(label,302,14,cyan);
}

void chart_render_refresh_tile(int y,int rows,uint16_t *buffer,const char *strategy,
                              float fps,int64_t elapsed_us,unsigned frame)
{
    tile=buffer; tile_y=y; tile_rows=rows;
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
