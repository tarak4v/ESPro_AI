/**
 * @file scr_settings.c
 * @brief Settings screen — volume, brightness, theme, perf stats.
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "hal/display.h"
#include "hal/audio.h"
#include "core/perf_monitor.h"
#include "services/wifi_manager.h"
#include "services/webserver.h"
#include "lvgl.h"
#include "esp_log.h"
#include "esp_system.h"
#include <stdio.h>

/* Forward declarations for self-referencing callbacks */
void scr_settings_create(void);
void scr_settings_destroy(void);

static lv_obj_t *scr = NULL;
static lv_obj_t *lbl_perf = NULL;
static lv_obj_t *lbl_wifi = NULL;
static lv_obj_t *lbl_bt = NULL;
static lv_obj_t *lbl_ws = NULL;
static lv_obj_t *btn_ws = NULL;
static lv_obj_t *lbl_ws_btn = NULL;
static bool s_webserver_on = false;

static void theme_toggle_cb(lv_event_t *e)
{
    theme_cycle();
    /* Recreate screen to apply new theme */
    scr_settings_destroy();
    scr_settings_create();
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    display_set_brightness((uint8_t)val);
}

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    int32_t val = lv_slider_get_value(slider);
    audio_set_volume((uint8_t)val);
}

static void webserver_toggle_cb(lv_event_t *e)
{
    if (s_webserver_on)
    {
        webserver_stop();
        s_webserver_on = false;
    }
    else
    {
        webserver_start();
        s_webserver_on = true;
    }

    if (lbl_ws_btn)
        lv_label_set_text(lbl_ws_btn, s_webserver_on ? "Stop Portal" : "Start Portal");
    if (btn_ws)
        lv_obj_set_style_bg_color(btn_ws, lv_color_hex(s_webserver_on ? 0xE53935 : th_btn), 0);
}

void scr_settings_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* Title */
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(th_text), 0);
    lv_obj_set_pos(title, 20, 4);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");

    /* Brightness slider */
    lv_obj_t *lbl_br = lv_label_create(scr);
    lv_label_set_text(lbl_br, "Brightness");
    lv_obj_set_style_text_color(lbl_br, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_br, 20, 38);

    lv_obj_t *sl_br = lv_slider_create(scr);
    lv_obj_set_size(sl_br, 150, 10);
    lv_obj_set_pos(sl_br, 120, 43);
    lv_slider_set_range(sl_br, 0, 255);
    lv_slider_set_value(sl_br, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl_br, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Volume slider */
    lv_obj_t *lbl_vol = lv_label_create(scr);
    lv_label_set_text(lbl_vol, "Volume");
    lv_obj_set_style_text_color(lbl_vol, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_vol, 20, 66);

    lv_obj_t *sl_vol = lv_slider_create(scr);
    lv_obj_set_size(sl_vol, 150, 10);
    lv_obj_set_pos(sl_vol, 120, 71);
    lv_slider_set_range(sl_vol, 0, 255);
    lv_slider_set_value(sl_vol, 128, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl_vol, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Theme toggle */
    lv_obj_t *btn_theme = lv_btn_create(scr);
    lv_obj_set_size(btn_theme, 100, 35);
    lv_obj_set_pos(btn_theme, 20, 96);
    lv_obj_set_style_bg_color(btn_theme, lv_color_hex(th_btn), 0);
    lv_obj_add_event_cb(btn_theme, theme_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_th = lv_label_create(btn_theme);
    char theme_buf[32];
    snprintf(theme_buf, sizeof(theme_buf), "%s " LV_SYMBOL_REFRESH, theme_get_name(theme_get_current()));
    lv_label_set_text(lbl_th, theme_buf);
    lv_obj_center(lbl_th);

    /* Webserver toggle (on-demand portal) */
    btn_ws = lv_btn_create(scr);
    lv_obj_set_size(btn_ws, 130, 35);
    lv_obj_set_pos(btn_ws, 140, 96);
    lv_obj_set_style_bg_color(btn_ws, lv_color_hex(th_btn), 0);
    lv_obj_add_event_cb(btn_ws, webserver_toggle_cb, LV_EVENT_CLICKED, NULL);
    lbl_ws_btn = lv_label_create(btn_ws);
    lv_label_set_text(lbl_ws_btn, "Start Portal");
    lv_obj_center(lbl_ws_btn);

    /* Connectivity icons/status */
    lv_obj_t *lbl_conn = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_conn, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_conn, lv_color_hex(th_text), 0);
    lv_obj_set_pos(lbl_conn, 300, 38);
    lv_label_set_text(lbl_conn, "Connectivity");

    lbl_wifi = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_wifi, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_wifi, 300, 62);
    lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " WiFi: --");

    lbl_bt = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_bt, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_bt, lv_color_hex(0x666666), 0);
    lv_obj_set_pos(lbl_bt, 300, 84);
    lv_label_set_text(lbl_bt, LV_SYMBOL_BLUETOOTH " BT: Off");

    lbl_ws = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_ws, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_ws, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_ws, 300, 106);
    lv_label_set_text(lbl_ws, "Portal: Off");

    /* Performance stats */
    lbl_perf = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_perf, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_perf, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_perf, 470, 38);
    lv_label_set_text(lbl_perf, "Perf: loading...");

    lv_disp_load_scr(scr);
}

void scr_settings_update(void)
{
    if (!lbl_perf)
        return;

    if (lbl_wifi)
    {
        if (wifi_is_connected())
        {
            lv_label_set_text_fmt(lbl_wifi, LV_SYMBOL_WIFI " WiFi: %s", wifi_get_sta_ip());
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(th_accent), 0);
        }
        else
        {
            lv_label_set_text(lbl_wifi, LV_SYMBOL_WIFI " WiFi: Disconnected");
            lv_obj_set_style_text_color(lbl_wifi, lv_color_hex(0x666666), 0);
        }
    }

    if (lbl_ws)
    {
        if (s_webserver_on)
        {
            if (wifi_is_connected())
                lv_label_set_text_fmt(lbl_ws, "Portal: On (%s)", wifi_get_sta_ip());
            else
                lv_label_set_text(lbl_ws, "Portal: On (AP 192.168.4.1)");
        }
        else
        {
            lv_label_set_text(lbl_ws, "Portal: Off");
        }
    }

    uint32_t min_f, max_f, avg_f, cnt_f;
    uint32_t min_t, max_t, avg_t, cnt_t;
    char buf[256];
    int pos = 0;

    if (perf_get_stats(PERF_SLOT_FRAME_TIME, &min_f, &max_f, &avg_f, &cnt_f))
    {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Frame: %lu/%lu/%lu ms\n", min_f, avg_f, max_f);
    }
    if (perf_get_stats(PERF_SLOT_TOUCH_READ, &min_t, &max_t, &avg_t, &cnt_t))
    {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Touch: %lu/%lu/%lu ms\n", min_t, avg_t, max_t);
    }

    uint32_t heap_free = esp_get_free_heap_size();
    snprintf(buf + pos, sizeof(buf) - pos,
             "Heap: %lu KB free", heap_free / 1024);

    lv_label_set_text(lbl_perf, buf);
}

void scr_settings_destroy(void)
{
    if (scr)
    {
        lbl_perf = NULL;
        lbl_wifi = NULL;
        lbl_bt = NULL;
        lbl_ws = NULL;
        btn_ws = NULL;
        lbl_ws_btn = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}
