/**
 * @file proactive_agent.c
 * @brief Proactive AI agent — runs in background, posts notifications to home screen.
 *
 * Features:
 *   - Weather alerts (rain, extreme heat/cold)
 *   - Inactivity reminders (move every 30 min)
 *   - Hourly chime (subtle notification)
 */

#include "ai/proactive_agent.h"
#include "ai/agent_orchestrator.h"
#include "hal/imu.h"
#include "services/weather.h"
#include "ui/ui_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "proactive";

/* State tracking */
static int64_t s_last_step_check_us = 0;
static uint32_t s_last_step_count = 0;
static bool s_rain_alerted = false;
static int s_last_alert_hour = -1;

extern uint32_t imu_get_steps(void); /* Declared in imu.c */

/* ── Can-handle: proactive agent doesn't handle direct requests ── */
static bool can_handle(const char *input)
{
    (void)input;
    return false; /* Background-only agent */
}

/* ── Process: not used for proactive agent ── */
static void process(const agent_request_t *req, agent_response_t *resp)
{
    resp->success = false;
    snprintf(resp->text, sizeof(resp->text), "Proactive agent: background only");
}

/* ── Background tick (runs every 60s) ── */
static void background_tick(void)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    /* ── Weather alert: rain or extreme temps ── */
    weather_data_t w = weather_get();
    if (w.valid)
    {
        /* Rain alert (once per weather update) */
        bool is_rain = (strstr(w.description, "rain") != NULL ||
                        strstr(w.description, "drizzle") != NULL ||
                        strstr(w.description, "shower") != NULL);
        if (is_rain && !s_rain_alerted)
        {
            scr_home_set_stage_text("Rain expected - carry umbrella!");
            s_rain_alerted = true;
            ESP_LOGI(TAG, "Rain alert posted");
        }
        else if (!is_rain)
        {
            s_rain_alerted = false;
        }

        /* Extreme temperature alert */
        if ((int)w.temp >= 40 && ti.tm_hour >= 10 && ti.tm_hour <= 16)
        {
            if (s_last_alert_hour != ti.tm_hour)
            {
                scr_home_set_stage_text("Extreme heat! Stay hydrated");
                s_last_alert_hour = ti.tm_hour;
            }
        }

        /* UV alert */
        if (w.uv_index >= 8 && ti.tm_hour >= 10 && ti.tm_hour <= 14)
        {
            if (s_last_alert_hour != ti.tm_hour)
            {
                scr_home_set_stage_text("High UV! Use sunscreen");
                s_last_alert_hour = ti.tm_hour;
            }
        }
    }

    /* ── Inactivity reminder: no steps for 30 min ── */
    int64_t now_us = esp_timer_get_time();
    uint32_t current_steps = imu_get_steps();

    if (s_last_step_check_us == 0)
    {
        /* First run */
        s_last_step_check_us = now_us;
        s_last_step_count = current_steps;
    }
    else
    {
        int64_t elapsed_ms = (now_us - s_last_step_check_us) / 1000;
        if (elapsed_ms >= 30 * 60 * 1000)
        { /* 30 minutes */
            if (current_steps == s_last_step_count)
            {
                /* No steps in 30 min — remind to move */
                if (ti.tm_hour >= 8 && ti.tm_hour <= 22)
                {
                    scr_home_set_stage_text("Time to stretch! Stand up and move");
                    ESP_LOGI(TAG, "Inactivity reminder posted");
                }
            }
            s_last_step_check_us = now_us;
            s_last_step_count = current_steps;
        }
    }

    /* ── Hourly nudge (morning greeting, evening summary) ── */
    if (ti.tm_min == 0 && s_last_alert_hour != ti.tm_hour)
    {
        s_last_alert_hour = ti.tm_hour;
        if (ti.tm_hour == 8)
        {
            char msg[80];
            if (w.valid)
            {
                snprintf(msg, sizeof(msg), "Good morning! %d C, %s",
                         (int)w.temp, w.description);
            }
            else
            {
                snprintf(msg, sizeof(msg), "Good morning!");
            }
            scr_home_set_stage_text(msg);
        }
        else if (ti.tm_hour == 22)
        {
            char msg[80];
            snprintf(msg, sizeof(msg), "Good night! Steps today: %lu",
                     (unsigned long)current_steps);
            scr_home_set_stage_text(msg);
        }
    }
}

/* ── Register with orchestrator ── */
void proactive_agent_register(void)
{
    agent_t agent = {
        .name = "Proactive",
        .id = AGENT_PROACTIVE,
        .state = AGENT_STATE_IDLE,
        .can_handle = can_handle,
        .process = process,
        .background_tick = background_tick,
        .bg_interval_ms = 60000, /* Every 60 seconds */
    };
    agent_register(&agent);
    ESP_LOGI(TAG, "Proactive agent registered (60s interval)");
}
