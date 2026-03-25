/**
 * @file tts_client.h
 * @brief Text-to-Speech client — synthesizes speech from text via cloud API.
 */
#ifndef TTS_CLIENT_H
#define TTS_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * @brief Synthesize speech from text and play through speaker.
 * @param text  The text to speak.
 * @return true on success, false on failure.
 */
bool tts_speak(const char *text);

#endif /* TTS_CLIENT_H */
