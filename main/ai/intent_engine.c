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
    if (!hay || !needle) return false;
    int nlen = strlen(needle);
    int hlen = strlen(hay);
    for (int i = 0; i <= hlen - nlen; i++) {
        bool match = true;
        for (int j = 0; j < nlen; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j])) {
                match = false;
                break;
            }
        }
        if (match) return true;
    }
    return false;
}

typedef struct {
    const char *keywords[3];
    const char *action;
    int         confidence;
} intent_rule_t;

static const intent_rule_t s_rules[] = {
    { {"start timer", "begin timer", NULL},  "start_timer",  90 },
    { {"stop timer",  "pause timer", NULL},  "stop_timer",   90 },
    { {"snooze",       NULL,         NULL},  "snooze",       85 },
    { {"next task",    NULL,         NULL},  "next_task",    85 },
    { {"done",        "complete",    NULL},  "complete_task", 80 },
    { {"what time",   "current time",NULL},  "get_time",     90 },
    { {"volume up",   "louder",      NULL},  "volume_up",    80 },
    { {"volume down", "quieter",     NULL},  "volume_down",  80 },
    { {"brightness",  "brighter",    "dim"}, "set_brightness",75 },
    { {"weather",     "temperature", NULL},  "get_weather",  80 },
    { {"steps",       "how many steps",NULL},"get_steps",    80 },
    { {"dark mode",   "dark theme",  NULL},  "theme_dark",   85 },
    { {"light mode",  "light theme", NULL},  "theme_light",  85 },
};
#define RULE_COUNT (sizeof(s_rules) / sizeof(s_rules[0]))

bool intent_classify_local(const char *transcript, intent_result_t *result)
{
    if (!transcript || !result) return false;

    for (int i = 0; i < (int)RULE_COUNT; i++) {
        for (int k = 0; k < 3 && s_rules[i].keywords[k]; k++) {
            if (str_icontains(transcript, s_rules[i].keywords[k])) {
                strncpy(result->action, s_rules[i].action,
                        sizeof(result->action) - 1);
                result->entity[0] = '\0';
                result->confidence = s_rules[i].confidence;
                ESP_LOGI(TAG, "Local: \"%s\" → %s (conf=%d)",
                         transcript, result->action, result->confidence);
                return true;
            }
        }
    }

    return false;
}
