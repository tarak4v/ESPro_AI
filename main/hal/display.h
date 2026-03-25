/**
 * @file display.h
 * @brief Display HAL — AXS15231B QSPI AMOLED (640×172).
 */
#ifndef HAL_DISPLAY_H
#define HAL_DISPLAY_H

#include <stdbool.h>
#include <stdint.h>

/** Initialise QSPI display + LVGL display driver. */
void display_init(void);

/** Set backlight brightness (0=full bright, 255=off, inverted PWM). */
void display_set_brightness(uint8_t duty);

/** Re-send DISPON to keep AMOLED panel awake. Call periodically. */
void display_keep_alive(void);

/** Flip frame buffer 180° for wrist orientation. */
void display_set_flip(bool flipped);

/** Get current flip state. */
bool display_get_flip(void);

#endif /* HAL_DISPLAY_H */
