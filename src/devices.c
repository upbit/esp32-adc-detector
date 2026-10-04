#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "devices.h"
#include "oui.h"

#define MAX_DEVICES 512

static device_t devs[MAX_DEVICES];
static int dev_count;
static int dropped;
static int round_no;
static int baseline_round;  // 0 表示还没有基线
static devices_summary_t summary;

static const char *const type_names[DEV_TYPE_NUM] = {"Wi-Fi AP", "Wi-Fi STA", "BLE"};
static const uint8_t zero_mac[6];

static device_t *find(dev_type_t type, const uint8_t mac[6])
{
    for (int i = 0; i < dev_count; i++) {
        if (devs[i].type == type && memcmp(devs[i].mac, mac, 6) == 0) {
            return &devs[i];
        }
    }
    return NULL;
}

void devices_begin_round(void)
{
    int n = 0;
    for (int i = 0; i < dev_count; i++) {
        if (!devs[i].in_baseline) {
            continue;
        }
        devs[n] = devs[i];
        devs[n].seen = false;
        devs[n].rssi = INT8_MIN;
        devs[n].pkts = 0;
        n++;
    }
    dev_count = n;
    dropped = 0;
    round_no++;
}

device_t *devices_update(dev_type_t type, const uint8_t mac[6], int8_t rssi)
{
    device_t *d = find(type, mac);
    if (!d) {
        if (dev_count == MAX_DEVICES) {
            dropped++;
            return NULL;
        }
        d = &devs[dev_count++];
        memset(d, 0, sizeof(*d));
        d->type = type;
        memcpy(d->mac, mac, 6);
        d->rssi = INT8_MIN;
    }
    d->seen = true;
    d->pkts++;
    if (rssi > d->rssi) {
        d->rssi = rssi;
    }
    return d;
}

void devices_set_name(device_t *d, const uint8_t *name, int len)
{
    if (d->name[0] || len <= 0 || name[0] == 0) {
        return;
    }
    if (len > (int)sizeof(d->name) - 1) {
        len = sizeof(d->name) - 1;
    }
    for (int i = 0; i < len; i++) {
        d->name[i] = name[i] < 0x20 ? '?' : name[i];
    }
    d->name[len] = 0;
}

static int cmp_dev(const void *a, const void *b)
{
    const device_t *x = *(device_t *const *)a, *y = *(device_t *const *)b;
    if (x->seen != y->seen) {
        return y->seen - x->seen;
    }
    return y->rssi - x->rssi;
}

static void format_mac(char *buf, const uint8_t mac[6])
{
    sprintf(buf, "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

const char *devices_tag(const device_t *d)
{
    if (!baseline_round) {
        return "";
    }
    return !d->seen ? "GONE" : !d->in_baseline ? "NEW" : "";
}

static void print_dev(const device_t *d)
{
    const char *tag = devices_tag(d);

    char mac[18], rssi[5] = "-";
    format_mac(mac, d->mac);
    if (d->seen) {
        sprintf(rssi, "%d", d->rssi);
    }

    const char *vendor = d->random ? "(random)" : oui_lookup(d->mac);

    printf("%-4s  %s  %2u  %4s  %5lu  %-10s  ", tag, mac, d->channel, rssi, (unsigned long)d->pkts,
           vendor ? vendor : "");
    if (d->type == DEV_WIFI_STA) {
        if (memcmp(d->ap, zero_mac, 6) != 0) {
            char ap[18];
            format_mac(ap, d->ap);
            const device_t *a = find(DEV_WIFI_AP, d->ap);
            printf("-> %s %s", ap, a ? a->name : "");
        }
    } else if (d->type == DEV_WIFI_AP && !d->name[0]) {
        printf("<hidden>");
    } else {
        printf("%s", d->name);
    }
    printf("\n");
}

void devices_end_round(void)
{
    static device_t *sorted[MAX_DEVICES];

    summary = (devices_summary_t){.round_no = round_no, .baseline_round = baseline_round};
    if (baseline_round) {
        printf("\n==== Round %d vs baseline (round %d) ====\n", round_no, baseline_round);
    } else {
        printf("\n==== Round %d (baseline) ====\n", round_no);
    }

    for (int t = 0; t < DEV_TYPE_NUM; t++) {
        int n = 0;
        for (int i = 0; i < dev_count; i++) {
            if (devs[i].type == t) {
                sorted[n++] = &devs[i];
            }
        }
        qsort(sorted, n, sizeof(sorted[0]), cmp_dev);

        summary.count[t] = n;
        printf("\n-- %s (%d) --\n", type_names[t], n);
        printf("%-4s  %-17s  %2s  %4s  %5s  %-10s  %s\n", "", "MAC", "CH", "RSSI", "PKTS", "VENDOR", "NAME");
        for (int i = 0; i < n; i++) {
            print_dev(sorted[i]);
            summary.new_count += sorted[i]->seen && !sorted[i]->in_baseline;
            summary.gone_count += !sorted[i]->seen;
        }
    }

    if (baseline_round) {
        printf("\nNEW: %d  GONE: %d\n", summary.new_count, summary.gone_count);
    } else {
        summary.new_count = 0;
    }
    if (dropped) {
        printf("table full, %d devices dropped\n", dropped);
    }

    if (!baseline_round) {
        for (int i = 0; i < dev_count; i++) {
            devs[i].in_baseline = true;
        }
        baseline_round = round_no;
    }
}

void devices_clear_baseline(void)
{
    for (int i = 0; i < dev_count; i++) {
        devs[i].in_baseline = false;
    }
    baseline_round = 0;
}

const devices_summary_t *devices_summary(void)
{
    return &summary;
}

static int rank(const device_t *d)
{
    const char *tag = devices_tag(d);
    return tag[0] == 'N' ? 0 : tag[0] == 'G' ? 2 : 1;
}

static int cmp_report(const void *a, const void *b)
{
    const device_t *x = *(const device_t *const *)a, *y = *(const device_t *const *)b;
    if (rank(x) != rank(y)) {
        return rank(x) - rank(y);
    }
    return y->rssi - x->rssi;
}

int devices_sorted(dev_type_t type, int skip, const device_t **out, int max)
{
    static const device_t *all[MAX_DEVICES];
    int n = 0;
    for (int i = 0; i < dev_count; i++) {
        if (devs[i].type == type) {
            all[n++] = &devs[i];
        }
    }
    qsort(all, n, sizeof(all[0]), cmp_report);
    n = n > skip ? n - skip : 0;
    if (n > max) {
        n = max;
    }
    memcpy(out, all + skip, n * sizeof(all[0]));
    return n;
}

const device_t *devices_find_ap(const uint8_t mac[6])
{
    return find(DEV_WIFI_AP, mac);
}
