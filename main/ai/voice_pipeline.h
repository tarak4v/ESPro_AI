/**
 * @file voice_pipeline.h
 * @brief Voice AI pipeline — VAD → STT → Intent → Action → TTS.
 */
#ifndef VOICE_PIPELINE_H
#define VOICE_PIPELINE_H

#include <stdbool.h>

typedef enum
{
    VP_IDLE,
    VP_LISTENING,
    VP_PROCESSING,
    VP_RESPONDING,
} voice_pipeline_state_t;

void voice_pipeline_init(void);
void voice_pipeline_task(void *arg);

voice_pipeline_state_t voice_pipeline_get_state(void);

/** Start listening (call on button press). */
void voice_pipeline_trigger(void);

/** Stop listening and process (call on button release). */
void voice_pipeline_stop_listening(void);

#endif /* VOICE_PIPELINE_H */
