/**
 * @file meeting_service.c
 * @brief Meeting assistant — MQTT bridge for desktop companion app.
 */

#include "services/meeting_service.h"
#include "services/mqtt_service.h"
#include "services/config_manager.h"
#include "core/event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "meeting_svc";

static meeting_state_t s_state;
static char s_topic_base[80]; /* e.g. "espro/ESPro_AI_Watch/meeting" */
static bool s_initialised = false;

/* ── Build topic path ── */
static void build_topic(char *out, size_t len, const char *suffix)
{
    snprintf(out, len, "%s/%s", s_topic_base, suffix);
}

/* ── Publish command to desktop ── */
static void send_cmd(const char *cmd_key, const char *value)
{
    char topic[120];
    snprintf(topic, sizeof(topic), "%s/cmd/%s", s_topic_base, cmd_key);
    mqtt_publish(topic, value);
    ESP_LOGI(TAG, "CMD: %s → %s", cmd_key, value);
}

/* ── MQTT message handler (called from mqtt_service for meeting topics) ── */
void meeting_handle_mqtt(const char *topic, const char *data)
{
    if (!s_initialised)
        return;

    /* Match against our base topic */
    int base_len = strlen(s_topic_base);
    if (strncmp(topic, s_topic_base, base_len) != 0)
        return;

    const char *sub = topic + base_len;

    if (strcmp(sub, "/state/active") == 0)
    {
        bool was_active = s_state.active;
        s_state.active = (data[0] == '1');
        if (s_state.active && !was_active)
        {
            s_state.start_time = (uint32_t)(esp_timer_get_time() / 1000000);
            s_state.handoff_active = false;
            s_state.live_note[0] = '\0';
            s_state.summary[0] = '\0';
            s_state.handoff[0] = '\0';
            ESP_LOGI(TAG, "Meeting started (%s)", s_state.app);
        }
        else if (!s_state.active && was_active)
        {
            ESP_LOGI(TAG, "Meeting ended");
        }
    }
    else if (strcmp(sub, "/state/app") == 0)
    {
        strncpy(s_state.app, data, sizeof(s_state.app) - 1);
        s_state.app[sizeof(s_state.app) - 1] = '\0';
    }
    else if (strcmp(sub, "/state/mic") == 0)
    {
        s_state.mic_on = (strcmp(data, "on") == 0);
    }
    else if (strcmp(sub, "/state/video") == 0)
    {
        s_state.video_on = (strcmp(data, "on") == 0);
    }
    else if (strcmp(sub, "/state/hand") == 0)
    {
        s_state.hand_raised = (strcmp(data, "up") == 0);
    }
    else if (strcmp(sub, "/state/audio_dev") == 0)
    {
        strncpy(s_state.audio_device, data, sizeof(s_state.audio_device) - 1);
        s_state.audio_device[sizeof(s_state.audio_device) - 1] = '\0';
    }
    else if (strcmp(sub, "/notes/live") == 0)
    {
        strncpy(s_state.live_note, data, sizeof(s_state.live_note) - 1);
        s_state.live_note[sizeof(s_state.live_note) - 1] = '\0';
        s_state.notes_dirty = true;
    }
    else if (strcmp(sub, "/notes/summary") == 0)
    {
        strncpy(s_state.summary, data, sizeof(s_state.summary) - 1);
        s_state.summary[sizeof(s_state.summary) - 1] = '\0';
        s_state.summary_dirty = true;
    }
    else if (strcmp(sub, "/notes/handoff") == 0)
    {
        strncpy(s_state.handoff, data, sizeof(s_state.handoff) - 1);
        s_state.handoff[sizeof(s_state.handoff) - 1] = '\0';
        s_state.handoff_dirty = true;
    }
}

/* ── Init ── */
void meeting_service_init(void)
{
    memset(&s_state, 0, sizeof(s_state));
    strncpy(s_state.app, "none", sizeof(s_state.app));

    /* Build base topic from device name */
    const device_config_t *cfg = config_get();
    char dev_name[32];
    strncpy(dev_name, cfg->device_name, sizeof(dev_name) - 1);
    dev_name[sizeof(dev_name) - 1] = '\0';
    for (char *p = dev_name; *p; p++)
    {
        if (*p == ' ')
            *p = '_';
    }
    snprintf(s_topic_base, sizeof(s_topic_base), "espro/%s/meeting", dev_name);

    s_initialised = true;
    ESP_LOGI(TAG, "Meeting service ready (topic: %s)", s_topic_base);

    /* Subscribe to meeting state topics — done via MQTT wildcard */
    if (mqtt_is_connected())
    {
        char sub_topic[100];
        snprintf(sub_topic, sizeof(sub_topic), "%s/#", s_topic_base);
        /* We need the MQTT client to subscribe for us */
        extern void mqtt_subscribe_extra(const char *topic);
        mqtt_subscribe_extra(sub_topic);
    }
}

const meeting_state_t *meeting_get_state(void)
{
    /* Update duration if meeting is active */
    if (s_state.active)
    {
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000000);
        s_state.duration_s = now - s_state.start_time;
    }
    return &s_state;
}

/* ── Control commands ── */
void meeting_toggle_mic(void) { send_cmd("mic", "toggle"); }
void meeting_toggle_video(void) { send_cmd("video", "toggle"); }
void meeting_toggle_hand(void) { send_cmd("hand", "toggle"); }
void meeting_volume_up(void) { send_cmd("volume", "up"); }
void meeting_volume_down(void) { send_cmd("volume", "down"); }

void meeting_volume_set(int vol)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", vol);
    send_cmd("volume", buf);
}

void meeting_next_audio_device(void) { send_cmd("audio_dev", "next"); }
void meeting_start_handoff(void)
{
    send_cmd("handoff", "start");
    s_state.handoff_active = true;
}
void meeting_stop_handoff(void)
{
    send_cmd("handoff", "stop");
    s_state.handoff_active = false;
}
void meeting_request_summary(void) { send_cmd("end", "1"); }
