/**
 * @file audio.c
 * @brief Audio HAL — I2S full-duplex, ES8311/ES7210 codecs, mic recording.
 */

#include "hal/audio.h"
#include "hal/display.h"
#include "hw_config.h"
#include "core/perf_monitor.h"

#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "es8311_codec.h"
#include "es7210_adc.h"
#include "driver/i2s_std.h"
#include "esp_io_expander_tca9554.h"
#include "i2c_bsp.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <math.h>

static const char *TAG = "audio";

/* Recording buffer — 10 s at 16 kHz mono */
#define MIC_BUF_SAMPLES (AUDIO_SAMPLE_RATE * 10)
#define MIC_CHUNK_SAMPLES 512
#define BEEP_FREQ_HZ 1000
#define BEEP_DURATION_MS 1000

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
    if (!s_mic_buf)
    {
        ESP_LOGE(TAG, "Failed to alloc mic buffer");
        return;
    }

    /* I2S full-duplex channel creation */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &s_i2s_tx, &s_i2s_rx));

    /* TX config — provides MCLK/BCLK/WS clocks to both codecs */
    i2s_std_config_t tx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_PIN_MCLK,
            .bclk = I2S_PIN_BCLK,
            .ws = I2S_PIN_WS,
            .dout = I2S_PIN_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    tx_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_tx, &tx_cfg));

    /* RX config — uses clock from TX, only DIN pin */
    i2s_std_config_t rx_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT,
                                                        I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = I2S_GPIO_UNUSED,
            .ws = I2S_GPIO_UNUSED,
            .dout = I2S_GPIO_UNUSED,
            .din = I2S_PIN_DIN,
        },
    };
    rx_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;
    ESP_ERROR_CHECK(i2s_channel_init_std_mode(s_i2s_rx, &rx_cfg));

    /* Enable TX to provide clock signals to codecs */
    i2s_channel_enable(s_i2s_tx);

    /* ── ES8311 Speaker DAC codec ── */
    audio_codec_i2c_cfg_t spk_i2c = {
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_port0_bus_handle,
    };
    const audio_codec_ctrl_if_t *spk_ctrl = audio_codec_new_i2c_ctrl(&spk_i2c);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = spk_ctrl,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,
        .use_mclk = true,
        .mclk_div = 256,
    };
    const audio_codec_if_t *spk_codec_if = es8311_codec_new(&es8311_cfg);

    audio_codec_i2s_cfg_t spk_data_cfg = {
        .port = 0,
        .tx_handle = s_i2s_tx,
    };
    const audio_codec_data_if_t *spk_data = audio_codec_new_i2s_data(&spk_data_cfg);

    esp_codec_dev_cfg_t spk_dev_cfg = {
        .codec_if = spk_codec_if,
        .data_if = spk_data,
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
    };
    s_spk_codec = esp_codec_dev_new(&spk_dev_cfg);
    if (s_spk_codec)
    {
        ESP_LOGI(TAG, "ES8311 speaker codec initialised");
    }
    else
    {
        ESP_LOGW(TAG, "ES8311 speaker codec init failed");
    }

    /* ── ES7210 Microphone ADC codec ── */
    audio_codec_i2c_cfg_t mic_i2c = {
        .addr = ES7210_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_port0_bus_handle,
    };
    const audio_codec_ctrl_if_t *mic_ctrl = audio_codec_new_i2c_ctrl(&mic_i2c);

    es7210_codec_cfg_t es7210_cfg = {
        .ctrl_if = mic_ctrl,
        .master_mode = false,
        .mic_selected = ES7210_SEL_MIC1,
        .mclk_src = ES7210_MCLK_FROM_PAD,
        .mclk_div = 256,
    };
    const audio_codec_if_t *mic_codec_if = es7210_codec_new(&es7210_cfg);

    audio_codec_i2s_cfg_t mic_data_cfg = {
        .port = 0,
        .rx_handle = s_i2s_rx,
    };
    const audio_codec_data_if_t *mic_data = audio_codec_new_i2s_data(&mic_data_cfg);

    esp_codec_dev_cfg_t mic_dev_cfg = {
        .codec_if = mic_codec_if,
        .data_if = mic_data,
        .dev_type = ESP_CODEC_DEV_TYPE_IN,
    };
    s_mic_codec = esp_codec_dev_new(&mic_dev_cfg);
    if (s_mic_codec)
    {
        ESP_LOGI(TAG, "ES7210 mic codec initialised");
    }
    else
    {
        ESP_LOGW(TAG, "ES7210 mic codec init failed");
    }

    /* Open speaker codec once and keep it open forever.
       This avoids esp_codec_dev_open/close cycles that disrupt LEDC backlight. */
    if (s_spk_codec)
    {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .sample_rate = AUDIO_SAMPLE_RATE,
            .mclk_multiple = 256,
        };
        esp_err_t err = esp_codec_dev_open(s_spk_codec, &fs);
        if (err == ESP_OK)
        {
            esp_codec_dev_set_out_vol(s_spk_codec, 60);
            ESP_LOGI(TAG, "Speaker codec opened (persistent)");
        }
        else
        {
            ESP_LOGE(TAG, "Speaker codec persistent open failed: %s", esp_err_to_name(err));
        }
    }

    ESP_LOGI(TAG, "Audio init: I2S %d Hz, mic buf %d samples in PSRAM",
             AUDIO_SAMPLE_RATE, MIC_BUF_SAMPLES);
}

void audio_pa_enable(bool enable)
{
    if (io_expander_handle)
    {
        esp_io_expander_set_level(io_expander_handle, TCA9554_PA_BIT, enable ? 1 : 0);
    }
}

void audio_set_volume(uint8_t vol)
{
    if (s_spk_codec)
    {
        esp_codec_dev_set_out_vol(s_spk_codec, vol);
    }
}

void audio_mic_start(void)
{
    if (s_mic_active)
        return;
    s_mic_pos = 0;
    s_mic_active = true;

    /* Open mic codec device */
    if (s_mic_codec)
    {
        esp_codec_dev_sample_info_t fs = {
            .bits_per_sample = 16,
            .channel = 1,
            .sample_rate = AUDIO_SAMPLE_RATE,
        };
        esp_codec_dev_open(s_mic_codec, &fs);
        esp_codec_dev_set_in_gain(s_mic_codec, 30.0);
    }

    /* codec_dev_open enables I2S RX internally; ensure TX is running for MCLK */
    i2s_channel_enable(s_i2s_tx); /* no-op if already enabled, provides MCLK */

    xTaskCreatePinnedToCore(mic_reader_task, "mic_rd", 4096, NULL, 3, NULL, 0);
    ESP_LOGI(TAG, "Mic started");
}

void audio_mic_stop(void)
{
    s_mic_active = false;
    vTaskDelay(pdMS_TO_TICKS(50)); /* let reader task exit */
    /* codec_dev_close disables I2S RX internally — no manual disable needed */
    if (s_mic_codec)
    {
        esp_codec_dev_close(s_mic_codec);
    }
    ESP_LOGI(TAG, "Mic stopped, %zu samples captured", s_mic_pos);
}

uint8_t audio_mic_get_level(void)
{
    return s_mic_level;
}

const int16_t *audio_mic_get_buffer(size_t *out_samples)
{
    if (out_samples)
        *out_samples = s_mic_pos;
    return s_mic_buf;
}

void audio_mic_clear(void)
{
    s_mic_pos = 0;
    s_mic_level = 0;
}

void audio_play_pcm(const int16_t *samples, size_t count)
{
    if (!s_spk_codec || !samples || count == 0)
        return;
    audio_pa_enable(true);
    vTaskDelay(pdMS_TO_TICKS(20));

    /* Codec is kept open permanently — just write data */
    size_t total_bytes = count * sizeof(int16_t);
    size_t offset = 0;
    const size_t chunk_bytes = 2048;
    while (offset < total_bytes)
    {
        size_t to_write = total_bytes - offset;
        if (to_write > chunk_bytes)
            to_write = chunk_bytes;
        esp_codec_dev_write(s_spk_codec, (void *)((uint8_t *)samples + offset), (int)to_write);
        offset += to_write;
    }

    vTaskDelay(pdMS_TO_TICKS(20));
    audio_pa_enable(false);

    ESP_LOGI(TAG, "Playback done (%d samples)", (int)count);
}

void audio_beep(uint16_t freq_hz, uint16_t duration_ms)
{
    int total_samples = (AUDIO_SAMPLE_RATE * duration_ms) / 1000;
    int16_t *buf = heap_caps_malloc(total_samples * sizeof(int16_t), MALLOC_CAP_SPIRAM);
    if (!buf)
    {
        ESP_LOGW(TAG, "Beep: alloc failed");
        return;
    }

    /* Generate sine wave */
    for (int i = 0; i < total_samples; i++)
    {
        float t = (float)i / AUDIO_SAMPLE_RATE;
        buf[i] = (int16_t)(8000.0f * sinf(2.0f * 3.14159265f * freq_hz * t));
    }

    ESP_LOGI(TAG, "Beep: %d Hz, %d ms", freq_hz, duration_ms);
    audio_play_pcm(buf, total_samples);
    heap_caps_free(buf);
}

/* ── Streaming speaker API ── */

bool audio_speaker_open(void)
{
    if (!s_spk_codec)
        return false;
    audio_pa_enable(true);
    vTaskDelay(pdMS_TO_TICKS(20));
    return true;
}

void audio_speaker_write(const int16_t *samples, size_t count)
{
    if (!s_spk_codec || !samples || count == 0)
        return;
    size_t total_bytes = count * sizeof(int16_t);
    size_t offset = 0;
    const size_t chunk_bytes = 2048;
    while (offset < total_bytes)
    {
        size_t to_write = total_bytes - offset;
        if (to_write > chunk_bytes)
            to_write = chunk_bytes;
        esp_codec_dev_write(s_spk_codec, (void *)((uint8_t *)samples + offset), (int)to_write);
        offset += to_write;
    }
}

void audio_speaker_close(void)
{
    vTaskDelay(pdMS_TO_TICKS(20));
    audio_pa_enable(false);
}

/* ── Mic reader task ── */
static void mic_reader_task(void *arg)
{
    int16_t chunk[MIC_CHUNK_SAMPLES];
    size_t bytes_read;

    while (s_mic_active)
    {
        if (i2s_channel_read(s_i2s_rx, chunk, sizeof(chunk),
                             &bytes_read, pdMS_TO_TICKS(100)) == ESP_OK)
        {
            int samples = bytes_read / sizeof(int16_t);

            /* Calculate RMS */
            int64_t sum_sq = 0;
            for (int i = 0; i < samples; i++)
            {
                sum_sq += (int32_t)chunk[i] * chunk[i];
            }
            float rms = sqrtf((float)sum_sq / samples);
            int level = (int)(20.0f * log10f(rms + 1.0f));
            if (level > 100)
                level = 100;
            if (level < 0)
                level = 0;
            s_mic_level = (uint8_t)level;

            /* Copy to recording buffer */
            if (s_mic_pos + samples <= MIC_BUF_SAMPLES)
            {
                memcpy(&s_mic_buf[s_mic_pos], chunk, bytes_read);
                s_mic_pos += samples;
            }
        }
    }

    vTaskDelete(NULL);
}
