/**
 * @file scr_health.c
 * @brief Health screen — steps, distance, calories, active minutes.
 *
 * Layout (640×172):
 *   [STEPS arc]  [DISTANCE]  [CALORIES]  [ACTIVE MIN]
 *   Big step count + progress arc on left, 3 metric cards on right
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "hal/imu.h"
#include "lvgl.h"
#include <stdio.h>

static lv_obj_t *scr = NULL;

/* Step arc + label */
static lv_obj_t *arc_steps = NULL;
static lv_obj_t *lbl_steps_val = NULL;
static lv_obj_t *lbl_steps_unit = NULL;

/* Metric cards */
static lv_obj_t *lbl_dist_val = NULL;
static lv_obj_t *lbl_cal_val = NULL;
static lv_obj_t *lbl_active_val = NULL;
static lv_obj_t *lbl_title = NULL;

#define STEP_GOAL  8000  /* Daily step goal */

static lv_obj_t *make_metric_card(lv_obj_t *parent, int x, int y,
                                   const char *icon, const char *unit,
                                   lv_obj_t **val_label)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_set_size(card, 130, 130);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_style_bg_color(card, lv_color_hex(th_card), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 12, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 8, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    /* Icon */
    lv_obj_t *lbl_icon = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_icon, lv_color_hex(th_accent), 0);
    lv_obj_align(lbl_icon, LV_ALIGN_TOP_MID, 0, 0);
    lv_label_set_text(lbl_icon, icon);

    /* Value */
    *val_label = lv_label_create(card);
    lv_obj_set_style_text_font(*val_label, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(*val_label, lv_color_hex(th_text), 0);
    lv_obj_align(*val_label, LV_ALIGN_CENTER, 0, 6);
    lv_label_set_text(*val_label, "0");

    /* Unit label */
    lv_obj_t *lbl_u = lv_label_create(card);
    lv_obj_set_style_text_font(lbl_u, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_u, lv_color_hex(th_label), 0);
    lv_obj_align(lbl_u, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_label_set_text(lbl_u, unit);

    return card;
}

void scr_health_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    /* Title */
    lbl_title = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_title, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_title, 20, 4);
    lv_label_set_text(lbl_title, LV_SYMBOL_GPS " Health");

    /* ── Step arc (left side) ── */
    arc_steps = lv_arc_create(scr);
    lv_obj_set_size(arc_steps, 140, 140);
    lv_obj_set_pos(arc_steps, 15, 22);
    lv_arc_set_rotation(arc_steps, 135);
    lv_arc_set_bg_angles(arc_steps, 0, 270);
    lv_arc_set_range(arc_steps, 0, STEP_GOAL);
    lv_arc_set_value(arc_steps, 0);
    lv_obj_remove_style(arc_steps, NULL, LV_PART_KNOB);
    lv_obj_clear_flag(arc_steps, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_color(arc_steps, lv_color_hex(0x333333), LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc_steps, lv_color_hex(th_accent), LV_PART_INDICATOR);
    lv_obj_set_style_arc_width(arc_steps, 10, LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc_steps, 10, LV_PART_INDICATOR);

    /* Step count — centered in arc */
    lbl_steps_val = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_steps_val, &lv_font_montserrat_36, 0);
    lv_obj_set_style_text_color(lbl_steps_val, lv_color_hex(th_text), 0);
    lv_obj_set_pos(lbl_steps_val, 48, 62);
    lv_label_set_text(lbl_steps_val, "0");

    lbl_steps_unit = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_steps_unit, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_steps_unit, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_steps_unit, 55, 100);
    lv_label_set_text(lbl_steps_unit, "steps");

    /* ── Metric cards ── */
    make_metric_card(scr, 180, 22, LV_SYMBOL_GPS, "meters", &lbl_dist_val);
    make_metric_card(scr, 330, 22, LV_SYMBOL_CHARGE, "kcal", &lbl_cal_val);
    make_metric_card(scr, 480, 22, LV_SYMBOL_LOOP, "active min", &lbl_active_val);

    lv_disp_load_scr(scr);
}

void scr_health_update(void)
{
    if (!scr) return;

    health_data_t h = imu_get_health();

    /* Steps arc + value */
    if (arc_steps) {
        int val = (int)h.steps;
        if (val > STEP_GOAL) val = STEP_GOAL;
        lv_arc_set_value(arc_steps, val);
    }
    if (lbl_steps_val)
        lv_label_set_text_fmt(lbl_steps_val, "%lu", (unsigned long)h.steps);

    /* Metric cards */
    if (lbl_dist_val)
        lv_label_set_text_fmt(lbl_dist_val, "%lu", (unsigned long)h.distance_m);
    if (lbl_cal_val)
        lv_label_set_text_fmt(lbl_cal_val, "%lu", (unsigned long)h.calories);
    if (lbl_active_val)
        lv_label_set_text_fmt(lbl_active_val, "%lu", (unsigned long)h.active_min);
}

void scr_health_destroy(void)
{
    if (scr) {
        arc_steps = NULL;
        lbl_steps_val = lbl_steps_unit = NULL;
        lbl_dist_val = lbl_cal_val = lbl_active_val = NULL;
        lbl_title = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}
