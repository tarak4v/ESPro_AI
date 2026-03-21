/**
 * @file os_kernel.c
 * @brief FreeRTOS task creation and orchestration.
 */

#include "os_kernel.h"
#include "event_bus.h"
#include "perf_monitor.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

/* Forward declarations of task entry points in other modules */
extern void lvgl_render_task(void *arg);
extern void touch_input_task(void *arg);
extern void app_manager_task(void *arg);
extern void voice_pipeline_task(void *arg);
extern void agent_orchestrator_task(void *arg);
extern void sensor_fusion_task(void *arg);

static const char *TAG = "os_kernel";

SemaphoreHandle_t g_lvgl_mutex = NULL;

TaskHandle_t g_task_lvgl   = NULL;
TaskHandle_t g_task_touch  = NULL;
TaskHandle_t g_task_app    = NULL;
TaskHandle_t g_task_voice  = NULL;
TaskHandle_t g_task_agent  = NULL;
TaskHandle_t g_task_sensor = NULL;

void os_kernel_start_tasks(void)
{
    g_lvgl_mutex = xSemaphoreCreateRecursiveMutex();

    ESP_LOGI(TAG, "Creating system tasks...");

    /* ── Core 0: UI + Sensors ── */
    xTaskCreatePinnedToCore(lvgl_render_task, "lvgl",
                            8192, NULL, 5, &g_task_lvgl, 0);

    xTaskCreatePinnedToCore(touch_input_task, "touch",
                            3072, NULL, 4, &g_task_touch, 0);

    xTaskCreatePinnedToCore(sensor_fusion_task, "sensor",
                            4096, NULL, 3, &g_task_sensor, 0);

    /* ── Core 1: AI + Network + App ── */
    xTaskCreatePinnedToCore(app_manager_task, "app_mgr",
                            4096, NULL, 4, &g_task_app, 1);

    /* Voice pipeline needs large stack for HTTP + audio buffers — use PSRAM for stack, internal for TCB */
    StaticTask_t *voice_tcb = heap_caps_calloc(1, sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    StackType_t  *voice_stk = heap_caps_calloc(1, 24576 * sizeof(StackType_t), MALLOC_CAP_SPIRAM);
    if (voice_tcb && voice_stk) {
        g_task_voice = xTaskCreateStaticPinnedToCore(
            voice_pipeline_task, "voice",
            24576, NULL, 3, voice_stk, voice_tcb, 1);
    } else {
        ESP_LOGE(TAG, "Failed to allocate PSRAM for voice task");
    }

    xTaskCreatePinnedToCore(agent_orchestrator_task, "ai_agent",
                            8192, NULL, 2, &g_task_agent, 1);

    ESP_LOGI(TAG, "All tasks created. System running.");
    event_post(EVT_BOOT_COMPLETE, 0);
}

void os_kernel_get_task_stats(char *buf, size_t buflen)
{
    if (!buf || buflen == 0) return;

    struct { const char *name; TaskHandle_t handle; } tasks[] = {
        {"lvgl",    g_task_lvgl},
        {"touch",   g_task_touch},
        {"app_mgr", g_task_app},
        {"voice",   g_task_voice},
        {"agent",   g_task_agent},
        {"sensor",  g_task_sensor},
    };

    int pos = 0;
    pos += snprintf(buf + pos, buflen - pos,
                    "%-10s %8s %5s\n", "Task", "FreeStk", "Core");
    for (int i = 0; i < 6 && tasks[i].handle; i++) {
        UBaseType_t watermark = uxTaskGetStackHighWaterMark(tasks[i].handle);
        pos += snprintf(buf + pos, buflen - pos,
                        "%-10s %8lu %5d\n",
                        tasks[i].name, (unsigned long)watermark,
                        xTaskGetAffinity(tasks[i].handle));
    }
}
