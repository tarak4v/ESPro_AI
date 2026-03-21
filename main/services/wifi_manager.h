/**
 * @file wifi_manager.h
 * @brief WiFi connection manager + NTP sync.
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>

void wifi_manager_init(void);
bool wifi_is_connected(void);

#endif /* WIFI_MANAGER_H */
