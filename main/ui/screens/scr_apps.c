/**
 * @file scr_apps.c
 * @brief Apps/launcher screen — grid of app icons.
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "services/music_stream.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *scr = NULL;

typedef struct
{
    const char *icon;
    const char *label;
} app_entry_t;

static const app_entry_t s_apps[] = {
    {LV_SYMBOL_AUDIO, "Music"},
    {LV_SYMBOL_SETTINGS, "Settings"},
    {LV_SYMBOL_CALL, "Meeting"},
    {LV_SYMBOL_CALL, "AI Chat"},
};
#define APP_COUNT (sizeof(s_apps) / sizeof(s_apps[0]))

static void app_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    switch (idx)
    {
    case 0: /* Music — toggle radio */
        if (music_stream_is_playing())
        {
            music_stream_stop();
            ui_switch_screen(SCR_HOME);
            scr_home_set_stage_text("Radio stopped");
        }
        else
        {
            music_stream_play();
            ui_switch_screen(SCR_HOME);
            {
                char msg[64];
                snprintf(msg, sizeof(msg), "Radio: %s", music_stream_station_name());
                scr_home_set_stage_text(msg);
            }
        }
        break;
    case 1: /* Settings */
        ui_switch_screen(SCR_SETTINGS);
        break;
    case 2: /* Meeting — open meeting screen */
        ui_switch_screen(SCR_MEETING);
        break;
    case 3: /* AI Chat — go home, ready for voice */
        ui_switch_screen(SCR_HOME);
        scr_home_set_stage_text("Hold mic to speak");
        break;
    default:
        break;
    }
}

void scr_apps_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* Layout apps in a horizontal row (fits well on 640×172) */
    int spacing = LCD_H_RES / (APP_COUNT + 1);
    for (int i = 0; i < (int)APP_COUNT; i++)
    {
        lv_obj_t *btn = lv_btn_create(scr);
        lv_obj_set_size(btn, 80, 80);
        lv_obj_set_pos(btn, spacing * (i + 1) - 40, 20);
        lv_obj_set_style_bg_color(btn, lv_color_hex(th_card), 0);
        lv_obj_set_style_radius(btn, 15, 0);
        lv_obj_add_event_cb(btn, app_btn_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)i);

        lv_obj_t *icon = lv_label_create(btn);
        lv_obj_set_style_text_font(icon, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(icon, lv_color_hex(th_text), 0);
        lv_obj_center(icon);
        lv_label_set_text(icon, s_apps[i].icon);

        lv_obj_t *lbl = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, lv_color_hex(th_label), 0);
        lv_obj_set_pos(lbl, spacing * (i + 1) - 20, 110);
        lv_label_set_text(lbl, s_apps[i].label);
    }

    lv_disp_load_scr(scr);
}

void scr_apps_update(void)
{
    /* Apps screen is static — no periodic updates needed */
}

void scr_apps_destroy(void)
{
    if (scr)
    {
        lv_obj_del(scr);
        scr = NULL;
    }
}
