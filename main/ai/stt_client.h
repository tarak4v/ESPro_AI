/**
 * @file stt_client.h
 * @brief Speech-to-text client — Groq Whisper API.
 */
#ifndef STT_CLIENT_H
#define STT_CLIENT_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/**
 * @brief Transcribe audio via Groq Whisper API.
 * @param samples  PCM 16-bit mono samples at 16 kHz.
 * @param count    Number of samples.
 * @param out      Output transcript buffer.
 * @param out_len  Output buffer size.
 * @return true on success.
 */
bool stt_transcribe(const int16_t *samples, size_t count,
                    char *out, size_t out_len);

#endif /* STT_CLIENT_H */
