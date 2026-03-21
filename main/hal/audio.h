/**
 * @file audio.h
 * @brief Audio HAL — I2S, ES8311 DAC, ES7210 ADC, TCA9554 PA.
 */
#ifndef HAL_AUDIO_H
#define HAL_AUDIO_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/** Initialise I2S + codec devices. */
void audio_init(void);

/** Enable/disable power amplifier. */
void audio_pa_enable(bool enable);

/** Set speaker volume (0-255). */
void audio_set_volume(uint8_t vol);

/** Start microphone recording into internal ring buffer. */
void audio_mic_start(void);

/** Stop microphone recording. */
void audio_mic_stop(void);

/** Get current mic RMS level (0-100). */
uint8_t audio_mic_get_level(void);

/** Get recorded audio buffer pointer + length. */
const int16_t *audio_mic_get_buffer(size_t *out_samples);

/** Clear recording buffer. */
void audio_mic_clear(void);

/** Play PCM samples through speaker. */
void audio_play_pcm(const int16_t *samples, size_t count);

#endif /* HAL_AUDIO_H */
