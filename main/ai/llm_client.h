/**
 * @file llm_client.h
 * @brief LLM client — Groq/OpenAI-compatible chat completions.
 */
#ifndef LLM_CLIENT_H
#define LLM_CLIENT_H

#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Send a chat completion request.
 * @param system_prompt  System message.
 * @param user_msg       User message.
 * @param out            Response buffer.
 * @param out_len        Buffer size.
 * @return true on success.
 */
bool llm_chat(const char *system_prompt, const char *user_msg,
              char *out, size_t out_len);

#endif /* LLM_CLIENT_H */
