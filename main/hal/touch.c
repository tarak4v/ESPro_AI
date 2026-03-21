/**
 * @file touch.c
 * @brief Touch HAL — raw I2C touch read (matching working SS_Smallstart_VS),
 *        detects gestures, posts events.
 */

#include "hal/touch.h"
#include "hw_config.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"

#include "i2c_bsp.h"
#include "driver/i2c_master.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>

static const char *TAG = "touch";

/* Touch state */
static int16_t s_last_x = -1;
static int16_t s_last_y = -1;
static bool s_pressed = false;

/* Swipe detection */
#define SWIPE_MIN_PX 40

void touch_init(void)
{
    /* disp_touch_dev_handle is already created by i2c_bsp.c on bus 1 */
    ESP_LOGI(TAG, "Touch controller init at 0x%02X", TOUCH_ADDR);
}

static bool read_touch_point(int16_t *x, int16_t *y)
{
    /* Exact same protocol as the working project's lvgl_touch_cb */
    uint8_t cmd[11] = {0xB5, 0xAB, 0xA5, 0x5A, 0, 0, 0, 0x0E, 0, 0, 0};
    uint8_t buf[32];
    memset(buf, 0, sizeof(buf));

    /* Use separate transmit + receive instead of combined transmit_receive,
       as some I2C slave implementations don't support repeated start */
    esp_err_t ret = i2c_master_transmit(disp_touch_dev_handle, cmd, 11, 1000);
    if (ret != ESP_OK)
        return false;
    ret = i2c_master_receive(disp_touch_dev_handle, buf, 32, 1000);
    if (ret != ESP_OK)
        return false;

    uint16_t px = (((uint16_t)buf[2] & 0x0F) << 8) | (uint16_t)buf[3];
    uint16_t py = (((uint16_t)buf[4] & 0x0F) << 8) | (uint16_t)buf[5];

    if (buf[1] == 0 || buf[1] >= 5)
        return false;

    /* Coordinate mapping (DISP_ROT_90 path from working project) */
    if (px > LCD_H_RES)
        px = LCD_H_RES;
    if (py > LCD_V_RES)
        py = LCD_V_RES;
    *x = LCD_H_RES - (int16_t)px;
    *y = LCD_V_RES - (int16_t)py;

    return true;
}

void touch_input_task(void *arg)
{
    /* Touch is now read directly from the LVGL indev callback in ui_manager.c
       (matching the working SS_Smallstart_VS project). This task is kept alive
       but does nothing — it exists only to satisfy the os_kernel task creation. */
    ESP_LOGI(TAG, "Touch handled via LVGL indev callback, task idle");
    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(portMAX_DELAY));
    }
}

bool touch_get_last(int16_t *x, int16_t *y, bool *p)
{
    if (x)
        *x = s_last_x;
    if (y)
        *y = s_last_y;
    if (p)
        *p = s_pressed;
    return s_pressed;
}
