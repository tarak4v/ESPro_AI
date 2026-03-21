/**
 * @file wgt_voice_indicator.c
 * @brief Voice indicator widget — animated mic icon showing AI state.
 *
 * States: idle (grey), listening (red pulse), processing (purple), responding (green).
 */

#include "ui/theme.h"
#include "hw_config.h"
#include "lvgl.h"

typedef enum {
    VOICE_IDLE,
    VOICE_LISTENING,
    VOICE_PROCESSING,
    VOICE_RESPONDING,
} voice_state_t;

static const uint32_t state_colors[] = {
    [VOICE_IDLE]       = 0x555555,
    [VOICE_LISTENING]  = 0xFF1744,
    [VOICE_PROCESSING] = 0xAA00FF,
    [VOICE_RESPONDING] = 0x00E676,
};

lv_obj_t *wgt_voice_indicator_create(lv_obj_t *parent, int x, int y)
{
    lv_obj_t *circle = lv_obj_create(parent);
    lv_obj_set_size(circle, 40, 40);
    lv_obj_set_pos(circle, x, y);
    lv_obj_set_style_radius(circle, 20, 0);
    lv_obj_set_style_bg_color(circle, lv_color_hex(state_colors[VOICE_IDLE]), 0);
    lv_obj_set_style_border_width(circle, 0, 0);
    lv_obj_clear_flag(circle, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *icon = lv_label_create(circle);
    lv_obj_set_style_text_font(icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(icon, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(icon);
    lv_label_set_text(icon, LV_SYMBOL_AUDIO);

    return circle;
}

void wgt_voice_indicator_set_state(lv_obj_t *indicator, int state)
{
    if (!indicator || state < 0 || state > VOICE_RESPONDING) return;
    lv_obj_set_style_bg_color(indicator, lv_color_hex(state_colors[state]), 0);
}
