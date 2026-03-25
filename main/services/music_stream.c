/**
 * @file music_stream.c
 * @brief Internet radio streaming — HTTP MP3 stream → minimp3 decode → speaker.
 *
 * Uses free Icecast/Shoutcast radio streams. No API key required.
 * MP3 frames are decoded with minimp3, resampled from source rate to 16 kHz,
 * and played through the streaming speaker API.
 */

#include "services/music_stream.h"
#include "hal/audio.h"
#include "hw_config.h"

#include "minimp3.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "music_stream";

/* ── Station presets (free internet radio — no key needed) ── */
typedef struct
{
    const char *name;
    const char *url;
} radio_station_t;

static const radio_station_t s_stations[] = {
    {"Lofi Hip Hop", "http://streams.fluxfm.de/Chillhop/mp3-128/streams.fluxfm.de/"},
    {"SomaFM Groove", "http://ice1.somafm.com/groovesalad-128-mp3"},
    {"SomaFM Drone", "http://ice1.somafm.com/dronezone-128-mp3"},
    {"SomaFM Lush", "http://ice1.somafm.com/lush-128-mp3"},
    {"Jazz Radio", "http://jazz-wr04.ice.infomaniak.ch/jazz-wr04-128.mp3"},
    {"Classical", "http://live-radio01.mediahubaustralia.com/2TJW/mp3/"},
};
#define STATION_COUNT (sizeof(s_stations) / sizeof(s_stations[0]))

/* ── State ── */
static volatile bool s_playing = false;
static volatile int s_station_idx = 0;
static TaskHandle_t s_stream_task = NULL;

/* ── Downsampling: convert from source sample rate to 16 kHz mono ── */
static void resample_to_16k(const int16_t *src, int src_samples, int src_channels,
                            int src_hz, int16_t *dst, int *dst_samples)
{
    /* Simple linear interpolation resampler */
    double ratio = (double)src_hz / AUDIO_SAMPLE_RATE;
    int out_count = (int)(src_samples / src_channels / ratio);
    if (out_count <= 0)
    {
        *dst_samples = 0;
        return;
    }

    for (int i = 0; i < out_count; i++)
    {
        double src_pos = i * ratio;
        int idx = (int)src_pos;
        double frac = src_pos - idx;

        /* Mix channels to mono */
        int32_t s0 = 0, s1 = 0;
        if (idx < src_samples / src_channels)
        {
            for (int c = 0; c < src_channels; c++)
                s0 += src[idx * src_channels + c];
            s0 /= src_channels;
        }
        if ((idx + 1) < src_samples / src_channels)
        {
            for (int c = 0; c < src_channels; c++)
                s1 += src[(idx + 1) * src_channels + c];
            s1 /= src_channels;
        }
        else
        {
            s1 = s0;
        }

        dst[i] = (int16_t)(s0 + frac * (s1 - s0));
    }
    *dst_samples = out_count;
}

/* ── Stream task ── */
#define MP3_BUF_SIZE (16 * 1024)                   /* HTTP read buffer */
#define PCM_BUF_SIZE MINIMP3_MAX_SAMPLES_PER_FRAME /* Decoded PCM per frame */

static void stream_task(void *arg)
{
    mp3dec_t mp3d;
    mp3dec_init(&mp3d);

    /* Allocate buffers in PSRAM */
    uint8_t *mp3_buf = heap_caps_malloc(MP3_BUF_SIZE, MALLOC_CAP_SPIRAM);
    int16_t *pcm_buf = heap_caps_malloc(PCM_BUF_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    int16_t *resamp_buf = heap_caps_malloc(PCM_BUF_SIZE * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!mp3_buf || !pcm_buf || !resamp_buf)
    {
        ESP_LOGE(TAG, "Buffer alloc failed");
        goto cleanup;
    }

    while (s_playing)
    {
        int idx = s_station_idx;
        const char *url = s_stations[idx].url;
        ESP_LOGI(TAG, "Connecting to: %s (%s)", s_stations[idx].name, url);

        esp_http_client_config_t cfg = {
            .url = url,
            .timeout_ms = 10000,
            .buffer_size = 4096,
        };
        esp_http_client_handle_t client = esp_http_client_init(&cfg);
        if (!client)
        {
            ESP_LOGE(TAG, "HTTP client init failed");
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        esp_err_t err = esp_http_client_open(client, 0);
        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }
        esp_http_client_fetch_headers(client);

        /* Open speaker for streaming */
        if (!audio_speaker_open())
        {
            ESP_LOGE(TAG, "Speaker open failed");
            esp_http_client_close(client);
            esp_http_client_cleanup(client);
            vTaskDelay(pdMS_TO_TICKS(2000));
            continue;
        }

        ESP_LOGI(TAG, "Streaming: %s", s_stations[idx].name);

        int mp3_fill = 0; /* bytes in mp3_buf */
        mp3dec_init(&mp3d);

        while (s_playing && s_station_idx == idx)
        {
            /* Fill MP3 buffer from HTTP */
            int space = MP3_BUF_SIZE - mp3_fill;
            if (space > 0)
            {
                int rd = esp_http_client_read(client, (char *)(mp3_buf + mp3_fill), space);
                if (rd > 0)
                {
                    mp3_fill += rd;
                }
                else if (rd == 0)
                {
                    /* Stream ended — reconnect */
                    ESP_LOGW(TAG, "Stream ended, reconnecting...");
                    break;
                }
                else
                {
                    ESP_LOGW(TAG, "HTTP read error");
                    break;
                }
            }

            /* Decode MP3 frames from buffer */
            int offset = 0;
            while (offset + 128 <= mp3_fill && s_playing)
            {
                mp3dec_frame_info_t info;
                int samples = mp3dec_decode_frame(&mp3d, mp3_buf + offset,
                                                  mp3_fill - offset, pcm_buf, &info);

                if (info.frame_bytes == 0)
                {
                    /* Not enough data for a frame — need more from HTTP */
                    break;
                }

                offset += info.frame_bytes;

                if (samples > 0 && info.hz > 0)
                {
                    /* Resample to 16 kHz mono and play */
                    if (info.hz == AUDIO_SAMPLE_RATE && info.channels == 1)
                    {
                        /* Already at target format — write directly */
                        audio_speaker_write(pcm_buf, samples);
                    }
                    else
                    {
                        int out_samples = 0;
                        resample_to_16k(pcm_buf, samples * info.channels,
                                        info.channels, info.hz,
                                        resamp_buf, &out_samples);
                        if (out_samples > 0)
                        {
                            audio_speaker_write(resamp_buf, out_samples);
                        }
                    }
                }
            }

            /* Shift remaining data to front */
            if (offset > 0 && offset < mp3_fill)
            {
                memmove(mp3_buf, mp3_buf + offset, mp3_fill - offset);
                mp3_fill -= offset;
            }
            else if (offset >= mp3_fill)
            {
                mp3_fill = 0;
            }

            /* Yield to other tasks */
            vTaskDelay(pdMS_TO_TICKS(5));
        }

        audio_speaker_close();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);

        /* Brief pause before reconnecting / switching */
        if (s_playing)
            vTaskDelay(pdMS_TO_TICKS(500));
    }

cleanup:
    if (mp3_buf)
        heap_caps_free(mp3_buf);
    if (pcm_buf)
        heap_caps_free(pcm_buf);
    if (resamp_buf)
        heap_caps_free(resamp_buf);
    s_stream_task = NULL;
    ESP_LOGI(TAG, "Stream task ended");
    vTaskDelete(NULL);
}

/* ── Public API ── */

void music_stream_play(void)
{
    if (s_playing)
        return;
    s_playing = true;
    /* Large stack in PSRAM — minimp3 + HTTP buffers need room */
    xTaskCreatePinnedToCore(stream_task, "radio", 8192, NULL, 3, &s_stream_task, 1);
}

void music_stream_stop(void)
{
    s_playing = false;
    /* Task will self-delete when it sees s_playing == false */
}

void music_stream_next(void)
{
    s_station_idx = (s_station_idx + 1) % STATION_COUNT;
    ESP_LOGI(TAG, "Next station: %s", s_stations[s_station_idx].name);
    /* If playing, the task will detect idx change and reconnect */
}

const char *music_stream_station_name(void)
{
    return s_stations[s_station_idx].name;
}

bool music_stream_is_playing(void)
{
    return s_playing;
}
