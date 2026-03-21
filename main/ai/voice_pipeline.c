/**
 * @file voice_pipeline.c
 * @brief Voice AI pipeline — continuous mic monitoring, VAD, STT, intent routing.
 *
 * State machine:
 *   IDLE → (level > threshold) → LISTENING
 *   LISTENING → (silence > 1.5s) → PROCESSING
 *   PROCESSING → (STT + intent) → RESPONDING
 *   RESPONDING → (display result) → IDLE
 */

#include "ai/voice_pipeline.h"
#include "ai/stt_client.h"
#include "ai/llm_client.h"
#include "ai/intent_engine.h"
#include "ai/agent_orchestrator.h"
#include "hal/audio.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "voice";

#define VOICE_THRESHOLD    30    /* Mic level to trigger recording */
#define SILENCE_THRESHOLD  30    /* Level below this = silence */
#define SILENCE_TIMEOUT_MS 1500  /* How long silence before stopping */
#define MIN_RECORD_MS      300   /* Minimum recording duration */
#define MAX_RECORD_MS      10000 /* Maximum recording duration */

static volatile voice_pipeline_state_t s_state = VP_IDLE;
static volatile bool s_force_trigger = false;

void voice_pipeline_init(void)
{
    ESP_LOGI(TAG, "Voice pipeline initialised");
}

voice_pipeline_state_t voice_pipeline_get_state(void)
{
    return s_state;
}

void voice_pipeline_trigger(void)
{
    s_force_trigger = true;
}

void voice_pipeline_task(void *arg)
{
    ESP_LOGI(TAG, "Voice pipeline running on core %d", xPortGetCoreID());

    /* Start mic for continuous monitoring */
    audio_mic_start();

    while (1) {
        switch (s_state) {

        case VP_IDLE: {
            uint8_t level = audio_mic_get_level();
            if (level > VOICE_THRESHOLD || s_force_trigger) {
                s_force_trigger = false;
                audio_mic_clear();
                s_state = VP_LISTENING;
                event_post(EVT_VOICE_WAKE, level);
                ESP_LOGI(TAG, "Voice wake (level=%d)", level);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }

        case VP_LISTENING: {
            int64_t listen_start = esp_timer_get_time();
            int64_t last_voice   = listen_start;

            while (s_state == VP_LISTENING) {
                uint8_t level = audio_mic_get_level();
                int64_t now = esp_timer_get_time();
                int64_t elapsed_ms = (now - listen_start) / 1000;

                if (level >= SILENCE_THRESHOLD) {
                    last_voice = now;
                }

                int64_t silence_ms = (now - last_voice) / 1000;

                /* Stop conditions */
                if (silence_ms > SILENCE_TIMEOUT_MS && elapsed_ms > MIN_RECORD_MS) {
                    ESP_LOGI(TAG, "Recording done: %lld ms, reason=silence", elapsed_ms);
                    s_state = VP_PROCESSING;
                } else if (elapsed_ms > MAX_RECORD_MS) {
                    ESP_LOGI(TAG, "Recording done: %lld ms, reason=max_time", elapsed_ms);
                    s_state = VP_PROCESSING;
                }

                vTaskDelay(pdMS_TO_TICKS(30));
            }
            break;
        }

        case VP_PROCESSING: {
            int64_t t0 = esp_timer_get_time();

            /* Get recorded audio */
            size_t samples = 0;
            const int16_t *buf = audio_mic_get_buffer(&samples);

            if (samples < 4800) {  /* < 300ms at 16kHz */
                ESP_LOGW(TAG, "Recording too short (%d samples), ignoring", (int)samples);
                s_state = VP_IDLE;
                break;
            }

            /* STT */
            char transcript[256] = {0};
            int64_t stt_start = esp_timer_get_time();
            bool stt_ok = stt_transcribe(buf, samples, transcript, sizeof(transcript));
            perf_log_event(PERF_SLOT_VOICE_STT,
                           (uint32_t)((esp_timer_get_time() - stt_start) / 1000));

            audio_mic_clear();

            if (!stt_ok || transcript[0] == '\0') {
                ESP_LOGW(TAG, "STT failed or empty");
                s_state = VP_IDLE;
                break;
            }

            ESP_LOGI(TAG, "STT: \"%s\"", transcript);

            /* Try local intent first */
            intent_result_t intent;
            if (intent_classify_local(transcript, &intent)) {
                ESP_LOGI(TAG, "Local intent: %s", intent.action);
                event_post_ptr(EVT_INTENT_LOCAL, NULL);
                s_state = VP_RESPONDING;
            } else {
                /* Route to agent orchestrator for cloud LLM */
                agent_request_t req = {0};
                req.target = AGENT_VOICE;
                strncpy(req.input, transcript, sizeof(req.input) - 1);

                int64_t llm_start = esp_timer_get_time();
                agent_route_request(&req);
                perf_log_event(PERF_SLOT_VOICE_LLM,
                               (uint32_t)((esp_timer_get_time() - llm_start) / 1000));
                s_state = VP_RESPONDING;
            }

            uint32_t total_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGI(TAG, "Processing complete in %lu ms", total_ms);
            break;
        }

        case VP_RESPONDING: {
            /* Display result for 3 seconds then return to idle */
            vTaskDelay(pdMS_TO_TICKS(3000));
            s_state = VP_IDLE;
            break;
        }
        }
    }
}
