/**
 * @file wifi_manager.h
 * @brief WiFi manager — STA + AP mode, scan, NTP sync, config-driven.
 */
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>

#define WIFI_SCAN_MAX_AP 20
#define WIFI_AP_SSID_PREFIX "ESPro_AI_"
#define WIFI_AP_PASS "espro1234"
#define WIFI_AP_CHANNEL 1
#define WIFI_AP_MAX_CONN 4

/* Scan result entry */
typedef struct
{
    char ssid[33];
    int8_t rssi;
    uint8_t authmode; /* wifi_auth_mode_t */
} wifi_scan_entry_t;

/**
 * @brief Init WiFi subsystem.
 *
 * Reads SSID/pass from config_manager. If credentials are present,
 * starts in STA mode. Always starts AP mode for config access.
 * Effectively runs APSTA mode.
 */
void wifi_manager_init(void);

/** @brief Check if STA is connected to an external AP. */
bool wifi_is_connected(void);

/**
 * @brief Trigger a WiFi scan (async, results via wifi_scan_get_results).
 * @return true if scan started successfully.
 */
bool wifi_scan_start(void);

/**
 * @brief Get cached scan results.
 * @param out        Array to fill (caller provides).
 * @param max_count  Max entries to return.
 * @return Number of entries filled.
 */
uint16_t wifi_scan_get_results(wifi_scan_entry_t *out, uint16_t max_count);

/** @brief Get the number of APs found in last scan. */
uint16_t wifi_scan_get_count(void);

/**
 * @brief Connect to a new AP (saves to config_manager).
 * @param ssid     SSID string.
 * @param pass     Password string.
 * @param hidden   true if SSID is hidden (not broadcast).
 */
void wifi_connect_to(const char *ssid, const char *pass, bool hidden);

/** @brief Get AP-mode IP address string (static: 192.168.4.1). */
const char *wifi_get_ap_ip(void);

/** @brief Get STA-mode IP address string (or "" if disconnected). */
const char *wifi_get_sta_ip(void);

#endif /* WIFI_MANAGER_H */
