/**
 * @file config_manager.h
 * @brief Centralized configuration manager — NVS-backed runtime config.
 *
 * All user-configurable settings are stored here and persisted to NVS.
 * The webserver reads/writes through this API. AI clients, wifi_manager,
 * theme engine, and OTA all consume config from here.
 */
#ifndef CONFIG_MANAGER_H
#define CONFIG_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* ── AI Provider enum ── */
typedef enum
{
    AI_PROVIDER_GROQ = 0,
    AI_PROVIDER_OPENAI,
    AI_PROVIDER_CLAUDE,
    AI_PROVIDER_GEMINI,
    AI_PROVIDER_HUGGINGFACE,
    AI_PROVIDER_COUNT,
} ai_provider_t;

/* ── Theme ID enum ── */
typedef enum
{
    THEME_DARK = 0,
    THEME_LIGHT,
    THEME_AUTUMN,
    THEME_SPRING,
    THEME_MONSOON,
    THEME_COUNT,
} theme_id_t;

/* ── Config structure ── */
typedef struct
{
    /* WiFi */
    char wifi_ssid[33]; /* max 32 chars + NUL */
    char wifi_pass[65]; /* max 64 chars + NUL */
    bool wifi_hidden;   /* true if SSID is hidden */

    /* Device */
    char device_name[32]; /* user-assigned device name */

    /* AI providers */
    ai_provider_t stt_provider; /* which provider for STT */
    ai_provider_t llm_provider; /* which provider for LLM */
    char api_key_groq[128];
    char api_key_openai[128];
    char api_key_claude[128];
    char api_key_gemini[128];
    char api_key_huggingface[128];

    /* Theme */
    theme_id_t theme_id;

    /* OTA */
    char ota_url[256];   /* firmware update URL */
    bool ota_auto_check; /* check for updates on boot */

    /* MQTT / Home Assistant */
    bool mqtt_enabled;     /* enable MQTT client */
    char mqtt_broker[128]; /* broker URI, e.g. mqtt://192.168.0.100:1883 */
    char mqtt_user[64];    /* broker username (optional) */
    char mqtt_pass[64];    /* broker password (optional) */

    /* Network AI — offload to any local machine (Ollama, Whisper, etc.) */
    bool local_ai_enabled;    /* master toggle for local AI endpoints */
    char local_llm_url[128];  /* e.g. http://192.168.0.50:11434 */
    char local_llm_model[64]; /* e.g. llama3.2, mistral, phi3 */
    char local_stt_url[128];  /* e.g. http://192.168.0.50:8080 */
    char local_stt_model[64]; /* e.g. whisper-large-v3 */
    char local_tts_url[128];  /* e.g. http://192.168.0.50:5000 */
} device_config_t;

/**
 * @brief Initialise config manager — load from NVS or apply defaults.
 */
void config_manager_init(void);

/**
 * @brief Get pointer to current config (read-only outside config_manager).
 */
const device_config_t *config_get(void);

/**
 * @brief Set a string config value by key name and persist to NVS.
 * @return true on success.
 *
 * Valid keys: "wifi_ssid", "wifi_pass", "device_name",
 *             "api_key_groq", "api_key_openai", "api_key_claude",
 *             "api_key_gemini", "api_key_huggingface", "ota_url",
 *             "mqtt_broker", "mqtt_user", "mqtt_pass",
 *             "local_llm_url", "local_llm_model", "local_stt_url",
 *             "local_stt_model", "local_tts_url"
 */
bool config_set_str(const char *key, const char *value);

/**
 * @brief Set a uint8 config value by key name and persist to NVS.
 * @return true on success.
 *
 * Valid keys: "wifi_hidden" (0/1), "stt_provider", "llm_provider",
 *             "theme_id", "ota_auto_check" (0/1), "mqtt_enabled" (0/1),
 *             "local_ai_enabled" (0/1)
 */
bool config_set_u8(const char *key, uint8_t value);

/**
 * @brief Get the API key string for a given provider.
 */
const char *config_get_api_key(ai_provider_t provider);

/**
 * @brief Get provider display name.
 */
const char *config_get_provider_name(ai_provider_t provider);

/**
 * @brief Persist all current config to NVS (bulk save).
 */
bool config_save_all(void);

/**
 * @brief Reset config to factory defaults and clear NVS.
 */
void config_factory_reset(void);

#endif /* CONFIG_MANAGER_H */
