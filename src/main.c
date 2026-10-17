#include "chart_hardware.h"

#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef CHART_LCD_HOST_TEST
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#ifndef CHART_LCD_HOST_TEST
typedef enum { CMD_MODE, CMD_SIGNAL, CMD_CLEAR, CMD_ZERO, CMD_CAL, CMD_IMU } command_type_t;
typedef struct { command_type_t type; float value; } command_t;
typedef struct { chart_mode_t mode; unsigned revision; } wifi_request_t;
static chart_model_t model;
static SemaphoreHandle_t model_lock;
static QueueHandle_t commands, signals, wifi_requests;
static unsigned mode_revision;
static atomic_uint frames;

static void lock(void) { xSemaphoreTake(model_lock,portMAX_DELAY); }
static void unlock(void) { xSemaphoreGive(model_lock); }

static void pose_task(void *arg)
{
    (void)arg;
    chart_integrator_t integrator;
    chart_integrator_reset(&integrator);
    bool imu_ready=false,signal_enabled=true;
    bool calibrated_before=false;
    int64_t previous=esp_timer_get_time(),last_sim=previous;
    chart_mode_t mode=CHART_DEFAULT_MODE;
    bool configure=true;
    TickType_t wake=xTaskGetTickCount();
    for (;;) {
        int64_t now=esp_timer_get_time();
        command_t cmd;
        while (xQueueReceive(commands,&cmd,0)==pdTRUE) {
            if (cmd.type==CMD_MODE) { mode=(chart_mode_t)cmd.value; configure=true; }
            else if (cmd.type==CMD_SIGNAL) signal_enabled=cmd.value!=0;
            else if (cmd.type==CMD_IMU) {
                /* Raw gyro/accel dump: confirms the mount axis and the yaw sign
                 * against a physical rotation, since neither is measured. */
                float gyro[3],accel[3];
                if (chart_imu_read(gyro,accel)==ESP_OK)
                    ESP_LOGI("imu","gyro=%.3f,%.3f,%.3f dps accel=%.3f,%.3f,%.3f g yaw_rate=%.3f",
                        gyro[0],gyro[1],gyro[2],accel[0],accel[1],accel[2],
                        chart_yaw_rate(gyro,integrator.bias,accel));
                else ESP_LOGW("imu","read failed");
            } else if (cmd.type==CMD_CLEAR || cmd.type==CMD_ZERO || cmd.type==CMD_CAL) {
                lock();
                if (cmd.type==CMD_CLEAR) chart_clear(&model,now);
                else {
                    chart_zero(&model,now);
                    integrator.heading=integrator.display_heading=0;
                    if (cmd.type==CMD_CAL) {
                        chart_integrator_reset(&integrator);
                        model.pose_status=CHART_CALIBRATING;
                        model.gate_retries=0;
                        model.fatal=CHART_FATAL_NONE;
                        calibrated_before=false;
                    }
                }
                unlock();
                xQueueReset(signals);
                if (cmd.type==CMD_CAL) {
                    esp_err_t err=chart_imu_init();
                    imu_ready=err==ESP_OK;
                    lock();
                    if (!imu_ready) {
                        model.pose_status=CHART_IMU_ERROR;
                        model.fatal=err==ESP_ERR_NOT_FOUND ? CHART_FATAL_NO_IMU : CHART_FATAL_I2C;
                    }
                    unlock();
                    ESP_LOGI("pose","IMU retry: %s",esp_err_to_name(err));
                    now=esp_timer_get_time();
                    wake=xTaskGetTickCount();
                }
            }
        }
        if (configure) {
            configure=false;
            chart_integrator_reset(&integrator);
            calibrated_before=false;
            signal_enabled=true;
            lock();
            chart_init(&model,mode,now);
            wifi_request_t request={mode,++mode_revision};
            unlock();
            xQueueReset(signals);
            xQueueOverwrite(wifi_requests,&request);
            imu_ready=false;
            esp_err_t err=chart_imu_init();
            imu_ready=err==ESP_OK;
            lock();
            if (!imu_ready) {
                model.pose_status=CHART_IMU_ERROR;
                model.fatal=err==ESP_ERR_NOT_FOUND ? CHART_FATAL_NO_IMU : CHART_FATAL_I2C;
            }
            unlock();
            ESP_LOGI("pose","IMU initialization: %s; no simulation fallback",esp_err_to_name(err));
            now=esp_timer_get_time(); previous=last_sim=now;
            wake=xTaskGetTickCount();
            ESP_LOGI("chart","mode=%s",mode==IMU_SIM_RSSI ? "IMU_SIM_RSSI" : "LIVE_WIFI");
        }
        previous=now;
        bool ready=false;
        if (imu_ready) {
            float gyro[3],accel[3],tilt;
            esp_err_t err=chart_imu_read(gyro,accel);
            now=esp_timer_get_time();
            if (err!=ESP_OK) {
                /* A read that fails after a good init is still unrecoverable
                 * here: without samples the relative yaw cannot be trusted. */
                imu_ready=false;
                lock();
                model.pose_status=CHART_IMU_ERROR;
                model.fatal=CHART_FATAL_I2C;
                chart_clear(&model,now);
                unlock();
                ESP_LOGE("pose","IMU read: %s; use cal to retry",esp_err_to_name(err));
            } else {
                ready=chart_integrate(&integrator,now,gyro,accel,&tilt);
                lock();
                if (ready && !calibrated_before) chart_zero(&model,now);
                if (!ready && calibrated_before) chart_zero(&model,now);
                model.pose_status=ready ? CHART_READY : CHART_CALIBRATING;
                model.tilt_deg=tilt;
                model.level_x=integrator.level_x;
                model.level_y=integrator.level_y;
                model.still_ms=integrator.still_ms;
                model.cal_reason=integrator.reason;
                /* A rejected window repeats the guidance instead of trusting the
                 * bias, and never falls back to simulated motion. Only completed
                 * windows count; motion merely restarts the countdown. Only a
                 * sensor that cannot be read at all is fatal. */
                if (integrator.rejected && model.fatal==CHART_FATAL_NONE) {
                    if (++model.gate_retries>=CHART_MAX_GATE_RETRIES) {
                        model.fatal=CHART_FATAL_GATE;
                        ESP_LOGE("pose","calibration gate failed %d times: %s",
                            model.gate_retries,chart_cal_reason_text(integrator.reason));
                    } else {
                        ESP_LOGW("pose","calibration rejected: %s; retry %d/%d",
                            chart_cal_reason_text(integrator.reason),
                            model.gate_retries,CHART_MAX_GATE_RETRIES);
                    }
                }
                if (ready) {
                    model.gate_retries=0;
                    chart_record_pose(&model,now,integrator.heading,integrator.display_heading);
                }
                unlock();
                if (ready && !calibrated_before)
                    ESP_LOGI("pose","calibrated bias=%.4f,%.4f,%.4f dps; relative yaw only",
                        integrator.bias[0],integrator.bias[1],integrator.bias[2]);
                if (!ready && calibrated_before) ESP_LOGE("pose","acquisition gap: clear and recalibrate");
                calibrated_before=ready;
            }
        }
        if (mode!=LIVE_WIFI && ready && signal_enabled && now-last_sim>=CHART_SIM_PERIOD_US) {
            last_sim=now;
            float heading=integrator.heading;
            float noise=CHART_SIM_NOISE_DB ? CHART_SIM_NOISE_DB*((esp_random()%2001)/1000.0f-1) : 0;
            chart_signal_event_t sample={now,(int8_t)lroundf(chart_synthetic_rssi(heading)+noise),true};
            if (xQueueSend(signals,&sample,0)!=pdTRUE) ESP_LOGW("chart","synthetic queue full");
        }
        xTaskDelayUntil(&wake,pdMS_TO_TICKS(1000/CHART_IMU_HZ));
    }
}

static void signal_task(void *arg)
{
    (void)arg;
    chart_signal_event_t pending={0}; bool has_pending=false;
    for (;;) {
        wifi_request_t request;
        if (xQueueReceive(wifi_requests,&request,0)==pdTRUE) {
            has_pending=false;
            chart_wifi_pause();
            if (request.mode==LIVE_WIFI) {
                char name[33]={0}; int frequency=0;
                esp_err_t err=chart_wifi_start(signals,name,&frequency);
                lock();
                bool current=request.revision==mode_revision && model.mode==LIVE_WIFI;
                if (current) {
                    model.target_status=err==ESP_OK ? CHART_TARGET_LOCKED :
                        err==ESP_ERR_NOT_FOUND ? CHART_TARGET_MISSING : CHART_WIFI_ERROR;
                    if (err==ESP_OK) { memcpy(model.source,name,33); model.frequency_mhz=frequency; }
                }
                unlock();
                if (!current) chart_wifi_pause();
                ESP_LOGI("wifi","selection: %s",esp_err_to_name(err));
            }
        }
        if (!has_pending) has_pending=xQueueReceive(signals,&pending,1)==pdTRUE;
        if (has_pending) {
            int64_t now=esp_timer_get_time();
            lock();
            unsigned latest=(model.pose_next+CHART_POSE_HISTORY-1)%CHART_POSE_HISTORY;
            if (pending.time_us<=model.epoch_us || model.pose_status!=CHART_READY) has_pending=false;
            else if ((model.pose_used && pending.time_us<=model.poses[latest].time_us) ||
                     now-pending.time_us>CHART_MAX_POSE_GAP_US) {
                chart_record_signal(&model,pending,now);
                has_pending=false;
            }
            unlock();
        }
        vTaskDelay(1); /* At least one tick; sub-tick waits would starve display. */
    }
}

static void help(void)
{
    puts("\nhelp | mode imu / live | signal on/off | clear | zero | cal | imu | stats\n"
         "IMU_SIM_RSSI uses real MPU6050 yaw with synthetic RSSI; LIVE_WIFI needs an SSID.\n"
         "There is no simulated pose: without a readable MPU6050 the panel shows the\n"
         "fatal screen instead of inventing motion.\n"
         "clear resets samples; zero also resets relative yaw; cal re-runs calibration;\n"
         "imu dumps raw gyro/accel and the projected yaw rate for axis/sign checks.\n");
}

static void serial_task(void *arg)
{
    (void)arg;
    char line[96]; unsigned used=0; bool overflow=false;
    help();
    for (;;) {
        int c=getchar();
        if (c==EOF) { clearerr(stdin); vTaskDelay(pdMS_TO_TICKS(20)); continue; }
        if (c=='\r' || c=='\n') {
            if (overflow) { puts("Command too long"); used=0; overflow=false; continue; }
            line[used]=0; used=0;
            if (!line[0]) continue;
            if (!strcmp(line,"help")) { help(); continue; }
            if (!strcmp(line,"stats")) {
                lock();
                ESP_LOGI("stats","heading=%.3f display=%.3f tilt=%.1f bins=%d/%d packets=%lu rejected=%lu age_max=%lld us",
                    model.heading,model.display_heading,model.tilt_deg,model.coverage,CHART_BINS,
                    (unsigned long)model.samples,(unsigned long)model.sync_rejected,(long long)model.max_event_age_us);
                ESP_LOGI("stats","pose=%d fatal=%d(%s) reason=%s still_ms=%d retries=%d level=%.3f,%.3f",
                    model.pose_status,model.fatal,chart_fatal_text(model.fatal),
                    chart_cal_reason_text(model.cal_reason),model.still_ms,model.gate_retries,
                    model.level_x,model.level_y);
                unlock(); continue;
            }
            command_t cmd={0}; bool valid=true;
            if (!strcmp(line,"mode imu")) cmd=(command_t){CMD_MODE,IMU_SIM_RSSI};
            else if (!strcmp(line,"mode live")) cmd=(command_t){CMD_MODE,LIVE_WIFI};
            else if (!strcmp(line,"signal on")) cmd=(command_t){CMD_SIGNAL,1};
            else if (!strcmp(line,"signal off")) cmd=(command_t){CMD_SIGNAL,0};
            else if (!strcmp(line,"clear")) cmd.type=CMD_CLEAR;
            else if (!strcmp(line,"zero")) cmd.type=CMD_ZERO;
            else if (!strcmp(line,"cal")) cmd.type=CMD_CAL;
            else if (!strcmp(line,"imu")) cmd.type=CMD_IMU;
            else valid=false;
            if (!valid) puts("Unknown command; type help");
            else if (xQueueSend(commands,&cmd,pdMS_TO_TICKS(100))!=pdTRUE) puts("Command queue busy");
            else puts("OK");
        } else if (c==8 || c==127) { if (used) --used; }
        else if (!overflow) {
            if (used<sizeof(line)-1) line[used++]=(char)c;
            else overflow=true;
        }
    }
}

#endif

typedef struct { int x,y,width,rows; } display_rect_t;
typedef struct {
    const chart_dirty_mask_t *mask;
    int row,col;
    bool bounded,done;
    display_rect_t bounds;
} display_rect_iterator_t;
typedef struct {
    chart_model_t snapshot;
    int64_t time;
    bool valid;
} displayed_state_t;
typedef struct {
    int64_t draw_us,overlap_draw_us,work_us;
    size_t bytes;
    unsigned windows;
    bool fallback,full;
    chart_lcd_timing_t lcd;
} display_work_t;

static bool dirty_cell(const chart_dirty_mask_t *mask,int row,int col)
{
    unsigned bit=(unsigned)(row*CHART_DIRTY_COLS+col);
    return (mask->bits[bit/8]&(1U<<(bit%8)))!=0;
}

static bool display_next_rect(display_rect_iterator_t *it,display_rect_t *rect)
{
    if (it->bounded) {
        if (it->done) return false;
        *rect=it->bounds; it->done=true; return true;
    }
    for (;it->row<CHART_DIRTY_ROWS;++it->row,it->col=0) {
        while (it->col<CHART_DIRTY_COLS && !dirty_cell(it->mask,it->row,it->col)) ++it->col;
        if (it->col==CHART_DIRTY_COLS) continue;
        int first=it->col,last=first;
        for (int col=first+1;col<CHART_DIRTY_COLS && col<=last+2;++col)
            if (dirty_cell(it->mask,it->row,col)) last=col;
        it->col=last+1;
        *rect=(display_rect_t){first*CHART_DIRTY_CELL,it->row*CHART_DIRTY_CELL,
                              (last-first+1)*CHART_DIRTY_CELL,CHART_DIRTY_CELL};
        return true;
    }
    return false;
}

static display_rect_iterator_t display_rect_plan(const chart_dirty_mask_t *mask)
{
    display_rect_iterator_t plan={.mask=mask},scan=plan;
    display_rect_t rect,bounds={320,320,0,0};
    unsigned cost=0;
    int right=0,bottom=0;
    while (display_next_rect(&scan,&rect)) {
        cost+=(unsigned)(rect.width*rect.rows)+420;
        if (rect.x<bounds.x) bounds.x=rect.x;
        if (rect.y<bounds.y) bounds.y=rect.y;
        if (rect.x+rect.width>right) right=rect.x+rect.width;
        if (rect.y+rect.rows>bottom) bottom=rect.y+rect.rows;
    }
    if (cost) {
        bounds.width=right-bounds.x; bounds.rows=bottom-bounds.y;
        if ((unsigned)(bounds.width*bounds.rows)+420<cost) {
            plan.bounded=true; plan.bounds=bounds;
        }
    }
    return plan;
}

#if CHART_RENDER_BENCHMARK
/* The renderer must not include platform headers, so it calls back for time. */
int64_t chart_bench_now(void) { return esp_timer_get_time(); }
#endif

static void display_draw(const chart_model_t *snapshot,int64_t now,display_rect_t rect,
                         int offset,int rows,uint16_t *pixels,bool in_flight,display_work_t *work)
{
    int64_t started=esp_timer_get_time();
    chart_render_rect(snapshot,now,rect.x,rect.y+offset,rect.width,rows,pixels);
#if CHART_LCD_SHIFT_DIAGNOSTIC
    /* Overlay inside the rectangle's own buffer, using rectangle-local rows, so
     * the bands travel the same pixel path as everything else. */
    chart_render_diag_ruler(rect.x,rect.y+offset,rect.width,rows,pixels);
    chart_render_diag_swatches(rect.x,rect.y+offset,rect.width,rows,pixels,NULL);
#endif
    int64_t elapsed=esp_timer_get_time()-started;
    work->draw_us+=elapsed;
    if (in_flight) work->overlap_draw_us+=elapsed;
}

static esp_err_t display_reclaim(uint16_t *pixels,bool busy[2])
{
    uint16_t *completed=NULL;
    esp_err_t err=chart_lcd_window_wait(&completed);
    if (err!=ESP_OK) return err;
    for (unsigned slot=0;slot<2;++slot)
        if (completed==pixels+slot*CHART_LCD_BUFFER_PIXELS && busy[slot]) {
            busy[slot]=false; return ESP_OK;
        }
    return ESP_ERR_INVALID_STATE;
}

static esp_err_t display_update(displayed_state_t *displayed,const chart_model_t *snapshot,
                                int64_t now,uint16_t *pixels,display_work_t *work)
{
    int64_t started=esp_timer_get_time();
    *work=(display_work_t){0};
    work->full=!displayed->valid || displayed->snapshot.mode!=snapshot->mode ||
               displayed->snapshot.epoch_us!=snapshot->epoch_us;
    chart_dirty_mask_t mask;
    chart_mark_dirty(displayed->valid ? &displayed->snapshot : NULL,displayed->time,snapshot,now,&mask);
    display_rect_iterator_t plan=display_rect_plan(&mask);
    display_rect_t rect,next;
    if (!display_next_rect(&plan,&rect)) { vTaskDelay(1); return ESP_OK; }
    work->fallback=plan.bounded;
    bool busy[2]={false,false};
    unsigned slot=0;
    int prepared=rect.rows<CHART_TILE_ROWS ? rect.rows : CHART_TILE_ROWS;
    display_draw(snapshot,now,rect,0,prepared,pixels,false,work);
    esp_err_t err=ESP_OK;
    for (;;) {
        err=chart_lcd_window_begin(rect.x,rect.y,rect.width,rect.rows);
        if (err!=ESP_OK) return err;
        int offset=0;
        while (err==ESP_OK) {
            uint16_t *buffer=pixels+slot*CHART_LCD_BUFFER_PIXELS;
            err=chart_lcd_window_submit(buffer,prepared,offset+prepared==rect.rows);
            if (err!=ESP_OK) break;
            busy[slot]=true;
            work->bytes+=(size_t)rect.width*prepared*2;
            offset+=prepared;
            slot=1-slot;
            if (offset==rect.rows) break;
            if (busy[slot]) err=display_reclaim(pixels,busy);
            if (err!=ESP_OK) break;
            prepared=rect.rows-offset<CHART_TILE_ROWS ? rect.rows-offset : CHART_TILE_ROWS;
            display_draw(snapshot,now,rect,offset,prepared,pixels+slot*CHART_LCD_BUFFER_PIXELS,
                         busy[0] || busy[1],work);
        }
        bool more=false;
        if (err==ESP_OK) {
            more=display_next_rect(&plan,&next);
            if (more) {
                if (busy[slot]) err=display_reclaim(pixels,busy);
                if (err==ESP_OK) {
                    prepared=next.rows<CHART_TILE_ROWS ? next.rows : CHART_TILE_ROWS;
                    display_draw(snapshot,now,next,0,prepared,pixels+slot*CHART_LCD_BUFFER_PIXELS,
                                 busy[0] || busy[1],work);
                }
            }
        }
        chart_lcd_timing_t timing={0};
        esp_err_t ended=chart_lcd_window_end(&timing);
        if (err==ESP_OK) err=ended;
        if (err!=ESP_OK) return err;
        busy[0]=busy[1]=false;
        ++work->windows;
        work->lcd.swap_us+=timing.swap_us; work->lcd.command_us+=timing.command_us;
        work->lcd.dma_us+=timing.dma_us; work->lcd.wait_us+=timing.wait_us;
        work->lcd.command_khz=timing.command_khz; work->lcd.pixel_khz=timing.pixel_khz;
        if (!more) break;
        rect=next;
    }
    memcpy(&displayed->snapshot,snapshot,sizeof(*snapshot));
    displayed->time=now; displayed->valid=true;
    work->work_us=esp_timer_get_time()-started;
    return ESP_OK;
}

#if CHART_LCD_BOOT_DIAGNOSTIC
static esp_err_t display_boot_pattern(uint16_t *pixels)
{
    chart_lcd_fast_pixels(false);
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        int rows=320-y<CHART_TILE_ROWS ? 320-y : CHART_TILE_ROWS;
        chart_render_test_tile(y,rows,pixels);
        esp_err_t err=chart_lcd_window_begin(0,y,320,rows);
        if (err!=ESP_OK) return err;
        err=chart_lcd_window_submit(pixels,rows,true);
        esp_err_t ended=chart_lcd_window_end(NULL);
        if (err!=ESP_OK) return err;
        if (ended!=ESP_OK) return ended;
    }
    return ESP_OK;
}
#endif

#ifndef CHART_LCD_HOST_TEST
static void render_task(void *arg)
{
    (void)arg;
    uint16_t *pixels=heap_caps_malloc(CHART_LCD_PIXEL_BYTES,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    chart_model_t *snapshot=malloc(sizeof(*snapshot));
    displayed_state_t *displayed=calloc(1,sizeof(*displayed));
    if (!pixels || !snapshot || !displayed) {
        free(pixels); free(snapshot); free(displayed);
        ESP_LOGE("lcd","tile/snapshot allocation failed"); vTaskDelete(NULL); return;
    }
    chart_render_init();
    esp_err_t err=chart_lcd_init();
    if (err!=ESP_OK) { ESP_LOGE("lcd","init: %s",esp_err_to_name(err)); free(pixels); free(snapshot); free(displayed); vTaskDelete(NULL); return; }
#if CHART_LCD_BOOT_DIAGNOSTIC
    /* Colour/boundary check stays available but off by default; see plan P0-D. */
    ESP_LOGI("lcd","sending full-screen RGB/white boot test pattern");
    err=display_boot_pattern(pixels);
    if (err!=ESP_OK) {
        ESP_LOGE("lcd","diagnostic stopped: %s; no pixel reuse",esp_err_to_name(err));
        free(snapshot); free(displayed);
        vTaskDelete(NULL); return;
    }
    ESP_LOGI("lcd","test pattern sent over SPI (no panel readback); holding for %d ms",CHART_LCD_TEST_MS);
    vTaskDelay(pdMS_TO_TICKS(CHART_LCD_TEST_MS));
    ESP_LOGI("lcd","boot test finished; starting star chart");
#endif
    chart_lcd_fast_pixels(true);
    int64_t report_started=esp_timer_get_time(),last_update=0,interval_max=0,frame_max=0;
    unsigned updates=0,idle=0,fallbacks=0,intervals[17]={0};
    unsigned full_updates=0;
    int64_t full_work=0,partial_work=0,partial_max=0;
    display_work_t totals={0};
    ESP_LOGI("lcd","starting dirty star chart; command=%d MHz pixel=%d MHz pixels=%d snapshot=%u displayed=%u stack=6144",
        CHART_LCD_SPI_HZ/1000000,CHART_LCD_FAST_SPI_HZ/1000000,CHART_LCD_PIXEL_BYTES,
        (unsigned)sizeof(*snapshot),(unsigned)sizeof(*displayed));
#if CHART_RENDER_BENCHMARK
    {   /* Attribute the draw cost once, on a populated snapshot, before the
         * loop starts competing with pose/signal tasks for the CPU. */
        lock(); memcpy(snapshot,&model,sizeof(model)); unlock();
        chart_render_cost_t cost;
        chart_render_benchmark(snapshot,esp_timer_get_time(),pixels,&cost);
        ESP_LOGW("bench","320x32 band: clear=%lld static=%lld bins=%lld peak=%lld trend=%lld text=%lld us; whole scene=%lld us; sum_parts=%lld us",
            (long long)cost.clear_us,(long long)cost.static_us,(long long)cost.bins_us,
            (long long)cost.peak_us,(long long)cost.trend_us,(long long)cost.text_us,
            (long long)cost.scene_us,
            (long long)(cost.static_us+cost.bins_us+cost.peak_us+cost.trend_us+cost.text_us));
        ESP_LOGW("bench","a 320x320 frame is 10 such bands: scene would be ~%lld us if cost scaled with area",
            (long long)(cost.scene_us*10));
        ESP_LOGW("bench","fixed cost of one chart_render_rect call (1x1) = %lld us; a 25-rect dirty frame pays this 25x = %lld us",
            (long long)cost.fixed_call_us,(long long)(cost.fixed_call_us*25));
        ESP_LOGW("bench","primitives per 4096 ops: float_mul=%lld float_div=%lld sqrtf=%lld sinf=%lld int_muldiv=%lld us (no FPU: RVFPLIB)",
            (long long)cost.float_mul_us,(long long)cost.float_div_us,(long long)cost.sqrtf_us,
            (long long)cost.sinf_us,(long long)cost.int_muldiv_us);
        ESP_LOGW("bench","ns per op: float_mul=%lld float_div=%lld sqrtf=%lld sinf=%lld int_muldiv=%lld (sink=%d)",
            (long long)(cost.float_mul_us*1000/4096),(long long)(cost.float_div_us*1000/4096),
            (long long)(cost.sqrtf_us*1000/4096),(long long)(cost.sinf_us*1000/4096),
            (long long)(cost.int_muldiv_us*1000/4096),(int)cost.sink);
    }
#endif
    for (;;) {
        int64_t started=esp_timer_get_time();
        lock(); memcpy(snapshot,&model,sizeof(model)); unlock();
        display_work_t work;
        err=display_update(displayed,snapshot,started,pixels,&work);
        if (err!=ESP_OK) {
            ESP_LOGE("lcd","display stopped: %s; no displayed-state commit or pixel reuse",esp_err_to_name(err));
            free(snapshot); free(displayed);
            /* Pixel ownership can remain unknown: retain the DMA allocation. */
            vTaskDelete(NULL); return;
        }
        int64_t finished=esp_timer_get_time();
        if (work.windows) {
            work.work_us=finished-started;
            atomic_fetch_add(&frames,1);
            if (last_update) {
                int64_t interval=started-last_update;
                unsigned bucket=interval/1000<16 ? (unsigned)(interval/1000) : 16;
                ++intervals[bucket];
                if (interval>interval_max) interval_max=interval;
            }
            last_update=started; ++updates; fallbacks+=work.fallback;
            if (work.full) { ++full_updates; full_work+=work.work_us; }
            else {
                partial_work+=work.work_us;
                if (work.work_us>partial_max) partial_max=work.work_us;
            }
            totals.bytes+=work.bytes; totals.windows+=work.windows;
            totals.draw_us+=work.draw_us; totals.overlap_draw_us+=work.overlap_draw_us;
            totals.work_us+=work.work_us;
            totals.lcd.swap_us+=work.lcd.swap_us; totals.lcd.command_us+=work.lcd.command_us;
            totals.lcd.dma_us+=work.lcd.dma_us; totals.lcd.wait_us+=work.lcd.wait_us;
            totals.lcd.command_khz=work.lcd.command_khz; totals.lcd.pixel_khz=work.lcd.pixel_khz;
            if (work.work_us>frame_max) frame_max=work.work_us;
        } else ++idle;
        if (finished-report_started>=5000000) {
            unsigned count=0,p50=0,p95=0,cumulative=0;
            for (unsigned i=0;i<17;++i) count+=intervals[i];
            for (unsigned i=0;i<17;++i) {
                cumulative+=intervals[i];
                if (count && cumulative>=(count+1)/2 && !p50) p50=i+1;
                if (count && cumulative>=(count*95+99)/100 && !p95) p95=i+1;
            }
            double scale=updates ? 1000.0*updates : 1;
            int64_t transfer=totals.lcd.dma_us+totals.lcd.command_us;
            ESP_LOGI("lcd","dirty updates=%u idle=%u fps=%.2f bytes/update=%.0f windows/update=%.2f fallback=%u draw=%.3f swap=%.3f cmd=%.3f dma_span=%.3f wait=%.3f work=%.3f max_work=%.3f ms pending_draw=%.3f draw+span=%.3f max(draw,span)=%.3f ms interval_hist_p50=%u p95=%u (17=>=16ms) interval_max=%.3f ms clocks=%d/%d kHz heap=%u min_heap=%u stack_free=%u",
                updates,idle,updates*1000000.0/(finished-report_started),
                updates ? (double)totals.bytes/updates : 0,updates ? (double)totals.windows/updates : 0,fallbacks,
                totals.draw_us/scale,totals.lcd.swap_us/scale,totals.lcd.command_us/scale,
                totals.lcd.dma_us/scale,totals.lcd.wait_us/scale,totals.work_us/scale,frame_max/1000.0,
                totals.overlap_draw_us/scale,(totals.draw_us+transfer)/scale,
                (totals.draw_us>transfer ? totals.draw_us : transfer)/scale,p50,p95,interval_max/1000.0,
                totals.lcd.command_khz,totals.lcd.pixel_khz,(unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL));
            ESP_LOGI("lcd","dirty budgets full=%u mean=%.3f ms partial=%u mean=%.3f max=%.3f ms target=8.333 ms; dma_span includes overlapping draw, not pure wire time",
                full_updates,full_updates ? full_work/(1000.0*full_updates) : 0,updates-full_updates,
                updates>full_updates ? partial_work/(1000.0*(updates-full_updates)) : 0,partial_max/1000.0);
            report_started=esp_timer_get_time(); updates=idle=fallbacks=0; totals=(display_work_t){0};
            memset(intervals,0,sizeof(intervals)); interval_max=frame_max=0;
            full_updates=0; full_work=partial_work=partial_max=0;
        }
    }
}

void app_main(void)
{
    model_lock=xSemaphoreCreateMutex();
    commands=xQueueCreate(12,sizeof(command_t));
    signals=xQueueCreate(CHART_WIFI_QUEUE_LENGTH,sizeof(chart_signal_event_t));
    wifi_requests=xQueueCreate(1,sizeof(wifi_request_t));
    configASSERT(model_lock && commands && signals && wifi_requests);
    chart_init(&model,CHART_DEFAULT_MODE,esp_timer_get_time());
    configASSERT(xTaskCreate(pose_task,"pose",4096,NULL,6,NULL)==pdPASS);
    configASSERT(xTaskCreate(signal_task,"signals",6144,NULL,4,NULL)==pdPASS);
    configASSERT(xTaskCreate(serial_task,"console",4096,NULL,2,NULL)==pdPASS);
    configASSERT(xTaskCreate(render_task,"display",6144,NULL,1,NULL)==pdPASS);
    int64_t previous=esp_timer_get_time(); uint32_t last_frames=0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        int64_t now=esp_timer_get_time();
        uint32_t count=atomic_load(&frames);
        ESP_LOGI("health","fps=%.2f heap=%u min_heap=%u queue=%u dropped=%lu uptime=%lld s",
            (count-last_frames)*1000000.0f/(now-previous),
            (unsigned)esp_get_free_heap_size(),(unsigned)esp_get_minimum_free_heap_size(),
            (unsigned)uxQueueMessagesWaiting(signals),(unsigned long)chart_wifi_dropped(),(long long)(now/1000000));
        lock();
        ESP_LOGI("health","heading=%.3f samples=%lu sync_rejected=%lu event_age_max=%lld us data_age=%lld ms",
            model.heading,(unsigned long)model.samples,(unsigned long)model.sync_rejected,
            (long long)model.max_event_age_us,
            model.last_signal_us ? (long long)((now-model.last_signal_us)/1000) : -1LL);
        unlock();
        last_frames=count; previous=now;
    }
}

#endif
