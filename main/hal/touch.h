/**
 * @file touch.h
 * @brief Touch HAL — AXS15231B capacitive touch (I2C bus 1).
 */
#ifndef HAL_TOUCH_H
#define HAL_TOUCH_H

#include <stdint.h>
#include <stdbool.h>

/** Initialise touch controller on I2C bus 1. */
void touch_init(void);

/** Touch input task (Core 0) — reads touch, posts events to event bus. */
void touch_input_task(void *arg);

/** Get last touch coordinates (for LVGL indev). */
bool touch_get_last(int16_t *x, int16_t *y, bool *pressed);

#endif /* HAL_TOUCH_H */
