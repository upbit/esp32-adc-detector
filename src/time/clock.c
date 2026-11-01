#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "esp_netif_sntp.h"
#include "esp_log.h"
#include "clock.h"

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
static bool notified, running, valid;
static clock_feedback_t feedback;
static int64_t feedback_until;

static bool plausible(time_t value)
{
    return value >= 1704067200LL && value < 4102444800LL; // 2024 through 2099.
}

static void synchronized(struct timeval *tv)
{
    if (!plausible(tv->tv_sec)) return;
    portENTER_CRITICAL(&lock);
    notified = true;
    portEXIT_CRITICAL(&lock);
}

void clock_init(void)
{
    setenv("TZ", "CST-8", 1);
    tzset();
    valid = plausible(time(NULL));
    feedback = CLOCK_NORMAL;
    feedback_until = 0;
}

esp_err_t clock_start(void)
{
    clock_stop();
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG_MULTIPLE(
        3, ESP_SNTP_SERVER_LIST("ntp.aliyun.com", "ntp.tencent.com", "time.cloudflare.com"));
    config.wait_for_sync = false;
    config.sync_cb = synchronized;
    esp_err_t err = esp_netif_sntp_init(&config);
    running = err == ESP_OK;
    return err;
}

void clock_stop(void)
{
    if (running) esp_netif_sntp_deinit();
    running = false;
    portENTER_CRITICAL(&lock);
    notified = false;
    portEXIT_CRITICAL(&lock);
}

bool clock_poll(int64_t now_us)
{
    portENTER_CRITICAL(&lock);
    bool done = notified;
    notified = false;
    portEXIT_CRITICAL(&lock);
    if (!running || !done) return false;
    valid = true;
    feedback = CLOCK_SYNCED;
    feedback_until = now_us + 2000000;
    ESP_LOGI("clock", "SNTP synchronized; green time feedback for 2 seconds");
    return true;
}

void clock_fail(int64_t now_us)
{
    feedback = CLOCK_FAILED;
    feedback_until = now_us + 2000000;
    ESP_LOGW("clock", "Time sync failed; previous valid time %s", valid ? "retained" : "unavailable");
}

void clock_get_view(int64_t now_us, clock_view_t *view)
{
    *view = (clock_view_t){.valid = valid,
        .feedback = now_us < feedback_until ? feedback : CLOCK_NORMAL};
    if (valid) {
        time_t now = time(NULL);
        view->timestamp = now;
        struct tm local;
        localtime_r(&now, &local);
        strftime(view->text, sizeof(view->text), "%H:%M", &local);
    }
}
