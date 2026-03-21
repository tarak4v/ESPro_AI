/**
 * @file event_bus.h
 * @brief System-wide event bus for inter-task communication.
 *
 * All tasks post events here instead of calling each other directly.
 * This decouples producers from consumers and enables logging/replay.
 */
#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

/* ── Event categories ── */
typedef enum {
    EVT_CAT_SYSTEM   = 0x00,
    EVT_CAT_INPUT    = 0x10,
    EVT_CAT_UI       = 0x20,
    EVT_CAT_AI       = 0x30,
    EVT_CAT_SENSOR   = 0x40,
    EVT_CAT_NETWORK  = 0x50,
    EVT_CAT_AUDIO    = 0x60,
} event_category_t;

/* ── Event types ── */
typedef enum {
    /* System */
    EVT_BOOT_COMPLETE       = EVT_CAT_SYSTEM | 0x01,
    EVT_LOW_BATTERY         = EVT_CAT_SYSTEM | 0x02,
    EVT_SLEEP_REQUEST       = EVT_CAT_SYSTEM | 0x03,
    EVT_WAKE_REQUEST        = EVT_CAT_SYSTEM | 0x04,

    /* Input */
    EVT_TOUCH_TAP           = EVT_CAT_INPUT | 0x01,
    EVT_TOUCH_SWIPE_LEFT    = EVT_CAT_INPUT | 0x02,
    EVT_TOUCH_SWIPE_RIGHT   = EVT_CAT_INPUT | 0x03,
    EVT_TOUCH_SWIPE_UP      = EVT_CAT_INPUT | 0x04,
    EVT_TOUCH_SWIPE_DOWN    = EVT_CAT_INPUT | 0x05,
    EVT_TOUCH_LONG_PRESS    = EVT_CAT_INPUT | 0x06,
    EVT_BUTTON_PRESS        = EVT_CAT_INPUT | 0x07,
    EVT_BUTTON_LONG_PRESS   = EVT_CAT_INPUT | 0x08,

    /* UI */
    EVT_SCREEN_CHANGE       = EVT_CAT_UI | 0x01,
    EVT_NOTIFICATION_SHOW   = EVT_CAT_UI | 0x02,
    EVT_NOTIFICATION_DISMISS= EVT_CAT_UI | 0x03,

    /* AI */
    EVT_VOICE_WAKE          = EVT_CAT_AI | 0x01,
    EVT_VOICE_RESULT        = EVT_CAT_AI | 0x02,
    EVT_AGENT_REQUEST       = EVT_CAT_AI | 0x03,
    EVT_AGENT_RESPONSE      = EVT_CAT_AI | 0x04,
    EVT_INTENT_LOCAL        = EVT_CAT_AI | 0x05,
    EVT_INTENT_CLOUD        = EVT_CAT_AI | 0x06,

    /* Sensor */
    EVT_IMU_GESTURE         = EVT_CAT_SENSOR | 0x01,
    EVT_IMU_ORIENTATION     = EVT_CAT_SENSOR | 0x02,
    EVT_STEP_COUNT          = EVT_CAT_SENSOR | 0x03,

    /* Network */
    EVT_WIFI_CONNECTED      = EVT_CAT_NETWORK | 0x01,
    EVT_WIFI_DISCONNECTED   = EVT_CAT_NETWORK | 0x02,
    EVT_NTP_SYNCED          = EVT_CAT_NETWORK | 0x03,
    EVT_HTTP_DONE           = EVT_CAT_NETWORK | 0x04,

    /* Audio */
    EVT_AUDIO_PLAY_START    = EVT_CAT_AUDIO | 0x01,
    EVT_AUDIO_PLAY_STOP     = EVT_CAT_AUDIO | 0x02,
    EVT_MIC_LEVEL           = EVT_CAT_AUDIO | 0x03,
} event_type_t;

/* ── Event payload ── */
typedef struct {
    event_type_t type;
    int64_t      timestamp_us;   /* esp_timer_get_time() at creation */
    union {
        int32_t  ival;
        float    fval;
        void    *ptr;            /* Caller owns lifetime */
        struct { int16_t x; int16_t y; } touch;
        struct { uint8_t id; uint8_t param; } agent;
    } data;
} event_t;

/* ── Subscriber callback ── */
typedef void (*event_handler_t)(const event_t *evt, void *ctx);

/**
 * @brief Initialise the event bus (call once from app_main).
 */
void event_bus_init(void);

/**
 * @brief Post an event (non-blocking, ISR-safe variant available).
 * @return true if queued successfully.
 */
bool event_post(event_type_t type, int32_t ival);
bool event_post_ptr(event_type_t type, void *ptr);
bool event_post_from_isr(event_type_t type, int32_t ival);

/**
 * @brief Subscribe to events matching a category mask.
 * @param mask  Bitwise OR of event_category_t values (0xFF = all).
 * @param handler  Callback invoked on the event dispatcher task.
 * @param ctx  User context passed to handler.
 * @return Subscription ID (for unsubscribe), or -1 on error.
 */
int event_subscribe(uint8_t category_mask, event_handler_t handler, void *ctx);

/**
 * @brief Unsubscribe a previously registered handler.
 */
void event_unsubscribe(int sub_id);

#endif /* EVENT_BUS_H */
