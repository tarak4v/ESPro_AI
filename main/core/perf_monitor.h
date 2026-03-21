/**
 * @file perf_monitor.h
 * @brief Performance monitoring — latency, FPS, heap, task stats.
 *
 * Every subsystem logs timestamped metrics here. The perf monitor
 * periodically flushes stats to serial and LittleFS for offline analysis.
 */
#ifndef PERF_MONITOR_H
#define PERF_MONITOR_H

#include <stdint.h>
#include <stdbool.h>

/* ── Metric slot names ── */
#define PERF_SLOT_LVGL_FLUSH     "lvgl_flush"
#define PERF_SLOT_TOUCH_READ     "touch_read"
#define PERF_SLOT_VOICE_STT      "voice_stt"
#define PERF_SLOT_VOICE_LLM      "voice_llm"
#define PERF_SLOT_EVT_DISPATCH   "evt_dispatch"
#define PERF_SLOT_IMU_READ       "imu_read"
#define PERF_SLOT_WIFI_LATENCY   "wifi_latency"
#define PERF_SLOT_FRAME_TIME     "frame_time"

/**
 * @brief Initialise performance monitor.
 * Starts the background reporting task.
 */
void perf_monitor_init(void);

/**
 * @brief Log a latency/duration event (in milliseconds).
 * @param slot  Named metric slot (use PERF_SLOT_* constants).
 * @param ms    Duration in milliseconds.
 */
void perf_log_event(const char *slot, uint32_t ms);

/**
 * @brief Log a counter increment.
 */
void perf_log_counter(const char *slot, uint32_t count);

/**
 * @brief Get current stats for a slot.
 * @param[out] min_ms  Minimum observed value.
 * @param[out] max_ms  Maximum observed value.
 * @param[out] avg_ms  Running average.
 * @param[out] count   Number of samples.
 * @return true if slot exists.
 */
bool perf_get_stats(const char *slot, uint32_t *min_ms,
                    uint32_t *max_ms, uint32_t *avg_ms, uint32_t *count);

/**
 * @brief Force flush current stats to LittleFS log file.
 */
void perf_flush_to_disk(void);

/**
 * @brief Print all stats to serial log.
 */
void perf_print_report(void);

#endif /* PERF_MONITOR_H */
