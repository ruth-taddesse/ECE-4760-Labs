/* Burst-read/error-checked collection API, based on mpu6050.c by Hunter Adams.
 * TDK MPU-6000/6050 Register Map rev 4.2. Legacy fixed-point API is unchanged.
 */
#include "imu.h"
#include "mpu6050.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include <math.h>
#include <stdio.h>
static char calibration_report[512];
const char *imu_calibration_report(void) { return calibration_report; }
static bool read_regs(uint8_t reg, uint8_t *bytes, unsigned n) {
    return i2c_write_timeout_us(I2C_CHAN, ADDRESS, &reg, 1, true, 1000) == 1 &&
           i2c_read_timeout_us(I2C_CHAN, ADDRESS, bytes, n, false, 1000) == (int)n;
}
static bool write_reg(uint8_t reg, uint8_t value) {
    uint8_t bytes[] = {reg, value};
    return i2c_write_timeout_us(I2C_CHAN, ADDRESS, bytes, 2, false, 1000) == 2;
}
bool imu_init(void) {
    i2c_init(I2C_CHAN, I2C_BAUD_RATE);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(SDA_PIN); gpio_pull_up(SCL_PIN);
    uint8_t who;
    if (!read_regs(0x75, &who, 1) || who != 0x68 || !write_reg(0x6b, 0x80)) return false;
    sleep_ms(100);
    // 1 kHz / 10 = 100 Hz, ~44/42 Hz accel/gyro low-pass bandwidth.
    if (!write_reg(0x6b, 1) || !write_reg(0x1a, 3) || !write_reg(0x19, 9) ||
        !mpu6050_set_gyro_range(2000) || !write_reg(0x1c, 0x10)) return false;
    sleep_ms(100);
    uint8_t config[4];
    return read_regs(0x19, config, 4) && config[0] == 9 && config[1] == 3 &&
           config[2] == 0x18 && config[3] == 0x10;
}
bool imu_read(ImuReading *r) {
    uint8_t bytes[14];
    if (!read_regs(0x3b, bytes, sizeof(bytes))) return false;
    r->near_rail = 0;
    for (int i = 0; i < 3; i++) {
        int16_t a = (int16_t)((bytes[2*i] << 8) | bytes[2*i+1]);
        int16_t g = (int16_t)((bytes[8+2*i] << 8) | bytes[9+2*i]);
        r->a[i] = a * (GRAVITY / 4096.0f);
        r->g[i] = g * (0.0174532925199433f / 16.4f);
        if (a > 32400 || a < -32400 || g > 32400 || g < -32400) r->near_rail = 1;
    }
    return true;
}
bool imu_calibrate(ImuBias *bias) {
    double sum[6] = {0}, square[6] = {0};
    // Allow vibration from pressing Enter/setting the controller down to settle.
    sleep_ms(1000);
    uint64_t next = time_us_64();
    for (int n = 0; n < 200; n++) {
        sleep_until(from_us_since_boot(next)); next += 10000;
        ImuReading r;
        if (!imu_read(&r)) {
            snprintf(calibration_report,sizeof(calibration_report),
                     "I2C read failed at sample %d/200; check wiring/power",n+1);
            return false;
        }
        if (r.near_rail) {
            snprintf(calibration_report,sizeof(calibration_report),
                     "Sensor near range limit at sample %d/200",n+1);
            return false;
        }
        for (int i = 0; i < 6; i++) {
            float v = i < 3 ? r.a[i] : r.g[i-3];
            sum[i] += v; square[i] += (double)v*v;
        }
    }
    ImuBias result;
    double means[6], deviations[6];
    const char *reason = NULL;
    for (int i = 0; i < 6; i++) {
        double mean = sum[i]/200, variance = square[i]/200 - mean*mean;
        means[i]=mean; deviations[i]=sqrt(fmax(0.0,variance));
        if (!reason && variance > (i < 3 ? 0.04 : 0.0004))
            reason = i < 3 ? "Acceleration varied: motion/vibration/noise" : "Gyro varied: motion/vibration/noise";
        if (i < 3) result.a[i] = (float)mean - (i == 2 ? GRAVITY : 0);
        else result.g[i-3] = (float)mean;
    }
    if (!reason && (fabsf(result.a[0]) > IMU_CAL_ACCEL_OFFSET_MAX ||
                    fabsf(result.a[1]) > IMU_CAL_ACCEL_OFFSET_MAX ||
                    fabsf(result.a[2]) > IMU_CAL_ACCEL_OFFSET_MAX))
        reason = "Acceleration outside +Z-up limits: tilt/orientation/offset";
    if (!reason && (fabsf(result.g[0]) > 0.15f || fabsf(result.g[1]) > 0.15f || fabsf(result.g[2]) > 0.15f))
        reason = "Mean gyro too large: rotation or sensor bias";
    snprintf(calibration_report,sizeof(calibration_report),
             "%s; mean accel XYZ=[%.3f %.3f %.3f] m/s2 (target [0 0 9.807], tolerance %.1f each); "
             "accel std=[%.3f %.3f %.3f] (max 0.200); "
             "mean gyro XYZ=[%.4f %.4f %.4f] rad/s (max abs 0.150); "
             "gyro std=[%.4f %.4f %.4f] (max 0.020)",
             reason ? reason : "Calibration passed",means[0],means[1],means[2],
             (double)IMU_CAL_ACCEL_OFFSET_MAX,
             deviations[0],deviations[1],deviations[2],means[3],means[4],means[5],
             deviations[3],deviations[4],deviations[5]);
    if (reason) return false;
    *bias = result;
    return true;
}
