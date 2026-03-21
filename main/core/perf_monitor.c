/**
 * @file perf_monitor.c
 * @brief Performance monitor — collects latency/counter metrics per named slot.
 *
 * Runs a background task that periodically prints stats and flushes to LittleFS.
 */

#include "perf_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "perf";

#define MAX_SLOTS       16
#define SLOT_NAME_LEN   20
#define REPORT_INTERVAL_MS  30000  /* Print every 30 s */
#define FLUSH_INTERVAL_MS  300000  /* Flush to disk every 5 min */

typedef struct {
    char     name[SLOT_NAME_LEN];
    uint32_t min_ms;
    uint32_t max_ms;
    uint64_t sum_ms;
    uint32_t count;
    bool     active;
} perf_slot_t;

static perf_slot_t       s_slots[MAX_SLOTS];
static SemaphoreHandle_t s_lock;
static uint32_t          s_boot_time_ms;

static int find_or_create_slot(const char *name)
{
    int first_free = -1;
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].active && strncmp(s_slots[i].name, name, SLOT_NAME_LEN) == 0)
            return i;
        if (!s_slots[i].active && first_free < 0)
            first_free = i;
    }
    if (first_free >= 0) {
        strncpy(s_slots[first_free].name, name, SLOT_NAME_LEN - 1);
        s_slots[first_free].name[SLOT_NAME_LEN - 1] = '\0';
        s_slots[first_free].min_ms  = UINT32_MAX;
        s_slots[first_free].max_ms  = 0;
        s_slots[first_free].sum_ms  = 0;
        s_slots[first_free].count   = 0;
        s_slots[first_free].active  = true;
    }
    return first_free;
}

/* ── Background task ── */
static void perf_task(void *arg)
{
    uint32_t last_report = 0;
    uint32_t last_flush  = 0;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);

        if (now - last_report >= REPORT_INTERVAL_MS) {
            perf_print_report();
            last_report = now;
        }
        if (now - last_flush >= FLUSH_INTERVAL_MS) {
            perf_flush_to_disk();
            last_flush = now;
        }
    }
}

/* ── Public API ── */

void perf_monitor_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    memset(s_slots, 0, sizeof(s_slots));
    s_boot_time_ms = (uint32_t)(esp_timer_get_time() / 1000);

    xTaskCreatePinnedToCore(perf_task, "perf_mon", 3072, NULL, 1, NULL, 0);
    ESP_LOGI(TAG, "Performance monitor started (report=%ds, flush=%ds)",
             REPORT_INTERVAL_MS / 1000, FLUSH_INTERVAL_MS / 1000);
}

void perf_log_event(const char *slot, uint32_t ms)
{
    if (!slot) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = find_or_create_slot(slot);
    if (idx >= 0) {
        perf_slot_t *s = &s_slots[idx];
        if (ms < s->min_ms) s->min_ms = ms;
        if (ms > s->max_ms) s->max_ms = ms;
        s->sum_ms += ms;
        s->count++;
    }
    xSemaphoreGive(s_lock);
}

void perf_log_counter(const char *slot, uint32_t count)
{
    if (!slot) return;
    xSemaphoreTake(s_lock, portMAX_DELAY);
    int idx = find_or_create_slot(slot);
    if (idx >= 0) {
        s_slots[idx].count += count;
    }
    xSemaphoreGive(s_lock);
}

bool perf_get_stats(const char *slot, uint32_t *min_ms,
                    uint32_t *max_ms, uint32_t *avg_ms, uint32_t *count)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].active && strncmp(s_slots[i].name, slot, SLOT_NAME_LEN) == 0) {
            if (min_ms) *min_ms = s_slots[i].min_ms;
            if (max_ms) *max_ms = s_slots[i].max_ms;
            if (avg_ms) *avg_ms = s_slots[i].count ?
                (uint32_t)(s_slots[i].sum_ms / s_slots[i].count) : 0;
            if (count)  *count  = s_slots[i].count;
            xSemaphoreGive(s_lock);
            return true;
        }
    }
    xSemaphoreGive(s_lock);
    return false;
}

void perf_print_report(void)
{
    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000) - s_boot_time_ms;
    uint32_t heap_free = esp_get_free_heap_size();
    uint32_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint32_t iram_free  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);

    ESP_LOGI(TAG, "══════ PERF REPORT (uptime %lu s) ══════", uptime / 1000);
    ESP_LOGI(TAG, "Heap: total_free=%lu  PSRAM=%lu  IRAM=%lu",
             heap_free, psram_free, iram_free);

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].active && s_slots[i].count > 0) {
            uint32_t avg = (uint32_t)(s_slots[i].sum_ms / s_slots[i].count);
            ESP_LOGI(TAG, "  %-18s  min=%4lu  max=%4lu  avg=%4lu  n=%lu",
                     s_slots[i].name, s_slots[i].min_ms,
                     s_slots[i].max_ms, avg, s_slots[i].count);
        }
    }
    xSemaphoreGive(s_lock);
    ESP_LOGI(TAG, "═══════════════════════════════════════");
}

void perf_flush_to_disk(void)
{
    FILE *f = fopen("/littlefs/perf_log.csv", "a");
    if (!f) {
        ESP_LOGW(TAG, "Cannot open perf log file");
        return;
    }

    uint32_t uptime = (uint32_t)(esp_timer_get_time() / 1000000);
    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SLOTS; i++) {
        if (s_slots[i].active && s_slots[i].count > 0) {
            uint32_t avg = (uint32_t)(s_slots[i].sum_ms / s_slots[i].count);
            fprintf(f, "%lu,%s,%lu,%lu,%lu,%lu\n",
                    uptime, s_slots[i].name,
                    s_slots[i].min_ms, s_slots[i].max_ms,
                    avg, s_slots[i].count);
        }
    }
    xSemaphoreGive(s_lock);
    fclose(f);
    ESP_LOGI(TAG, "Perf data flushed to /littlefs/perf_log.csv");
}
