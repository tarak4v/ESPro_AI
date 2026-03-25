/**
 * @file power_manager.c
 * @brief Display sleep/wake — auto-dim after 15s, screen-off after 30s,
 *        wake on touch, double-tap, or wrist-raise.
 */

#include "services/power_manager.h"
#include "hal/display.h"
#include "core/event_bus.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "power_mgr";

#define DIM_TIMEOUT_MS 15000   /* Dim backlight after 15s idle */
#define SLEEP_TIMEOUT_MS 30000 /* Screen off after 30s idle */

static volatile int64_t s_last_activity_us = 0;

typedef enum
{
    PWR_ACTIVE, /* Full brightness */
    PWR_DIM,    /* Dimmed */
    PWR_SLEEP,  /* Screen off */
} power_state_t;

static volatile power_state_t s_state = PWR_ACTIVE;

/* Event handler — wake on any input or sensor gesture */
static void on_wake_event(const event_t *evt, void *ctx)
{
    switch (evt->type)
    {
    case EVT_TOUCH_TAP:
    case EVT_TOUCH_SWIPE_LEFT:
    case EVT_TOUCH_SWIPE_RIGHT:
    case EVT_TOUCH_SWIPE_UP:
    case EVT_TOUCH_SWIPE_DOWN:
    case EVT_TOUCH_LONG_PRESS:
    case EVT_BUTTON_PRESS:
        power_manager_activity();
        break;
    default:
        break;
    }
}

static void on_gesture_event(const event_t *evt, void *ctx)
{
    if (evt->type == EVT_IMU_GESTURE)
    {
        int gesture_id = evt->data.ival;
        /* 1 = double-tap, 2 = tilt-acknowledge */
        if (gesture_id == 1 || gesture_id == 2)
        {
            power_manager_activity();
        }
    }
}

void power_manager_init(void)
{
    s_last_activity_us = esp_timer_get_time();
    s_state = PWR_ACTIVE;

    event_subscribe(EVT_CAT_INPUT, on_wake_event, NULL);
    event_subscribe(EVT_CAT_SENSOR, on_gesture_event, NULL);

    ESP_LOGI(TAG, "Power manager init: dim=%ds, sleep=%ds",
             DIM_TIMEOUT_MS / 1000, SLEEP_TIMEOUT_MS / 1000);
}

void power_manager_activity(void)
{
    s_last_activity_us = esp_timer_get_time();

    if (s_state != PWR_ACTIVE)
    {
        s_state = PWR_ACTIVE;
        display_set_brightness(0); /* 0 = full bright (inverted) */
        ESP_LOGI(TAG, "Wake — display active");
    }
}

bool power_manager_is_sleeping(void)
{
    return s_state == PWR_SLEEP;
}

/**
 * Called periodically from app_manager_task to check idle timeout.
 * This avoids creating yet another FreeRTOS task.
 */
void power_manager_tick(void)
{
    int64_t idle_ms = (esp_timer_get_time() - s_last_activity_us) / 1000;

    switch (s_state)
    {
    case PWR_ACTIVE:
        if (idle_ms >= SLEEP_TIMEOUT_MS)
        {
            s_state = PWR_SLEEP;
            display_set_brightness(255); /* 255 = off (inverted) */
            ESP_LOGI(TAG, "Display sleep");
        }
        else if (idle_ms >= DIM_TIMEOUT_MS)
        {
            s_state = PWR_DIM;
            display_set_brightness(180); /* Dimmed */
            ESP_LOGI(TAG, "Display dimmed");
        }
        break;
    case PWR_DIM:
        if (idle_ms >= SLEEP_TIMEOUT_MS)
        {
            s_state = PWR_SLEEP;
            display_set_brightness(255);
            ESP_LOGI(TAG, "Display sleep");
        }
        break;
    case PWR_SLEEP:
        /* Stays sleeping until power_manager_activity() is called */
        break;
    }
}
