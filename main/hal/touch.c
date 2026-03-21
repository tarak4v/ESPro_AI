/**
 * @file touch.c
 * @brief Touch HAL — reads I2C touch, detects gestures, posts events.
 */

#include "hal/touch.h"
#include "hw_config.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"

#include "i2c_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "touch";

/* Touch state */
static int16_t s_last_x = -1;
static int16_t s_last_y = -1;
static bool    s_pressed = false;

/* Swipe detection */
#define SWIPE_MIN_PX     40
#define TOUCH_CMD        0xB5
#define TOUCH_READ_LEN   6

static i2c_master_dev_handle_t s_touch_dev = NULL;

void touch_init(void)
{
    extern i2c_master_bus_handle_t i2c_bus_1_handle;
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address  = TOUCH_ADDR,
        .scl_speed_hz    = I2C1_FREQ_HZ,
    };
    i2c_master_bus_add_device(i2c_bus_1_handle, &dev_cfg, &s_touch_dev);
    ESP_LOGI(TAG, "Touch controller init at 0x%02X", TOUCH_ADDR);
}

static bool read_touch_point(int16_t *x, int16_t *y)
{
    uint8_t cmd[] = {TOUCH_CMD, 0x00, 0x00};
    uint8_t buf[TOUCH_READ_LEN] = {0};

    if (i2c_master_transmit_receive(s_touch_dev, cmd, sizeof(cmd),
                                     buf, TOUCH_READ_LEN, 50) != ESP_OK)
        return false;

    uint8_t touch_count = buf[0];
    if (touch_count == 0) return false;

    /* Raw coords are in portrait orientation — remap to landscape */
    int16_t raw_x = (buf[2] << 8) | buf[3];
    int16_t raw_y = (buf[4] << 8) | buf[5];

    /* 90° CCW rotation: landscape_x = raw_y, landscape_y = NOROT_HRES - raw_x */
    *x = raw_y;
    *y = LCD_NOROT_HRES - 1 - raw_x;

    return true;
}

void touch_input_task(void *arg)
{
    int16_t down_x = 0, down_y = 0;
    bool was_pressed = false;

    while (1) {
        int64_t t0 = esp_timer_get_time();
        int16_t x, y;
        bool pressed = read_touch_point(&x, &y);

        if (pressed) {
            s_last_x = x;
            s_last_y = y;
            s_pressed = true;
            if (!was_pressed) {
                down_x = x;
                down_y = y;
            }
        } else if (was_pressed) {
            /* Touch released — check for gesture */
            s_pressed = false;
            int16_t dx = s_last_x - down_x;
            int16_t dy = s_last_y - down_y;

            if (dx > SWIPE_MIN_PX)       event_post(EVT_TOUCH_SWIPE_RIGHT, dx);
            else if (dx < -SWIPE_MIN_PX) event_post(EVT_TOUCH_SWIPE_LEFT, -dx);
            else if (dy > SWIPE_MIN_PX)  event_post(EVT_TOUCH_SWIPE_DOWN, dy);
            else if (dy < -SWIPE_MIN_PX) event_post(EVT_TOUCH_SWIPE_UP, -dy);
            else                          event_post(EVT_TOUCH_TAP, (down_x << 16) | down_y);
        }

        was_pressed = pressed;
        perf_log_event(PERF_SLOT_TOUCH_READ,
                       (uint32_t)((esp_timer_get_time() - t0) / 1000));
        vTaskDelay(pdMS_TO_TICKS(20));  /* 50 Hz touch polling */
    }
}

bool touch_get_last(int16_t *x, int16_t *y, bool *p)
{
    if (x) *x = s_last_x;
    if (y) *y = s_last_y;
    if (p) *p = s_pressed;
    return s_pressed;
}
