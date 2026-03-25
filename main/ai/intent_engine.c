/**
 * @file intent_engine.c
 * @brief Local intent classification — keyword matching for offline commands.
 */

#include "ai/intent_engine.h"
#include "esp_log.h"
#include <string.h>
#include <ctype.h>

static const char *TAG = "intent";

static bool str_icontains(const char *hay, const char *needle)
{
    if (!hay || !needle)
        return false;
    int nlen = strlen(needle);
    int hlen = strlen(hay);
    for (int i = 0; i <= hlen - nlen; i++)
    {
        bool match = true;
        for (int j = 0; j < nlen; j++)
        {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j]))
            {
                match = false;
                break;
            }
        }
        if (match)
            return true;
    }
    return false;
}

typedef struct
{
    const char *keywords[3];
    const char *action;
    int confidence;
} intent_rule_t;

static const intent_rule_t s_rules[] = {
    {{"play music", "play song", "start music"}, "play_music", 90},
    {{"stop music", "pause music", "stop song"}, "stop_music", 90},
    {{"next station", "next channel", "change station"}, "next_station", 90},
    {{"next song", "skip song", "change song"}, "next_station", 85},
    {{"start timer", "set timer", "begin timer"}, "start_timer", 90},
    {{"stop timer", "cancel timer", NULL}, "stop_timer", 90},
    {{"snooze", NULL, NULL}, "snooze", 85},
    {{"next task", NULL, NULL}, "next_task", 85},
    {{"done", "complete", NULL}, "complete_task", 80},
    {{"what time", "current time", "tell time"}, "get_time", 90},
    {{"what date", "today's date", "what day"}, "get_date", 90},
    {{"volume up", "louder", "increase volume"}, "volume_up", 80},
    {{"volume down", "quieter", "decrease volume"}, "volume_down", 80},
    {{"mute", "silence", NULL}, "mute", 85},
    {{"brightness", "brighter", "dim"}, "set_brightness", 75},
    {{"weather", "temperature", "forecast"}, "get_weather", 80},
    {{"steps", "how many steps", "step count"}, "get_steps", 80},
    {{"battery", "charge", "power left"}, "get_battery", 80},
    {{"dark mode", "dark theme", NULL}, "theme_dark", 85},
    {{"light mode", "light theme", NULL}, "theme_light", 85},
    {{"autumn", "autumn theme", NULL}, "theme_autumn", 85},
    {{"spring", "spring theme", NULL}, "theme_spring", 85},
    {{"monsoon", "monsoon theme", "rainy theme"}, "theme_monsoon", 85},
    {{"next theme", "change theme", "cycle theme"}, "theme_cycle", 85},
    {{"wifi status", "wifi info", "ip address"}, "get_wifi", 80},
    {{"device name", "who are you", "what's your name"}, "get_device", 80},
    {{"mqtt status", "mqtt info", "broker status"}, "get_mqtt_status", 80},
    {{"publish sensor", "send sensor", "mqtt publish"}, "mqtt_publish_all", 80},
    {{"mute mic", "unmute mic", "toggle mic"}, "meeting_mic", 90},
    {{"camera on", "camera off", "toggle camera"}, "meeting_video", 90},
    {{"raise hand", "lower hand", "toggle hand"}, "meeting_hand", 85},
    {{"meeting status", "meeting info", NULL}, "meeting_status", 80},
    {{"open meeting", "meeting screen", "show meeting"}, "open_meeting", 85},
    {{"away mode", "hand off", "im back"}, "meeting_handoff", 80},
};
#define RULE_COUNT (sizeof(s_rules) / sizeof(s_rules[0]))

bool intent_classify_local(const char *transcript, intent_result_t *result)
{
    if (!transcript || !result)
        return false;

    for (int i = 0; i < (int)RULE_COUNT; i++)
    {
        for (int k = 0; k < 3 && s_rules[i].keywords[k]; k++)
        {
            if (str_icontains(transcript, s_rules[i].keywords[k]))
            {
                strncpy(result->action, s_rules[i].action,
                        sizeof(result->action) - 1);
                result->entity[0] = '\0';

                /* Extract number entity for timer/volume/brightness */
                const char *p = transcript;
                while (*p)
                {
                    if (*p >= '0' && *p <= '9')
                    {
                        int j = 0;
                        while (*p >= '0' && *p <= '9' && j < 15)
                            result->entity[j++] = *p++;
                        result->entity[j] = '\0';
                        break;
                    }
                    p++;
                }

                result->confidence = s_rules[i].confidence;
                ESP_LOGI(TAG, "Local: \"%s\" → %s entity=\"%s\" (conf=%d)",
                         transcript, result->action, result->entity, result->confidence);
                return true;
            }
        }
    }

    return false;
}
