/**
 * V. Hunter Adams (vha3@cornell.edu)
 * 
 * This demonstration utilizes the MPU6050.
 * It gathers raw accelerometer/gyro measurements, scales
 * them, and plots them to the VGA display. The top plot
 * shows gyro measurements, bottom plot shows accelerometer
 * measurements.
 * 
 * HARDWARE CONNECTIONS
 *  - GPIO 16 ---> VGA Hsync
 *  - GPIO 17 ---> VGA Vsync
 *  - GPIO 18 ---> 470 ohm resistor ---> VGA Green
 *  - GPIO 19 ---> 330 ohm resistor ---> VGA Green
 *  - GPIO 20 ---> 330 ohm resistor ---> VGA Blue
 *  - GPIO 21 ---> 330 ohm resistor ---> VGA Red
 *  - RP2040 GND ---> VGA GND
 *  - GPIO 8 ---> MPU6050 SDA
 *  - GPIO 9 ---> MPU6050 SCL
 *  - 3.3v ---> MPU6050 VCC
 *  - RP2040 GND ---> MPU6050 GND
 */


// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include PICO libraries
#include "pico/stdlib.h"
#include "pico/multicore.h"
// Include hardware libraries
#include "hardware/pwm.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/adc.h"
#include "hardware/pio.h"
#include "hardware/i2c.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"
// Include custom libraries
#include "VGA/vga16_graphics_v3.h"
#include "mpu6050.h"
#include "pt_cornell_rp2040_v1_4.h"

// We will start drawing at column 81
#define LEFT_EDGE 81 

// Rescale the measurements for display
#define OLD_RANGE 500.  // (+/- 250)
#define NEW_RANGE 150.  // (looks nice on VGA)
#define OLD_MIN -250. 
#define OLD_MAX 250.
#define ACCEL_SCALE 120.0

// Place the plots
#define TOP_PLOT_BOTTOM 230
#define BOTTOM_PLOT_BOTTOM 430
#define PLOT_WIDTH 530

#define PI 3.14159265358979323846

// === the fixed point macros ========================================
#define absfix15(a) abs(a) 
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)

// Arrays in which raw measurements will be stored
fix15 acceleration[3], gyro[3];

// Arrays for circular display buffer
fix15 ax[PLOT_WIDTH] ;
fix15 ay[PLOT_WIDTH] ;
fix15 az[PLOT_WIDTH] ;
fix15 gx[PLOT_WIDTH] ;
fix15 gy[PLOT_WIDTH] ;
fix15 gz[PLOT_WIDTH] ;

// Pointer to head of buffer
volatile int head = 0 ;

// character array
char screentext[40];

// draw speed
volatile int threshold = 10 ;

// Some macros for max/min/abs
#define min(a,b) ((a<b) ? a:b)
#define max(a,b) ((a<b) ? b:a)
#define abs(a) ((a>0) ? a:-a)

// semaphore
static struct pt_sem vga_semaphore ;

// Some paramters for PWM
#define CLKDIV  25.0

volatile fix15 filtered_ax;
volatile fix15 filtered_az;
fix15 accel_angle;
fix15 gyro_angle_delta;
volatile fix15 complementary_angle;

// 6000 counter ticks per period: level 6000 = 100%.
#define PWM_COUNTS 6000
#define WRAPVAL (PWM_COUNTS - 1)
#define PWM_HZ 1000
// w/inversion: level 0 produces constant HIGH output
#define PWM_INVERTED false

// GPIO we're using for PWM
#define PWM_OUT 4

// Variable to hold PWM slice number
uint slice_num ;

// PWM duty cycle
volatile int control ;
volatile int old_control ;
volatile float filtered_control = 0.0f;
volatile fix15 angle_history[PLOT_WIDTH]; // for VGA display
volatile float motor_history[PLOT_WIDTH]; // "

//PID Loop
volatile fix15 target_angle = int2fix15(90);
volatile float error;
volatile float prop;
volatile float integral;
volatile float deriv;
volatile float kp = 27.0f; //inital 27
volatile float ki = 0.0f;
volatile float kd = 0.0f;


void drawAxes() {
    // Draw the static aspects of the display
    setTextSize(1) ;
    setTextColor(WHITE);

    // Draw bottom plot
    drawHLine(75, 430, 5, CYAN) ;
    drawHLine(75, 355, 5, CYAN) ;
    drawHLine(75, 280, 5, CYAN) ;
    drawVLine(80, 280, 150, CYAN) ;
    sprintf(screentext, "3000") ;
    setCursor(50, 350) ;
    writeString(screentext) ;
    sprintf(screentext, "6000") ;
    setCursor(50, 280) ;
    writeString(screentext) ;
    sprintf(screentext, "0") ;
    setCursor(50, 425) ;
    writeString(screentext) ;

    // Draw top plot
    drawHLine(75, 230, 5, CYAN) ;
    drawHLine(75, 155, 5, CYAN) ;
    drawHLine(75, 80, 5, CYAN) ;
    drawVLine(80, 80, 150, CYAN) ;
    sprintf(screentext, "0") ;
    setCursor(50, 150) ;
    writeString(screentext) ;
    sprintf(screentext, "+90") ;
    setCursor(45, 75) ;
    writeString(screentext) ;
    sprintf(screentext, "-90") ;
    setCursor(45, 225) ;
    writeString(screentext) ;

    setCursor(81, 60);
    writeString("Measured angle (deg)");
    setCursor(81, 260);
    writeString("Filtered motor command (PWM counts)");
}

// convert value to screen coordinate (clipped to graph)
static int plot_y(float value, float low, float high, int bottom) {
    if (value < low) value = low;
    if (value > high) value = high;
    return bottom - (int)((value - low) * 150.0f / (high - low));
}

volatile int throttle = 10 ;

// Interrupt service routine
void on_pwm_wrap() {

    // Clear the interrupt flag that brought us here
    pwm_clear_irq(pwm_gpio_to_slice_num(PWM_OUT));
    
    gpio_put(14, !gpio_get(14)) ;

    // Read the IMU
    // NOTE! This is in 15.16 fixed point. Accel in g's, gyro in deg/s
    // If you want these values in floating point, call fix2float15() on
    // the raw measurements.
    mpu6050_read_raw(acceleration, gyro);

    //low pass accerleration

    filtered_ax = filtered_ax + ((acceleration[0] - filtered_ax)>>6);
    filtered_az = filtered_az + ((acceleration[2] - filtered_az)>>6);

    // NO SMALL ANGLE APPROXIMATION
    accel_angle = multfix15(float2fix15(atan2(-filtered_ax, filtered_az) + PI), oneeightyoverpi);

    accel_angle -= int2fix15(90);

    // Gyro angle delta (measurement times timestep) (15.16 fixed point)
    gyro_angle_delta = multfix15(gyro[1], zeropt001) ;

    // Complementary angle (degrees - 15.16 fixed point)
    //change to plus becasue of how imu is mounted
    complementary_angle = multfix15(complementary_angle + gyro_angle_delta, zeropt999) + multfix15(accel_angle, zeropt001);

    // smooth a copy of the motor command for display
    filtered_control += 0.01f * ((float)control - filtered_control);

    // PID LOOP - arbitrary bounds for now
    error = fix2float15(target_angle - complementary_angle);
    float angular_velocity = fix2float15(gyro[1]);

    prop = kp * error;
    integral += ki * error * 0.001f;

    if (integral > 500.0f) integral = 500.0f;
    if (integral < -500.0f) integral = -500.0f;
    deriv = -kd * angular_velocity;

    int pid_output = prop + integral + deriv;

    if (pid_output > 2500) {
        pid_output = 2500;
    }
    else if (pid_output < 0) {
        pid_output = 0;
    }

    control = pid_output;

    // Update duty cycle
    if (control!=old_control) {
        old_control = control ;
        pwm_set_chan_level(slice_num, PWM_CHAN_A, control);
    }

}

static PT_THREAD (protothread_draw(struct pt *pt))
{
    PT_BEGIN(pt) ;

    static int start ;
    static int i ;
    static int angle_hundredths;
    static int angle_magnitude;

    // Draw axes
    drawAxes() ;
    // Copy to other buffer
    copy_buffer_to_other() ;

    while(1) {
        // Wait for a buffer swap
        PT_YIELD_UNTIL(pt, draw_start_signal()) ;
        gpio_put(15, !gpio_get(15)) ;

        // assign current angle + motor command to VGA display buffers
        angle_history[head] = complementary_angle;
        motor_history[head] = filtered_control;

        head = ((head + 1) == PLOT_WIDTH) ? 0 : head + 1;
        
        // Clear the buffer, draw the axes
        clearLowFrame(0, BLACK) ;
        drawAxes() ;

        start = head;
        for (i = 0; i < PLOT_WIDTH; i++) {
            // top: measured angle (-90 to +90 degrees)
            drawPixel(
                LEFT_EDGE + i,
                plot_y(fix2float15(angle_history[start]),
                    -90.0f, 90.0f, TOP_PLOT_BOTTOM),
                YELLOW
            );

            // bottom: filtered motor command (0 to 6000)
            drawPixel(
                LEFT_EDGE + i,
                plot_y(motor_history[start],
                    0.0f, (float)PWM_COUNTS, BOTTOM_PLOT_BOTTOM),
                GREEN
            );

            start = ((start + 1) == PLOT_WIDTH) ? 0 : start + 1;
        }

        // Convert the 15.16 fixed-point angle to hundredths of a degree.
        angle_hundredths = (int)(((signed long long)complementary_angle * 100) >> 16);
        angle_magnitude = abs(angle_hundredths);
        snprintf(screentext, sizeof(screentext), "Angle: %s%d.%02d deg",
                 angle_hundredths < 0 ? "-" : "",
                 angle_magnitude / 100, angle_magnitude % 100);
        setTextColor(YELLOW);
        setCursor(250, 20);
        writeString(screentext);
    }

    PT_END(pt) ;
}

// User input thread. User can change PID constants
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt) ;
    static char classifier ;
    static float float_in ;
    
    while(1) {
        sprintf(pt_serial_out_buffer, "Enter command (p, i, d, or s for status): ");
        serial_write ;
        
        // Spawn a thread to do the non-blocking serial read
        serial_read ;
        
        // Read the first character command
        sscanf(pt_serial_in_buffer, "%c", &classifier) ;

        //prop
        if (classifier == 'p' || classifier == 'P') {
            sprintf(pt_serial_out_buffer, "New Kp: ");
            serial_write ;
            serial_read ;
            sscanf(pt_serial_in_buffer, "%f", &float_in) ;
            kp = float_in ;
            sprintf(pt_serial_out_buffer, "-> Kp updated to %f\n\r", kp);
            serial_write ;
        }
        //integral
        else if (classifier == 'i' || classifier == 'I') {
            sprintf(pt_serial_out_buffer, "New Ki: ");
            serial_write ;
            serial_read ;
            sscanf(pt_serial_in_buffer, "%f", &float_in) ;
            ki = float_in ;
            sprintf(pt_serial_out_buffer, "-> Ki updated to %f\n\r", ki);
            serial_write ;
        }
        //deriv
        else if (classifier == 'd' || classifier == 'D') {
            sprintf(pt_serial_out_buffer, "New Kd: ");
            serial_write ;
            serial_read ;
            sscanf(pt_serial_in_buffer, "%f", &float_in) ;
            kd = float_in ;
            sprintf(pt_serial_out_buffer, "-> Kd updated to %f\n\r", kd);
            serial_write ;
        }
        //print
        else if (classifier == 's' || classifier == 'S') {
            sprintf(pt_serial_out_buffer, "\n\rCurrent PID -> Kp: %f | Ki: %f | Kd: %f\n\r", kp, ki, kd);
            serial_write ;
        }
    }
    PT_END(pt) ;
}

// Entry point for core 1
void core1_entry() {
    gpio_init(15) ;
    gpio_set_dir(15, GPIO_OUT) ;
    gpio_put(15, 0) ;
    pt_add_thread(protothread_draw) ;
    pt_schedule_start ;
}

int main() {

    // Overclock
    set_sys_clock_khz(150000, true) ;

    // Initialize stdio
    stdio_init_all();

    // Initialize VGA
    initVGA() ;

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////// I2C CONFIGURATION ////////////////////////////
    i2c_init(I2C_CHAN, I2C_BAUD_RATE) ;
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C) ;
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C) ;

    // MPU6050 initialization
    mpu6050_reset();
    mpu6050_read_raw(acceleration, gyro);

    filtered_ax = acceleration[0];
    filtered_az = acceleration[2];  

    accel_angle = multfix15(
    float2fix15(atan2(-filtered_ax, filtered_az) + PI),
    oneeightyoverpi
    );

    accel_angle -= int2fix15(90);
    complementary_angle = accel_angle;

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////// PWM CONFIGURATION ////////////////////////////
    ////////////////////////////////////////////////////////////////////////
    // Tell GPIO's 4,5 that they allocated to the PWM
    gpio_set_function(5, GPIO_FUNC_PWM);
    gpio_set_function(4, GPIO_FUNC_PWM);

    // Find out which PWM slice is connected to GPIO 5 (it's slice 2, same for 4)
    slice_num = pwm_gpio_to_slice_num(5);

    // Mask our slice's IRQ output into the PWM block's single interrupt line,
    // and register our interrupt handler
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // This section configures the period of the PWM signals
    pwm_set_wrap(slice_num, WRAPVAL) ;
    pwm_set_clkdiv(slice_num, CLKDIV) ;

    // This sets duty cycle
    pwm_set_chan_level(slice_num, PWM_CHAN_B, 0);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, 0);

    // Start the channel
    pwm_set_mask_enabled((1u << slice_num));

   
    ////////////////////////////////////////////////////////////////////////
    ///////////////////////////// ROCK AND ROLL ////////////////////////////
    ////////////////////////////////////////////////////////////////////////
    // start core 1 
    multicore_reset_core1();
    multicore_launch_core1(core1_entry);

    // start core 0
    gpio_init(14) ;
    gpio_set_dir(14, GPIO_OUT) ;
    gpio_put(14, 0) ;
    pt_add_thread(protothread_serial) ;
    pt_schedule_start ;

}
