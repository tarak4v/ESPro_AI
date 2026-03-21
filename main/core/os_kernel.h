/**
 * @file os_kernel.h
 * @brief FreeRTOS task orchestration — creation, priorities, core pinning.
 *
 * Task architecture:
 *   Core 0 (UI + Sensors):
 *     - LVGL render     (prio 5, 8192 stack)
 *     - Touch input     (prio 4, 3072 stack)
 *     - Sensor fusion   (prio 3, 4096 stack, IMU + wellness)
 *     - Perf monitor    (prio 1, 3072 stack)
 *
 *   Core 1 (AI + Network + App):
 *     - Event dispatcher (prio 5, 4096 stack)
 *     - App manager      (prio 4, 4096 stack)
 *     - Voice pipeline   (prio 3, 24K PSRAM stack)
 *     - Agent orchestrator (prio 2, 8192 stack)
 *     - WiFi manager     (prio 2, 4096 stack)
 */
#ifndef OS_KERNEL_H
#define OS_KERNEL_H

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

/* ── LVGL thread safety ── */
extern SemaphoreHandle_t g_lvgl_mutex;

static inline bool lvgl_lock(uint32_t timeout_ms) {
    return xSemaphoreTake(g_lvgl_mutex, pdMS_TO_TICKS(timeout_ms)) == pdTRUE;
}

static inline void lvgl_unlock(void) {
    xSemaphoreGive(g_lvgl_mutex);
}

/* ── Task handles (for monitoring) ── */
extern TaskHandle_t g_task_lvgl;
extern TaskHandle_t g_task_touch;
extern TaskHandle_t g_task_app;
extern TaskHandle_t g_task_voice;
extern TaskHandle_t g_task_agent;
extern TaskHandle_t g_task_sensor;

/**
 * @brief Create all system tasks.
 * Call after all subsystem init is complete.
 */
void os_kernel_start_tasks(void);

/**
 * @brief Get task runtime stats as formatted string.
 * @param buf    Output buffer.
 * @param buflen Buffer size.
 */
void os_kernel_get_task_stats(char *buf, size_t buflen);

#endif /* OS_KERNEL_H */
