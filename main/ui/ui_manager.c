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
#include "hal/display.h"
#include "services/power_manager.h"
#include "hw_config.h"
#include "i2c_bsp.h"
#include "driver/i2c_master.h"

#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "ui_mgr";

#define LVGL_TICK_PERIOD_MS 2

static screen_id_t s_current = SCR_HOME;
static screen_id_t s_pending = SCR_HOME;
static bool s_switch_pending = false;

/* Screen function tables */
typedef struct
{
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
extern void scr_meeting_create(void);
extern void scr_meeting_update(void);
extern void scr_meeting_destroy(void);

static const screen_ops_t s_screens[SCR_COUNT] = {
    [SCR_HOME] = {scr_home_create, scr_home_update, scr_home_destroy},
    [SCR_APPS] = {scr_apps_create, scr_apps_update, scr_apps_destroy},
    [SCR_SETTINGS] = {scr_settings_create, scr_settings_update, scr_settings_destroy},
    [SCR_MEETING] = {scr_meeting_create, scr_meeting_update, scr_meeting_destroy},
};

/* ── Swipe detection state ── */
#define SWIPE_MIN_PX 40
static bool s_touch_tracking = false;
static int16_t s_touch_start_x = 0;
static int16_t s_touch_last_x = 0;

/* ── LVGL input device callback — reads touch directly via I2C
      (matching working SS_Smallstart_VS project) ── */
static uint32_t s_indev_call_count = 0;

static void indev_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 0x0E, 0, 0, 0};
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));

    /* Use separate transmit + receive (full STOP between write/read)
       instead of combined transmit_receive (repeated start).
       The AXS15231B touch needs the STOP to process the command. */
    esp_err_t ret = i2c_master_transmit(disp_touch_dev_handle, cmd, 11, 100);
    if (ret == ESP_OK)
    {
        ret = i2c_master_receive(disp_touch_dev_handle, buf, 32, 100);
    }

    /* Debug: log first few calls and any touch events */
    s_indev_call_count++;
    if (s_indev_call_count <= 5 || (s_indev_call_count % 500 == 0))
    {
        ESP_LOGI(TAG, "indev_cb #%lu: ret=%d buf[0..5]=%02X %02X %02X %02X %02X %02X",
                 (unsigned long)s_indev_call_count, ret,
                 buf[0], buf[1], buf[2], buf[3], buf[4], buf[5]);
    }

    if (ret != ESP_OK)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t px = (((uint16_t)buf[2] & 0x0F) << 8) | (uint16_t)buf[3];
    uint16_t py = (((uint16_t)buf[4] & 0x0F) << 8) | (uint16_t)buf[5];

    if (buf[1] > 0 && buf[1] < 5)
    {
        if (px > LCD_H_RES)
            px = LCD_H_RES;
        if (py > LCD_V_RES)
            py = LCD_V_RES;
        int16_t sx = LCD_H_RES - (int16_t)px;
        int16_t sy = LCD_V_RES - (int16_t)py;

        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = sx;
        data->point.y = sy;

        if (!s_touch_tracking)
        {
            s_touch_tracking = true;
            s_touch_start_x = sx;
        }
        s_touch_last_x = sx;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;

        if (s_touch_tracking)
        {
            s_touch_tracking = false;
            int16_t dx = s_touch_last_x - s_touch_start_x;
            if (dx < -SWIPE_MIN_PX)
                event_post(EVT_TOUCH_SWIPE_LEFT, -dx);
            else if (dx > SWIPE_MIN_PX)
                event_post(EVT_TOUCH_SWIPE_RIGHT, dx);
        }
    }
}

/* ── LVGL tick callback ── */
static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_PERIOD_MS);
}

/* ── Init ── */
void ui_manager_init(void)
{
    /* lv_init() already called in app_main before display_init */

    /* Start periodic LVGL tick timer */
    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .name = "lvgl_tick",
    };
    esp_timer_handle_t tick_timer;
    ESP_ERROR_CHECK(esp_timer_create(&tick_args, &tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(tick_timer,
                                             LVGL_TICK_PERIOD_MS * 1000));

    /* Register touch input device */
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = indev_read_cb;
    lv_indev_drv_register(&indev_drv);

    ESP_LOGI(TAG, "LVGL initialised, input device registered");
}

/* ── LVGL render task ── */
void lvgl_render_task(void *arg)
{
    ESP_LOGI(TAG, "LVGL render task started on core %d", xPortGetCoreID());

    while (1)
    {
        int64_t t0 = esp_timer_get_time();

        if (lvgl_lock(50))
        {
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
    switch (evt->type)
    {
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
    if (lvgl_lock(100))
    {
        s_screens[s_current].create();
        lvgl_unlock();
    }

    while (1)
    {
        /* Handle pending screen switch */
        if (s_switch_pending)
        {
            s_switch_pending = false;
            if (lvgl_lock(100))
            {
                /* Create new screen first (calls lv_disp_load_scr),
                   THEN destroy old one — avoids dangling act_scr pointer */
                screen_id_t old = s_current;
                s_current = s_pending;
                s_screens[s_current].create();
                s_screens[old].destroy();
                lvgl_unlock();
                event_post(EVT_SCREEN_CHANGE, (int32_t)s_current);
                ESP_LOGI(TAG, "Switched to screen %d", s_current);
            }
        }

        /* Periodic screen update */
        if (lvgl_lock(50))
        {
            s_screens[s_current].update();
            lvgl_unlock();
        }

        /* Power management — auto-dim/sleep */
        power_manager_tick();

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

void ui_switch_screen(screen_id_t id)
{
    if (id >= SCR_COUNT)
        return;
    s_pending = id;
    s_switch_pending = true;
}

screen_id_t ui_get_current_screen(void)
{
    return s_current;
}
