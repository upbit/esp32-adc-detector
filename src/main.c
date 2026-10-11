#include "chart_hardware.h"

#include <errno.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

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
    uint16_t *pixels=heap_caps_malloc(320*CHART_TILE_ROWS*2,MALLOC_CAP_DMA|MALLOC_CAP_INTERNAL);
    chart_model_t *snapshot=malloc(sizeof(*snapshot));
    if (!pixels || !snapshot) {
        free(pixels); free(snapshot);
        ESP_LOGE("lcd","tile/snapshot allocation failed"); vTaskDelete(NULL); return;
    }
    chart_render_init();
    esp_err_t err=chart_lcd_init();
    if (err!=ESP_OK) { ESP_LOGE("lcd","init: %s",esp_err_to_name(err)); free(pixels); free(snapshot); vTaskDelete(NULL); return; }
    for (int y=0;y<320;y+=CHART_TILE_ROWS) {
        int rows=320-y<CHART_TILE_ROWS ? 320-y : CHART_TILE_ROWS;
        chart_render_test_tile(y,rows,pixels);
        ESP_ERROR_CHECK(chart_lcd_tile(y,rows,pixels));
    }
    vTaskDelay(pdMS_TO_TICKS(CHART_LCD_TEST_MS));
    for (;;) {
        int64_t started=esp_timer_get_time();
        lock(); memcpy(snapshot,&model,sizeof(model)); unlock();
        for (int y=0;y<320;y+=CHART_TILE_ROWS) {
            int rows=320-y<CHART_TILE_ROWS ? 320-y : CHART_TILE_ROWS;
            chart_render_tile(snapshot,started,y,rows,pixels);
            err=chart_lcd_tile(y,rows,pixels);
            if (err!=ESP_OK) { ESP_LOGE("lcd","write: %s",esp_err_to_name(err)); break; }
            taskYIELD();
        }
        if (err==ESP_OK) atomic_fetch_add(&frames,1);
        int64_t remaining=1000000/CHART_FPS-(esp_timer_get_time()-started);
        TickType_t wait=remaining>0 ? pdMS_TO_TICKS(remaining/1000) : 0;
        vTaskDelay(wait ? wait : 1);
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
