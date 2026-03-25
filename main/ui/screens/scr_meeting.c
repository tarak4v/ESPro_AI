/**
 * @file scr_meeting.c
 * @brief Meeting assistant screen — controls and live notes on 640x172 AMOLED.
 *
 * Layout:
 *   [MIC btn] [VIDEO btn] [HAND btn] | [Meeting info + notes] | [HANDOFF/VOL]
 *   Status bar: app name, duration, audio device
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "hw_config.h"
#include "services/meeting_service.h"
#include "lvgl.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static lv_obj_t *scr = NULL;

/* Control buttons */
static lv_obj_t *btn_mic = NULL;
static lv_obj_t *lbl_mic = NULL;
static lv_obj_t *btn_video = NULL;
static lv_obj_t *lbl_video = NULL;
static lv_obj_t *btn_hand = NULL;
static lv_obj_t *lbl_hand = NULL;
static lv_obj_t *btn_handoff = NULL;
static lv_obj_t *lbl_handoff = NULL;

/* Info area */
static lv_obj_t *lbl_app_name = NULL;
static lv_obj_t *lbl_duration = NULL;
static lv_obj_t *lbl_notes = NULL;
static lv_obj_t *lbl_audio_dev = NULL;

/* Volume buttons */
static lv_obj_t *btn_vol_up = NULL;
static lv_obj_t *btn_vol_down = NULL;

/* No meeting label */
static lv_obj_t *lbl_no_meeting = NULL;

/* ── Button callbacks ── */
static void mic_cb(lv_event_t *e) { meeting_toggle_mic(); }
static void video_cb(lv_event_t *e) { meeting_toggle_video(); }
static void hand_cb(lv_event_t *e) { meeting_toggle_hand(); }
static void vol_up_cb(lv_event_t *e) { meeting_volume_up(); }
static void vol_down_cb(lv_event_t *e) { meeting_volume_down(); }
static void audio_dev_cb(lv_event_t *e) { meeting_next_audio_device(); }

static void handoff_cb(lv_event_t *e)
{
    const meeting_state_t *m = meeting_get_state();
    if (m->handoff_active)
        meeting_stop_handoff();
    else
        meeting_start_handoff();
}

static void summary_cb(lv_event_t *e)
{
    meeting_request_summary();
}

/* ── Helper: create a control button ── */
static lv_obj_t *make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                          const char *text, lv_event_cb_t cb, lv_obj_t **lbl_out)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_style_bg_color(btn, lv_color_hex(th_card), 0);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *lbl = lv_label_create(btn);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl, lv_color_hex(th_text), 0);
    lv_obj_center(lbl);
    lv_label_set_text(lbl, text);

    if (lbl_out)
        *lbl_out = lbl;
    return btn;
}

void scr_meeting_create(void)
{
    scr = lv_obj_create(NULL);
    lv_obj_set_size(scr, LCD_H_RES, LCD_V_RES);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(scr, lv_color_hex(th_bg), 0);

    const meeting_state_t *m = meeting_get_state();

    /* ── "No meeting" overlay — shown when inactive ── */
    lbl_no_meeting = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_no_meeting, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(lbl_no_meeting, lv_color_hex(th_label), 0);
    lv_obj_center(lbl_no_meeting);
    lv_label_set_text(lbl_no_meeting, "No active meeting\nStart meeting on desktop");
    lv_obj_set_style_text_align(lbl_no_meeting, LV_TEXT_ALIGN_CENTER, 0);

    /* ── Left panel: large control buttons (3 rows) ── */
    /* Mic */
    btn_mic = make_btn(scr, 6, 4, 90, 44, LV_SYMBOL_AUDIO " Mic", mic_cb, &lbl_mic);

    /* Video */
    btn_video = make_btn(scr, 6, 52, 90, 44, LV_SYMBOL_EYE_OPEN " Cam", video_cb, &lbl_video);

    /* Hand raise */
    btn_hand = make_btn(scr, 6, 100, 90, 44, LV_SYMBOL_UP " Hand", hand_cb, &lbl_hand);

    /* ── Right panel: handoff + volume (stacked) ── */
    /* Handoff / Away */
    btn_handoff = make_btn(scr, 540, 4, 92, 40, "Away", handoff_cb, &lbl_handoff);

    /* Volume up */
    btn_vol_up = make_btn(scr, 540, 48, 44, 36, LV_SYMBOL_PLUS, vol_up_cb, NULL);

    /* Volume down */
    btn_vol_down = make_btn(scr, 588, 48, 44, 36, LV_SYMBOL_MINUS, vol_down_cb, NULL);

    /* Audio device button */
    make_btn(scr, 540, 88, 92, 36, LV_SYMBOL_AUDIO " Dev", audio_dev_cb, NULL);

    /* Summary request button */
    make_btn(scr, 540, 128, 92, 36, "Summary", summary_cb, NULL);

    /* ── Center: meeting info + live notes ── */
    /* App name + duration header */
    lbl_app_name = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_app_name, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(lbl_app_name, lv_color_hex(th_accent), 0);
    lv_obj_set_pos(lbl_app_name, 106, 4);
    lv_label_set_text(lbl_app_name, "No Meeting");

    lbl_duration = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_duration, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_duration, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_duration, 106, 22);
    lv_label_set_text(lbl_duration, "00:00");

    /* Separator */
    lv_obj_t *sep = lv_obj_create(scr);
    lv_obj_set_size(sep, 420, 1);
    lv_obj_set_pos(sep, 106, 38);
    lv_obj_set_style_bg_color(sep, lv_color_hex(th_label), 0);
    lv_obj_set_style_bg_opa(sep, LV_OPA_40, 0);
    lv_obj_set_style_border_width(sep, 0, 0);
    lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);

    /* Live notes area */
    lbl_notes = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_notes, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_notes, lv_color_hex(th_text), 0);
    lv_obj_set_pos(lbl_notes, 106, 42);
    lv_obj_set_size(lbl_notes, 424, 100);
    lv_label_set_long_mode(lbl_notes, LV_LABEL_LONG_CLIP);
    lv_label_set_text(lbl_notes, "");

    /* Audio device label — bottom of center area */
    lbl_audio_dev = lv_label_create(scr);
    lv_obj_set_style_text_font(lbl_audio_dev, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lbl_audio_dev, lv_color_hex(th_label), 0);
    lv_obj_set_pos(lbl_audio_dev, 106, 148);
    lv_obj_set_width(lbl_audio_dev, 420);
    lv_label_set_long_mode(lbl_audio_dev, LV_LABEL_LONG_DOT);
    lv_label_set_text(lbl_audio_dev, "");

    lv_disp_load_scr(scr);
}

void scr_meeting_update(void)
{
    if (!scr)
        return;

    const meeting_state_t *m = meeting_get_state();

    /* Show/hide "no meeting" overlay */
    if (lbl_no_meeting)
    {
        if (m->active)
            lv_obj_add_flag(lbl_no_meeting, LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_clear_flag(lbl_no_meeting, LV_OBJ_FLAG_HIDDEN);
    }

    /* ── Update button states ── */
    if (btn_mic)
    {
        uint32_t col = m->mic_on ? 0x4CAF50 : 0xE53935; /* green / red */
        lv_obj_set_style_bg_color(btn_mic, lv_color_hex(col), 0);
        if (lbl_mic)
            lv_label_set_text(lbl_mic, m->mic_on ? LV_SYMBOL_AUDIO " ON" : LV_SYMBOL_MUTE " OFF");
    }

    if (btn_video)
    {
        uint32_t col = m->video_on ? 0x4CAF50 : 0xE53935;
        lv_obj_set_style_bg_color(btn_video, lv_color_hex(col), 0);
        if (lbl_video)
            lv_label_set_text(lbl_video, m->video_on ? LV_SYMBOL_EYE_OPEN " ON" : LV_SYMBOL_EYE_CLOSE " OFF");
    }

    if (btn_hand)
    {
        uint32_t col = m->hand_raised ? 0xFF9800 : th_card;
        lv_obj_set_style_bg_color(btn_hand, lv_color_hex(col), 0);
        if (lbl_hand)
            lv_label_set_text(lbl_hand, m->hand_raised ? LV_SYMBOL_UP " UP" : LV_SYMBOL_UP " Hand");
    }

    if (btn_handoff)
    {
        uint32_t col = m->handoff_active ? 0x9C27B0 : th_card;
        lv_obj_set_style_bg_color(btn_handoff, lv_color_hex(col), 0);
        if (lbl_handoff)
            lv_label_set_text(lbl_handoff, m->handoff_active ? "Back" : "Away");
    }

    /* ── App name + duration ── */
    if (lbl_app_name)
    {
        if (m->active)
        {
            char upper[16];
            strncpy(upper, m->app, sizeof(upper) - 1);
            upper[sizeof(upper) - 1] = '\0';
            /* Capitalize first letter */
            if (upper[0] >= 'a' && upper[0] <= 'z')
                upper[0] -= 32;
            lv_label_set_text_fmt(lbl_app_name, "%s Meeting", upper);
        }
        else
        {
            lv_label_set_text(lbl_app_name, "No Meeting");
        }
    }

    if (lbl_duration && m->active)
    {
        uint32_t d = m->duration_s;
        uint32_t h = d / 3600;
        uint32_t mins = (d % 3600) / 60;
        uint32_t secs = d % 60;
        if (h > 0)
            lv_label_set_text_fmt(lbl_duration, "%lu:%02lu:%02lu",
                                  (unsigned long)h, (unsigned long)mins, (unsigned long)secs);
        else
            lv_label_set_text_fmt(lbl_duration, "%02lu:%02lu",
                                  (unsigned long)mins, (unsigned long)secs);
    }

    /* ── Notes display ── */
    if (lbl_notes)
    {
        if (m->summary_dirty)
        {
            lv_label_set_text(lbl_notes, m->summary);
            /* Clear dirty flag — cast away const for internal flag */
            ((meeting_state_t *)m)->summary_dirty = false;
        }
        else if (m->handoff_dirty)
        {
            lv_label_set_text(lbl_notes, m->handoff);
            ((meeting_state_t *)m)->handoff_dirty = false;
        }
        else if (m->notes_dirty)
        {
            lv_label_set_text(lbl_notes, m->live_note);
            ((meeting_state_t *)m)->notes_dirty = false;
        }
    }

    /* ── Audio device ── */
    if (lbl_audio_dev)
    {
        if (m->audio_device[0])
            lv_label_set_text(lbl_audio_dev, m->audio_device);
        else
            lv_label_set_text(lbl_audio_dev, "");
    }
}

void scr_meeting_destroy(void)
{
    if (scr)
    {
        btn_mic = btn_video = btn_hand = btn_handoff = NULL;
        lbl_mic = lbl_video = lbl_hand = lbl_handoff = NULL;
        btn_vol_up = btn_vol_down = NULL;
        lbl_app_name = lbl_duration = lbl_notes = lbl_audio_dev = NULL;
        lbl_no_meeting = NULL;
        lv_obj_del(scr);
        scr = NULL;
    }
}
