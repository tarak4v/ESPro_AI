/**
 * @file ui_manager.h
 * @brief LVGL render task + screen lifecycle management.
 */
#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include "lvgl.h"

typedef enum
{
    SCR_HOME = 0,
    SCR_APPS,
    SCR_SETTINGS,
    SCR_MEETING,
    SCR_COUNT,
} screen_id_t;

/** Initialise LVGL library (call before display_init registers driver). */
void ui_manager_init(void);

/** LVGL render task entry point (Core 0, prio 5). */
void lvgl_render_task(void *arg);

/** App manager task — handles screen transitions + app logic. */
void app_manager_task(void *arg);

/** Switch to a new screen (thread-safe, posts event). */
void ui_switch_screen(screen_id_t id);

/** Get current screen ID. */
screen_id_t ui_get_current_screen(void);

/** Set the center stage text on the home screen (thread-safe, buffered). */
void scr_home_set_stage_text(const char *text);

#endif /* UI_MANAGER_H */
