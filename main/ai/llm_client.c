/**
 * @file llm_client.c
 * @brief Groq LLM client — Llama 3.3 70B chat completions.
 */

#include "ai/llm_client.h"
#include "secrets.h"
#include "core/perf_monitor.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include <string.h>

static const char *TAG = "llm";

#define LLM_URL     "https://api.groq.com/openai/v1/chat/completions"
#define LLM_MODEL   "llama-3.3-70b-versatile"
#define MAX_RESP     (8 * 1024)

typedef struct {
    char *buf;
    int   len;
    int   cap;
} resp_ctx_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    resp_ctx_t *ctx = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && ctx) {
        int new_len = ctx->len + evt->data_len;
        if (new_len < ctx->cap) {
            memcpy(ctx->buf + ctx->len, evt->data, evt->data_len);
            ctx->len = new_len;
            ctx->buf[ctx->len] = '\0';
        }
    }
    return ESP_OK;
}

bool llm_chat(const char *system_prompt, const char *user_msg,
              char *out, size_t out_len)
{
    if (!user_msg || !out) return false;

    /* Build JSON body */
    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "model", LLM_MODEL);
    cJSON *msgs = cJSON_AddArrayToObject(root, "messages");

    if (system_prompt) {
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
    if (!body) return false;

    /* Response buffer */
    resp_ctx_t ctx = {
        .buf = heap_caps_malloc(MAX_RESP, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = MAX_RESP - 1,
    };
    if (!ctx.buf) { free(body); return false; }
    ctx.buf[0] = '\0';

    char auth[128];
    snprintf(auth, sizeof(auth), "Bearer %s", GROQ_API_KEY);

    esp_http_client_config_t cfg = {
        .url               = LLM_URL,
        .timeout_ms        = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler     = http_event_handler,
        .user_data         = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    free(body);

    bool ok = false;
    if (err == ESP_OK && status == 200) {
        cJSON *resp = cJSON_Parse(ctx.buf);
        if (resp) {
            cJSON *choices = cJSON_GetObjectItem(resp, "choices");
            if (cJSON_IsArray(choices) && cJSON_GetArraySize(choices) > 0) {
                cJSON *msg = cJSON_GetObjectItem(
                    cJSON_GetArrayItem(choices, 0), "message");
                cJSON *content = cJSON_GetObjectItem(msg, "content");
                if (cJSON_IsString(content)) {
                    strncpy(out, content->valuestring, out_len - 1);
                    out[out_len - 1] = '\0';
                    ok = true;
                }
            }
            cJSON_Delete(resp);
        }
    } else {
        ESP_LOGE(TAG, "LLM error: %s, status=%d", esp_err_to_name(err), status);
    }

    heap_caps_free(ctx.buf);
    if (ok) ESP_LOGI(TAG, "LLM response: %.60s...", out);
    return ok;
}
