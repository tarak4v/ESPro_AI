/**
 * @file event_bus.c
 * @brief System event bus — queue-based pub/sub for inter-task communication.
 *
 * A single FreeRTOS queue collects events from any task/ISR.
 * A dedicated dispatcher task delivers them to registered subscribers.
 */

#include "event_bus.h"
#include "perf_monitor.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "evt_bus";

#define EVT_QUEUE_LEN       32
#define MAX_SUBSCRIBERS      16

typedef struct {
    uint8_t         category_mask;
    event_handler_t handler;
    void           *ctx;
    bool            active;
} subscriber_t;

static QueueHandle_t    s_evt_queue;
static subscriber_t     s_subs[MAX_SUBSCRIBERS];
static SemaphoreHandle_t s_subs_lock;

/* ── Dispatcher task ── */
static void event_dispatcher_task(void *arg)
{
    event_t evt;
    while (1) {
        if (xQueueReceive(s_evt_queue, &evt, portMAX_DELAY) == pdTRUE) {
            int64_t dispatch_start = esp_timer_get_time();
            uint8_t cat = evt.type & 0xF0;

            xSemaphoreTake(s_subs_lock, portMAX_DELAY);
            for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
                if (s_subs[i].active && (s_subs[i].category_mask & cat)) {
                    s_subs[i].handler(&evt, s_subs[i].ctx);
                }
            }
            xSemaphoreGive(s_subs_lock);

            int64_t latency = esp_timer_get_time() - evt.timestamp_us;
            perf_log_event("evt_dispatch", (uint32_t)(latency / 1000));
        }
    }
}

/* ── Public API ── */

void event_bus_init(void)
{
    s_evt_queue = xQueueCreate(EVT_QUEUE_LEN, sizeof(event_t));
    s_subs_lock = xSemaphoreCreateMutex();
    memset(s_subs, 0, sizeof(s_subs));

    xTaskCreatePinnedToCore(event_dispatcher_task, "evt_disp",
                            4096, NULL, 5, NULL, 1);
    ESP_LOGI(TAG, "Event bus initialised (queue=%d, max_subs=%d)",
             EVT_QUEUE_LEN, MAX_SUBSCRIBERS);
}

static event_t make_event(event_type_t type)
{
    event_t evt = {0};
    evt.type = type;
    evt.timestamp_us = esp_timer_get_time();
    return evt;
}

bool event_post(event_type_t type, int32_t ival)
{
    event_t evt = make_event(type);
    evt.data.ival = ival;
    return xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool event_post_ptr(event_type_t type, void *ptr)
{
    event_t evt = make_event(type);
    evt.data.ptr = ptr;
    return xQueueSend(s_evt_queue, &evt, pdMS_TO_TICKS(10)) == pdTRUE;
}

bool event_post_from_isr(event_type_t type, int32_t ival)
{
    event_t evt = make_event(type);
    evt.data.ival = ival;
    BaseType_t woken = pdFALSE;
    BaseType_t ret = xQueueSendFromISR(s_evt_queue, &evt, &woken);
    if (woken) portYIELD_FROM_ISR();
    return ret == pdTRUE;
}

int event_subscribe(uint8_t category_mask, event_handler_t handler, void *ctx)
{
    xSemaphoreTake(s_subs_lock, portMAX_DELAY);
    for (int i = 0; i < MAX_SUBSCRIBERS; i++) {
        if (!s_subs[i].active) {
            s_subs[i].category_mask = category_mask;
            s_subs[i].handler = handler;
            s_subs[i].ctx = ctx;
            s_subs[i].active = true;
            xSemaphoreGive(s_subs_lock);
            ESP_LOGI(TAG, "Subscriber %d registered (mask=0x%02X)", i, category_mask);
            return i;
        }
    }
    xSemaphoreGive(s_subs_lock);
    ESP_LOGE(TAG, "No subscriber slots available");
    return -1;
}

void event_unsubscribe(int sub_id)
{
    if (sub_id < 0 || sub_id >= MAX_SUBSCRIBERS) return;
    xSemaphoreTake(s_subs_lock, portMAX_DELAY);
    s_subs[sub_id].active = false;
    xSemaphoreGive(s_subs_lock);
}
