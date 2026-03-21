/**
 * @file audio.c
 * @brief Audio HAL — I2S full-duplex, ES8311/ES7210 codecs, mic recording.
 */

#include "hal/audio.h"
#include "hw_config.h"
#include "core/perf_monitor.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "driver/i2s_std.h"
#include "esp_io_expander_tca9554.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "audio";

/* Recording buffer — 10 s at 16 kHz mono */
#define MIC_BUF_SAMPLES    (AUDIO_SAMPLE_RATE * 10)
#define MIC_CHUNK_SAMPLES  512

static int16_t *s_mic_buf = NULL;
static volatile size_t s_mic_pos = 0;
static volatile uint8_t s_mic_level = 0;
static volatile bool s_mic_active = false;

static esp_codec_dev_handle_t s_spk_codec = NULL;
static esp_codec_dev_handle_t s_mic_codec = NULL;
static i2s_chan_handle_t s_i2s_tx = NULL;
static i2s_chan_handle_t s_i2s_rx = NULL;

static void mic_reader_task(void *arg);

void audio_init(void)
{
    /* Allocate mic buffer in PSRAM */
    s_mic_buf = heap_caps_calloc(MIC_BUF_SAMPLES, sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!s_mic_buf) {
        ESP_LOGE(TAG, "Failed to alloc mic buffer");
        return;
    }

    /* I2S channel init (full-duplex) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx);

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                         I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_PIN_MCLK,
            .bclk = I2S_PIN_BCLK,
            .ws   = I2S_PIN_WS,
            .dout = I2S_PIN_DOUT,
            .din  = I2S_PIN_DIN,
        },
    };
    i2s_channel_init_std_mode(s_i2s_tx, &std_cfg);
    i2s_channel_init_std_mode(s_i2s_rx, &std_cfg);

    /* TODO: Init ES8311 speaker codec + ES7210 mic codec via esp_codec_dev */
    /* This follows the same pattern as the existing project's mic_input.c */

    ESP_LOGI(TAG, "Audio init: I2S %d Hz, mic buf %d samples in PSRAM",
             AUDIO_SAMPLE_RATE, MIC_BUF_SAMPLES);
}

void audio_pa_enable(bool enable)
{
    /* TCA9554 bit 7 = PA enable */
    extern esp_io_expander_handle_t g_io_expander;
    if (g_io_expander) {
        esp_io_expander_set_level(g_io_expander, TCA9554_PA_BIT, enable ? 1 : 0);
    }
}

void audio_set_volume(uint8_t vol)
{
    if (s_spk_codec) {
        esp_codec_dev_set_out_vol(s_spk_codec, vol);
    }
}

void audio_mic_start(void)
{
    if (s_mic_active) return;
    s_mic_pos = 0;
    s_mic_active = true;
    i2s_channel_enable(s_i2s_rx);

    xTaskCreatePinnedToCore(mic_reader_task, "mic_rd", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "Mic started");
}

void audio_mic_stop(void)
{
    s_mic_active = false;
    i2s_channel_disable(s_i2s_rx);
    ESP_LOGI(TAG, "Mic stopped, %zu samples captured", s_mic_pos);
}

uint8_t audio_mic_get_level(void)
{
    return s_mic_level;
}

const int16_t *audio_mic_get_buffer(size_t *out_samples)
{
    if (out_samples) *out_samples = s_mic_pos;
    return s_mic_buf;
}

void audio_mic_clear(void)
{
    s_mic_pos = 0;
    s_mic_level = 0;
}

void audio_play_pcm(const int16_t *samples, size_t count)
{
    if (!s_i2s_tx || !samples || count == 0) return;
    audio_pa_enable(true);
    i2s_channel_enable(s_i2s_tx);

    size_t written = 0;
    i2s_channel_write(s_i2s_tx, samples, count * sizeof(int16_t),
                      &written, pdMS_TO_TICKS(5000));

    i2s_channel_disable(s_i2s_tx);
    audio_pa_enable(false);
}

/* ── Mic reader task ── */
static void mic_reader_task(void *arg)
{
    int16_t chunk[MIC_CHUNK_SAMPLES];
    size_t bytes_read;

    while (s_mic_active) {
        if (i2s_channel_read(s_i2s_rx, chunk, sizeof(chunk),
                             &bytes_read, pdMS_TO_TICKS(100)) == ESP_OK)
        {
            int samples = bytes_read / sizeof(int16_t);

            /* Calculate RMS */
            int64_t sum_sq = 0;
            for (int i = 0; i < samples; i++) {
                sum_sq += (int32_t)chunk[i] * chunk[i];
            }
            float rms = sqrtf((float)sum_sq / samples);
            int level = (int)(20.0f * log10f(rms + 1.0f));
            if (level > 100) level = 100;
            if (level < 0) level = 0;
            s_mic_level = (uint8_t)level;

            /* Copy to recording buffer */
            if (s_mic_pos + samples <= MIC_BUF_SAMPLES) {
                memcpy(&s_mic_buf[s_mic_pos], chunk, bytes_read);
                s_mic_pos += samples;
            }
        }
    }

    vTaskDelete(NULL);
}
