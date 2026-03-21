/**
 * @file stt_client.c
 * @brief Groq Whisper STT — sends WAV audio, receives transcript.
 */

#include "ai/stt_client.h"
#include "services/config_manager.h"
#include "core/perf_monitor.h"

#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "stt";

#define GROQ_STT_URL "https://api.groq.com/openai/v1/audio/transcriptions"
#define MAX_RESP_BUF 4096

/* WAV header for 16-bit mono PCM */
static void write_wav_header(uint8_t *buf, uint32_t data_size, uint16_t sample_rate)
{
    uint32_t file_size = data_size + 36;
    memcpy(buf, "RIFF", 4);
    memcpy(buf + 4, &file_size, 4);
    memcpy(buf + 8, "WAVEfmt ", 8);
    uint32_t fmt_size = 16;
    memcpy(buf + 16, &fmt_size, 4);
    uint16_t pcm = 1;
    memcpy(buf + 20, &pcm, 2);
    uint16_t channels = 1;
    memcpy(buf + 22, &channels, 2);
    memcpy(buf + 24, &sample_rate, 4);
    uint32_t byte_rate = sample_rate * 2;
    memcpy(buf + 28, &byte_rate, 4);
    uint16_t block_align = 2;
    memcpy(buf + 32, &block_align, 2);
    uint16_t bits = 16;
    memcpy(buf + 34, &bits, 2);
    memcpy(buf + 36, "data", 4);
    memcpy(buf + 40, &data_size, 4);
}

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

bool stt_transcribe(const int16_t *samples, size_t count,
                    char *out, size_t out_len)
{
    if (!samples || count == 0 || !out)
        return false;

    uint32_t data_size = count * sizeof(int16_t);
    uint32_t wav_size = 44 + data_size;

    /* Build WAV in PSRAM */
    uint8_t *wav = heap_caps_malloc(wav_size, MALLOC_CAP_SPIRAM);
    if (!wav)
    {
        ESP_LOGE(TAG, "Failed to alloc WAV buffer (%lu bytes)", wav_size);
        return false;
    }
    write_wav_header(wav, data_size, 16000);
    memcpy(wav + 44, samples, data_size);

    /* Build multipart body */
    const char *boundary = "----ESProAI";
    size_t body_cap = wav_size + 512;
    char *body = heap_caps_malloc(body_cap, MALLOC_CAP_SPIRAM);
    if (!body)
    {
        heap_caps_free(wav);
        return false;
    }

    int pos = snprintf(body, body_cap,
                       "--%s\r\n"
                       "Content-Disposition: form-data; name=\"file\"; filename=\"audio.wav\"\r\n"
                       "Content-Type: audio/wav\r\n\r\n",
                       boundary);
    memcpy(body + pos, wav, wav_size);
    pos += wav_size;
    pos += snprintf(body + pos, body_cap - pos,
                    "\r\n--%s\r\n"
                    "Content-Disposition: form-data; name=\"model\"\r\n\r\n"
                    "whisper-large-v3-turbo"
                    "\r\n--%s\r\n"
                    "Content-Disposition: form-data; name=\"response_format\"\r\n\r\n"
                    "text"
                    "\r\n--%s--\r\n",
                    boundary, boundary, boundary);

    heap_caps_free(wav);

    /* Response buffer */
    resp_ctx_t ctx = {
        .buf = heap_caps_malloc(MAX_RESP_BUF, MALLOC_CAP_SPIRAM),
        .len = 0,
        .cap = MAX_RESP_BUF - 1,
    };
    if (!ctx.buf)
    {
        heap_caps_free(body);
        return false;
    }
    ctx.buf[0] = '\0';

    /* HTTP POST — use API key from config_manager */
    const char *api_key = config_get_api_key(config_get()->stt_provider);
    if (!api_key || strlen(api_key) == 0)
    {
        ESP_LOGE(TAG, "No API key configured for STT provider");
        heap_caps_free(body);
        heap_caps_free(ctx.buf);
        return false;
    }
    char auth[256];
    snprintf(auth, sizeof(auth), "Bearer %s", api_key);
    char ct[64];
    snprintf(ct, sizeof(ct), "multipart/form-data; boundary=%s", boundary);

    esp_http_client_config_t cfg = {
        .url = GROQ_STT_URL,
        .timeout_ms = 15000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = http_event_handler,
        .user_data = &ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    esp_http_client_set_method(client, HTTP_METHOD_POST);
    esp_http_client_set_header(client, "Authorization", auth);
    esp_http_client_set_header(client, "Content-Type", ct);
    esp_http_client_set_post_field(client, body, pos);

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    heap_caps_free(body);

    bool ok = false;
    if (err == ESP_OK && status == 200 && ctx.len > 0)
    {
        strncpy(out, ctx.buf, out_len - 1);
        out[out_len - 1] = '\0';
        ok = true;
        ESP_LOGI(TAG, "STT result (%d chars): %.60s", ctx.len, out);
    }
    else
    {
        ESP_LOGE(TAG, "STT error: %s, status=%d", esp_err_to_name(err), status);
    }

    heap_caps_free(ctx.buf);
    return ok;
}
