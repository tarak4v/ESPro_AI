/**
 * @file voice_pipeline.c
 * @brief Voice AI pipeline — push-to-talk, STT, intent routing.
 *
 * State machine:
 *   IDLE → (mic button pressed) → LISTENING
 *   LISTENING → (silence > 1.5s) → PROCESSING
 *   PROCESSING → (STT + intent) → RESPONDING
 *   RESPONDING → (display result) → IDLE
 */

#include "ai/voice_pipeline.h"
#include "ai/stt_client.h"
#include "ai/llm_client.h"
#include "ai/intent_engine.h"
#include "ai/action_executor.h"
#include "ai/agent_orchestrator.h"
#include "hal/audio.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"
#include "ui/ui_manager.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "voice";

#define SILENCE_THRESHOLD 30    /* Level below this = silence */
#define SILENCE_TIMEOUT_MS 1500 /* How long silence before stopping */
#define MIN_RECORD_MS 300       /* Minimum recording duration */
#define MAX_RECORD_MS 10000     /* Maximum recording duration */

static volatile voice_pipeline_state_t s_state = VP_IDLE;
static volatile bool s_force_trigger = false;
static volatile bool s_stop_listening = false;

void voice_pipeline_init(void)
{
    ESP_LOGI(TAG, "Voice pipeline initialised (push-to-talk mode)");
}

voice_pipeline_state_t voice_pipeline_get_state(void)
{
    return s_state;
}

void voice_pipeline_trigger(void)
{
    s_force_trigger = true;
    ESP_LOGI(TAG, ">>> Mic trigger requested (press)");
}

void voice_pipeline_stop_listening(void)
{
    if (s_state == VP_LISTENING)
    {
        s_stop_listening = true;
        ESP_LOGI(TAG, ">>> Stop listening requested (release)");
    }
}

void voice_pipeline_task(void *arg)
{
    ESP_LOGI(TAG, "Voice pipeline running on core %d (push-to-talk)", xPortGetCoreID());

    /* Mic is NOT started here — only on trigger */

    while (1)
    {
        switch (s_state)
        {

        case VP_IDLE:
        {
            /* Wait for explicit trigger (mic button tap) */
            if (s_force_trigger)
            {
                s_force_trigger = false;
                ESP_LOGI(TAG, ">>> Mic activated — starting recording");
                audio_mic_start();
                audio_mic_clear();
                vTaskDelay(pdMS_TO_TICKS(100)); /* let mic warm up */
                s_state = VP_LISTENING;
                event_post(EVT_VOICE_WAKE, 0);
            }
            vTaskDelay(pdMS_TO_TICKS(50));
            break;
        }

        case VP_LISTENING:
        {
            int64_t listen_start = esp_timer_get_time();

            /* Record while button held — stop on release or max time */
            while (s_state == VP_LISTENING)
            {
                int64_t now = esp_timer_get_time();
                int64_t elapsed_ms = (now - listen_start) / 1000;

                if (s_stop_listening)
                {
                    s_stop_listening = false;
                    ESP_LOGI(TAG, "Recording done: %lld ms, reason=button_release", elapsed_ms);
                    s_state = VP_PROCESSING;
                }
                else if (elapsed_ms > MAX_RECORD_MS)
                {
                    ESP_LOGI(TAG, "Recording done: %lld ms, reason=max_time", elapsed_ms);
                    s_state = VP_PROCESSING;
                }

                vTaskDelay(pdMS_TO_TICKS(30));
            }
            break;
        }

        case VP_PROCESSING:
        {
            int64_t t0 = esp_timer_get_time();

            /* Stop mic first so buffer is stable during STT */
            audio_mic_stop();

            /* Get recorded audio */
            size_t samples = 0;
            const int16_t *buf = audio_mic_get_buffer(&samples);

            ESP_LOGI(TAG, ">>> PROCESSING: %d samples (%.1f sec)",
                     (int)samples, (float)samples / 16000.0f);

            if (samples < 4800)
            { /* < 300ms at 16kHz */
                ESP_LOGW(TAG, "Recording too short (%d samples), ignoring", (int)samples);
                s_state = VP_IDLE;
                break;
            }

            /* STT */
            ESP_LOGI(TAG, ">>> Sending %d samples to STT...", (int)samples);
            char transcript[256] = {0};
            int64_t stt_start = esp_timer_get_time();
            bool stt_ok = stt_transcribe(buf, samples, transcript, sizeof(transcript));
            uint32_t stt_ms = (uint32_t)((esp_timer_get_time() - stt_start) / 1000);
            perf_log_event(PERF_SLOT_VOICE_STT, stt_ms);

            audio_mic_clear();

            if (!stt_ok || transcript[0] == '\0')
            {
                ESP_LOGW(TAG, ">>> STT failed or empty (ok=%d, ms=%lu)", stt_ok, stt_ms);
                audio_mic_stop();
                s_state = VP_IDLE;
                break;
            }

            ESP_LOGI(TAG, ">>> STT result (%lu ms): \"%s\"", stt_ms, transcript);

            /* Show transcript on home screen stage */
            scr_home_set_stage_text(transcript);

            /* Try local intent first */
            intent_result_t intent;
            if (intent_classify_local(transcript, &intent))
            {
                ESP_LOGI(TAG, "Local intent: %s", intent.action);

                /* Execute action and display response */
                char response[256] = {0};
                if (action_execute(&intent, response, sizeof(response)))
                {
                    scr_home_set_stage_text(response);
                }

                event_post_ptr(EVT_INTENT_LOCAL, NULL);
                s_state = VP_RESPONDING;
            }
            else
            {
                /* Cloud LLM — synchronous call */
                scr_home_set_stage_text(transcript);

                char llm_resp[512] = {0};
                int64_t llm_start = esp_timer_get_time();
                bool llm_ok = llm_chat(
                    "You are a helpful smartwatch assistant. Be concise.",
                    transcript, llm_resp, sizeof(llm_resp));
                perf_log_event(PERF_SLOT_VOICE_LLM,
                               (uint32_t)((esp_timer_get_time() - llm_start) / 1000));

                if (llm_ok && llm_resp[0] != '\0')
                {
                    ESP_LOGI(TAG, "LLM response: %.100s", llm_resp);
                    scr_home_set_stage_text(llm_resp);
                }
                else
                {
                    ESP_LOGW(TAG, "LLM failed or empty");
                    scr_home_set_stage_text("No response from AI");
                }

                event_post_ptr(EVT_AGENT_RESPONSE, NULL);
                s_state = VP_RESPONDING;
            }

            uint32_t total_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
            ESP_LOGI(TAG, "Processing complete in %lu ms", total_ms);
            break;
        }

        case VP_RESPONDING:
        {
            /* Display result for 5 seconds then return to idle */
            vTaskDelay(pdMS_TO_TICKS(5000));
            audio_mic_stop();
            ESP_LOGI(TAG, ">>> Returning to idle, mic stopped");
            s_state = VP_IDLE;
            break;
        }
        }
    }
}
