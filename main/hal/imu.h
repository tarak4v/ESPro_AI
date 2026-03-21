/**
 * @file imu.h
 * @brief IMU HAL — QMI8658 6-axis accelerometer + gyroscope.
 */
#ifndef HAL_IMU_H
#define HAL_IMU_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    float ax, ay, az;   /* Acceleration in g */
    float gx, gy, gz;   /* Angular velocity in dps */
} imu_data_t;

void imu_init(void);
bool imu_read(imu_data_t *data);

/** Sensor fusion task — reads IMU, detects gestures, counts steps. */
void sensor_fusion_task(void *arg);

#endif /* HAL_IMU_H */
