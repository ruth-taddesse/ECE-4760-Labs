#ifndef SPORTS_IMU_H
#define SPORTS_IMU_H
#include <stdbool.h>
#include <stdint.h>
#define IMU_HZ 100u
#define GRAVITY 9.80665f
// Maximum per-axis stationary offset from [0, 0, gravity], in m/s^2.
#define IMU_CAL_ACCEL_OFFSET_MAX 2.0f
typedef struct { float a[3], g[3]; uint8_t near_rail; } ImuReading;
typedef struct { float a[3], g[3]; } ImuBias;
bool imu_init(void);
bool imu_read(ImuReading *reading);
// Keep level, stationary, +Z up. Single-pose offset estimate, not scale calibration.
bool imu_calibrate(ImuBias *bias);
const char *imu_calibration_report(void);
#endif
