/**
 * @file meeting_service.h
 * @brief Meeting Assistant service — watch-side MQTT bridge for desktop meeting control.
 *
 * Communicates with a desktop companion app over MQTT to:
 *   - Monitor meeting state (mic, video, app, hand-raise)
 *   - Send control commands (mute, unmute, video toggle, hand raise)
 *   - Receive AI meeting notes and summaries
 *   - Track meeting duration and hand-off mode
 *
 * MQTT topics (base = espro/<device>/meeting):
 *   State (desktop → watch):
 *     .../state/active      "1" / "0"
 *     .../state/app         "meet" / "teams" / "zoom" / "none"
 *     .../state/mic         "on" / "off"
 *     .../state/video       "on" / "off"
 *     .../state/audio_dev   device name string
 *     .../state/hand        "up" / "down"
 *     .../notes/live        latest transcribed line
 *     .../notes/summary     end-of-meeting summary
 *     .../notes/handoff     discussion points while away
 *
 *   Commands (watch → desktop):
 *     .../cmd/mic           "toggle"
 *     .../cmd/video         "toggle"
 *     .../cmd/hand          "toggle"
 *     .../cmd/volume        "up" / "down" / "50" (0-100)
 *     .../cmd/audio_dev     "next" / device name
 *     .../cmd/handoff       "start" / "stop"
 *     .../cmd/end           "1"  (request meeting end summary)
 */
#ifndef MEETING_SERVICE_H
#define MEETING_SERVICE_H

#include <stdbool.h>
#include <stdint.h>

/* ── Meeting state structure ── */
typedef struct
{
    bool active;           /* Meeting currently in progress */
    char app[16];          /* "meet", "teams", "zoom", "none" */
    bool mic_on;           /* Microphone state */
    bool video_on;         /* Camera state */
    bool hand_raised;      /* Hand raise state */
    char audio_device[64]; /* Current audio output device */
    bool handoff_active;   /* Away/hand-off mode active */
    uint32_t duration_s;   /* Meeting duration in seconds */
    uint32_t start_time;   /* esp_timer seconds at meeting start */

    /* Notes */
    char live_note[128]; /* Latest transcribed line */
    char summary[256];   /* End-of-meeting summary */
    char handoff[256];   /* Discussion points while away */
    bool notes_dirty;    /* New notes available */
    bool summary_dirty;  /* New summary available */
    bool handoff_dirty;  /* New handoff points available */
} meeting_state_t;

/**
 * @brief Initialise meeting service — subscribes to MQTT meeting topics.
 */
void meeting_service_init(void);

/**
 * @brief Get current meeting state (read-only).
 */
const meeting_state_t *meeting_get_state(void);

/* ── Control commands (sent to desktop via MQTT) ── */
void meeting_toggle_mic(void);
void meeting_toggle_video(void);
void meeting_toggle_hand(void);
void meeting_volume_up(void);
void meeting_volume_down(void);
void meeting_volume_set(int vol);
void meeting_next_audio_device(void);
void meeting_start_handoff(void);
void meeting_stop_handoff(void);
void meeting_request_summary(void);

/**
 * @brief Handle incoming MQTT message on meeting topics.
 *        Called by mqtt_service when data arrives on meeting/# topics.
 */
void meeting_handle_mqtt(const char *topic, const char *data);

#endif /* MEETING_SERVICE_H */
