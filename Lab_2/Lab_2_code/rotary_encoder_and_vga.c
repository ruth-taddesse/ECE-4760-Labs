
/**
 * Hunter Adams (vha3@cornell.edu)
 * 
 * This demonstration animates two balls bouncing about the screen.
 * Through a serial interface, the user can change the ball color.
 *
 * HARDWARE CONNECTIONS
  - GPIO 16 ---> VGA Hsync
  - GPIO 17 ---> VGA Vsync
  - GPIO 18 ---> VGA Green lo-bit --> 470 ohm resistor --> VGA_Green
  - GPIO 19 ---> VGA Green hi_bit --> 330 ohm resistor --> VGA_Green
  - GPIO 20 ---> 330 ohm resistor ---> VGA-Blue
  - GPIO 21 ---> 330 ohm resistor ---> VGA-Red
  - RP2040 GND ---> VGA-GND
 *
 * RESOURCES USED
 *  - PIO state machines 0, 1, and 2 on PIO instance 0
 *  - DMA channels (2, by claim mechanism)
 *  - 153.6 kBytes of RAM (for pixel color data)
 *
 */

// Include the VGA grahics library
#include "VGA/vga16_graphics_v3.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
// Include Pico libraries
#include "pico/stdlib.h"
#include "pico/divider.h"
#include "pico/multicore.h"
#include "pico/sync.h"
// Include hardware libraries
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_4.h"

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
#define divfix(a,b) (fix15)(div_s64s64( (((signed long long)(a)) << 15), ((signed long long)(b))))

// Wall detection
#define hitBottom(b) (b>int2fix15(380))
#define hitTop(b) (b<int2fix15(100))
#define hitLeft(a) (a<int2fix15(100))
#define hitRight(a) (a>int2fix15(540))

// uS per frame
#define FRAME_RATE 33000

//a and b pin for rotary encoder
#define a_pin 14
#define b_pin 15

// the color of the boid
char color = WHITE ;
char text[16];
volatile int count = 0;

void print_to_vga(int number){
  snprintf(text, sizeof(text), "%d", number);

  setCursor(20, 20);             // Position: x, y in pixels
  setTextColor2(WHITE, BLACK);   // Text color, background color
  setTextSize(2);                // 2× normal text size
  writeString(text);
}

void gpio_callback(uint gpio, uint32_t event_mask) {

    if (gpio_get(b_pin)){
      count++;
    }

    else{
      count--;
    }
}

static PT_THREAD (protothread_draw_count(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    while(1) {
      PT_YIELD_UNTIL(pt, draw_start_signal());
      clearLowFrame(0, BLACK);
      print_to_vga(count);
      
    } // END WHILE(1)
  PT_END(pt);
} // animation thread

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  set_sys_clock_khz(150000, true) ;
  // initialize stio
  stdio_init_all() ;

  // initialize VGA
  initVGA() ;

  //initialize rotary A
  gpio_init(a_pin) ;
  gpio_set_dir(a_pin, GPIO_IN) ;
  gpio_pull_up(a_pin) ;

  // initialize rotary B
  gpio_init(b_pin);
  gpio_pull_up(b_pin) ;
  gpio_set_dir(b_pin, GPIO_IN);

  //interrupt on A fall
  gpio_set_irq_enabled_with_callback(a_pin, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);


  pt_add_thread(protothread_draw_count);

  pt_schedule_start;

} 
