/**
 * @file agent_orchestrator.c
 * @brief Multi-agent AI orchestrator — routes requests, runs background agents.
 */

#include "ai/agent_orchestrator.h"
#include "ai/intent_engine.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>

static const char *TAG = "ai_orch";

static agent_t         s_agents[AGENT_COUNT];
static bool            s_registered[AGENT_COUNT];
static agent_response_t s_last_resp[AGENT_COUNT];
static QueueHandle_t   s_req_queue;

#define REQ_QUEUE_LEN   8

/* ── Event handler: listen for AI requests from event bus ── */
static void on_agent_event(const event_t *evt, void *ctx)
{
    if (evt->type == EVT_VOICE_RESULT && evt->data.ptr) {
        agent_request_t req = {0};
        req.target = AGENT_VOICE;  /* Will be auto-routed */
        strncpy(req.input, (const char *)evt->data.ptr, sizeof(req.input) - 1);
        xQueueSend(s_req_queue, &req, pdMS_TO_TICKS(100));
    }
}

void agent_orchestrator_init(void)
{
    s_req_queue = xQueueCreate(REQ_QUEUE_LEN, sizeof(agent_request_t));
    memset(s_agents, 0, sizeof(s_agents));
    memset(s_registered, 0, sizeof(s_registered));

    event_subscribe(EVT_CAT_AI, on_agent_event, NULL);
    ESP_LOGI(TAG, "Agent orchestrator initialised");
}

void agent_register(const agent_t *agent)
{
    if (agent->id >= AGENT_COUNT) return;
    memcpy(&s_agents[agent->id], agent, sizeof(agent_t));
    s_registered[agent->id] = true;
    ESP_LOGI(TAG, "Agent registered: %s (id=%d, bg=%lu ms)",
             agent->name, agent->id, agent->bg_interval_ms);
}

/* ── Auto-routing: find best agent for input ── */
static agent_id_t find_best_agent(const char *input)
{
    /* Priority: check local intent first, then ask each agent */
    for (int i = 0; i < AGENT_COUNT; i++) {
        if (s_registered[i] && s_agents[i].can_handle &&
            s_agents[i].can_handle(input)) {
            return (agent_id_t)i;
        }
    }
    return AGENT_VOICE;  /* Default to voice agent (cloud LLM) */
}

void agent_route_request(const agent_request_t *req)
{
    if (req) xQueueSend(s_req_queue, req, pdMS_TO_TICKS(100));
}

agent_state_t agent_get_state(agent_id_t id)
{
    if (id >= AGENT_COUNT || !s_registered[id]) return AGENT_STATE_IDLE;
    return s_agents[id].state;
}

const agent_response_t *agent_get_last_response(agent_id_t id)
{
    if (id >= AGENT_COUNT) return NULL;
    return &s_last_resp[id];
}

/* ── Main orchestrator task ── */
void agent_orchestrator_task(void *arg)
{
    ESP_LOGI(TAG, "Agent orchestrator running on core %d", xPortGetCoreID());

    uint32_t bg_timers[AGENT_COUNT] = {0};
    agent_request_t req;

    while (1) {
        /* Process pending requests (non-blocking, short timeout) */
        if (xQueueReceive(s_req_queue, &req, pdMS_TO_TICKS(50)) == pdTRUE) {
            agent_id_t target = req.target;

            /* Auto-route if target is voice (default) */
            if (target == AGENT_VOICE) {
                agent_id_t best = find_best_agent(req.input);
                if (best != AGENT_VOICE && s_registered[best]) {
                    target = best;
                }
            }

            if (s_registered[target] && s_agents[target].process) {
                int64_t t0 = esp_timer_get_time();
                s_agents[target].state = AGENT_STATE_THINKING;

                agent_response_t resp = {0};
                resp.source = target;
                s_agents[target].process(&req, &resp);

                s_agents[target].state = resp.success ?
                    AGENT_STATE_IDLE : AGENT_STATE_ERROR;
                memcpy(&s_last_resp[target], &resp, sizeof(resp));

                uint32_t latency = (uint32_t)((esp_timer_get_time() - t0) / 1000);
                perf_log_event("agent_process", latency);
                ESP_LOGI(TAG, "Agent %s processed in %lu ms: %s",
                         s_agents[target].name, latency,
                         resp.success ? "OK" : "FAIL");

                event_post(EVT_AGENT_RESPONSE, (int32_t)target);
            }
        }

        /* Run background agents */
        uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
        for (int i = 0; i < AGENT_COUNT; i++) {
            if (s_registered[i] && s_agents[i].bg_interval_ms > 0 &&
                s_agents[i].background_tick &&
                (now - bg_timers[i] >= s_agents[i].bg_interval_ms))
            {
                s_agents[i].background_tick();
                bg_timers[i] = now;
            }
        }
    }
}
