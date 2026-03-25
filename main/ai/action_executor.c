/**
 * @file action_executor.c
 * @brief Executes local intent actions — theme, volume, brightness, weather, time, steps.
 */

#include "ai/action_executor.h"
#include "ui/theme.h"
#include "ui/ui_manager.h"
#include "hal/audio.h"
#include "hal/display.h"
#include "hw_config.h"

#include "services/weather.h"
#include "services/timer_service.h"
#include "services/wifi_manager.h"
#include "services/config_manager.h"
#include "services/music_stream.h"
#include "services/mqtt_service.h"
#include "services/meeting_service.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "action";

/* ── Theme actions ── */
static bool do_theme(const char *action, char *out, size_t len)
{
    if (strcmp(action, "theme_dark") == 0)
    {
        theme_apply(THEME_DARK);
        snprintf(out, len, "Dark theme applied");
    }
    else if (strcmp(action, "theme_light") == 0)
    {
        theme_apply(THEME_LIGHT);
        snprintf(out, len, "Light theme applied");
    }
    else if (strcmp(action, "theme_autumn") == 0)
    {
        theme_apply(THEME_AUTUMN);
        snprintf(out, len, "Autumn theme applied");
    }
    else if (strcmp(action, "theme_spring") == 0)
    {
        theme_apply(THEME_SPRING);
        snprintf(out, len, "Spring theme applied");
    }
    else if (strcmp(action, "theme_monsoon") == 0)
    {
        theme_apply(THEME_MONSOON);
        snprintf(out, len, "Monsoon theme applied");
    }
    else if (strcmp(action, "theme_cycle") == 0)
    {
        theme_cycle();
        snprintf(out, len, "Theme: %s", theme_get_name(theme_get_current()));
    }
    else
    {
        return false;
    }
    return true;
}

/* ── Volume actions ── */
static bool do_volume(const char *action, char *out, size_t len)
{
    static uint8_t s_volume = 60;

    if (strcmp(action, "volume_up") == 0)
    {
        if (s_volume < 100)
            s_volume += 10;
        audio_set_volume(s_volume);
        snprintf(out, len, "Volume: %d%%", s_volume);
    }
    else if (strcmp(action, "volume_down") == 0)
    {
        if (s_volume > 10)
            s_volume -= 10;
        audio_set_volume(s_volume);
        snprintf(out, len, "Volume: %d%%", s_volume);
    }
    else
    {
        return false;
    }
    return true;
}

/* ── Brightness action ── */
static bool do_brightness(const char *action, char *out, size_t len)
{
    if (strcmp(action, "set_brightness") != 0)
        return false;

    /* Toggle between dim and bright (inverted PWM: 0=bright, 200=dim) */
    static bool dim = false;
    dim = !dim;
    display_set_brightness(dim ? 200 : 0);
    snprintf(out, len, "Display: %s", dim ? "Dim" : "Bright");
    return true;
}

/* ── Info queries ── */
static bool do_get_time(char *out, size_t len)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    snprintf(out, len, "It's %02d:%02d", ti.tm_hour, ti.tm_min);
    return true;
}

static bool do_get_weather(char *out, size_t len)
{
    weather_data_t w = weather_get();
    if (w.valid)
    {
        int wi = (int)w.wind_speed;
        int wd = (int)((w.wind_speed - wi) * 10);
        if (wd < 0)
            wd = -wd;
        if (w.uv_index > 0)
        {
            snprintf(out, len, "%d C, %s, %d%% humidity, wind %d.%d m/s, UV %d",
                     (int)w.temp, w.description, w.humidity, wi, wd, w.uv_index);
        }
        else
        {
            snprintf(out, len, "%d C, %s, %d%% humidity, wind %d.%d m/s",
                     (int)w.temp, w.description, w.humidity, wi, wd);
        }
    }
    else
    {
        snprintf(out, len, "Weather data unavailable");
    }
    return true;
}

/* ── Timer actions ── */
static bool do_timer(const char *action, const char *entity, char *out, size_t len)
{
    if (strcmp(action, "start_timer") == 0)
    {
        uint32_t secs = 60; /* default 1 minute */
        if (entity[0] != '\0')
        {
            int val = atoi(entity);
            if (val > 0)
                secs = (uint32_t)val * 60; /* treat as minutes */
        }
        timer_start(secs);
        uint32_t m = secs / 60;
        snprintf(out, len, "Timer: %lu min", (unsigned long)m);
    }
    else if (strcmp(action, "stop_timer") == 0)
    {
        timer_stop();
        snprintf(out, len, "Timer stopped");
    }
    else
    {
        return false;
    }
    return true;
}

/* ── Music actions (internet radio streaming) ── */
static bool do_music(const char *action, char *out, size_t len)
{
    if (strcmp(action, "play_music") == 0)
    {
        if (!music_stream_is_playing())
        {
            music_stream_play();
            snprintf(out, len, "Radio: %s", music_stream_station_name());
        }
        else
        {
            snprintf(out, len, "Already playing: %s", music_stream_station_name());
        }
    }
    else if (strcmp(action, "stop_music") == 0)
    {
        music_stream_stop();
        snprintf(out, len, "Music stopped");
    }
    else if (strcmp(action, "next_station") == 0)
    {
        music_stream_next();
        if (!music_stream_is_playing())
        {
            music_stream_play();
        }
        snprintf(out, len, "Radio: %s", music_stream_station_name());
    }
    else
    {
        return false;
    }
    return true;
}

/* ── Volume mute ── */
static bool do_mute(char *out, size_t len)
{
    audio_set_volume(0);
    snprintf(out, len, "Muted");
    return true;
}

/* ── Info queries ── */
static bool do_get_date(char *out, size_t len)
{
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);
    static const char *days[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
    static const char *mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    snprintf(out, len, "%s, %s %d %d", days[ti.tm_wday], mons[ti.tm_mon], ti.tm_mday, 1900 + ti.tm_year);
    return true;
}

static bool do_get_battery(char *out, size_t len)
{
    /* ESP32-S3 has no built-in battery monitor — report free heap instead */
    uint32_t free_heap = esp_get_free_heap_size() / 1024;
    snprintf(out, len, "Free memory: %lu KB", (unsigned long)free_heap);
    return true;
}

static bool do_get_wifi(char *out, size_t len)
{
    if (wifi_is_connected())
    {
        snprintf(out, len, "WiFi connected, IP: %s", wifi_get_sta_ip());
    }
    else
    {
        snprintf(out, len, "WiFi disconnected");
    }
    return true;
}

static bool do_get_device(char *out, size_t len)
{
    const device_config_t *cfg = config_get();
    snprintf(out, len, "I'm %s", cfg->device_name);
    return true;
}

/* ── Stub actions ── */
static bool do_stub(const char *action, char *out, size_t len)
{
    if (strcmp(action, "snooze") == 0)
    {
        snprintf(out, len, "Snoozed");
    }
    else if (strcmp(action, "next_task") == 0)
    {
        snprintf(out, len, "Next task");
    }
    else if (strcmp(action, "complete_task") == 0)
    {
        snprintf(out, len, "Task completed");
    }
    else
    {
        return false;
    }
    return true;
}

/* ── Public API ── */
bool action_execute(const intent_result_t *intent, char *response_out, size_t resp_len)
{
    if (!intent || !response_out)
        return false;

    const char *a = intent->action;

    /* Theme */
    if (strncmp(a, "theme_", 6) == 0)
    {
        bool ok = do_theme(a, response_out, resp_len);
        ESP_LOGI(TAG, "Theme action: %s → %s", a, response_out);
        return ok;
    }

    /* Volume */
    if (strncmp(a, "volume_", 7) == 0)
    {
        bool ok = do_volume(a, response_out, resp_len);
        ESP_LOGI(TAG, "Volume action: %s → %s", a, response_out);
        return ok;
    }

    /* Brightness */
    if (strcmp(a, "set_brightness") == 0)
    {
        bool ok = do_brightness(a, response_out, resp_len);
        ESP_LOGI(TAG, "Brightness action → %s", response_out);
        return ok;
    }

    /* Music */
    if (strcmp(a, "play_music") == 0 || strcmp(a, "stop_music") == 0 ||
        strcmp(a, "next_station") == 0)
    {
        bool ok = do_music(a, response_out, resp_len);
        ESP_LOGI(TAG, "Music action: %s → %s", a, response_out);
        return ok;
    }

    /* Mute */
    if (strcmp(a, "mute") == 0)
    {
        return do_mute(response_out, resp_len);
    }

    /* Info queries */
    if (strcmp(a, "get_time") == 0)
    {
        return do_get_time(response_out, resp_len);
    }
    if (strcmp(a, "get_date") == 0)
    {
        return do_get_date(response_out, resp_len);
    }
    if (strcmp(a, "get_weather") == 0)
    {
        return do_get_weather(response_out, resp_len);
    }
    if (strcmp(a, "get_battery") == 0)
    {
        return do_get_battery(response_out, resp_len);
    }
    if (strcmp(a, "get_wifi") == 0)
    {
        return do_get_wifi(response_out, resp_len);
    }
    if (strcmp(a, "get_device") == 0)
    {
        return do_get_device(response_out, resp_len);
    }
    if (strcmp(a, "get_steps") == 0)
    {
        extern uint32_t imu_get_steps(void);
        uint32_t steps = imu_get_steps();
        snprintf(response_out, resp_len, "Steps today: %lu", (unsigned long)steps);
        return true;
    }

    /* Timer */
    if (strcmp(a, "start_timer") == 0 || strcmp(a, "stop_timer") == 0)
    {
        bool ok = do_timer(a, intent->entity, response_out, resp_len);
        ESP_LOGI(TAG, "Timer action: %s → %s", a, response_out);
        return ok;
    }

    /* Stubs */
    if (do_stub(a, response_out, resp_len))
    {
        ESP_LOGI(TAG, "Stub action: %s → %s", a, response_out);
        return true;
    }

    /* MQTT */
    if (strcmp(a, "get_mqtt_status") == 0)
    {
        snprintf(response_out, resp_len, "MQTT: %s", mqtt_status_str());
        return true;
    }
    if (strcmp(a, "mqtt_publish_all") == 0)
    {
        if (mqtt_is_connected())
        {
            mqtt_publish_all_sensors();
            snprintf(response_out, resp_len, "Sensors published via MQTT");
        }
        else
        {
            snprintf(response_out, resp_len, "MQTT not connected");
        }
        return true;
    }

    /* Meeting controls */
    if (strcmp(a, "meeting_mic") == 0)
    {
        meeting_toggle_mic();
        const meeting_state_t *m = meeting_get_state();
        snprintf(response_out, resp_len, "Mic %s", m->mic_on ? "muted" : "unmuted");
        return true;
    }
    if (strcmp(a, "meeting_video") == 0)
    {
        meeting_toggle_video();
        const meeting_state_t *m = meeting_get_state();
        snprintf(response_out, resp_len, "Camera %s", m->video_on ? "off" : "on");
        return true;
    }
    if (strcmp(a, "meeting_hand") == 0)
    {
        meeting_toggle_hand();
        snprintf(response_out, resp_len, "Hand toggled");
        return true;
    }
    if (strcmp(a, "meeting_status") == 0)
    {
        const meeting_state_t *m = meeting_get_state();
        if (m->active)
        {
            uint32_t mins = m->duration_s / 60;
            snprintf(response_out, resp_len, "%s meeting, %lu min, mic %s, cam %s",
                     m->app, (unsigned long)mins,
                     m->mic_on ? "on" : "off",
                     m->video_on ? "on" : "off");
        }
        else
        {
            snprintf(response_out, resp_len, "No active meeting");
        }
        return true;
    }
    if (strcmp(a, "open_meeting") == 0)
    {
        ui_switch_screen(SCR_MEETING);
        snprintf(response_out, resp_len, "Meeting screen");
        return true;
    }
    if (strcmp(a, "meeting_handoff") == 0)
    {
        const meeting_state_t *m = meeting_get_state();
        if (m->handoff_active)
        {
            meeting_stop_handoff();
            snprintf(response_out, resp_len, "Welcome back! Checking notes...");
        }
        else
        {
            meeting_start_handoff();
            snprintf(response_out, resp_len, "Away mode active");
        }
        return true;
    }

    snprintf(response_out, resp_len, "Unknown: %s", a);
    return false;
}
