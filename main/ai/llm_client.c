/**
 * @file llm_client.c
 * @brief LLM client — routes to local Ollama/network AI or cloud (Groq).
 *
 * When local_ai_enabled and local_llm_url are configured, requests go to
 * the local machine's OpenAI-compatible endpoint (Ollama, vLLM, etc.).
 * Otherwise falls back to cloud Groq API.
 */

#include "ai/llm_client.h"
#include "services/config_manager.h"
#include "core/perf_monitor.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "llm";

#define CLOUD_LLM_URL "https://api.groq.com/openai/v1/chat/completions"
#define CLOUD_LLM_MODEL "llama-3.3-70b-versatile"
#define LOCAL_API_PATH "/v1/chat/completions"
#define MAX_RESP (8 * 1024)

typedef struct
{
    char *buf;
    int len;
    int cap;
} resp_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx)
    {
        int new_len = ctx->len + evt->data_len;
        if (new_len < ctx->cap)
        {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len = new_len;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

/* Check if local AI should be used for LLM */
static bool use_local_llm(const device_config_t *cfg)
{
    return cfg->local_ai_enabled && cfg->local_llm_url[0] != '\0';
}

bool llm_chat(const char *system_prompt, const char *user_msg,
              char *out, size_t out_len)
{
    if (!user_msg || !out)
        return false;

    const device_config_t *cfg = config_get();
    bool local = use_local_llm(cfg);

    /* Select model */
    const char *model = local ? cfg->local_llm_model : CLOUD_LLM_MODEL;
    if (local && model[0] == '\0')
        model = "llama3.2";

    /* Build JSON body */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", model);
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");

    if (system_prompt)
    {
        cJSON *sys = cJSON_CreateObject();
        cJSON_AddStringToObject(sys, "role", "system");
        cJSON_AddStringToObject(sys, "content", system_prompt);
        cJSON_AddItemToArray(msgs, sys);
    }

    cJSON *usr = cJSON_CreateObject();
    cJSON_AddStringToObject(usr, "role", "user");
    cJSON_AddStringToObject(usr, "content", user_msg);
    cJSON_AddItemToArray(msgs, usr);

    cJSON_AddNumberToObject(root, "max_tokens", 100);
    cJSON_AddNumberToObject(root, "temperature", 0.7);

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body)
        return false;

    /* Response buffer */
    resp_ctx_t ctx = {
        .buf = heap_caps_malloc(MAX_RESP, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = MAX_RESP - 1,
    };
    if (!ctx.buf)
    {
        free(body);
        return false;
    }
    ctx.buf[0] = '\0';

    /* Build URL and auth */
    char url[192];
    char auth[256] = {0};

    if (local)
    {
        snprintf(url, sizeof(url), "%s%s", cfg->local_llm_url, LOCAL_API_PATH);
        ESP_LOGI(TAG, "LLM -> local: %s (model=%s)", url, model);
    }
    else
    {
        strncpy(url, CLOUD_LLM_URL, sizeof(url) - 1);
        url[sizeof(url) - 1] = '\0';
        const char *api_key = config_get_api_key(cfg->llm_provider);
        if (!api_key || strlen(api_key) == 0)
        {
            ESP_LOGE(TAG, "No API key configured for LLM provider");
            free(body);
            heap_caps_free(ctx.buf);
            return false;
        }
        snprintf(auth, sizeof(auth), "Bearer %s", api_key);
    }

    esp_http_client_config_t http_cfg = {
        .url = url,
        .timeout_ms = local ? 30000 : 15000, /* local models can be slower */
        .event_handler = http_event_handler,
        .user_data = &ctx,
    };
    /* Only attach TLS cert bundle for HTTPS (cloud) */
    if (!local)
        http_cfg.crt_bundle_attach = esp_crt_bundle_attach;

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (auth[0] != '\0')
        esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    bool ok = false;
    if (err == ESP_OK && status == 200)
    {
        cJSON *resp = cJSON_Parse(ctx.buf);
        if (resp)
        {
            cJSON *choices = cJSON_GetObjectItem(resp, "choices");
            if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0)
            {
                cJSON *msg = cJSON_GetObjectItem(
                    cJSON_GetArrayItem(choices, 0), "message");
                cJSON *content = cJSON_GetObjectItem(msg, "content");
                if (cJSON_IsString(content))
                {
                    strncpy(out, content->valuestring, out_len - 1);
                    out[out_len - 1] = '\0';
                    ok = true;
                }
            }
            cJSON_Delete(resp);
        }
    }
    else
    {
        ESP_LOGE(TAG, "LLM error (%s): %s, status=%d",
                 local ? "local" : "cloud", esp_err_to_name(err), status);
    }

    heap_caps_free(ctx.buf);
    if (ok)
        ESP_LOGI(TAG, "LLM response (%s): %.60s...", local ? "local" : "cloud", out);
    return ok;
}
