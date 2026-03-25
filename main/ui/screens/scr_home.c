/**
 * @file scr_home.c
 * @brief Home screen — right-aligned clock/weather, center stage for AI text,
 *        status line below, mic button on left.
 *
 * Layout (640×172):
 *   [MIC 56×56] [  CENTER STAGE (text+status)  ] [CLOCK/DATE/WEATHER]
 *     x=6          x=70..430                       x=440..630
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "hal/rtc.h"
#include "services/weather.h"
#include "services/wifi_manager.h"
#include "services/mqtt_service.h"
#include "services/music_stream.h"
#include "ai/voice_pipeline.h"
#include "lvgl.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>
#include <string.h>

static const char *TAG_HOME = "scr_home";

static lv_obj_t *scr = NULL;

/* Right panel — clock / date / weather */
static lv_obj_t *lbl_time = NULL;
static lv_obj_t *lbl_date = NULL;
static lv_obj_t *lbl_weather = NULL;

/* Bottom status bar */
static lv_obj_t *status_bar = NULL;
static lv_obj_t *lbl_music = NULL;
static lv_obj_t *lbl_ip = NULL;
static lv_obj_t *lbl_wifi_icon = NULL;
static lv_obj_t *lbl_bt_icon = NULL;
static lv_obj_t *lbl_mqtt_icon = NULL;

/* Center stage — AI communication area */
static lv_obj_t *lbl_stage = NULL;
static lv_obj_t *lbl_voice_status = NULL;

/* Mic button */
static lv_obj_t *mic_dot = NULL;
static lv_obj_t *mic_icon = NULL;

/* Action history — rolling log of last N actions */
#define ACTION_HISTORY_MAX 5
#define ACTION_LINE_LEN 80
static char s_history[ACTION_HISTORY_MAX][ACTION_LINE_LEN];
static int s_history_count = 0;
static bool s_stage_dirty = false;

/* Mic button press — start listening */
static void mic_press_cb(lv_event_t *e)
{
    voice_pipeline_state_t vs = voice_pipeline_get_state();
    if (vs == VP_IDLE)
    {
        ESP_LOGI(TAG_HOME, "Mic PRESSED — start listening");
        voice_pipeline_trigger();
    }
}

/* Mic button release — stop listening, process */
static void mic_release_cb(lv_event_t *e)
{
    voice_pipeline_state_t vs = voice_pipeline_get_state();
    if (vs == VP_LISTENING)
    {
        ESP_LOGI(TAG_HOME, "Mic RELEASED — stop listening");
        voice_pipeline_stop_listening();
    }
}

void scr_home_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* ── Right panel: clock + date + weather ── */

    /* Time — right aligned, large */
    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(th_text), 0);
    lv_obj_align(lbl_time, LV_ALIGN_TOP_RIGHT, -10, 4);
    lv_label_set_text(lbl_time, "00:00");

    /* Date — below time, right aligned */
    lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(th_label), 0);
    lv_obj_align(lbl_date, LV_ALIGN_TOP_RIGHT, -10, 58);
    lv_label_set_text(lbl_date, "Mon, Jan 01");

    /* Weather — below date, right aligned, two lines */
    lbl_weather = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_weather, lv_color_hex(th_label), 0);
    lv_obj_set_style_text_align(lbl_weather, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_align(lbl_weather, LV_ALIGN_TOP_RIGHT, -10, 80);
    lv_obj_set_width(lbl_weather, 190);
    lv_label_set_long_mode(lbl_weather, LV_LABEL_LONG_WRAP);
    lv_label_set_text(lbl_weather, "-- C");

    /* ── Bottom status bar (full width, at y=152) ── */
    status_bar = lv_obj_create(scr);
    lv_obj_set_size(status_bar, LCD_H_RES, 20);
    lv_obj_set_pos(status_bar, 0, LCD_V_RES - 20);
    lv_obj_set_style_bg_color(status_bar, lv_color_hex(th_bg), 0);
    lv_obj_set_style_bg_opa(status_bar, LV_OPA_80, 0);
    lv_obj_set_style_border_width(status_bar, 0, 0);
    lv_obj_set_style_pad_all(status_bar, 0, 0);
    lv_obj_set_style_radius(status_bar, 0, 0);
    lv_obj_clear_flag(status_bar, LV_OBJ_FLAG_SCROLLABLE);

    /* Music indicator — right side */
    lbl_music = lv_label_create(status_bar);
    lv_obj_set_style_text_font(lbl_music, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_music, lv_color_hex(th_accent), 0);
    lv_obj_align(lbl_music, LV_ALIGN_RIGHT_MID, -8, 0);
    lv_label_set_text(lbl_music, "");

    /* IP address — left side of status bar */
    lbl_ip = lv_label_create(status_bar);
    lv_obj_set_style_text_font(lbl_ip, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_ip, lv_color_hex(th_label), 0);
    lv_obj_align(lbl_ip, LV_ALIGN_LEFT_MID, 8, 0);
    lv_label_set_text(lbl_ip, "");

    /* WiFi icon — centre-left of status bar */
    lbl_wifi_icon = lv_label_create(status_bar);
    lv_obj_set_style_text_font(lbl_wifi_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_wifi_icon, lv_color_hex(0x555555), 0);
    lv_obj_set_pos(lbl_wifi_icon, 270, 3);
    lv_label_set_text(lbl_wifi_icon, LV_SYMBOL_WIFI);

    /* BT icon */
    lbl_bt_icon = lv_label_create(status_bar);
    lv_obj_set_style_text_font(lbl_bt_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_bt_icon, lv_color_hex(0x555555), 0);
    lv_obj_set_pos(lbl_bt_icon, 300, 3);
    lv_label_set_text(lbl_bt_icon, LV_SYMBOL_BLUETOOTH);

    /* MQTT icon — "M" text */
    lbl_mqtt_icon = lv_label_create(status_bar);
    lv_obj_set_style_text_font(lbl_mqtt_icon, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_mqtt_icon, lv_color_hex(0x555555), 0);
    lv_obj_set_pos(lbl_mqtt_icon, 326, 3);
    lv_label_set_text(lbl_mqtt_icon, "M");

    /* ── Separator line ── */
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, 2, 130);
    lv_obj_set_pos(sep, 430, 6);
    lv_obj_set_style_bg_color(sep, lv_color_hex(th_label), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_set_style_radius(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    /* ── Center stage: Action history log ── */
    lbl_stage = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_stage, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_stage, lv_color_hex(th_text), 0);
    lv_obj_set_pos(lbl_stage, 72, 6);
    lv_obj_set_size(lbl_stage, 350, 118);
    lv_label_set_long_mode(lbl_stage, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_stage, "");

    /* Voice status — small text below stage area */
    lbl_voice_status = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_voice_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_voice_status, lv_color_hex(th_accent), 0);
    lv_obj_set_pos(lbl_voice_status, 72, 130);
    lv_obj_set_width(lbl_voice_status, 350);
    lv_label_set_text(lbl_voice_status, "");

    /* ── Mic button — left side, vertically centered ── */
    mic_dot = lv_obj_create(scr);
    lv_obj_set_size(mic_dot, 56, 56);
    lv_obj_set_pos(mic_dot, 6, (LCD_V_RES - 56) / 2);
    lv_obj_set_style_radius(mic_dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(mic_dot, lv_color_hex(0x666666), 0);
    lv_obj_set_style_bg_opa(mic_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(mic_dot, 0, 0);
    lv_obj_clear_flag(mic_dot, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(mic_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(mic_dot, mic_press_cb, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(mic_dot, mic_release_cb, LV_EVENT_RELEASED, NULL);

    mic_icon = lv_label_create(mic_dot);
    lv_obj_set_style_text_font(mic_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(mic_icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(mic_icon);
    lv_label_set_text(mic_icon, LV_SYMBOL_AUDIO);

    s_history_count = 0;
    memset(s_history, 0, sizeof(s_history));
    s_stage_dirty = false;

    lv_disp_load_scr(scr);
}

void scr_home_update(void)
{
    if (!scr)
        return;

    /* ── Clock ── */
    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    static const char *days[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    static const char *mons[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                 "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    lv_label_set_text_fmt(lbl_time, "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text_fmt(lbl_date, "%s, %s %02d",
                          days[ti.tm_wday], mons[ti.tm_mon], ti.tm_mday);

    /* ── Weather with wind + UV ── */
    weather_data_t w = weather_get();
    if (w.valid)
    {
        int wind_i = (int)w.wind_speed;
        int wind_d = (int)((w.wind_speed - wind_i) * 10);
        if (wind_d < 0)
            wind_d = -wind_d;

        if (w.uv_index > 0)
        {
            lv_label_set_text_fmt(lbl_weather, "%d C %s\nWind %d.%dm/s  UV %d",
                                  (int)w.temp, w.description,
                                  wind_i, wind_d, w.uv_index);
        }
        else
        {
            lv_label_set_text_fmt(lbl_weather, "%d C %s\nWind %d.%dm/s",
                                  (int)w.temp, w.description,
                                  wind_i, wind_d);
        }
    }

    /* ── IP ── */
    bool wifi_ok = wifi_is_connected();
    if (lbl_ip)
    {
        if (wifi_ok)
        {
            const char *ip = wifi_get_sta_ip();
            if (ip && ip[0] != '\0')
                lv_label_set_text(lbl_ip, ip);
            else
                lv_label_set_text(lbl_ip, "");
        }
        else
        {
            lv_label_set_text(lbl_ip, "");
        }
    }

    /* ── WiFi / BT / MQTT status dots ── */
    if (lbl_wifi_icon)
        lv_obj_set_style_text_color(lbl_wifi_icon,
                                    lv_color_hex(wifi_ok ? 0x4CAF50 : 0x555555), 0);

    /* BT not available — always grey */
    (void)lbl_bt_icon;

    if (lbl_mqtt_icon)
        lv_obj_set_style_text_color(lbl_mqtt_icon,
                                    lv_color_hex(mqtt_is_connected() ? 0x4CAF50 : 0x555555), 0);

    /* ── Music indicator ── */
    if (lbl_music)
    {
        if (music_stream_is_playing())
        {
            lv_label_set_text(lbl_music, LV_SYMBOL_AUDIO);
        }
        else
        {
            lv_label_set_text(lbl_music, "");
        }
    }

    /* ── Mic button color reflects voice state ── */
    if (mic_dot)
    {
        voice_pipeline_state_t vs = voice_pipeline_get_state();
        uint32_t dot_color;
        switch (vs)
        {
        case VP_LISTENING:
            dot_color = 0x4CAF50;
            break;
        case VP_PROCESSING:
            dot_color = 0x9C27B0;
            break;
        case VP_RESPONDING:
            dot_color = 0x2196F3;
            break;
        default:
            dot_color = 0x666666;
            break;
        }
        lv_obj_set_style_bg_color(mic_dot, lv_color_hex(dot_color), 0);

        /* ── Voice status label ── */
        if (lbl_voice_status)
        {
            const char *status = "";
            switch (vs)
            {
            case VP_LISTENING:
            {
                static uint8_t dot_phase = 0;
                dot_phase = (dot_phase + 1) % 4;
                const char *dots[] = {"Listening", "Listening.", "Listening..", "Listening..."};
                status = dots[dot_phase];
                break;
            }
            case VP_PROCESSING:
                status = "Processing...";
                break;
            case VP_RESPONDING:
                status = "Response";
                break;
            default:
                break;
            }
            lv_label_set_text(lbl_voice_status, status);
        }
    }

    /* ── Center stage: action history (set from voice pipeline) ── */
    if (s_stage_dirty && lbl_stage)
    {
        /* Build display string from history lines */
        char display[ACTION_HISTORY_MAX * ACTION_LINE_LEN + 16];
        display[0] = '\0';
        int start = 0;
        int count = s_history_count;
        if (count > ACTION_HISTORY_MAX)
        {
            start = count - ACTION_HISTORY_MAX;
            count = ACTION_HISTORY_MAX;
        }
        for (int i = 0; i < count; i++)
        {
            int idx = (start + i) % ACTION_HISTORY_MAX;
            if (i > 0)
                strcat(display, "\n");
            /* Newest entries at bottom; prefix with > */
            strcat(display, "> ");
            strncat(display, s_history[idx], ACTION_LINE_LEN - 1);
        }
        lv_label_set_text(lbl_stage, display);
        s_stage_dirty = false;
    }
}

void scr_home_destroy(void)
{
    if (scr)
    {
        lbl_time = lbl_date = lbl_weather = NULL;
        lbl_music = lbl_ip = NULL;
        status_bar = NULL;
        lbl_stage = lbl_voice_status = NULL;
        mic_dot = mic_icon = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}

/* ── Public API: push text into action history from any task ── */
void scr_home_set_stage_text(const char *text)
{
    if (!text || text[0] == '\0')
        return;
    /* Add to ring buffer */
    int idx = s_history_count % ACTION_HISTORY_MAX;
    strncpy(s_history[idx], text, ACTION_LINE_LEN - 1);
    s_history[idx][ACTION_LINE_LEN - 1] = '\0';
    s_history_count++;
    s_stage_dirty = true;
}
