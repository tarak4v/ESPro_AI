/**
 * @file scr_home.c
 * @brief Home screen — clock, date, weather, voice indicator, status bar.
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "hal/rtc.h"
#include "lvgl.h"
#include "esp_log.h"
#include <time.h>
#include <stdio.h>

static lv_obj_t *scr         = NULL;
static lv_obj_t *lbl_time    = NULL;
static lv_obj_t *lbl_date    = NULL;
static lv_obj_t *lbl_weather = NULL;
static lv_obj_t *lbl_status  = NULL;

void scr_home_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* Time — large centered */
    lbl_time = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_time, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(lbl_time, lv_color_hex(th_text), 0);
    lv_obj_align(lbl_time, LV_ALIGN_CENTER, 0, -20);
    lv_label_set_text(lbl_time, "00:00");

    /* Date */
    lbl_date = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_date, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl_date, lv_color_hex(th_label), 0);
    lv_obj_align(lbl_date, LV_ALIGN_CENTER, 0, 30);
    lv_label_set_text(lbl_date, "Mon, Jan 01");

    /* Weather */
    lbl_weather = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_weather, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_weather, lv_color_hex(th_label), 0);
    lv_obj_align(lbl_weather, LV_ALIGN_CENTER, 0, 55);
    lv_label_set_text(lbl_weather, "-- °C");

    /* Status bar */
    lbl_status = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_status, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_status, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_status, 10, 5);
    lv_label_set_text(lbl_status, "WiFi --  Steps: 0");

    lv_disp_load_scr(scr);
}

void scr_home_update(void)
{
    if (!scr) return;

    time_t now;
    struct tm ti;
    time(&now);
    localtime_r(&now, &ti);

    static const char *days[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
    static const char *mons[] = {"Jan","Feb","Mar","Apr","May","Jun",
                                  "Jul","Aug","Sep","Oct","Nov","Dec"};

    lv_label_set_text_fmt(lbl_time, "%02d:%02d", ti.tm_hour, ti.tm_min);
    lv_label_set_text_fmt(lbl_date, "%s, %s %02d",
                          days[ti.tm_wday], mons[ti.tm_mon], ti.tm_mday);
}

void scr_home_destroy(void)
{
    if (scr) {
        lbl_time = lbl_date = lbl_weather = lbl_status = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}
