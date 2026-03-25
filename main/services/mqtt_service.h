/**
 * @file mqtt_service.h
 * @brief MQTT client service — pub/sub for Home Assistant & smart home integration.
 *
 * Publishes watch sensor data (steps, battery, health) to configurable topics.
 * Subscribes to command topics for remote control (theme, notification, etc.).
 * Broker URL, credentials, and topics are all config-driven via config_manager.
 */
#ifndef MQTT_SERVICE_H
#define MQTT_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Initialise the MQTT service.
 *
 * Reads broker config from config_manager. If mqtt_enabled and broker URL
 * are set, connects to the broker. Subscribes to command topics.
 * Starts a background publish task for periodic sensor data.
 */
void mqtt_service_init(void);

/**
 * @brief Check if MQTT is currently connected to broker.
 */
bool mqtt_is_connected(void);

/**
 * @brief Publish a message to a topic.
 * @param topic  MQTT topic string (e.g. "espro/health/steps").
 * @param data   Payload string.
 * @return true if published (or queued) successfully.
 */
bool mqtt_publish(const char *topic, const char *data);

/**
 * @brief Publish a key=value to the device's base topic.
 * @param key   Sub-topic appended to base (e.g. "steps" → "espro/watch/steps").
 * @param value Payload string.
 */
bool mqtt_publish_sensor(const char *key, const char *value);

/**
 * @brief Force an immediate publish of all sensor data.
 */
void mqtt_publish_all_sensors(void);

/**
 * @brief Disconnect and stop the MQTT service.
 */
void mqtt_service_stop(void);

/**
 * @brief Reconnect MQTT (e.g. after config change).
 */
void mqtt_service_reconnect(void);

/**
 * @brief Get MQTT connection status string.
 */
const char *mqtt_status_str(void);

/**
 * @brief Subscribe to an additional MQTT topic (e.g. for meeting service).
 */
void mqtt_subscribe_extra(const char *topic);

#endif /* MQTT_SERVICE_H */
