/**
 * @file timer_service.c
 * @brief Simple countdown timer — beeps when done, updates home screen stage.
 */

#include "services/timer_service.h"
#include "hal/audio.h"
#include "ui/ui_manager.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "timer";

static volatile uint32_t s_remaining = 0;
static volatile bool s_running = false;
static esp_timer_handle_t s_tick_timer = NULL;

static void alarm_task(void *arg)
{
    scr_home_set_stage_text("Timer done!");
    /* Triple beep */
    for (int i = 0; i < 3; i++)
    {
        audio_beep(1500, 300);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    vTaskDelete(NULL);
}

static void tick_cb(void *arg)
{
    if (!s_running || s_remaining == 0)
        return;

    s_remaining--;

    /* Update stage with remaining time */
    uint32_t m = s_remaining / 60;
    uint32_t s = s_remaining % 60;
    char buf[64];
    snprintf(buf, sizeof(buf), "Timer: %lu:%02lu", (unsigned long)m, (unsigned long)s);
    scr_home_set_stage_text(buf);

    if (s_remaining == 0)
    {
        s_running = false;
        esp_timer_stop(s_tick_timer);
        ESP_LOGI(TAG, "Timer expired — alarm!");
        xTaskCreate(alarm_task, "tmr_alarm", 4096, NULL, 2, NULL);
    }
}

void timer_start(uint32_t seconds)
{
    if (seconds == 0)
        return;

    /* Create one-shot 1s periodic timer if not yet created */
    if (!s_tick_timer)
    {
        esp_timer_create_args_t args = {
            .callback = tick_cb,
            .name = "countdown",
        };
        esp_timer_create(&args, &s_tick_timer);
    }

    /* Stop existing timer if running */
    if (s_running)
    {
        esp_timer_stop(s_tick_timer);
    }

    s_remaining = seconds;
    s_running = true;
    esp_timer_start_periodic(s_tick_timer, 1000000); /* 1s = 1,000,000 us */

    uint32_t m = seconds / 60;
    uint32_t s = seconds % 60;
    ESP_LOGI(TAG, "Timer started: %lu:%02lu", (unsigned long)m, (unsigned long)s);
}

void timer_stop(void)
{
    if (s_running && s_tick_timer)
    {
        esp_timer_stop(s_tick_timer);
    }
    s_running = false;
    s_remaining = 0;
    ESP_LOGI(TAG, "Timer stopped");
}

uint32_t timer_get_remaining(void)
{
    return s_remaining;
}

bool timer_is_running(void)
{
    return s_running;
}
