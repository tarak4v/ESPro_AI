/**
 * @file scr_apps.c
 * @brief Apps/launcher screen — grid of app icons.
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "lvgl.h"

static lv_obj_t *scr = NULL;

typedef struct {
    const char *icon;
    const char *label;
} app_entry_t;

static const app_entry_t s_apps[] = {
    { LV_SYMBOL_AUDIO,    "Music" },
    { LV_SYMBOL_SETTINGS, "Settings" },
    { LV_SYMBOL_WIFI,     "WiFi" },
    { LV_SYMBOL_BLUETOOTH,"BLE" },
    { LV_SYMBOL_GPS,      "Health" },
    { LV_SYMBOL_CALL,     "AI Chat" },
};
#define APP_COUNT (sizeof(s_apps) / sizeof(s_apps[0]))

static void app_btn_cb(lv_event_t *e)
{
    int idx = (int)(intptr_t)lv_event_get_user_data(e);
    if (idx == 1) ui_switch_screen(SCR_SETTINGS);
    /* Other app handlers can post events to the agent orchestrator */
}

void scr_apps_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* Layout apps in a horizontal row (fits well on 640×172) */
    int spacing = LCD_H_RES / (APP_COUNT + 1);
    for (int i = 0; i < (int)APP_COUNT; i++) {
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
    if (scr) { lv_obj_del(scr); scr = NULL; }
}
