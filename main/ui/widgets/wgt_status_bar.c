/**
 * @file wgt_status_bar.c
 * @brief Status bar widget — WiFi, battery, time header.
 */

#include "ui/theme.h"
#include "hw_config.h"
#include "lvgl.h"

lv_obj_t *wgt_status_bar_create(lv_obj_t *parent)
{
    lv_obj_t *bar = lv_obj_create(parent);
    lv_obj_set_size(bar, LCD_H_RES, 24);
    lv_obj_set_pos(bar, 0, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(bar, lv_color_hex(th_card), 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_radius(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 2, 0);

    lv_obj_t *lbl = lv_label_create(bar);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(th_label), 0);
    lv_obj_align(lbl, LV_ALIGN_LEFT_MID, 4, 0);
    lv_label_set_text(lbl, LV_SYMBOL_WIFI " ESPro AI");

    return bar;
}
