/**
 * @file agent_orchestrator.h
 * @brief Multi-agent AI orchestrator.
 *
 * Manages multiple AI agents that can run concurrently:
 *   - Voice Agent:     STT → Intent → Action → TTS
 *   - Proactive Agent: Context-aware suggestions (background)
 *   - Wellness Agent:  Health monitoring + reminders (background)
 *   - Task Agent:      Productivity, timers, calendar
 *
 * Agents register capabilities and the orchestrator routes
 * requests to the most appropriate agent.
 */
#ifndef AGENT_ORCHESTRATOR_H
#define AGENT_ORCHESTRATOR_H

#include <stdint.h>
#include <stdbool.h>

/* ── Agent IDs ── */
typedef enum {
    AGENT_VOICE = 0,
    AGENT_PROACTIVE,
    AGENT_WELLNESS,
    AGENT_TASK,
    AGENT_COUNT,
} agent_id_t;

/* ── Agent state ── */
typedef enum {
    AGENT_STATE_IDLE,
    AGENT_STATE_THINKING,
    AGENT_STATE_ACTING,
    AGENT_STATE_WAITING,
    AGENT_STATE_ERROR,
} agent_state_t;

/* ── Agent request ── */
typedef struct {
    agent_id_t  target;
    char        input[256];
    void       *context;
} agent_request_t;

/* ── Agent response ── */
typedef struct {
    agent_id_t  source;
    bool        success;
    char        text[256];
    int         action_code;
} agent_response_t;

/* ── Agent interface ── */
typedef struct {
    const char   *name;
    agent_id_t    id;
    agent_state_t state;
    bool        (*can_handle)(const char *input);
    void        (*process)(const agent_request_t *req, agent_response_t *resp);
    void        (*background_tick)(void);   /* Called periodically for bg agents */
    uint32_t      bg_interval_ms;           /* 0 = no background work */
} agent_t;

void agent_orchestrator_init(void);
void agent_orchestrator_task(void *arg);

/** Route a request to the best agent (auto-selects or uses specified target). */
void agent_route_request(const agent_request_t *req);

/** Register a custom agent. */
void agent_register(const agent_t *agent);

/** Get agent state. */
agent_state_t agent_get_state(agent_id_t id);

/** Get last response from an agent. */
const agent_response_t *agent_get_last_response(agent_id_t id);

#endif /* AGENT_ORCHESTRATOR_H */
