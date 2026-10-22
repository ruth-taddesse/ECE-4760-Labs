/**
 *
 * ## wiring
 *
 * - GP4 (physical pin 6): PWM signal to input of course optoisolated motor PCB
 * - connect the Pico USB cable and open its USB serial port
 * - use any terminal at 115200 baud, 8N1, with Enter sending CR or CRLF
 * - GP4 (physical pin 6) is the PWM output to the motor circuit
 *
 * ## notes
 * 
 * - PWM duty cycle: enter integer from 0 to 6000 and press Enter
 *    - ex: `0` = 0%, `600` = 10%, `1500` = 25%, `3000` = 50%, `6000` = 100%
 *    - enter zero to stop
 *    - serial disconnects: last duty persists
 * - PWM ~1kHz (for default 150MHz clock)
 *    - 6000 ticks, divider 25
 * - starts at zero duty
 *    - PWM_INVERTED = false --> GP4 stays low
 *    - if circuit uses high for off: set PWM_INVERTED initially to true, zero now drives GP4 high
 *
 * ## changes from demo
 * 
 * - output initialized to zero
 * - clock divider derived from clk_sys
 * - 6000 ticks: TOP = 5999
 * - USB serial reader accepts duty-cycle values from 0 to 6000
 */

#include <stdio.h>
#include <errno.h>
#include <ctype.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/stdio_usb.h"

#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/irq.h"

#include "pt_cornell_rp2040_v1_4.h"

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

// PWM interrupt service routine
void on_pwm_wrap() {
    // Clear the interrupt flag that brought us here
    pwm_clear_irq(pwm_gpio_to_slice_num(PWM_OUT));
    // Update duty cycle
    if (control!=old_control) {
        old_control = control ;
        pwm_set_chan_level(slice_num, PWM_CHAN_A, control);
    }
}

// USB serial control thread
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt) ;
    static char input[16];
    static int input_length;
    static int ch;
    static int test_in;

    while (!stdio_usb_connected()) {
        PT_YIELD_usec(10000);
    }
    // Allow the terminal to settle after the USB connection opens.
    PT_YIELD_usec(500000);

    printf("Helicopter week 1: GP4, ~1 kHz, startup duty 0.\r\n");
    printf("input a duty cycle (0-6000): ");
    stdio_flush();
    while(1) {
        ch = getchar_timeout_us(0);
        if (ch == PICO_ERROR_TIMEOUT) {
            PT_YIELD_usec(1000);
            continue;
        }

        // serial monitor: use CR / CRLF as line ending - CR submits the line, LF ignored
        if (ch == '\n') continue;

        if (ch == '\r') {
            input[input_length] = '\0';
            if (sscanf(input, "%d", &test_in) == 1 &&
                test_in >= 0 && test_in <= PWM_COUNTS) {
                control = test_in;
                pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PWM_OUT),
                                   (uint16_t)control);
                printf("\r\nDuty cycle set to %d\r\n", control);
            } else {
                printf("\r\nEnter an integer from 0 to 6000\r\n");
            }
            input_length = 0;
            printf("input a duty cycle (0-6000): ");
            stdio_flush();
        } else if (ch == '\b' || ch == 127) {
            if (input_length > 0) {
                input_length--;
                printf("\b \b");
            }
        } else if (ch >= '0' && ch <= '9' &&
                   input_length < (int)sizeof(input) - 1) {
            input[input_length++] = (char)ch;
            putchar(ch);
        }
    }
    PT_END(pt) ;
}

int main() {

    // Initialize stdio
    stdio_init_all();

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////// PWM CONFIGURATION ////////////////////////////
    ////////////////////////////////////////////////////////////////////////
    // Tell GPIO PWM_OUT that it is allocated to the PWM
    gpio_set_function(PWM_OUT, GPIO_FUNC_PWM);

    // Find out which PWM slice is connected to GPIO PWM_OUT (it's slice 2)
    slice_num = pwm_gpio_to_slice_num(PWM_OUT);

    // Mask our slice's IRQ output into the PWM block's single interrupt line,
    // and register our interrupt handler
    pwm_clear_irq(slice_num);
    pwm_set_irq_enabled(slice_num, true);
    irq_set_exclusive_handler(PWM_IRQ_WRAP, on_pwm_wrap);
    irq_set_enabled(PWM_IRQ_WRAP, true);

    // This section configures the period of the PWM signals
    pwm_set_wrap(slice_num, WRAPVAL) ;
    // divider: 25 at 150 MHz
    // hardware quantizes divider to increments of 1/16
    pwm_set_clkdiv(slice_num, (float)clock_get_hz(clk_sys) /
                   (PWM_HZ * PWM_COUNTS));
    pwm_set_output_polarity(slice_num, PWM_INVERTED, PWM_INVERTED);

    // This sets duty cycle
    pwm_set_chan_level(slice_num, pwm_gpio_to_channel(PWM_OUT), 0);

    // Start the channel
    pwm_set_mask_enabled((1u << slice_num));

    ////////////////////////////////////////////////////////////////////////
    ///////////////////////////// ROCK AND ROLL ////////////////////////////
    ////////////////////////////////////////////////////////////////////////
    pt_add_thread(protothread_serial);
    pt_schedule_start ;

}
