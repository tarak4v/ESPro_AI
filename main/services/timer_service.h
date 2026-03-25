/**
 * @file timer_service.h
 * @brief Simple countdown timer with beep alarm.
 */
#ifndef TIMER_SERVICE_H
#define TIMER_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/** Start a countdown timer (seconds). When it expires, beep + notify UI. */
void timer_start(uint32_t seconds);

/** Stop the running timer. */
void timer_stop(void);

/** Get remaining seconds (0 if not running). */
uint32_t timer_get_remaining(void);

/** Is the timer currently running? */
bool timer_is_running(void);

#endif /* TIMER_SERVICE_H */
