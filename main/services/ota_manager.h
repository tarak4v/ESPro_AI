/**
 * @file ota_manager.h
 * @brief OTA firmware update manager.
 *
 * Supports HTTPS OTA from a configured URL.
 * The update runs in a background task and reboots on success.
 */
#ifndef OTA_MANAGER_H
#define OTA_MANAGER_H

#include <stdbool.h>

typedef enum
{
    OTA_STATE_IDLE = 0,
    OTA_STATE_DOWNLOADING,
    OTA_STATE_DONE,
    OTA_STATE_ERROR,
} ota_state_t;

/**
 * @brief Initialise OTA subsystem.
 */
void ota_manager_init(void);

/**
 * @brief Start an OTA update from the given URL.
 * @param url  HTTPS URL to firmware .bin file.
 * @return true if the update task was started.
 */
bool ota_start_update(const char *url);

/**
 * @brief Get current OTA state.
 */
ota_state_t ota_get_state(void);

/**
 * @brief Get OTA progress percentage (0-100).
 */
int ota_get_progress(void);

#endif /* OTA_MANAGER_H */
