/**
 * @file ui_manager.c
 * @brief LVGL render loop + screen lifecycle management.
 */

#include "ui/ui_manager.h"
#include "ui/theme.h"
#include "core/os_kernel.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"
#include "hal/touch.h"
#include "hw_config.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ui_mgr";

static screen_id_t s_current = SCR_HOME;
static screen_id_t s_pending = SCR_HOME;
static bool s_switch_pending = false;

/* Screen function tables */
typedef struct {
    void (*create)(void);
    void (*update)(void);
    void (*destroy)(void);
} screen_ops_t;

extern void scr_home_create(void);
extern void scr_home_update(void);
extern void scr_home_destroy(void);
extern void scr_apps_create(void);
extern void scr_apps_update(void);
extern void scr_apps_destroy(void);
extern void scr_settings_create(void);
extern void scr_settings_update(void);
extern void scr_settings_destroy(void);

static const screen_ops_t s_screens[SCR_COUNT] = {
    [SCR_HOME]     = { scr_home_create,     scr_home_update,     scr_home_destroy },
    [SCR_APPS]     = { scr_apps_create,     scr_apps_update,     scr_apps_destroy },
    [SCR_SETTINGS] = { scr_settings_create, scr_settings_update, scr_settings_destroy },
};

/* ── LVGL input device callback ── */
static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    int16_t x, y;
    bool pressed;
    touch_get_last(&x, &y, &pressed);
    data->point.x = x;
    data->point.y = y;
    data->state = pressed ? LV_INDEV_DATA_PRESSED : LV_INDEV_DATA_RELEASED;
}

/* ── Init ── */
void ui_manager_init(void)
{
    lv_init();

    /* Register touch input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type    = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = indev_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "LVGL initialised, input device registered");
}

/* ── LVGL render task ── */
void lvgl_render_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL render task started on core %d", xPortGetCoreID());

    while (1) {
        int64_t t0 = esp_timer_get_time();

        if (lvgl_lock(50)) {
            lv_timer_handler();
            lvgl_unlock();
        }

        uint32_t frame_ms = (uint32_t)((esp_timer_get_time() - t0) / 1000);
        perf_log_event(PERF_SLOT_FRAME_TIME, frame_ms);

        /* Target ~200 FPS max, but LVGL will self-throttle */
        uint32_t delay = (frame_ms < 5) ? (5 - frame_ms) : 1;
        vTaskDelay(pdMS_TO_TICKS(delay));
    }
}

/* ── App manager task ── */

static void handle_navigation(const event_t *evt, void *ctx)
{
    switch (evt->type) {
        case EVT_TOUCH_SWIPE_LEFT:
            if (s_current < SCR_COUNT - 1)
                ui_switch_screen(s_current + 1);
            break;
        case EVT_TOUCH_SWIPE_RIGHT:
            if (s_current > 0)
                ui_switch_screen(s_current - 1);
            break;
        case EVT_BUTTON_PRESS:
            ui_switch_screen((s_current + 1) % SCR_COUNT);
            break;
        default:
            break;
    }
}

void app_manager_task(void *arg)
{
    ESP_LOGI(TAG, "App manager started on core %d", xPortGetCoreID());

    /* Subscribe to input events */
    event_subscribe(EVT_CAT_INPUT, handle_navigation, NULL);

    /* Create initial screen */
    if (lvgl_lock(100)) {
        s_screens[s_current].create();
        lvgl_unlock();
    }

    while (1) {
        /* Handle pending screen switch */
        if (s_switch_pending) {
            s_switch_pending = false;
            if (lvgl_lock(100)) {
                s_screens[s_current].destroy();
                s_current = s_pending;
                s_screens[s_current].create();
                lvgl_unlock();
                event_post(EVT_SCREEN_CHANGE, (int32_t)s_current);
                ESP_LOGI(TAG, "Switched to screen %d", s_current);
            }
        }

        /* Periodic screen update */
        if (lvgl_lock(50)) {
            s_screens[s_current].update();
            lvgl_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ui_switch_screen(screen_id_t id)
{
    if (id >= SCR_COUNT) return;
    s_pending = id;
    s_switch_pending = true;
}

screen_id_t ui_get_current_screen(void)
{
    return s_current;
}
