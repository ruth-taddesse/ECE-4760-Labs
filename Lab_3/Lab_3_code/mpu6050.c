/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 *
 */

#include "hardware/i2c.h"
#include "mpu6050.h"
#include <stdio.h>

// Global tracking variable for the active scaling divisor
// Defaults to 131.0f (+/- 250 deg/s range)
static float current_gyro_scale_factor = 131.0f;

void mpu6050_reset() {
    // Two byte reset. First byte register, second byte data
    // There are a load more options to set up the device in
    // different ways that could be added here
    uint8_t buf[] = {0x6B, 0x00};
    i2c_write_blocking(I2C_CHAN, ADDRESS, buf, 2, false);

    // Set gyro sample rate (set to 1KHz, same as accel)
    uint8_t gyro_rate[] = {0x19, 0b00000111} ;
    i2c_write_blocking(I2C_CHAN, ADDRESS, gyro_rate, 2, false);

    // Configure the Gyro range (+/- 250 deg/s)
    uint8_t gyro_settings[] = {0x1b, 0b00000000} ;
    i2c_write_blocking(I2C_CHAN, ADDRESS, gyro_settings, 2, false);

    // Configure the Accel range (+/- 2g's)
    uint8_t accel_settings[] = {0x1c, 0b00000000} ;
    i2c_write_blocking(I2C_CHAN, ADDRESS, accel_settings, 2, false);

    // Configure interrupt pin
    uint8_t pin_settings[] = {0x37, 0b00010000} ;
    i2c_write_blocking(I2C_CHAN, ADDRESS, pin_settings, 2, false);

    // Configure data ready interrupt
    uint8_t int_config[] = {0x38, 0x01} ;
    i2c_write_blocking(I2C_CHAN, ADDRESS, int_config, 2, false);
}

void mpu6050_read_raw(fix15 accel[3], fix15 gyro[3]) {
    // For this particular device, we send the device the register we want to read
    // first, then subsequently read from the device. The register is auto incrementing
    // so we don't need to keep sending the register we want, just the first.

    uint8_t buffer[6];
    int16_t temp_accel, temp_gyro ;

    // Status value
    uint32_t success;

    // Start reading acceleration registers from register 0x3B for 6 bytes
    uint8_t val = 0x3B;
    i2c_write_timeout_us(I2C_CHAN, ADDRESS, &val, 1, true, 250); // true to keep master control of bus
    i2c_read_timeout_us(I2C_CHAN, ADDRESS, buffer, 6, false, 250);

    for (int i = 0; i < 3; i++) {
        temp_accel = (buffer[i<<1] << 8 | buffer[(i<<1) + 1]);
        accel[i] = temp_accel ;
        accel[i] <<= 2 ; // convert to g's (fixed point)
    }

    // Now gyro data from reg 0x43 for 6 bytes
    // The register is auto incrementing on each read
    val = 0x43;
    i2c_write_timeout_us(I2C_CHAN, ADDRESS, &val, 1, true, 250);
    i2c_read_timeout_us(I2C_CHAN, ADDRESS, buffer, 6, false, 250);  // False - finished with bus

    for (int i = 0; i < 3; i++) {
        temp_gyro = (buffer[i<<1] << 8 | buffer[(i<<1) + 1]);
        // include set gyro rate
        gyro[i] = float2fix15(
            (float)temp_gyro / current_gyro_scale_factor);
    }
}

bool mpu6050_set_gyro_range(int range_deg_s) {
    uint8_t config_byte = 0x00;
    float scale_factor = 131.0f;

    // Map the human-readable integer to the correct register value and scale factor
    switch (range_deg_s) {
        case 250:
            config_byte = 0x00; // Binary 00000000 (FS_SEL = 0)
            scale_factor = 131.0f;
            break;
        case 500:
            config_byte = 0x08; // Binary 00001000 (FS_SEL = 1)
            scale_factor = 65.5f;
            break;
        case 1000:
            config_byte = 0x10; // Binary 00010000 (FS_SEL = 2)
            scale_factor = 32.8f;
            break;
        case 2000:
            config_byte = 0x18; // Binary 00011000 (FS_SEL = 3)
            scale_factor = 16.4f;
            break;
        default:
            // Return false if an invalid range integer is supplied
            printf("Error: Invalid gyro range %d. Use 250, 500, 1000, or 2000.\n", range_deg_s);
            return false;
    }

    // Write the dynamic byte to the configuration register (0x1B)
    uint8_t buf[] = {0x1B, config_byte};
    int result = i2c_write_blocking(I2C_CHAN, ADDRESS, buf, 2, false);

    // If writing over I2C succeeded, update the active global scaling divisor
    if (result == 2) { // confirms both bytes were written
        current_gyro_scale_factor = scale_factor;
        return true;
    }

    return false;
}

/////////////////////////////////////////////////////////////////