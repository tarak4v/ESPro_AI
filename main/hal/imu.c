/**
 * @file imu.c
 * @brief IMU HAL — QMI8658 init, read, gesture detection, step counting.
 */

#include "hal/imu.h"
#include "hw_config.h"
#include "core/event_bus.h"
#include "core/perf_monitor.h"

#include "i2c_bsp.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char *TAG = "imu";

static i2c_master_dev_handle_t s_imu_dev = NULL;

/* QMI8658 registers */
#define REG_WHO_AM_I    0x00
#define REG_CTRL1       0x02
#define REG_CTRL2       0x03
#define REG_CTRL3       0x04
#define REG_CTRL7       0x08
#define REG_AX_L        0x35

/* Step detection state */
static uint32_t s_step_count = 0;
static float    s_prev_mag   = 1.0f;
static bool     s_peak_state = false;

/* Gesture detection */
#define DOUBLETAP_THRESHOLD  2.0f
#define DOUBLETAP_WINDOW_MS  400
#define TILT_ANGLE_DEG       30.0f
#define TILT_HOLD_MS         500

void imu_init(void)
{
    extern i2c_master_dev_handle_t imu_handle;
    s_imu_dev = imu_handle;

    /* Read WHO_AM_I */
    uint8_t id = 0;
    i2c_read_buff(s_imu_dev, REG_WHO_AM_I, &id, 1);
    ESP_LOGI(TAG, "QMI8658 WHO_AM_I: 0x%02X", id);

    /* Configure: accel ±8g, gyro ±512dps, ODR 250Hz */
    uint8_t ctrl2 = 0x65;  /* accel ±8g, 250Hz */
    uint8_t ctrl3 = 0x54;  /* gyro ±512dps, 250Hz */
    uint8_t ctrl7 = 0x03;  /* enable accel + gyro */
    i2c_writr_buff(s_imu_dev, REG_CTRL2, &ctrl2, 1);
    i2c_writr_buff(s_imu_dev, REG_CTRL3, &ctrl3, 1);
    i2c_writr_buff(s_imu_dev, REG_CTRL7, &ctrl7, 1);

    ESP_LOGI(TAG, "IMU init: ±%dg, ±%ddps, %dHz",
             IMU_ACCEL_RANGE, IMU_GYRO_RANGE, IMU_ODR);
}

bool imu_read(imu_data_t *data)
{
    uint8_t buf[12];
    if (i2c_read_buff(s_imu_dev, REG_AX_L, buf, 12) != ESP_OK) return false;

    int16_t raw_ax = (buf[1] << 8) | buf[0];
    int16_t raw_ay = (buf[3] << 8) | buf[2];
    int16_t raw_az = (buf[5] << 8) | buf[4];
    int16_t raw_gx = (buf[7] << 8) | buf[6];
    int16_t raw_gy = (buf[9] << 8) | buf[8];
    int16_t raw_gz = (buf[11] << 8) | buf[10];

    data->ax = raw_ax / IMU_ACCEL_LSB_G;
    data->ay = raw_ay / IMU_ACCEL_LSB_G;
    data->az = raw_az / IMU_ACCEL_LSB_G;
    data->gx = raw_gx / 64.0f;  /* ±512 dps = 64 LSB/dps */
    data->gy = raw_gy / 64.0f;
    data->gz = raw_gz / 64.0f;

    return true;
}

/* ── Step detection via peak counting ── */
static void detect_step(const imu_data_t *d)
{
    float mag = sqrtf(d->ax * d->ax + d->ay * d->ay + d->az * d->az);
    bool peak = (mag > 1.2f && s_prev_mag <= 1.2f);

    if (peak && !s_peak_state) {
        s_step_count++;
        s_peak_state = true;
        if (s_step_count % 50 == 0) {
            event_post(EVT_STEP_COUNT, (int32_t)s_step_count);
        }
    }
    if (mag < 1.0f) s_peak_state = false;
    s_prev_mag = mag;
}

/* ── Gesture detection ── */
static void detect_gestures(const imu_data_t *d)
{
    static int64_t last_tap_time = 0;
    static int     tap_count     = 0;
    static int64_t tilt_start    = 0;

    float mag = sqrtf(d->ax * d->ax + d->ay * d->ay + d->az * d->az);
    int64_t now = esp_timer_get_time() / 1000;

    /* Double-tap: spike > 2g within 400ms window */
    if (mag > DOUBLETAP_THRESHOLD) {
        if (now - last_tap_time < DOUBLETAP_WINDOW_MS) {
            tap_count++;
            if (tap_count >= 2) {
                event_post(EVT_IMU_GESTURE, 1);  /* 1 = double-tap */
                tap_count = 0;
            }
        } else {
            tap_count = 1;
        }
        last_tap_time = now;
    }

    /* Forward tilt: >30° for 500ms */
    float pitch = atan2f(d->ax, sqrtf(d->ay * d->ay + d->az * d->az)) * 57.3f;
    if (pitch > TILT_ANGLE_DEG) {
        if (tilt_start == 0) tilt_start = now;
        else if (now - tilt_start > TILT_HOLD_MS) {
            event_post(EVT_IMU_GESTURE, 2);  /* 2 = tilt-acknowledge */
            tilt_start = 0;
        }
    } else {
        tilt_start = 0;
    }

    /* Orientation flip: Z < -0.5g = upside-down */
    static int64_t flip_start = 0;
    if (d->az < -0.5f) {
        if (flip_start == 0) flip_start = now;
        else if (now - flip_start > 1000) {
            event_post(EVT_IMU_ORIENTATION, 1);  /* 1 = flipped */
            flip_start = 0;
        }
    } else {
        flip_start = 0;
    }
}

void sensor_fusion_task(void *arg)
{
    imu_data_t data;
    while (1) {
        int64_t t0 = esp_timer_get_time();
        if (imu_read(&data)) {
            detect_step(&data);
            detect_gestures(&data);
        }
        perf_log_event(PERF_SLOT_IMU_READ,
                       (uint32_t)((esp_timer_get_time() - t0) / 1000));
        vTaskDelay(pdMS_TO_TICKS(20));  /* 50 Hz */
    }
}
