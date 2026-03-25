/**
 * @file imu.h
 * @brief IMU HAL — QMI8658 6-axis accelerometer + gyroscope.
 */
#ifndef HAL_IMU_H
#define HAL_IMU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct
{
    float ax, ay, az; /* Acceleration in g */
    float gx, gy, gz; /* Angular velocity in dps */
} imu_data_t;

/** Health metrics snapshot. */
typedef struct
{
    uint32_t steps;
    uint32_t distance_m; /* Estimated distance in meters */
    uint32_t calories;   /* Estimated kcal burned */
    uint32_t active_min; /* Minutes with movement */
} health_data_t;

void imu_init(void);
bool imu_read(imu_data_t *data);

/** Get current step count. */
uint32_t imu_get_steps(void);

/** Get full health metrics snapshot. */
health_data_t imu_get_health(void);

/** Sensor fusion task — reads IMU, detects gestures, counts steps. */
void sensor_fusion_task(void *arg);

#endif /* HAL_IMU_H */
