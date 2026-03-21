/**
 * @file webserver.h
 * @brief HTTP config portal — serves configuration UI over WiFi.
 *
 * Accessible on both:
 *   - AP mode:  http://192.168.4.1/
 *   - STA mode: http://<device_ip>/
 *
 * Endpoints:
 *   GET  /           → Config page HTML
 *   GET  /api/config → JSON of all config
 *   POST /api/config → Set config values (JSON body)
 *   GET  /api/scan   → Trigger WiFi scan + return results
 *   POST /api/wifi   → Connect to a new WiFi network
 *   POST /api/ota    → Trigger OTA update
 *   POST /api/reset  → Factory reset
 */
#ifndef WEBSERVER_H
#define WEBSERVER_H

/**
 * @brief Start the HTTP config server on port 80.
 * Call after wifi_manager_init().
 */
void webserver_start(void);

/**
 * @brief Stop the HTTP config server.
 */
void webserver_stop(void);

#endif /* WEBSERVER_H */
