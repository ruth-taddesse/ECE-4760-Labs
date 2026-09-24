# Appendix A: Annotated Source Snapshot

Snapshot date: September 20, 2026. Original application files are unchanged. Comments marked REPORT NOTE were inserted only in the final-source listing below. Other source text is preserved. These notes explain behavior, not implemented fixes.

| File | SHA-256 of original bytes |
|---|---|
| dactest.c | cc4bc1069280c53bd303e90ed557e279c2094c1c9d0648b568e8e1117b79701e |
| dactest_other_channel.c | db55fd5c39fc7d93591a67f3af02b89bf2e392f994765fd3cda78612e6356906 |
| ADC_with_DDS.c | 3fc01390cb1ba028b35a32cbb4d661edfecb50ade77d1f5c9b18ae416890af19 |
| ADC_DDS_keypad_mute.c | 669d9393b6a637c8b51e0ce2d6ae9fc89231432dbc44c26fcb4d360034178846 |
| ADC_DDS_keypad_record.c | 6ea0fd24342e753c004fb8e74a9a648411303fd9e9ab400118e07ef4037f6c55 |
| ADC_DDS_keypad_speedup.c | bb1a61ee89eeca9226ba5d9b1199d1cd2c6f6714c88119244d862d7483b6ac75 |
| CMakeLists.txt | abbacbd8c017fe768584f6b78d768bbd91732b4158c4b4a4c254a1e04fd141c6 |

## dactest.c

Week 1 baseline: a fixed 800 Hz phase increment feeds channel A. The timer ISR performs the table lookup and SPI write.

```c
/**
 * V. Hunter Adams
 * DDS of sine wave on MCP4822 DAC w/ ISR
 * 
 * Modified example code from Raspberry Pi
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
   GPIO 5 (pin 7) Chip select
   GPIO 6 (pin 9) SCK/spi0_sclk
   GPIO 7 (pin 10) MOSI/spi0_tx
   GPIO 2 (pin 4) GPIO output for timing ISR
   3.3v (pin 36) -> VCC on DAC 
   GND (pin 3)  -> GND on DAC 
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main = (800.0*two32)/Fs ;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

// Alarm ISR
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

	// DDS phase and sine table lookup
	phase_accum_main += phase_incr_main  ;
    DAC_data = (DAC_config_chan_A | ((sin_table[phase_accum_main>>24] + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

int main() {
    // Initialize stdio
    stdio_init_all();
    printf("Hello, DAC!\n");

    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Setup the ISR-timing GPIO
    gpio_init(ISR_GPIO) ;
    gpio_set_dir(ISR_GPIO, GPIO_OUT);
    gpio_put(ISR_GPIO, 0) ;

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

    // === build the sine lookup table =======
   	// scaled to produce values between 0 and 4096
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

    // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true) ;
    // Write the lower 32 bits of the target time to the alarm register, arming it.
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

    // Nothing happening here
    while(1){
    }
    return 0;
}
```


## dactest_other_channel.c

Week 1 channel experiment: the DAC control word selects B rather than A. The DDS frequency remains 800 Hz.

```c
/**
 * V. Hunter Adams
 * DDS of sine wave on MCP4822 DAC w/ ISR
 * 
 * Modified example code from Raspberry Pi
 * Copyright (c) 2020 Raspberry Pi (Trading) Ltd.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 *
   GPIO 5 (pin 7) Chip select
   GPIO 6 (pin 9) SCK/spi0_sclk
   GPIO 7 (pin 10) MOSI/spi0_tx
   GPIO 2 (pin 4) GPIO output for timing ISR
   3.3v (pin 36) -> VCC on DAC 
   GND (pin 3)  -> GND on DAC 
 */

#include <stdio.h>
#include <math.h>
#include "pico/stdlib.h"
#include "hardware/timer.h"
#include "hardware/irq.h"
#include "hardware/spi.h"

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main = (800.0*two32)/Fs ;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

// Alarm ISR
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

	// DDS phase and sine table lookup
	phase_accum_main += phase_incr_main  ;
    DAC_data = (DAC_config_chan_B | ((sin_table[phase_accum_main>>24] + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

int main() {
    // Initialize stdio
    stdio_init_all();
    printf("Hello, DAC!\n");

    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Setup the ISR-timing GPIO
    gpio_init(ISR_GPIO) ;
    gpio_set_dir(ISR_GPIO, GPIO_OUT);
    gpio_put(ISR_GPIO, 0) ;

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

    // === build the sine lookup table =======
   	// scaled to produce values between 0 and 4096
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

    // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true) ;
    // Write the lower 32 bits of the target time to the alarm register, arming it.
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

    // Nothing happening here
    while(1){
    }
    return 0;
}
```


## ADC_with_DDS.c

Week 1 integration: the ADC thread converts a potentiometer code to Hz, then to a phase increment. The timer ISR independently emits audio samples.

```c

/*
Simple ADC/Protothreads demo

Schedules a single thread, reads/prints ADC value

 */

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "hardware/spi.h"
#include <math.h>
#include "hardware/irq.h"

// ==========================================
// === protothreads globals
// ==========================================
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

#define LED_PIN 25
#define ADC_PIN 26
#define ADC_MUX 0

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

volatile static unsigned int adc_val ;

static int map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

// ==================================================
// === toggle25 thread 
// ==================================================
//  
static PT_THREAD (protothread_toggle25(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
        // toggle gpio 25
        gpio_put(LED_PIN, !gpio_get(LED_PIN));

        // Read the ADC
        adc_val = adc_read() ;

        phase_incr_main = (map(adc_val, 0, 4095, 0, 10000)*two32)/Fs;

        // Print the value
        printf("ADC value: %d\n", adc_val) ;

        // Yield
        PT_YIELD_usec(10000) ; //yeild every 10 ms
      } // END WHILE(1)
      // every thread ends with PT_END(pt);
      PT_END(pt);
} // end blink thread

// Alarm ISR
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ; 

    // DDS phase and sine table lookup
    phase_accum_main += phase_incr_main  ;
    DAC_data = (DAC_config_chan_A | ((sin_table[phase_accum_main>>24] + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

// ========================================
// === core 0 main
// ========================================
int main(){
  //===  start the serial i/o ==================
  stdio_init_all() ;
  // announce the threader version on system reset
  // if there is a seral terminal attached
  printf("\n\rProtothreads RP2040 v1.4\n\r");

  // Setup the ADC
  adc_init() ;
  adc_gpio_init(ADC_PIN) ;
  adc_select_input(ADC_MUX) ;

  // Initialize stdio
    stdio_init_all();
    printf("Hello, DAC!\n");

    // Initialize SPI channel (channel, baud rate set to 20MHz)
    spi_init(SPI_PORT, 20000000) ;
    // Format (channel, data bits per transfer, polarity, phase, order)
    spi_set_format(SPI_PORT, 16, 0, 0, 0);

    // Setup the ISR-timing GPIO
    gpio_init(ISR_GPIO) ;
    gpio_set_dir(ISR_GPIO, GPIO_OUT);
    gpio_put(ISR_GPIO, 0) ;

    // Map SPI signals to GPIO ports
    gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

    // === build the sine lookup table =======
   	// scaled to produce values between 0 and 4096
    int ii;
    for (ii = 0; ii < sine_table_size; ii++){
         sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
    }

        // Enable the interrupt for the alarm (we're using Alarm 0)
    hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
    // Associate an interrupt handler with the ALARM_IRQ
    irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
    // Enable the alarm interrupt
    irq_set_enabled(ALARM_IRQ, true) ;
    // Write the lower 32 bits of the target time to the alarm register, arming it.
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

  // set up LED gpio 25
  gpio_init(LED_PIN) ;  
  gpio_set_dir(LED_PIN, GPIO_OUT) ;
  gpio_put(LED_PIN, true);

  // === config threads ========================
  pt_add_thread(protothread_toggle25);
  
  // === initalize the scheduler ===============
  pt_schedule_start ;

    // Nothing happening here
    while(1){
    }
    return 0;
} // end main
```


## ADC_DDS_keypad_mute.c

Week 2 mute stage: keypad scanning and a four-state debounce machine set tone_enabled; the amplitude thread ramps output rather than rebuilding the sine table.

```c

/*
Simple ADC/Protothreads demo

Schedules a single thread, reads/prints ADC value

 */

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "hardware/spi.h"
#include <math.h>
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"

// ==========================================
// === protothreads globals
// ==========================================
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

#define LED_PIN 25
#define ADC_PIN 26
#define ADC_MUX 0

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)

//keypad constants
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

volatile static unsigned int adc_val ;

static int map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//keycodes
unsigned int keycodes[NUMKEYS] = {      0x57, 0x6E, 0x5E, 0x3E, 0x6D,
                                        0x5D, 0x3D, 0x6B, 0x5B, 0x3B,
                                        0x67, 0x37} ;
unsigned int scancodes[KEYROWS] = {   0xE, 0xD, 0xB, 0x7} ;
unsigned int button = 0x70 ;
char keytext[40];
int prev_key = 0;

//muting variables
#define AMPLITUDE_STEP 103 // 20 updates at 1 ms each for a full fade
volatile bool tone_enabled = true;
volatile int amplitude = 2047; // 0 = silent, 2047 = full amplitude

//debouncing states
#define NOT_PRESSED 0
#define MAYBE_PRESSED 1
#define PRESSED 2
#define MAYBE_NOT_PRESSED 3


// ==================================================
// === toggle25 thread 
// ==================================================
//  
static PT_THREAD (protothread_toggle25(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
        // toggle gpio 25
        gpio_put(LED_PIN, !gpio_get(LED_PIN));

        // Read the ADC
        adc_val = adc_read() ;

        phase_incr_main = (map(adc_val, 0, 4095, 0, 10000)*two32)/Fs;

        // Print the value
        printf("ADC value: %d\n", adc_val) ;

        // Yield
        PT_YIELD_usec(10000) ; //yeild every 10 ms
      } // END WHILE(1)
      // every thread ends with PT_END(pt);
      PT_END(pt);
} // end blink thread

// This thread runs on core 0
static PT_THREAD (protothread_core_0(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    // Some variables
    static int i ;
    static uint32_t keypad ;
    static int possible;
    static int state;

    while(1) {

        // Scan the keypad!
        for (i=0; i<KEYROWS; i++) {
            // Set a row high
            gpio_put_masked((0xF << BASE_KEYPAD_PIN),
                            (scancodes[i] << BASE_KEYPAD_PIN)) ;
            // Small delay required
            sleep_us(1) ;
            // Read the keycode
            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F) ;
            // Break if button(s) are pressed
            if ((~keypad) & button) break ;
        }
        // If we found a button . . .
        if ((~keypad) & button) {
            // Look for a valid keycode.
            for (i=0; i<NUMKEYS; i++) {
                if (keypad == keycodes[i]) break ;
            }
            // If we don't find one, report invalid keycode
            if (i==NUMKEYS) (i = -1) ;
        }
        // Otherwise, indicate invalid/non-pressed buttons
        else (i=-1) ;

        switch (state) {

          case NOT_PRESSED:
            if (i != -1) {
              possible = i;
              state = MAYBE_PRESSED;
            }
            break;

          case MAYBE_PRESSED:
            if (i == possible){
              state = PRESSED;
              // Toggle once on confirmed release of the remembered key.
              if (possible == 0) {
                tone_enabled = !tone_enabled;
              // Print key to terminal
              printf("\n%d", i) ;
              }
            }
            else {
              state = NOT_PRESSED;
            }
            break;
          
          case PRESSED:
            if (i == possible){
              state = PRESSED;
            }
            else {
              state = MAYBE_NOT_PRESSED;
            }
            break;

          case MAYBE_NOT_PRESSED:
            if (i == possible){
              state = PRESSED;
            }
            else {
              state = NOT_PRESSED;
              }
            break;

          default:
            state = NOT_PRESSED;
            break;

        }

        PT_YIELD_usec(30000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}

// This thread runs on core 0
static PT_THREAD (protothread_amplitude(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (tone_enabled && amplitude < 2047){
        int next_amplitude = amplitude + AMPLITUDE_STEP;
        amplitude = (next_amplitude > 2047) ? 2047 : next_amplitude;
      }

      else if (!tone_enabled && amplitude > 0){
        int next_amplitude = amplitude - AMPLITUDE_STEP;
        amplitude = (next_amplitude < 0) ? 0 : next_amplitude;
      }

      PT_YIELD_usec(1000) ;
    }

    PT_END(pt) ;

}

// Alarm ISR
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ; 

    // DDS phase and sine table lookup
    phase_accum_main += phase_incr_main  ;
    DAC_data = (DAC_config_chan_A | (((sin_table[phase_accum_main>>24] * amplitude)/2047 + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

// ========================================
// === core 0 main
// ========================================
int main(){
  //===  start the serial i/o ==================
  stdio_init_all() ;
  // announce the threader version on system reset
  // if there is a seral terminal attached
  printf("\n\rProtothreads RP2040 v1.4\n\r");

  // Setup the ADC
  adc_init() ;
  adc_gpio_init(ADC_PIN) ;
  adc_select_input(ADC_MUX) ;

  // Initialize stdio
  stdio_init_all();
  printf("Hello, DAC!\n");

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;
  // Format (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Setup the ISR-timing GPIO
  gpio_init(ISR_GPIO) ;
  gpio_set_dir(ISR_GPIO, GPIO_OUT);
  gpio_put(ISR_GPIO, 0) ;

  // Map SPI signals to GPIO ports
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

  // === build the sine lookup table =======
  // scaled to produce values between 0 and 4096
  int ii;
  for (ii = 0; ii < sine_table_size; ii++){
        sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
  }

      // Enable the interrupt for the alarm (we're using Alarm 0)
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
  // Associate an interrupt handler with the ALARM_IRQ
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
  // Enable the alarm interrupt
  irq_set_enabled(ALARM_IRQ, true) ;
  // Write the lower 32 bits of the target time to the alarm register, arming it.
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

  // set up LED gpio 25
  gpio_init(LED_PIN) ;  
  gpio_set_dir(LED_PIN, GPIO_OUT) ;
  gpio_put(LED_PIN, true);

    ////////////////// KEYPAD INITS ///////////////////////
  // Initialize the keypad GPIO's
  gpio_init_mask((0x7F << BASE_KEYPAD_PIN)) ;
  gpio_set_dir((BASE_KEYPAD_PIN+4), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+5), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+6), GPIO_IN);
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN+4)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+5)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+6)) ;

  // === config threads ========================
  pt_add_thread(protothread_toggle25);
  pt_add_thread(protothread_core_0);
  pt_add_thread(protothread_amplitude);
  
  // === initalize the scheduler ===============
  pt_schedule_start ;

  // Nothing happening here
  while(1){
  }
  return 0;
} // end main
```


## ADC_DDS_keypad_record.c

Week 2 recording stage: nine buffers store frequencies in Hz. The recording and playback threads both request 10 ms intervals. Historical snapshot, not the current build target.

```c

/*
Simple ADC/Protothreads demo

Schedules a single thread, reads/prints ADC value

 */

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "hardware/spi.h"
#include <math.h>
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"

// ==========================================
// === protothreads globals
// ==========================================
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

#define LED_PIN 25
#define ADC_PIN 26
#define ADC_MUX 0

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)

//keypad constants
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

volatile static unsigned int adc_val ;

static int map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//keycodes
unsigned int keycodes[NUMKEYS] = {      0x57, 0x6E, 0x5E, 0x3E, 0x6D,
                                        0x5D, 0x3D, 0x6B, 0x5B, 0x3B,
                                        0x67, 0x37} ;
unsigned int scancodes[KEYROWS] = {   0xE, 0xD, 0xB, 0x7} ;
unsigned int button = 0x70 ;
char keytext[40];
int prev_key = 0;

//muting variables
#define AMPLITUDE_STEP 103 // 20 updates at 1 ms each for a full fade
volatile bool tone_enabled = false;
volatile int amplitude = 0; // 0 = silent, 2047 = full amplitude

//record variables

#define RECORD_HZ 100
#define MAX_SAMPLES (10 * RECORD_HZ)

volatile bool record_mode = false;
volatile bool record = false;
volatile bool play = false;
volatile int record_button = -1;
volatile int play_button = -1;
volatile int play_index = 0;
volatile uint16_t sounds[9][MAX_SAMPLES];
static unsigned int sound_length[9] = {0};

//debouncing states
#define NOT_PRESSED 0
#define MAYBE_PRESSED 1
#define PRESSED 2
#define MAYBE_NOT_PRESSED 3


// ==================================================
// === toggle25 thread 
// ==================================================
//  
static PT_THREAD (protothread_toggle25(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
        // toggle gpio 25
        gpio_put(LED_PIN, !gpio_get(LED_PIN));

        // Read the ADC
        adc_val = adc_read() ;

        if (!play){
          phase_incr_main = (map(adc_val, 0, 4095, 0, 10000)*two32)/Fs;
        }
        

        // Print the value
        //printf("ADC value: %d\n", adc_val) ;

        // Yield
        PT_YIELD_usec(10000) ; //yeild every 10 ms
      } // END WHILE(1)
      // every thread ends with PT_END(pt);
      PT_END(pt);
} // end blink thread

// This thread runs on core 0
static PT_THREAD (protothread_core_0(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    // Some variables
    static int i ;
    static uint32_t keypad ;
    static int possible;
    static int state;

    while(1) {

        // Scan the keypad!
        for (i=0; i<KEYROWS; i++) {
            // Set a row high
            gpio_put_masked((0xF << BASE_KEYPAD_PIN),
                            (scancodes[i] << BASE_KEYPAD_PIN)) ;
            // Small delay required
            sleep_us(1) ;
            // Read the keycode
            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F) ;
            // Break if button(s) are pressed
            if ((~keypad) & button) break ;
        }
        // If we found a button . . .
        if ((~keypad) & button) {
            // Look for a valid keycode.
            for (i=0; i<NUMKEYS; i++) {
                if (keypad == keycodes[i]) break ;
            }
            // If we don't find one, report invalid keycode
            if (i==NUMKEYS) (i = -1) ;
        }
        // Otherwise, indicate invalid/non-pressed buttons
        else (i=-1) ;

        switch (state) {

          case NOT_PRESSED:
            if (i != -1) {
              possible = i;
              state = MAYBE_PRESSED;
            }
            break;

          case MAYBE_PRESSED:
            if (i == possible){
              state = PRESSED;
              // Toggle once on confirmed release of the remembered key.
              if (possible == 0) {
                tone_enabled = !tone_enabled;
              ;
              }

              if (possible == 10){
                record_mode = true;
                play = false;
              }

              if (!record_mode && (possible <=9 && possible >= 1)){
                play = true;
                play_button = possible;
                play_index = 0;
                tone_enabled = true;
              }
              // Print key to terminal
              printf("\n%d", i); 
            }
            else {
              state = NOT_PRESSED;
            }
            break;
          
          case PRESSED:
            if (i == possible){
              state = PRESSED;
              if (record_mode && (possible <=9 && possible >= 1)){
                record_button = possible;
                record = true;
              }
            }
            else {
              state = MAYBE_NOT_PRESSED;
            }
            break;

          case MAYBE_NOT_PRESSED:
            if (i == possible){
              state = PRESSED;
            }
            else {
              state = NOT_PRESSED;
              if (record_mode && possible == record_button){
                record_mode = false;
                record = false;
                record_button = -1;
              }
              }
            break;

          default:
            state = NOT_PRESSED;
            break;

        }

        PT_YIELD_usec(30000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}

// This thread ramps up/down amplitude of output wave
static PT_THREAD (protothread_amplitude(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (tone_enabled && amplitude < 2047){
        int next_amplitude = amplitude + AMPLITUDE_STEP;
        amplitude = (next_amplitude > 2047) ? 2047 : next_amplitude;
      }

      else if (!tone_enabled && amplitude > 0){
        int next_amplitude = amplitude - AMPLITUDE_STEP;
        amplitude = (next_amplitude < 0) ? 0 : next_amplitude;
      }

      PT_YIELD_usec(1000) ;
    }

    PT_END(pt) ;

}

// This thread handkes playback
static PT_THREAD (protothread_playback(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (play){
        if (play_index < sound_length[play_button - 1]) {
          uint16_t frequency = sounds[play_button - 1][play_index++];

          phase_incr_main = (unsigned int)((frequency * two32) / Fs);
        }
        else {
          play_index = 0;
          play = false;
          tone_enabled = false;
        }
      }
      else{
        play_index = 0;
      }

      PT_YIELD_usec(10000) ;
    }

    PT_END(pt) ;

}

static PT_THREAD (protothread_recording(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    static int length = 0;

    while (1){

      if (record){

        if(record_button != -1 && length < MAX_SAMPLES){
          sounds[record_button - 1][length] = (uint16_t)map(adc_val, 0, 4095, 0, 10000);;
          length++;
          sound_length[record_button - 1] = length;
        }
      }
      else{
        length = 0;
      }


      PT_YIELD_usec(10000) ;
    }

    PT_END(pt) ;

}

// Alarm ISR
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ; 

    // DDS phase and sine table lookup
    phase_accum_main += phase_incr_main  ;

    DAC_data = (DAC_config_chan_A | (((sin_table[phase_accum_main>>24] * amplitude)/2047 + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

// ========================================
// === core 0 main
// ========================================
int main(){
  //===  start the serial i/o ==================
  stdio_init_all() ;
  // announce the threader version on system reset
  // if there is a seral terminal attached
  printf("\n\rProtothreads RP2040 v1.4\n\r");

  // Setup the ADC
  adc_init() ;
  adc_gpio_init(ADC_PIN) ;
  adc_select_input(ADC_MUX) ;

  // Initialize stdio
  stdio_init_all();
  printf("Hello, DAC!\n");

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;
  // Format (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Setup the ISR-timing GPIO
  gpio_init(ISR_GPIO) ;
  gpio_set_dir(ISR_GPIO, GPIO_OUT);
  gpio_put(ISR_GPIO, 0) ;

  // Map SPI signals to GPIO ports
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

  // === build the sine lookup table =======
  // scaled to produce values between 0 and 4096
  int ii;
  for (ii = 0; ii < sine_table_size; ii++){
        sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
  }

      // Enable the interrupt for the alarm (we're using Alarm 0)
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
  // Associate an interrupt handler with the ALARM_IRQ
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
  // Enable the alarm interrupt
  irq_set_enabled(ALARM_IRQ, true) ;
  // Write the lower 32 bits of the target time to the alarm register, arming it.
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

  // set up LED gpio 25
  gpio_init(LED_PIN) ;  
  gpio_set_dir(LED_PIN, GPIO_OUT) ;
  gpio_put(LED_PIN, true);

    ////////////////// KEYPAD INITS ///////////////////////
  // Initialize the keypad GPIO's
  gpio_init_mask((0x7F << BASE_KEYPAD_PIN)) ;
  gpio_set_dir((BASE_KEYPAD_PIN+4), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+5), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+6), GPIO_IN);
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN+4)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+5)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+6)) ;

  // === config threads ========================
  pt_add_thread(protothread_toggle25);
  pt_add_thread(protothread_core_0);
  pt_add_thread(protothread_amplitude);
  pt_add_thread(protothread_playback);
  pt_add_thread(protothread_recording);
  
  // === initalize the scheduler ===============
  pt_schedule_start ;

  // Nothing happening here
  while(1){
  }
  return 0;
} // end main
```


## ADC_DDS_keypad_speedup.c

Final target: frequency recording at 10 ms, playback at 1 ms, soundboard previews during compose entry, and indirect sequence playback. Read the known limitations in the main report.

```c

/*
Simple ADC/Protothreads demo

Schedules a single thread, reads/prints ADC value

 */

#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/adc.h"
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include <stdio.h>
#include <string.h>
#include "stdlib.h"
#include "hardware/spi.h"
#include <math.h>
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/sync.h"
#include "hardware/spi.h"
#include "hardware/clocks.h"

// ==========================================
// === protothreads globals
// ==========================================
// protothreads header
#include "pt_cornell_rp2040_v1_4.h"

#define LED_PIN 25
#define ADC_PIN 26
#define ADC_MUX 0

// Low-level alarm infrastructure we'll be using
#define ALARM_NUM 0
#define ALARM_IRQ timer_hardware_alarm_get_irq_num(timer_hw, ALARM_NUM)

//DDS parameters
#define two32 4294967296.0 // 2^32 
#define Fs 50000
#define DELAY 20 // 1/Fs (in microseconds)

//keypad constants
#define BASE_KEYPAD_PIN 9
#define KEYROWS         4
#define NUMKEYS         12
// the DDS units:
volatile unsigned int phase_accum_main;
volatile unsigned int phase_incr_main;

// SPI data
uint16_t DAC_data ; // output value

//DAC parameters
// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000
// B-channel, 1x, active
#define DAC_config_chan_B 0b1011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

//GPIO for timing the ISR
#define ISR_GPIO 2

// DDS sine table
#define sine_table_size 256
volatile int sin_table[sine_table_size] ;

volatile static unsigned int adc_val ;

static int map(int x, int in_min, int in_max, int out_min, int out_max) {
    return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}

//keycodes
unsigned int keycodes[NUMKEYS] = {      0x57, 0x6E, 0x5E, 0x3E, 0x6D,
                                        0x5D, 0x3D, 0x6B, 0x5B, 0x3B,
                                        0x67, 0x37} ;
unsigned int scancodes[KEYROWS] = {   0xE, 0xD, 0xB, 0x7} ;
unsigned int button = 0x70 ;
char keytext[40];
int prev_key = 0;

// REPORT NOTE: amplitude scales the signed sample; midpoint is added afterward.
// In the final file, startup sound is enabled and the ramp yield is 50 us.
//muting variables
#define AMPLITUDE_STEP 103 // 20 updates at 1 ms each for a full fade
volatile bool tone_enabled = true;
volatile int amplitude = 2047; // 0 = silent, 2047 = full amplitude

// REPORT NOTE: frequency storage is RAM-only. Key k uses row k-1.
// Valid lengths bound replay; capacity is 1000 samples per sound.
//record variables

#define RECORD_HZ 100
#define MAX_SAMPLES (10 * RECORD_HZ)

volatile bool record_mode = false;
volatile bool record = false;
volatile bool play = false;
volatile int record_button = -1;
volatile int play_button = -1;
volatile int play_index = 0;
volatile uint16_t sounds[9][MAX_SAMPLES];
static unsigned int sound_length[9] = {0};

// REPORT NOTE: the sequence stores key IDs, not waveform data or delays.
// current_button indexes the sequence; its value selects a sound slot.
//compose variables
volatile bool compose_mode = false;
volatile bool play_compose = false;
volatile int current_button = 0;
volatile int compose_sequence[9];
volatile int compose_length;

//debouncing states
#define NOT_PRESSED 0
#define MAYBE_PRESSED 1
#define PRESSED 2
#define MAYBE_NOT_PRESSED 3


// ==================================================
// === toggle25 thread 
// ==================================================
//  
// REPORT NOTE: live ADC updates must not replace a playback frequency.
// Diagnostic printing and cooperative scheduling affect update timing.
static PT_THREAD (protothread_toggle25(struct pt *pt))
{
    PT_BEGIN(pt);

      while(1) {
        // toggle gpio 25
        gpio_put(LED_PIN, !gpio_get(LED_PIN));

        // Read the ADC
        adc_val = adc_read() ;

        printf("FREQUENCY: \n%d", map(adc_val, 0, 4095, 0, 10000));

        if (!play && !play_compose){
          phase_incr_main = (map(adc_val, 0, 4095, 0, 10000)*two32)/Fs;
        }
        

        // Print the value
        //printf("ADC value: %d\n", adc_val) ;

        // Yield
        PT_YIELD_usec(10000) ; //yeild every 10 ms
      } // END WHILE(1)
      // every thread ends with PT_END(pt);
      PT_END(pt);
} // end blink thread

// This thread runs on core 0
// REPORT NOTE: scan and debounce once per 30 ms yield cycle.
// This snapshot dispatches controls on confirmed PRESS, not release.
static PT_THREAD (protothread_core_0(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    // Some variables
    static int i ;
    static uint32_t keypad ;
    static int possible;
    static int state;

    while(1) {

        // Scan the keypad!
        for (i=0; i<KEYROWS; i++) {
            // Set a row high
            gpio_put_masked((0xF << BASE_KEYPAD_PIN),
                            (scancodes[i] << BASE_KEYPAD_PIN)) ;
            // Small delay required
            sleep_us(1) ;
            // Read the keycode
            keypad = ((gpio_get_all() >> BASE_KEYPAD_PIN) & 0x7F) ;
            // Break if button(s) are pressed
            if ((~keypad) & button) break ;
        }
        // If we found a button . . .
        if ((~keypad) & button) {
            // Look for a valid keycode.
            for (i=0; i<NUMKEYS; i++) {
                if (keypad == keycodes[i]) break ;
            }
            // If we don't find one, report invalid keycode
            if (i==NUMKEYS) (i = -1) ;
        }
        // Otherwise, indicate invalid/non-pressed buttons
        else (i=-1) ;

        switch (state) {

          case NOT_PRESSED:
            if (i != -1) {
              possible = i;
              state = MAYBE_PRESSED;
            }
            break;

// REPORT NOTE: only this transition dispatches new press actions.
          case MAYBE_PRESSED:
            if (i == possible){
              state = PRESSED;
              // Toggle once on confirmed release of the remembered key.
              if (possible == 0) {
                bool other_mode =
                  record_mode || record || compose_mode || play || play_compose;

                record_mode = false;
                record = false;
                record_button = -1;

                compose_mode = false;
                play_compose = false;
                current_button = 0;

                play = false;
                play_button = -1;
                play_index = 0;

                // Immediately restore the potentiometer frequency.
                phase_incr_main =
                    (unsigned int)((map(adc_val, 0, 4095, 0, 10000) * two32) / Fs);

                tone_enabled = other_mode ? true : !tone_enabled;
              
              }

              if (possible == 10){
                record_mode = true;
                record = false;
                record_button = -1;
                play = false;
                compose_mode = false;
                play_compose = false;
                play_index = 0;
                current_button = 0;
                tone_enabled = true;
              }

              if (possible == 11){
                if (compose_mode) {
                  compose_mode = false;
                  // start playing
                  play = false;
                  current_button = 0;
                  play_index = 0;
                  play_compose = (compose_length > 0);
                  tone_enabled = play_compose;
                }
                else {
                  compose_mode = true;
                  record_mode = false;
                  record = false;
                  record_button = -1;
                  compose_length = 0;
                  play = false;
                  play_compose = false;
                  play_index = 0;
                  current_button = 0;
                  tone_enabled = false;
                }
              }

              if (compose_mode && (possible <=9 && possible >= 1) && compose_length < 9){
                compose_sequence[compose_length] = possible;
                compose_length++;
              }

              if (!record_mode && (possible <=9 && possible >= 1 && sound_length[possible - 1] > 0)){
                play = true;
                play_button = possible;
                play_index = 0;
                tone_enabled = true;
                play_compose = false;
                current_button = 0;
              }

              // Print key to terminal
              printf("\n%d", i); 
            }
            else {
              state = NOT_PRESSED;
            }
            break;
          
// REPORT NOTE: the held state starts recording in an armed mode.
// It is revisited while held, so do not reset the write index here every scan.
          case PRESSED:
            if (i == possible){
              state = PRESSED;
              if (record_mode && (possible <=9 && possible >= 1)){
                record_button = possible;
                record = true;
              }
            }
            else {
              state = MAYBE_NOT_PRESSED;
            }
            break;

// REPORT NOTE: two observations of loss of the remembered key end a hold.
          case MAYBE_NOT_PRESSED:
            if (i == possible){
              state = PRESSED;
            }
            else {
              state = NOT_PRESSED;
              if (record_mode && possible == record_button){
                record_mode = false;
                record = false;
                record_button = -1;
              }
              }
            break;

          default:
            state = NOT_PRESSED;
            break;

        }

        PT_YIELD_usec(30000) ;
    }
    // Indicate thread end
    PT_END(pt) ;
}

// This thread ramps up/down amplitude of output wave
// REPORT NOTE: clamp before publishing amplitude so ISR samples remain in range.
static PT_THREAD (protothread_amplitude(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (tone_enabled && amplitude < 2047){
        int next_amplitude = amplitude + AMPLITUDE_STEP;
        amplitude = (next_amplitude > 2047) ? 2047 : next_amplitude;
      }

      else if (!tone_enabled && amplitude > 0){
        int next_amplitude = amplitude - AMPLITUDE_STEP;
        amplitude = (next_amplitude < 0) ? 0 : next_amplitude;
      }

      PT_YIELD_usec(50) ;
    }

    PT_END(pt) ;

}

// This thread handkes playback
// REPORT NOTE: a frequency entry changes DDS increment, not the DAC sample directly.
// Final composition advances entries in a separate iteration, adding a short dwell.
static PT_THREAD (protothread_playback(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    while (1){

      if (play){
        if (play_index < sound_length[play_button - 1]) {
          uint16_t frequency = sounds[play_button - 1][play_index++];

          phase_incr_main = (unsigned int)((frequency * two32) / Fs);
        }
        else {
          play_index = 0;
          play = false;
          tone_enabled = false;
        }
      }
      else if (play_compose) {
        if (current_button < compose_length) {
          int slot = compose_sequence[current_button] - 1;

          if (play_index < sound_length[slot]) {
              uint16_t frequency = sounds[slot][play_index++];
              phase_incr_main =
                  (unsigned int)((frequency * two32) / Fs);
          } else {
              current_button++;
              play_index = 0;
          }
        } 
        else {
            play_compose = false;
            current_button = 0;
            play_index = 0;
            tone_enabled = false;
        }
      }
      else{
        play_index = 0;
      }

      PT_YIELD_usec(1000) ;
    }

    PT_END(pt) ;

}

// REPORT NOTE: store mapped Hz, not the much larger 32-bit DDS increment.
// The idle branch resets the write index; the source does not clear a slot at start.
static PT_THREAD (protothread_recording(struct pt *pt))
{
    // Indicate thread beginning
    PT_BEGIN(pt) ;

    static int length = 0;

    while (1){

      if (record){

        if(record_button != -1 && length < MAX_SAMPLES){
          sounds[record_button - 1][length] = (uint16_t)map(adc_val, 0, 4095, 0, 10000);;
          length++;
          sound_length[record_button - 1] = length;
        }
      }
      else{
        length = 0;
      }


      PT_YIELD_usec(10000) ;
    }

    PT_END(pt) ;

}

// Alarm ISR
// REPORT NOTE: GPIO2 high/low brackets the instrumented synthesis body.
// Rearming relative to the current timer introduces latency into the actual period.
static void alarm_irq(void) {

    // Assert a GPIO when we enter the interrupt
    gpio_put(ISR_GPIO, 1) ;

    // Clear the alarm irq
    hw_clear_bits(&timer_hw->intr, 1u << ALARM_NUM);

    // Reset the alarm register
    timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ; 

    // DDS phase and sine table lookup
    phase_accum_main += phase_incr_main  ;

// REPORT NOTE: sine sample is centered around zero before the +2048 offset.
// The existing 0xffff mask is retained; valid bounds keep data in 12 bits.
    DAC_data = (DAC_config_chan_A | (((sin_table[phase_accum_main>>24] * amplitude)/2047 + 2048) & 0xffff))  ;

    // Perform an SPI transaction
    spi_write16_blocking(SPI_PORT, &DAC_data, 1) ;

    // De-assert the GPIO when we leave the interrupt
    gpio_put(ISR_GPIO, 0) ;

}

// ========================================
// === core 0 main
// ========================================
// REPORT NOTE: initialize peripherals and sine table before scheduling control work.
int main(){
  //===  start the serial i/o ==================
  stdio_init_all() ;
  // announce the threader version on system reset
  // if there is a seral terminal attached
  printf("\n\rProtothreads RP2040 v1.4\n\r");

  // Setup the ADC
  adc_init() ;
  adc_gpio_init(ADC_PIN) ;
  adc_select_input(ADC_MUX) ;

  // Initialize stdio
  stdio_init_all();
  printf("Hello, DAC!\n");

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;
  // Format (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Setup the ISR-timing GPIO
  gpio_init(ISR_GPIO) ;
  gpio_set_dir(ISR_GPIO, GPIO_OUT);
  gpio_put(ISR_GPIO, 0) ;

  // Map SPI signals to GPIO ports
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;

  // === build the sine lookup table =======
  // scaled to produce values between 0 and 4096
  int ii;
  for (ii = 0; ii < sine_table_size; ii++){
        sin_table[ii] = (int)(2047*sin((float)ii*6.283/(float)sine_table_size));
  }

      // Enable the interrupt for the alarm (we're using Alarm 0)
  hw_set_bits(&timer_hw->inte, 1u << ALARM_NUM) ;
  // Associate an interrupt handler with the ALARM_IRQ
  irq_set_exclusive_handler(ALARM_IRQ, alarm_irq) ;
  // Enable the alarm interrupt
  irq_set_enabled(ALARM_IRQ, true) ;
  // Write the lower 32 bits of the target time to the alarm register, arming it.
  timer_hw->alarm[ALARM_NUM] = timer_hw->timerawl + DELAY ;

  // set up LED gpio 25
  gpio_init(LED_PIN) ;  
  gpio_set_dir(LED_PIN, GPIO_OUT) ;
  gpio_put(LED_PIN, true);

    ////////////////// KEYPAD INITS ///////////////////////
  // Initialize the keypad GPIO's
  gpio_init_mask((0x7F << BASE_KEYPAD_PIN)) ;
  gpio_set_dir((BASE_KEYPAD_PIN+4), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+5), GPIO_IN);
  gpio_set_dir((BASE_KEYPAD_PIN+6), GPIO_IN);
  // Set row-pins to output
  gpio_set_dir_out_masked((0xF << BASE_KEYPAD_PIN)) ;
  // Set all output pins to low
  gpio_put_masked((0xF << BASE_KEYPAD_PIN), (0xF << BASE_KEYPAD_PIN)) ;
  // Turn on pulldown resistors for column pins (on by default)
  gpio_pull_up((BASE_KEYPAD_PIN+4)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+5)) ;
  gpio_pull_up((BASE_KEYPAD_PIN+6)) ;

  // === config threads ========================
  pt_add_thread(protothread_toggle25);
  pt_add_thread(protothread_core_0);
  pt_add_thread(protothread_amplitude);
  pt_add_thread(protothread_playback);
  pt_add_thread(protothread_recording);
  
  // === initalize the scheduler ===============
  pt_schedule_start ;

  // Nothing happening here
  while(1){
  }
  return 0;
} // end main
```


## CMakeLists.txt

Build configuration selects only ADC_DDS_keypad_speedup.c as the application source and targets pico2.

```cmake
# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.0)
set(toolchainVersion 15_2_Rel1)
set(picotoolVersion 2.3.0)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
set(PICO_BOARD pico2 CACHE STRING "Board type")

# cmake version
cmake_minimum_required(VERSION 3.13)

# include the sdk.cmake file
include(pico_sdk_import.cmake)

# give the project a name (anything you want)
project(Audio_Timer_Interrupt_DDS C CXX ASM)

# initialize the sdk
pico_sdk_init()

add_executable(Audio_Timer_Interrupt_DDS)

target_sources(Audio_Timer_Interrupt_DDS PRIVATE ADC_DDS_keypad_speedup.c)

target_link_libraries(  Audio_Timer_Interrupt_DDS 
                        pico_stdlib 
                        pico_bootsel_via_double_reset 
                        hardware_spi 
                        hardware_timer 
                        hardware_irq 
                        pico_stdlib 
                        pico_sync
                        hardware_sync
                        hardware_clocks
                        hardware_vreg
                        hardware_clocks
                        hardware_adc
                        hardware_pio
                        hardware_dma)

pico_add_extra_outputs(Audio_Timer_Interrupt_DDS)
```
