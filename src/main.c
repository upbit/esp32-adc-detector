#include "chart_hardware.h"

#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifndef CHART_LCD_HOST_TEST
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#endif

#if !CHART_LCD_REFRESH_TEST

typedef enum { CMD_MODE, CMD_ANGLE, CMD_AUTO, CMD_SIGNAL, CMD_CLEAR, CMD_ZERO, CMD_CAL } command_type_t;
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
    bool imu_ready=false,automatic=true,signal_enabled=true;
    bool calibrated_before=false;
    float sim_heading=0,sim_display=0;
    int64_t previous=esp_timer_get_time(),last_sim=previous;
    chart_mode_t mode=CHART_DEFAULT_MODE;
    bool configure=true;
    TickType_t wake=xTaskGetTickCount();
    for (;;) {
        int64_t now=esp_timer_get_time();
        command_t cmd;
        while (xQueueReceive(commands,&cmd,0)==pdTRUE) {
            if (cmd.type==CMD_MODE) { mode=(chart_mode_t)cmd.value; configure=true; }
            else if (cmd.type==CMD_AUTO) automatic=cmd.value!=0;
            else if (cmd.type==CMD_SIGNAL) signal_enabled=cmd.value!=0;
            else if (cmd.type==CMD_ANGLE && mode==UI_SIM) {
                automatic=false;
                sim_heading += chart_wrap180(cmd.value-sim_heading);
            } else if (cmd.type==CMD_CLEAR || cmd.type==CMD_ZERO || cmd.type==CMD_CAL) {
                lock();
                if (cmd.type==CMD_CLEAR) chart_clear(&model,now);
                else {
                    chart_zero(&model,now);
                    sim_heading=sim_display=0;
                    integrator.heading=integrator.display_heading=0;
                    if (cmd.type==CMD_CAL && mode!=UI_SIM) {
                        chart_integrator_reset(&integrator);
                        model.pose_status=CHART_CALIBRATING;
                        calibrated_before=false;
                    }
                }
                unlock();
                xQueueReset(signals);
                if (cmd.type==CMD_CAL && mode!=UI_SIM) {
                    esp_err_t err=chart_imu_init();
                    imu_ready=err==ESP_OK;
                    if (!imu_ready) { lock(); model.pose_status=CHART_IMU_ERROR; unlock(); }
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
            sim_heading=sim_display=0;
            automatic=true; signal_enabled=true;
            lock();
            chart_init(&model,mode,now);
            wifi_request_t request={mode,++mode_revision};
            unlock();
            xQueueReset(signals);
            xQueueOverwrite(wifi_requests,&request);
            imu_ready=false;
            if (mode!=UI_SIM) {
                esp_err_t err=chart_imu_init();
                imu_ready=err==ESP_OK;
                if (!imu_ready) { lock(); model.pose_status=CHART_IMU_ERROR; unlock(); }
                ESP_LOGI("pose","IMU initialization: %s; no simulation fallback",esp_err_to_name(err));
            }
            now=esp_timer_get_time(); previous=last_sim=now;
            wake=xTaskGetTickCount();
            ESP_LOGI("chart","mode=%s",mode==UI_SIM ? "UI_SIM" : mode==IMU_SIM_RSSI ? "IMU_SIM_RSSI" : "LIVE_WIFI");
        }
        float dt=(now-previous)/1000000.0f;
        previous=now;
        bool ready=false;
        if (mode==UI_SIM) {
            if (automatic) sim_heading+=CHART_SIM_SPEED_DPS*dt;
            sim_display+=(1-expf(-dt/CHART_DISPLAY_TAU_S))*(sim_heading-sim_display);
            lock();
            chart_record_pose(&model,now,sim_heading,sim_display);
            unlock();
            ready=true;
        } else if (imu_ready) {
            float gyro[3],accel[3],tilt;
            esp_err_t err=chart_imu_read(gyro,accel);
            now=esp_timer_get_time();
            if (err!=ESP_OK) {
                imu_ready=false;
                lock(); model.pose_status=CHART_IMU_ERROR; chart_clear(&model,now); unlock();
                ESP_LOGE("pose","IMU read: %s; use cal to retry",esp_err_to_name(err));
            } else {
                ready=chart_integrate(&integrator,now,gyro,accel,&tilt);
                lock();
                if (ready && !calibrated_before) chart_zero(&model,now);
                if (!ready && calibrated_before) chart_zero(&model,now);
                model.pose_status=ready ? CHART_READY : CHART_CALIBRATING;
                model.tilt_deg=tilt;
                if (ready) chart_record_pose(&model,now,integrator.heading,integrator.display_heading);
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
            float heading=mode==UI_SIM ? sim_heading : integrator.heading;
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
    puts("\nhelp | mode ui / imu / live | angle <degrees> (UI_SIM only)\n"
         "auto on/off | signal on/off (synthetic only) | clear | zero | cal | stats\n"
         "UI_SIM and IMU_SIM_RSSI use synthetic RSSI; LIVE_WIFI needs configured SSID.\n"
         "clear resets samples; zero also resets relative yaw; cal retries IMU calibration.\n");
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
                unlock(); continue;
            }
            command_t cmd={0}; bool valid=true;
            if (!strcmp(line,"mode ui")) cmd=(command_t){CMD_MODE,UI_SIM};
            else if (!strcmp(line,"mode imu")) cmd=(command_t){CMD_MODE,IMU_SIM_RSSI};
            else if (!strcmp(line,"mode live")) cmd=(command_t){CMD_MODE,LIVE_WIFI};
            else if (!strcmp(line,"auto on")) cmd=(command_t){CMD_AUTO,1};
            else if (!strcmp(line,"auto off")) cmd=(command_t){CMD_AUTO,0};
            else if (!strcmp(line,"signal on")) cmd=(command_t){CMD_SIGNAL,1};
            else if (!strcmp(line,"signal off")) cmd=(command_t){CMD_SIGNAL,0};
            else if (!strcmp(line,"clear")) cmd.type=CMD_CLEAR;
            else if (!strcmp(line,"zero")) cmd.type=CMD_ZERO;
            else if (!strcmp(line,"cal")) cmd.type=CMD_CAL;
            else if (!strncmp(line,"angle ",6)) {
                char *end; errno=0;
                cmd=(command_t){CMD_ANGLE,strtof(line+6,&end)};
                valid=end!=line+6 && !*end && !errno && isfinite(cmd.value);
            } else valid=false;
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

static void render_task(void *arg)
{
    (void)arg;
    uint16_t *pixels=heap_caps_malloc(CHART_LCD_PIXEL_BYTES,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    chart_model_t *snapshot=malloc(sizeof(*snapshot));
    if (!pixels || !snapshot) {
        free(pixels); free(snapshot);
        ESP_LOGE("lcd","tile/snapshot allocation failed"); vTaskDelete(NULL); return;
    }
    chart_render_init();
    esp_err_t err=chart_lcd_init();
    if (err!=ESP_OK) { ESP_LOGE("lcd","init: %s",esp_err_to_name(err)); free(pixels); free(snapshot); vTaskDelete(NULL); return; }
#if CHART_LCD_BOOT_DIAGNOSTIC
    /* Colour/boundary check stays available but off by default; see plan P0-D. */
    ESP_LOGI("lcd","sending full-screen RGB/white boot test pattern");
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        int rows=320-y<CHART_TILE_ROWS ? 320-y : CHART_TILE_ROWS;
        chart_render_test_tile(y,rows,pixels);
        ESP_ERROR_CHECK(chart_lcd_tile(y,rows,pixels));
    }
    ESP_LOGI("lcd","test pattern sent over SPI (no panel readback); holding for %d ms",CHART_LCD_TEST_MS);
    vTaskDelay(pdMS_TO_TICKS(CHART_LCD_TEST_MS));
    ESP_LOGI("lcd","boot test finished; starting star chart");
#endif
    int64_t report_started=esp_timer_get_time(),draw_us=0,spi_us=0,frame_max_us=0;
    unsigned report_frames=0,display_frame=0;
    float display_fps=0;
    ESP_LOGI("lcd","starting star chart at %d MHz",CHART_LCD_SPI_HZ/1000000);
    for (;;) {
        int64_t started=esp_timer_get_time();
        int64_t frame_draw_us=0,frame_spi_us=0;
        lock(); memcpy(snapshot,&model,sizeof(model)); unlock();
        for (int y=0;y<320;y+=CHART_TILE_ROWS) {
            int rows=320-y<CHART_TILE_ROWS ? 320-y : CHART_TILE_ROWS;
            int64_t tile_started=esp_timer_get_time();
            chart_render_tile(snapshot,started,y,rows,pixels);
            int64_t drawn=esp_timer_get_time();
            err=chart_lcd_tile(y,rows,pixels);
            frame_draw_us+=drawn-tile_started;
            frame_spi_us+=esp_timer_get_time()-drawn;
            if (err!=ESP_OK) { ESP_LOGE("lcd","write: %s",esp_err_to_name(err)); break; }
            taskYIELD();
        }
        int64_t finished=esp_timer_get_time();
        if (err==ESP_OK) {
            atomic_fetch_add(&frames,1);
            ++display_frame;
            ++report_frames;
            draw_us+=frame_draw_us; spi_us+=frame_spi_us;
            if (finished-started>frame_max_us) frame_max_us=finished-started;
        }
        if (finished-report_started>=5000000 && report_frames) {
            display_fps=report_frames*1000000.0f/(finished-report_started);
            ESP_LOGI("lcd","perf fps=%.2f draw=%.2f ms/frame spi=%.2f ms/frame max_work=%.2f ms",
                display_fps,
                draw_us/(1000.0f*report_frames),spi_us/(1000.0f*report_frames),frame_max_us/1000.0f);
            report_started=esp_timer_get_time(); report_frames=0;
            draw_us=spi_us=frame_max_us=0;
        }
        int64_t remaining=1000000/CHART_FPS-(esp_timer_get_time()-started);
        TickType_t wait=remaining>0 ? pdMS_TO_TICKS(remaining/1000) : 0;
        if (wait || err!=ESP_OK) vTaskDelay(wait ? wait : 1);
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

#else

typedef struct { unsigned id; const char *label; bool window,pipeline,o2; } refresh_strategy_t;
static const refresh_strategy_t refresh_strategies[]={
    {14,"14 WINDOW PIPE16",true,true,false},
    {15,"15 FRAME O2",true,true,true},
    {10,"10 WIRE 80M MAX",false,false,false},
    {13,"13 WINDOW SYNC",true,false,false},
};

static bool refresh_button_pressed(int64_t now)
{
    static bool raw=true,stable=true;
    static int64_t changed;
    bool level=gpio_get_level(CHART_LCD_BUTTON)!=0;
    if (level!=raw) { raw=level; changed=now; }
    if (raw==stable || now-changed<CHART_LCD_BUTTON_DEBOUNCE_US) return false;
    stable=raw;
    return !stable;
}

static void refresh_add_timing(chart_lcd_timing_t *total,const chart_lcd_timing_t *t)
{
    total->swap_us+=t->swap_us; total->command_us+=t->command_us;
    total->dma_us+=t->dma_us; total->wait_us+=t->wait_us;
    if (t->command_khz) total->command_khz=t->command_khz;
    if (t->pixel_khz) total->pixel_khz=t->pixel_khz;
}

#define REFRESH_FRAME_FN refresh_frame
#define REFRESH_WINDOW_FN(name) chart_lcd_window_##name
#define REFRESH_FRAME_ATTR
#include "lcd_refresh_frame.inc"
#undef REFRESH_FRAME_FN
#undef REFRESH_WINDOW_FN
#undef REFRESH_FRAME_ATTR

#define REFRESH_FRAME_FN refresh_frame_o2
#define REFRESH_WINDOW_FN(name) chart_lcd_window_##name
#if defined(__GNUC__) && !defined(__clang__)
#define REFRESH_FRAME_ATTR __attribute__((optimize("O2"),noinline))
#else
#define REFRESH_FRAME_ATTR
#endif
#include "lcd_refresh_frame.inc"
#undef REFRESH_FRAME_FN
#undef REFRESH_WINDOW_FN
#undef REFRESH_FRAME_ATTR

#ifndef CHART_LCD_HOST_TEST
static void render_task(void *arg)
{
    (void)arg;
    uint16_t *pixels=heap_caps_malloc(CHART_LCD_PIXEL_BYTES,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    if (!pixels) { ESP_LOGE("lcd","tile allocation failed"); vTaskDelete(NULL); return; }
    ESP_ERROR_CHECK(chart_lcd_init());
    gpio_config_t button={.pin_bit_mask=1ULL<<CHART_LCD_BUTTON,.mode=GPIO_MODE_INPUT,
        .pull_up_en=GPIO_PULLUP_ENABLE};
    ESP_ERROR_CHECK(gpio_config(&button));
    unsigned strategy_index=0;
    const refresh_strategy_t *strategy=&refresh_strategies[0];
    bool setup=true;
    unsigned frame=0,report_frames=0;
    bool footer=false;
    float fps=0;
    int64_t epoch=esp_timer_get_time(),report_started=epoch,draw_us=0,spi_us=0,max_us=0;
    chart_lcd_timing_t totals={0};
    int64_t bytes=0;
    int64_t last_start=0,interval_min=INT64_MAX,interval_max=0;
    int64_t dynamic_us=0,dynamic_max=0;
    ESP_LOGI("lcd","P3 adapted demo (not historical baseline); BOOT cycles 14/15/10/13; command=%d MHz pixel=%d MHz dynamic=320x128; UI/IMU/Wi-Fi disabled; buffer=%d bytes",
        CHART_LCD_SPI_HZ/1000000,CHART_LCD_FAST_SPI_HZ/1000000,CHART_LCD_PIXEL_BYTES);
    for (;;) {
        if (refresh_button_pressed(esp_timer_get_time())) {
            strategy_index=(strategy_index+1)%(sizeof(refresh_strategies)/sizeof(*refresh_strategies));
            strategy=&refresh_strategies[strategy_index];
            setup=true;
        }
        if (setup) {
            setup=false;
            chart_lcd_fast_pixels(false);
            for (int y=0;y<320;y+=CHART_TILE_ROWS) {
                chart_render_refresh_tile(y,CHART_TILE_ROWS,pixels,strategy->label,0,0,0);
                ESP_ERROR_CHECK(chart_lcd_tile(y,CHART_TILE_ROWS,pixels));
            }
            chart_lcd_fast_pixels(true);
            frame=report_frames=0; footer=false; fps=0;
            epoch=report_started=esp_timer_get_time();
            last_start=0; interval_min=INT64_MAX; interval_max=0;
            draw_us=spi_us=max_us=bytes=dynamic_us=dynamic_max=0;
            totals=(chart_lcd_timing_t){0};
            ESP_LOGI("lcd","strategy=%u %s window=%d pipeline=%d hot_o2=%d rows=%d",
                strategy->id,strategy->label,strategy->window,strategy->pipeline,strategy->o2,
                strategy->window ? CHART_TILE_ROWS : CHART_LCD_REFRESH_ROWS);
        }
        int64_t started=esp_timer_get_time(),elapsed=started-epoch;
        if (last_start) {
            int64_t interval=started-last_start;
            if (interval<interval_min) interval_min=interval;
            if (interval>interval_max) interval_max=interval;
        }
        last_start=started;
        int64_t frame_draw=0,frame_spi=0;
        esp_err_t err=strategy->o2 ?
            refresh_frame_o2(strategy,pixels,elapsed,frame,&frame_draw,&frame_spi,&totals) :
            refresh_frame(strategy,pixels,elapsed,frame,&frame_draw,&frame_spi,&totals);
        if (err!=ESP_OK) {
            ESP_LOGE("lcd","strategy=%u transfer stopped: %s",strategy->id,esp_err_to_name(err));
            /* No more commands or buffer reuse after a failed/partial window. */
            vTaskDelete(NULL); return;
        }
        int64_t dynamic=esp_timer_get_time()-started;
        dynamic_us+=dynamic;
        if (dynamic>dynamic_max) dynamic_max=dynamic;
        bytes+=320*CHART_LCD_REFRESH_HEIGHT*2;
        if (footer) {
            footer=false;
            for (int y=240;y<272;y+=CHART_TILE_ROWS) {
                int64_t drawing=esp_timer_get_time();
                chart_render_refresh_tile(y,CHART_TILE_ROWS,pixels,strategy->label,fps,elapsed,frame);
                int64_t drawn=esp_timer_get_time();
                chart_lcd_timing_t timing={0};
                ESP_ERROR_CHECK(chart_lcd_tile_profiled(y,CHART_TILE_ROWS,pixels,CHART_LCD_WRITE_SWAP_O2,&timing));
                refresh_add_timing(&totals,&timing);
                frame_draw+=drawn-drawing; frame_spi+=esp_timer_get_time()-drawn;
                bytes+=320*CHART_TILE_ROWS*2;
            }
        }
        int64_t finished=esp_timer_get_time();
        ++frame; ++report_frames;
        draw_us+=frame_draw; spi_us+=frame_spi;
        if (finished-started>max_us) max_us=finished-started;
        if (finished-report_started>=1000000) {
            fps=report_frames*1000000.0f/(finished-report_started);
            ESP_LOGI("lcd","perf strategy=%u fps=%.2f draw=%.2f ms/frame spi=%.2f ms/frame max_work=%.2f ms payload=%.2f MB/s frames=%u interval=%.2f..%.2f ms swap=%.3f cmd=%.3f dma=%.3f ms/frame heap=%u min_heap=%u stack_free=%u clocks=%d/%d kHz dynamic=%.3f max_dynamic=%.3f wait=%.3f ms/frame",
                strategy->id,fps,draw_us/(1000.0f*report_frames),spi_us/(1000.0f*report_frames),
                max_us/1000.0f,bytes/(double)(finished-report_started),frame,
                interval_min==INT64_MAX ? 0 : interval_min/1000.0f,interval_max/1000.0f,
                totals.swap_us/(1000.0f*report_frames),totals.command_us/(1000.0f*report_frames),
                totals.dma_us/(1000.0f*report_frames),(unsigned)esp_get_free_heap_size(),
                (unsigned)esp_get_minimum_free_heap_size(),(unsigned)uxTaskGetStackHighWaterMark(NULL),
                totals.command_khz,totals.pixel_khz,dynamic_us/(1000.0f*report_frames),
                dynamic_max/1000.0f,totals.wait_us/(1000.0f*report_frames));
            report_started=finished; report_frames=0;
            draw_us=spi_us=max_us=bytes=0; footer=true;
            interval_min=INT64_MAX; interval_max=0;
            totals=(chart_lcd_timing_t){0};
            dynamic_us=dynamic_max=0;
        }
    }
}

void app_main(void)
{
    configASSERT(xTaskCreate(render_task,"display",6144,NULL,1,NULL)==pdPASS);
}

#endif
#endif
