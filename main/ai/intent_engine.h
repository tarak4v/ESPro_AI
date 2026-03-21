/**
 * @file intent_engine.h
 * @brief Local intent classification — offline command matching.
 */
#ifndef INTENT_ENGINE_H
#define INTENT_ENGINE_H

#include <stdbool.h>

typedef struct {
    char action[32];    /* e.g., "start_timer", "next_task", "set_volume" */
    char entity[64];    /* e.g., "10 minutes", "meeting", "80" */
    int  confidence;    /* 0-100 */
} intent_result_t;

/**
 * @brief Classify intent locally (no network).
 * @return true if a local intent was matched.
 */
bool intent_classify_local(const char *transcript, intent_result_t *result);

#endif /* INTENT_ENGINE_H */
