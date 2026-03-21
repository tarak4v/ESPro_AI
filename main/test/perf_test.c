/**
 * @file perf_test.c
 * @brief Performance benchmarks — runs at boot to baseline system metrics.
 */

#include "test/perf_test.h"
#include "core/perf_monitor.h"
#include "hal/audio.h"
#include "hal/imu.h"
#include "services/storage.h"
#include "services/wifi_manager.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "lvgl.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "perf_test";

void perf_test_memory(void)
{
    ESP_LOGI(TAG, "=== Memory Benchmark ===");

    /* PSRAM allocation speed */
    int64_t t0 = esp_timer_get_time();
    void *p = heap_caps_malloc(100 * 1024, MALLOC_CAP_SPIRAM);
    int64_t alloc_us = esp_timer_get_time() - t0;
    if (p) {
        memset(p, 0xAA, 100 * 1024);
        int64_t write_us = esp_timer_get_time() - t0 - alloc_us;
        heap_caps_free(p);
        ESP_LOGI(TAG, "PSRAM 100KB: alloc=%lld us, write=%lld us", alloc_us, write_us);
        perf_log_event("bench_psram_alloc", (uint32_t)(alloc_us / 1000));
        perf_log_event("bench_psram_write", (uint32_t)(write_us / 1000));
    }

    /* Internal RAM */
    t0 = esp_timer_get_time();
    p = heap_caps_malloc(4096, MALLOC_CAP_INTERNAL);
    alloc_us = esp_timer_get_time() - t0;
    if (p) {
        heap_caps_free(p);
        ESP_LOGI(TAG, "IRAM 4KB alloc: %lld us", alloc_us);
    }

    ESP_LOGI(TAG, "Free heap: total=%lu PSRAM=%lu IRAM=%lu",
             esp_get_free_heap_size(),
             heap_caps_get_free_size(MALLOC_CAP_SPIRAM),
             heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
}

void perf_test_imu(void)
{
    ESP_LOGI(TAG, "=== IMU Benchmark (100 reads) ===");
    imu_data_t d;
    int64_t total = 0;
    int ok = 0;

    for (int i = 0; i < 100; i++) {
        int64_t t0 = esp_timer_get_time();
        if (imu_read(&d)) ok++;
        total += esp_timer_get_time() - t0;
    }

    ESP_LOGI(TAG, "IMU: %d/100 ok, avg=%lld us/read", ok, total / 100);
    perf_log_event("bench_imu_read", (uint32_t)(total / 100 / 1000));
}

void perf_test_storage(void)
{
    ESP_LOGI(TAG, "=== Storage Benchmark ===");

    char buf[1024];
    memset(buf, 'X', sizeof(buf));

    /* Write */
    int64_t t0 = esp_timer_get_time();
    storage_write_file("/littlefs/bench.txt", buf, sizeof(buf));
    int64_t write_us = esp_timer_get_time() - t0;

    /* Read */
    t0 = esp_timer_get_time();
    size_t len;
    char *data = storage_read_file("/littlefs/bench.txt", &len);
    int64_t read_us = esp_timer_get_time() - t0;

    if (data) heap_caps_free(data);
    ESP_LOGI(TAG, "LittleFS 1KB: write=%lld us, read=%lld us", write_us, read_us);
    perf_log_event("bench_fs_write", (uint32_t)(write_us / 1000));
    perf_log_event("bench_fs_read", (uint32_t)(read_us / 1000));
}

void perf_test_run_all(void)
{
    ESP_LOGI(TAG, "╔══════════════════════════════╗");
    ESP_LOGI(TAG, "║   Performance Benchmarks     ║");
    ESP_LOGI(TAG, "╚══════════════════════════════╝");

    perf_test_memory();
    perf_test_imu();
    perf_test_storage();

    ESP_LOGI(TAG, "Benchmarks complete. Results logged to perf monitor.");
}
