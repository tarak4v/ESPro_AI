/**
 * @file mqtt_service.c
 * @brief MQTT client — connects to broker, publishes sensor data, receives commands.
 *
 * Uses esp_mqtt_client (built into ESP-IDF). Publishes health, battery, and
 * sensor data periodically. Subscribes to command topics for remote control.
 */

#include "services/mqtt_service.h"
#include "services/config_manager.h"
#include "services/meeting_service.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"
#include "hal/imu.h"

#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "mqtt_svc";

/* ── State ── */
static esp_mqtt_client_handle_t s_client = NULL;
static bool s_connected = false;
static TaskHandle_t s_pub_task = NULL;

/* ── Topic prefix — built from config device_name ── */
static char s_base_topic[64]; /* e.g. "espro/watch" */

/* ── Forward declarations for command handling ── */
static void handle_incoming_command(const char *topic, int topic_len,
                                    const char *data, int data_len);

/* ────────────────────────────────────────────────────────────────────
 *  MQTT event handler (runs on mqtt task context)
 * ──────────────────────────────────────────────────────────────────── */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id)
    {
    case MQTT_EVENT_CONNECTED:
        s_connected = true;
        ESP_LOGI(TAG, "MQTT connected to broker");
        event_post(EVT_WIFI_CONNECTED, 2); /* reuse network event, ival=2 means MQTT */

        /* Subscribe to command topics: <base>/cmd/# */
        char sub_topic[80];
        snprintf(sub_topic, sizeof(sub_topic), "%s/cmd/#", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub_topic, 1);
        ESP_LOGI(TAG, "Subscribed to %s", sub_topic);

        /* Subscribe to HA notification topic */
        snprintf(sub_topic, sizeof(sub_topic), "%s/notify", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub_topic, 1);

        /* Subscribe to meeting state topics */
        snprintf(sub_topic, sizeof(sub_topic), "%s/meeting/#", s_base_topic);
        esp_mqtt_client_subscribe(s_client, sub_topic, 1);
        ESP_LOGI(TAG, "Subscribed to meeting topics");

        /* Publish online status */
        char status_topic[80];
        snprintf(status_topic, sizeof(status_topic), "%s/status", s_base_topic);
        esp_mqtt_client_publish(s_client, status_topic, "online", 0, 1, 1); /* retained */
        break;

    case MQTT_EVENT_DISCONNECTED:
        s_connected = false;
        ESP_LOGW(TAG, "MQTT disconnected");
        break;

    case MQTT_EVENT_DATA:
        handle_incoming_command(event->topic, event->topic_len,
                                event->data, event->data_len);
        /* Also forward to meeting service */
        {
            char mt[80] = {0};
            char md[260] = {0};
            int mtl = event->topic_len < 79 ? event->topic_len : 79;
            int mdl = event->data_len < 259 ? event->data_len : 259;
            memcpy(mt, event->topic, mtl);
            memcpy(md, event->data, mdl);
            meeting_handle_mqtt(mt, md);
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
        break;

    default:
        break;
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  Incoming command handler
 *
 *  Topics handled:
 *    <base>/cmd/theme    → payload: "dark", "light", "autumn", etc.
 *    <base>/cmd/volume   → payload: "0"-"100"
 *    <base>/cmd/brightness → payload: "0"-"255"
 *    <base>/cmd/message  → payload: text to show on home screen
 *    <base>/notify       → payload: notification text
 * ──────────────────────────────────────────────────────────────────── */
static void handle_incoming_command(const char *topic, int topic_len,
                                    const char *data, int data_len)
{
    /* Null-terminate for safe string ops */
    char t[80] = {0};
    char d[200] = {0};
    int tlen = topic_len < (int)sizeof(t) - 1 ? topic_len : (int)sizeof(t) - 1;
    int dlen = data_len < (int)sizeof(d) - 1 ? data_len : (int)sizeof(d) - 1;
    memcpy(t, topic, tlen);
    memcpy(d, data, dlen);

    ESP_LOGI(TAG, "MQTT cmd: %s → %s", t, d);

    /* Build expected prefix */
    char prefix[72];
    snprintf(prefix, sizeof(prefix), "%s/cmd/", s_base_topic);
    int prefix_len = strlen(prefix);

    if (strncmp(t, prefix, prefix_len) == 0)
    {
        const char *cmd = t + prefix_len;

        if (strcmp(cmd, "theme") == 0)
        {
            extern void theme_apply(int id);
            int id = -1;
            if (strcmp(d, "dark") == 0)
                id = 0;
            else if (strcmp(d, "light") == 0)
                id = 1;
            else if (strcmp(d, "autumn") == 0)
                id = 2;
            else if (strcmp(d, "spring") == 0)
                id = 3;
            else if (strcmp(d, "monsoon") == 0)
                id = 4;
            if (id >= 0)
            {
                theme_apply(id);
                ESP_LOGI(TAG, "Theme set to %s via MQTT", d);
            }
        }
        else if (strcmp(cmd, "volume") == 0)
        {
            extern void audio_set_volume(uint8_t vol);
            int vol = atoi(d);
            if (vol >= 0 && vol <= 100)
            {
                audio_set_volume((uint8_t)vol);
                ESP_LOGI(TAG, "Volume set to %d via MQTT", vol);
            }
        }
        else if (strcmp(cmd, "brightness") == 0)
        {
            extern void display_set_brightness(uint8_t duty);
            int br = atoi(d);
            if (br >= 0 && br <= 255)
            {
                display_set_brightness((uint8_t)br);
                ESP_LOGI(TAG, "Brightness set to %d via MQTT", br);
            }
        }
        else if (strcmp(cmd, "message") == 0)
        {
            /* Show message on home screen stage area */
            extern void scr_home_set_stage_text(const char *text);
            scr_home_set_stage_text(d);
            ESP_LOGI(TAG, "Message shown via MQTT: %s", d);
        }
    }
    else
    {
        /* Check for notification topic */
        char notify_topic[80];
        snprintf(notify_topic, sizeof(notify_topic), "%s/notify", s_base_topic);
        if (strcmp(t, notify_topic) == 0)
        {
            extern void scr_home_set_stage_text(const char *text);
            char notif[200];
            snprintf(notif, sizeof(notif), "[HA] %.190s", d);
            scr_home_set_stage_text(notif);
            /* Wake display on notification */
            extern void power_manager_activity(void);
            power_manager_activity();
            ESP_LOGI(TAG, "HA notification: %s", d);
        }
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  Periodic sensor publish task
 * ──────────────────────────────────────────────────────────────────── */
static void mqtt_publish_task(void *arg)
{
    /* Wait for connection */
    vTaskDelay(pdMS_TO_TICKS(5000));

    while (1)
    {
        if (s_connected)
        {
            mqtt_publish_all_sensors();
        }
        /* Publish every 5 minutes */
        vTaskDelay(pdMS_TO_TICKS(300000));
    }
}

/* ────────────────────────────────────────────────────────────────────
 *  Public API
 * ──────────────────────────────────────────────────────────────────── */
void mqtt_service_init(void)
{
    const device_config_t *cfg = config_get();

    if (!cfg->mqtt_enabled)
    {
        ESP_LOGI(TAG, "MQTT disabled in config");
        return;
    }

    if (strlen(cfg->mqtt_broker) == 0)
    {
        ESP_LOGW(TAG, "MQTT broker URL not configured");
        return;
    }

    /* Build base topic: "espro/<device_name>" sanitized */
    snprintf(s_base_topic, sizeof(s_base_topic), "espro/%s", cfg->device_name);
    /* Replace spaces with underscores in topic */
    for (char *p = s_base_topic; *p; p++)
    {
        if (*p == ' ')
            *p = '_';
    }

    /* Build last-will topic */
    char lwt_topic[80];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/status", s_base_topic);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = cfg->mqtt_broker,
        .credentials.username = cfg->mqtt_user,
        .credentials.authentication.password = cfg->mqtt_pass,
        .session.last_will = {
            .topic = lwt_topic,
            .msg = "offline",
            .msg_len = 7,
            .qos = 1,
            .retain = 1,
        },
        .session.keepalive = 60,
        .network.reconnect_timeout_ms = 10000,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (!s_client)
    {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_client);
    ESP_LOGI(TAG, "MQTT connecting to %s (base topic: %s)", cfg->mqtt_broker, s_base_topic);

    /* Start periodic publish task */
    xTaskCreatePinnedToCore(mqtt_publish_task, "mqtt_pub", 4096, NULL, 1, &s_pub_task, 1);
}

bool mqtt_is_connected(void)
{
    return s_connected;
}

bool mqtt_publish(const char *topic, const char *data)
{
    if (!s_client || !s_connected || !topic || !data)
        return false;

    int msg_id = esp_mqtt_client_publish(s_client, topic, data, 0, 0, 0);
    return (msg_id >= 0);
}

bool mqtt_publish_sensor(const char *key, const char *value)
{
    if (!key || !value)
        return false;

    char topic[96];
    snprintf(topic, sizeof(topic), "%s/%s", s_base_topic, key);
    return mqtt_publish(topic, value);
}

void mqtt_publish_all_sensors(void)
{
    if (!s_connected)
        return;

    int64_t t0 = esp_timer_get_time();
    char buf[32];

    /* Steps */
    extern uint32_t imu_get_steps(void);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)imu_get_steps());
    mqtt_publish_sensor("steps", buf);

    /* Free heap (battery proxy) */
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(esp_get_free_heap_size() / 1024));
    mqtt_publish_sensor("free_heap_kb", buf);

    /* Uptime */
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)(esp_timer_get_time() / 1000000));
    mqtt_publish_sensor("uptime_s", buf);

    /* Health data */
    extern health_data_t imu_get_health(void);
    health_data_t h = imu_get_health();
    snprintf(buf, sizeof(buf), "%d", (int)(h.calories * 10));
    mqtt_publish_sensor("calories_x10", buf);
    snprintf(buf, sizeof(buf), "%d", (int)(h.distance_m));
    mqtt_publish_sensor("distance_m", buf);
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)h.active_min);
    mqtt_publish_sensor("active_min", buf);

    int64_t elapsed = (esp_timer_get_time() - t0) / 1000;
    perf_log_event("mqtt_pub_all", (uint32_t)elapsed);
    ESP_LOGI(TAG, "Published all sensors (%lld ms)", elapsed);
}

void mqtt_service_stop(void)
{
    if (s_pub_task)
    {
        vTaskDelete(s_pub_task);
        s_pub_task = NULL;
    }
    if (s_client)
    {
        /* Publish offline before disconnect */
        char status_topic[80];
        snprintf(status_topic, sizeof(status_topic), "%s/status", s_base_topic);
        esp_mqtt_client_publish(s_client, status_topic, "offline", 0, 1, 1);
        vTaskDelay(pdMS_TO_TICKS(200));

        esp_mqtt_client_stop(s_client);
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
        s_connected = false;
        ESP_LOGI(TAG, "MQTT service stopped");
    }
}

void mqtt_service_reconnect(void)
{
    mqtt_service_stop();
    vTaskDelay(pdMS_TO_TICKS(500));
    mqtt_service_init();
}

const char *mqtt_status_str(void)
{
    if (!config_get()->mqtt_enabled)
        return "Disabled";
    if (s_connected)
        return "Connected";
    if (s_client)
        return "Connecting...";
    return "Not configured";
}

void mqtt_subscribe_extra(const char *topic)
{
    if (!s_client || !s_connected || !topic)
        return;
    esp_mqtt_client_subscribe(s_client, topic, 1);
    ESP_LOGI(TAG, "Extra subscription: %s", topic);
}
