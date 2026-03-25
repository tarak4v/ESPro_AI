/**
 * @file config_manager.c
 * @brief NVS-backed configuration store for all user-configurable settings.
 */

#include "services/config_manager.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include <string.h>

static const char *TAG = "config_mgr";
#define NVS_NS "dev_config"

static device_config_t s_config;

static const char *s_provider_names[AI_PROVIDER_COUNT] = {
    [AI_PROVIDER_GROQ] = "Groq",
    [AI_PROVIDER_OPENAI] = "OpenAI",
    [AI_PROVIDER_CLAUDE] = "Claude",
    [AI_PROVIDER_GEMINI] = "Gemini",
    [AI_PROVIDER_HUGGINGFACE] = "HuggingFace",
};

/* ── Defaults ── */
static void apply_defaults(void)
{
    memset(&s_config, 0, sizeof(s_config));
    strncpy(s_config.wifi_ssid, "Tarak 2.4_EXT", sizeof(s_config.wifi_ssid) - 1);
    strncpy(s_config.wifi_pass, "Tarak3385", sizeof(s_config.wifi_pass) - 1);
    s_config.wifi_hidden = false;
    strncpy(s_config.device_name, "ESPro AI Watch", sizeof(s_config.device_name) - 1);
    s_config.stt_provider = AI_PROVIDER_GROQ;
    s_config.llm_provider = AI_PROVIDER_GROQ;
    s_config.theme_id = THEME_DARK;
    strncpy(s_config.ota_url, "", sizeof(s_config.ota_url) - 1);
    s_config.ota_auto_check = false;
    s_config.mqtt_enabled = false;
    strncpy(s_config.mqtt_broker, "", sizeof(s_config.mqtt_broker) - 1);
    strncpy(s_config.mqtt_user, "", sizeof(s_config.mqtt_user) - 1);
    strncpy(s_config.mqtt_pass, "", sizeof(s_config.mqtt_pass) - 1);
    s_config.local_ai_enabled = false;
    strncpy(s_config.local_llm_url, "", sizeof(s_config.local_llm_url) - 1);
    strncpy(s_config.local_llm_model, "llama3.2", sizeof(s_config.local_llm_model) - 1);
    strncpy(s_config.local_stt_url, "", sizeof(s_config.local_stt_url) - 1);
    strncpy(s_config.local_stt_model, "whisper-large-v3", sizeof(s_config.local_stt_model) - 1);
    strncpy(s_config.local_tts_url, "", sizeof(s_config.local_tts_url) - 1);
}

/* ── NVS helpers ── */
static void nvs_load_str(nvs_handle_t h, const char *key, char *buf, size_t max)
{
    size_t len = max;
    if (nvs_get_str(h, key, buf, &len) != ESP_OK)
    {
        /* keep default */
    }
}

static void nvs_load_u8(nvs_handle_t h, const char *key, uint8_t *val)
{
    nvs_get_u8(h, key, val);
}

void config_manager_init(void)
{
    apply_defaults();

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READONLY, &h) == ESP_OK)
    {
        nvs_load_str(h, "wifi_ssid", s_config.wifi_ssid, sizeof(s_config.wifi_ssid));
        nvs_load_str(h, "wifi_pass", s_config.wifi_pass, sizeof(s_config.wifi_pass));
        nvs_load_str(h, "dev_name", s_config.device_name, sizeof(s_config.device_name));
        nvs_load_str(h, "key_groq", s_config.api_key_groq, sizeof(s_config.api_key_groq));
        nvs_load_str(h, "key_oai", s_config.api_key_openai, sizeof(s_config.api_key_openai));
        nvs_load_str(h, "key_claude", s_config.api_key_claude, sizeof(s_config.api_key_claude));
        nvs_load_str(h, "key_gemini", s_config.api_key_gemini, sizeof(s_config.api_key_gemini));
        nvs_load_str(h, "key_hf", s_config.api_key_huggingface, sizeof(s_config.api_key_huggingface));
        nvs_load_str(h, "ota_url", s_config.ota_url, sizeof(s_config.ota_url));

        nvs_load_str(h, "mqtt_uri", s_config.mqtt_broker, sizeof(s_config.mqtt_broker));
        nvs_load_str(h, "mqtt_user", s_config.mqtt_user, sizeof(s_config.mqtt_user));
        nvs_load_str(h, "mqtt_pass", s_config.mqtt_pass, sizeof(s_config.mqtt_pass));

        uint8_t tmp;
        tmp = 0;
        nvs_load_u8(h, "wifi_hide", &tmp);
        s_config.wifi_hidden = (tmp != 0);
        tmp = 0;
        nvs_load_u8(h, "stt_prov", &tmp);
        if (tmp < AI_PROVIDER_COUNT)
            s_config.stt_provider = tmp;
        tmp = 0;
        nvs_load_u8(h, "llm_prov", &tmp);
        if (tmp < AI_PROVIDER_COUNT)
            s_config.llm_provider = tmp;
        tmp = 0;
        nvs_load_u8(h, "theme_id", &tmp);
        if (tmp < THEME_COUNT)
            s_config.theme_id = tmp;
        tmp = 0;
        nvs_load_u8(h, "ota_auto", &tmp);
        s_config.ota_auto_check = (tmp != 0);
        tmp = 0;
        nvs_load_u8(h, "mqtt_en", &tmp);
        s_config.mqtt_enabled = (tmp != 0);

        tmp = 0;
        nvs_load_u8(h, "local_ai_en", &tmp);
        s_config.local_ai_enabled = (tmp != 0);
        nvs_load_str(h, "loc_llm_url", s_config.local_llm_url, sizeof(s_config.local_llm_url));
        nvs_load_str(h, "loc_llm_mod", s_config.local_llm_model, sizeof(s_config.local_llm_model));
        nvs_load_str(h, "loc_stt_url", s_config.local_stt_url, sizeof(s_config.local_stt_url));
        nvs_load_str(h, "loc_stt_mod", s_config.local_stt_model, sizeof(s_config.local_stt_model));
        nvs_load_str(h, "loc_tts_url", s_config.local_tts_url, sizeof(s_config.local_tts_url));

        nvs_close(h);
        ESP_LOGI(TAG, "Config loaded from NVS (device: %s, theme: %d)", s_config.device_name, s_config.theme_id);
    }
    else
    {
        ESP_LOGW(TAG, "No saved config, using defaults");
    }
}

const device_config_t *config_get(void)
{
    return &s_config;
}

bool config_set_str(const char *key, const char *value)
{
    if (!key || !value)
        return false;

    /* Map key name → struct field + NVS key */
    char nvs_key[16] = {0};
    char *dest = NULL;
    size_t max = 0;

    if (strcmp(key, "wifi_ssid") == 0)
    {
        dest = s_config.wifi_ssid;
        max = sizeof(s_config.wifi_ssid);
        strncpy(nvs_key, "wifi_ssid", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "wifi_pass") == 0)
    {
        dest = s_config.wifi_pass;
        max = sizeof(s_config.wifi_pass);
        strncpy(nvs_key, "wifi_pass", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "device_name") == 0)
    {
        dest = s_config.device_name;
        max = sizeof(s_config.device_name);
        strncpy(nvs_key, "dev_name", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "api_key_groq") == 0)
    {
        dest = s_config.api_key_groq;
        max = sizeof(s_config.api_key_groq);
        strncpy(nvs_key, "key_groq", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "api_key_openai") == 0)
    {
        dest = s_config.api_key_openai;
        max = sizeof(s_config.api_key_openai);
        strncpy(nvs_key, "key_oai", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "api_key_claude") == 0)
    {
        dest = s_config.api_key_claude;
        max = sizeof(s_config.api_key_claude);
        strncpy(nvs_key, "key_claude", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "api_key_gemini") == 0)
    {
        dest = s_config.api_key_gemini;
        max = sizeof(s_config.api_key_gemini);
        strncpy(nvs_key, "key_gemini", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "api_key_huggingface") == 0)
    {
        dest = s_config.api_key_huggingface;
        max = sizeof(s_config.api_key_huggingface);
        strncpy(nvs_key, "key_hf", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "ota_url") == 0)
    {
        dest = s_config.ota_url;
        max = sizeof(s_config.ota_url);
        strncpy(nvs_key, "ota_url", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "mqtt_broker") == 0)
    {
        dest = s_config.mqtt_broker;
        max = sizeof(s_config.mqtt_broker);
        strncpy(nvs_key, "mqtt_uri", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "mqtt_user") == 0)
    {
        dest = s_config.mqtt_user;
        max = sizeof(s_config.mqtt_user);
        strncpy(nvs_key, "mqtt_user", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "mqtt_pass") == 0)
    {
        dest = s_config.mqtt_pass;
        max = sizeof(s_config.mqtt_pass);
        strncpy(nvs_key, "mqtt_pass", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_llm_url") == 0)
    {
        dest = s_config.local_llm_url;
        max = sizeof(s_config.local_llm_url);
        strncpy(nvs_key, "loc_llm_url", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_llm_model") == 0)
    {
        dest = s_config.local_llm_model;
        max = sizeof(s_config.local_llm_model);
        strncpy(nvs_key, "loc_llm_mod", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_stt_url") == 0)
    {
        dest = s_config.local_stt_url;
        max = sizeof(s_config.local_stt_url);
        strncpy(nvs_key, "loc_stt_url", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_stt_model") == 0)
    {
        dest = s_config.local_stt_model;
        max = sizeof(s_config.local_stt_model);
        strncpy(nvs_key, "loc_stt_mod", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_tts_url") == 0)
    {
        dest = s_config.local_tts_url;
        max = sizeof(s_config.local_tts_url);
        strncpy(nvs_key, "loc_tts_url", sizeof(nvs_key) - 1);
    }
    else
    {
        ESP_LOGW(TAG, "Unknown str key: %s", key);
        return false;
    }

    strncpy(dest, value, max - 1);
    dest[max - 1] = '\0';

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_str(h, nvs_key, dest);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Config set: %s", key);
    return true;
}

bool config_set_u8(const char *key, uint8_t value)
{
    if (!key)
        return false;

    char nvs_key[16] = {0};

    if (strcmp(key, "wifi_hidden") == 0)
    {
        s_config.wifi_hidden = (value != 0);
        strncpy(nvs_key, "wifi_hide", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "stt_provider") == 0)
    {
        if (value >= AI_PROVIDER_COUNT)
            return false;
        s_config.stt_provider = value;
        strncpy(nvs_key, "stt_prov", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "llm_provider") == 0)
    {
        if (value >= AI_PROVIDER_COUNT)
            return false;
        s_config.llm_provider = value;
        strncpy(nvs_key, "llm_prov", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "theme_id") == 0)
    {
        if (value >= THEME_COUNT)
            return false;
        s_config.theme_id = value;
        strncpy(nvs_key, "theme_id", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "ota_auto_check") == 0)
    {
        s_config.ota_auto_check = (value != 0);
        strncpy(nvs_key, "ota_auto", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "mqtt_enabled") == 0)
    {
        s_config.mqtt_enabled = (value != 0);
        strncpy(nvs_key, "mqtt_en", sizeof(nvs_key) - 1);
    }
    else if (strcmp(key, "local_ai_enabled") == 0)
    {
        s_config.local_ai_enabled = (value != 0);
        strncpy(nvs_key, "local_ai_en", sizeof(nvs_key) - 1);
    }
    else
    {
        ESP_LOGW(TAG, "Unknown u8 key: %s", key);
        return false;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_set_u8(h, nvs_key, value);
        nvs_commit(h);
        nvs_close(h);
    }
    ESP_LOGI(TAG, "Config set: %s = %u", key, value);
    return true;
}

const char *config_get_api_key(ai_provider_t provider)
{
    switch (provider)
    {
    case AI_PROVIDER_GROQ:
        return s_config.api_key_groq;
    case AI_PROVIDER_OPENAI:
        return s_config.api_key_openai;
    case AI_PROVIDER_CLAUDE:
        return s_config.api_key_claude;
    case AI_PROVIDER_GEMINI:
        return s_config.api_key_gemini;
    case AI_PROVIDER_HUGGINGFACE:
        return s_config.api_key_huggingface;
    default:
        return "";
    }
}

const char *config_get_provider_name(ai_provider_t provider)
{
    if (provider < AI_PROVIDER_COUNT)
        return s_provider_names[provider];
    return "Unknown";
}

bool config_save_all(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) != ESP_OK)
        return false;

    nvs_set_str(h, "wifi_ssid", s_config.wifi_ssid);
    nvs_set_str(h, "wifi_pass", s_config.wifi_pass);
    nvs_set_str(h, "dev_name", s_config.device_name);
    nvs_set_str(h, "key_groq", s_config.api_key_groq);
    nvs_set_str(h, "key_oai", s_config.api_key_openai);
    nvs_set_str(h, "key_claude", s_config.api_key_claude);
    nvs_set_str(h, "key_gemini", s_config.api_key_gemini);
    nvs_set_str(h, "key_hf", s_config.api_key_huggingface);
    nvs_set_str(h, "ota_url", s_config.ota_url);
    nvs_set_str(h, "mqtt_uri", s_config.mqtt_broker);
    nvs_set_str(h, "mqtt_user", s_config.mqtt_user);
    nvs_set_str(h, "mqtt_pass", s_config.mqtt_pass);
    nvs_set_u8(h, "wifi_hide", s_config.wifi_hidden ? 1 : 0);
    nvs_set_u8(h, "stt_prov", (uint8_t)s_config.stt_provider);
    nvs_set_u8(h, "llm_prov", (uint8_t)s_config.llm_provider);
    nvs_set_u8(h, "theme_id", (uint8_t)s_config.theme_id);
    nvs_set_u8(h, "ota_auto", s_config.ota_auto_check ? 1 : 0);
    nvs_set_u8(h, "mqtt_en", s_config.mqtt_enabled ? 1 : 0);
    nvs_set_u8(h, "local_ai_en", s_config.local_ai_enabled ? 1 : 0);
    nvs_set_str(h, "loc_llm_url", s_config.local_llm_url);
    nvs_set_str(h, "loc_llm_mod", s_config.local_llm_model);
    nvs_set_str(h, "loc_stt_url", s_config.local_stt_url);
    nvs_set_str(h, "loc_stt_mod", s_config.local_stt_model);
    nvs_set_str(h, "loc_tts_url", s_config.local_tts_url);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "All config saved to NVS");
    return true;
}

void config_factory_reset(void)
{
    nvs_handle_t h;
    if (nvs_open(NVS_NS, NVS_READWRITE, &h) == ESP_OK)
    {
        nvs_erase_all(h);
        nvs_commit(h);
        nvs_close(h);
    }
    apply_defaults();
    ESP_LOGW(TAG, "Factory reset complete");
}
