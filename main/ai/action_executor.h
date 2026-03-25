/**
 * @file action_executor.h
 * @brief Executes actions from local intent classification results.
 */
#ifndef ACTION_EXECUTOR_H
#define ACTION_EXECUTOR_H

#include "ai/intent_engine.h"
#include <stddef.h>

/**
 * @brief Execute a local intent action and return a human-readable response.
 * @param intent  The classified intent (action + entity).
 * @param response_out  Buffer for the response text to display.
 * @param resp_len  Size of response_out buffer.
 * @return true if the action was executed successfully.
 */
bool action_execute(const intent_result_t *intent, char *response_out, size_t resp_len);

#endif /* ACTION_EXECUTOR_H */
