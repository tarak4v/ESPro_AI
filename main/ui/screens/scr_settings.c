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
#include "lvgl.h"
#include "esp_log.h"

static lv_obj_t *scr = NULL;
static lv_obj_t *lbl_perf = NULL;

static void theme_toggle_cb(lv_event_t *e)
{
    theme_toggle();
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
    lv_obj_set_pos(title, 20, 10);
    lv_label_set_text(title, LV_SYMBOL_SETTINGS " Settings");

    /* Brightness slider */
    lv_obj_t *lbl_br = lv_label_create(scr);
    lv_label_set_text(lbl_br, "Brightness");
    lv_obj_set_style_text_color(lbl_br, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_br, 20, 50);

    lv_obj_t *sl_br = lv_slider_create(scr);
    lv_obj_set_size(sl_br, 150, 10);
    lv_obj_set_pos(sl_br, 120, 55);
    lv_slider_set_range(sl_br, 0, 255);
    lv_slider_set_value(sl_br, 0, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl_br, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Volume slider */
    lv_obj_t *lbl_vol = lv_label_create(scr);
    lv_label_set_text(lbl_vol, "Volume");
    lv_obj_set_style_text_color(lbl_vol, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_vol, 20, 90);

    lv_obj_t *sl_vol = lv_slider_create(scr);
    lv_obj_set_size(sl_vol, 150, 10);
    lv_obj_set_pos(sl_vol, 120, 95);
    lv_slider_set_range(sl_vol, 0, 255);
    lv_slider_set_value(sl_vol, 128, LV_ANIM_OFF);
    lv_obj_add_event_cb(sl_vol, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);

    /* Theme toggle */
    lv_obj_t *btn_theme = lv_btn_create(scr);
    lv_obj_set_size(btn_theme, 100, 35);
    lv_obj_set_pos(btn_theme, 20, 130);
    lv_obj_set_style_bg_color(btn_theme, lv_color_hex(th_btn), 0);
    lv_obj_add_event_cb(btn_theme, theme_toggle_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *lbl_th = lv_label_create(btn_theme);
    lv_label_set_text(lbl_th, g_theme_dark ? "Light" : "Dark");
    lv_obj_center(lbl_th);

    /* Performance stats */
    lbl_perf = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_perf, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_perf, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_perf, 320, 20);
    lv_label_set_text(lbl_perf, "Perf: loading...");

    lv_disp_load_scr(scr);
}

void scr_settings_update(void)
{
    if (!lbl_perf) return;

    uint32_t min_f, max_f, avg_f, cnt_f;
    uint32_t min_t, max_t, avg_t, cnt_t;
    char buf[256];
    int pos = 0;

    if (perf_get_stats(PERF_SLOT_FRAME_TIME, &min_f, &max_f, &avg_f, &cnt_f)) {
        pos += snprintf(buf + pos, sizeof(buf) - pos,
                        "Frame: %lu/%lu/%lu ms\n", min_f, avg_f, max_f);
    }
    if (perf_get_stats(PERF_SLOT_TOUCH_READ, &min_t, &max_t, &avg_t, &cnt_t)) {
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
    if (scr) {
        lbl_perf = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}
