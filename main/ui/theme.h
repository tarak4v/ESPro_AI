/**
 * @file theme.h
 * @brief Theme engine — 5 distinct palettes with pastel colors and
 *        complementary font colors. Config-driven via config_manager.
 *
 * Themes: Dark, Light, Autumn, Spring, Monsoon.
 */
#ifndef UI_THEME_H
#define UI_THEME_H

#include <stdint.h>
#include <stdbool.h>
#include "services/config_manager.h"

/* Global palette variables — used by all UI code */
extern uint32_t th_bg, th_card, th_text, th_label, th_btn, th_accent;

/* Legacy — kept for backward compat, true if theme_id == THEME_DARK */
extern bool g_theme_dark;

/**
 * @brief Initialise theme from config_manager (call after config_manager_init).
 */
void theme_init(void);

/**
 * @brief Apply a specific theme by ID.
 */
void theme_apply(theme_id_t id);

/**
 * @brief Cycle to next theme and save.
 */
void theme_cycle(void);

/**
 * @brief Get current theme ID.
 */
theme_id_t theme_get_current(void);

/**
 * @brief Get theme display name.
 */
const char *theme_get_name(theme_id_t id);

/* Legacy API — wraps theme_apply for Dark/Light */
void theme_set_dark(bool dark);
void theme_toggle(void);

#endif /* UI_THEME_H */
