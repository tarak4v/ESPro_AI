/**
 * @file power_manager.h
 * @brief Display sleep/wake management — auto-dim, auto-sleep, wake on gesture/touch.
 */
#ifndef SERVICES_POWER_MANAGER_H
#define SERVICES_POWER_MANAGER_H

#include <stdbool.h>

/** Initialise power manager — subscribes to input/sensor events. */
void power_manager_init(void);

/** Notify user activity (called on touch, voice, etc.). */
void power_manager_activity(void);

/** Returns true if display is currently sleeping. */
bool power_manager_is_sleeping(void);

/** Called periodically from app_manager to check idle timeout. */
void power_manager_tick(void);

#endif /* SERVICES_POWER_MANAGER_H */
