/**
 * @file theme.h
 * @brief Theme engine — dark/light palette, dynamic switching.
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>
#include <stdbool.h>

extern uint32_t th_bg, th_card, th_text, th_label, th_btn, th_accent;
extern bool g_theme_dark;

void theme_init(void);
void theme_set_dark(bool dark);
void theme_toggle(void);

#endif /* UI_THEME_H */
