
/**
Lab 2, Milestone 1 - simple Galton Board with 1 peg
- current limitations: busy playback may drop new sound requests

THINGS TO VERIFY
VGA output
- Confirm the image is stable, with no rolling, flickering, or corrupted colors.
- You should see the arena, magenta peg, and white ball.
- Moving balls should leave no trails.

Ball behavior
- The ball starts above the peg with zero vertical velocity, then accelerates downward.
- A peg hit should deflect it and reduce its speed.
- It should separate cleanly instead of sticking, vibrating, or passing through.
- After reaching your current arena’s bottom, it should respawn.
- Observe several drops; randomized horizontal velocity means some may miss.

Sound
- Each accepted peg impact should produce one short tone, roughly 40 ms long.
- There should be silence between impacts—not a continuous tone.
- Wall hits and respawns should not trigger sound.
- Confirm later impacts still play audio; this checks DMA rearming.
- Your current busy check skips impacts occurring while a previous sound plays.

Timing
- Animation should remain smooth while audio plays.
- To verify the 60 fps budget, measure the time spent updating and drawing each frame, excluding the wait for draw_start_signal().
- That work should finish within approximately 16,667 microseconds. Use time_us_32() before/after the work and report results occasionally, rather than printing every frame.
 */

// Include the VGA grahics library
#include "VGA/vga16_graphics_v3.h"
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
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
#include "hardware/spi.h"
// Include protothreads
#include "pt_cornell_rp2040_v1_4.h"

// A-channel, 1x, active
#define DAC_config_chan_A 0b0011000000000000

//SPI configurations
#define PIN_MISO 4
#define PIN_CS   5
#define PIN_SCK  6
#define PIN_MOSI 7
#define SPI_PORT spi0

// === the fixed point macros ========================================
typedef signed int fix15 ;
#define multfix15(a,b) ((fix15)((((signed long long)(a))*((signed long long)(b)))>>15))
#define float2fix15(a) ((fix15)((a)*32768.0)) // 2^15
#define fix2float15(a) ((float)(a)/32768.0)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)
// multiplication instead of left-shifting negative integers
#define divfix(a,b) ((fix15)div_s64s64((long long)(a) * 32768LL, (long long)(b)))

// Wall detection
#define ARENA_LEFT 50
#define ARENA_TOP 50
#define ARENA_RIGHT 590
#define ARENA_BOTTOM 450
#define hitBottom(b) (b>int2fix15(ARENA_BOTTOM - BALL_RADIUS))
#define hitTop(b) (b<int2fix15(ARENA_TOP + BALL_RADIUS))
#define hitLeft(a) (a<int2fix15(ARENA_LEFT + BALL_RADIUS))
#define hitRight(a) (a>int2fix15(ARENA_RIGHT - BALL_RADIUS))

//a and b pin for rotary encoder
#define a_pin 14
#define b_pin 15

// the color of the boid
char color = WHITE ;
char text[16];
volatile int count = 0;

// Coordinate drawing between the animation and count threads.
semaphore_t draw_count_start ;
semaphore_t draw_count_done ;

void print_to_vga(int number){
  snprintf(text, sizeof(text), "%d", number);

  setCursor(ARENA_LEFT + 10, ARENA_TOP + 10);
  setTextColor2(WHITE, BLACK);   // Text color, background color
  setTextSize(2);                // 2× normal text size
  writeString(text);
}

void gpio_callback(uint gpio, uint32_t event_mask) {
    static uint32_t last_event_us = 0;
    uint32_t now = time_us_32();

    // Ignore repeated edges caused by mechanical contact bounce.
    if ((uint32_t)(now - last_event_us) < 2000) {
      return;
    }
    last_event_us = now;

    if (gpio_get(b_pin)){
      count++;
    }

    else{
      count--;
    }
}

// static PT_THREAD (protothread_draw_count(struct pt *pt))
// {
//     // Mark beginning of thread
//     PT_BEGIN(pt);

//     while(1) {
//       PT_YIELD_UNTIL(pt, draw_start_signal());
//       clearLowFrame(0, BLACK);
//       print_to_vga(count);
      
//     } // END WHILE(1)
//   PT_END(pt);
// } // animation thread

// the color of the ball
char ball_color = WHITE ;

// the color of the peg
char peg_color = MAGENTA ;

// lab-defined constants
#define BALL_RADIUS 4
#define PEG_RADIUS 6
#define COLLISION_DISTANCE int2fix15(BALL_RADIUS + PEG_RADIUS)
#define GRAVITY float2fix15(0.37)
#define BOUNCINESS float2fix15(0.5)

// Ball
fix15 ball_x ;
fix15 ball_y ;
fix15 ball_vx ;
fix15 ball_vy ;

// Singular, stationary peg (stored in fixed-point pixels)
static const fix15 peg_x = int2fix15(320);
static const fix15 peg_y = int2fix15(250);

// audio related constants + storage

#define SOUND_SAMPLES 1000
#define SAMPLE_RATE 25000

static uint16_t DAC_data[SOUND_SAMPLES];
static uint16_t *address_pointer = DAC_data;
static int data_chan, ctrl_chan;

// create a ball
void spawnBall(fix15* x, fix15* y, fix15* vx, fix15* vy)
{
  // start in top of screen
  *x = int2fix15(320) ;
  *y = int2fix15(125 + BALL_RADIUS) ;
  // small, randomized horizontal velocity: -1 to +1 pixels/frame
  *vx = float2fix15(0.25f * ((float)rand() / RAND_MAX) - 0.125f) ;
  // zero vertical velocity
  *vy = int2fix15(0) ;
}

// Draw the boundaries
void drawArena() {
  drawVLine(ARENA_LEFT, ARENA_TOP, ARENA_BOTTOM - ARENA_TOP, WHITE) ;
  drawVLine(ARENA_RIGHT, ARENA_TOP, ARENA_BOTTOM - ARENA_TOP, WHITE) ;
  drawHLine(ARENA_LEFT, ARENA_TOP, ARENA_RIGHT - ARENA_LEFT, WHITE) ;
  drawHLine(ARENA_LEFT, ARENA_BOTTOM, ARENA_RIGHT - ARENA_LEFT, WHITE) ;
}

void initAudio() { // 40 ms decaying tone

  // Initialize SPI channel (channel, baud rate set to 20MHz)
  spi_init(SPI_PORT, 20000000) ;

  // Format SPI channel (channel, data bits per transfer, polarity, phase, order)
  spi_set_format(SPI_PORT, 16, 0, 0, 0);

  // Map SPI signals to GPIO ports, acts like framed SPI with this CS mapping
  gpio_set_function(PIN_MISO, GPIO_FUNC_SPI);
  gpio_set_function(PIN_CS, GPIO_FUNC_SPI) ;
  gpio_set_function(PIN_SCK, GPIO_FUNC_SPI);
  gpio_set_function(PIN_MOSI, GPIO_FUNC_SPI);

  for (int i = 0; i < SOUND_SAMPLES; i++) {
    float remaining = 1.0f - (float)i / (SOUND_SAMPLES - 1);
    float envelope = remaining * remaining;
    float phase = 2.0f * 3.14159265f * 500.0f * i / SAMPLE_RATE;

    int sample = (int)(2048 + 1500 * envelope * sinf(phase));
    DAC_data[i] = DAC_config_chan_A | (sample & 0x0fff);
  }
  
  data_chan = dma_claim_unused_channel(true);
  ctrl_chan = dma_claim_unused_channel(true);

  // Setup the control channel
  dma_channel_config ctrl_config = dma_channel_get_default_config(ctrl_chan);   // default configs
  channel_config_set_transfer_data_size(&ctrl_config, DMA_SIZE_32);             // 32-bit txfers
  channel_config_set_read_increment(&ctrl_config, false);                       // no read incrementing
  channel_config_set_write_increment(&ctrl_config, false);                      // no write incrementing
  channel_config_set_chain_to(&ctrl_config, data_chan);                         // chain to data channel

  dma_channel_configure(
      ctrl_chan,                          // Channel to be configured
      &ctrl_config,                                 // The configuration we just created
      &dma_hw->ch[data_chan].read_addr,   // Write address (data channel read address)
      &address_pointer,                   // Read address (POINTER TO AN ADDRESS)
      1,                                  // Number of transfers
      false                               // Don't start immediately
  );

  // Setup the data channel
  dma_channel_config data_config = dma_channel_get_default_config(data_chan);  // Default configs
  channel_config_set_transfer_data_size(&data_config, DMA_SIZE_16);            // 16-bit txfers
  channel_config_set_read_increment(&data_config, true);                       // yes read incrementing
  channel_config_set_write_increment(&data_config, false);                     // no write incrementing
  
  // DMA timer 0 at 25 kHz [150 MHz system clock × (1 / 6000)]
  dma_timer_set_fraction(0, 1, 6000);
  // transfer one DAC sample per request from DMA timer 0
  channel_config_set_dreq(&data_config, dma_get_timer_dreq(0));
  // playback stops after buffer
  channel_config_set_chain_to(&data_config, data_chan);

  dma_channel_configure(
      data_chan,                  // Channel to be configured
      &data_config,                        // The configuration we just created
      &spi_get_hw(SPI_PORT)->dr,  // write address (SPI data register)
      DAC_data,                   // The initial read address
      SOUND_SAMPLES,            // Number of transfers
      false                       // Don't start immediately.
  );

}

// starts one playback
void triggerSound(void) {
    if (dma_channel_is_busy(data_chan) ||
        dma_channel_is_busy(ctrl_chan)) {
        return;
    }

    dma_channel_set_trans_count(data_chan, SOUND_SAMPLES, false);
    dma_channel_set_trans_count(ctrl_chan, 1, false);
    dma_start_channel_mask(1u << ctrl_chan);
}

// update velocity and position of ball
void updateBall(fix15* x, fix15* y, fix15* vx, fix15* vy)
{
  // update position using velocity
  *x = *x + *vx ;
  *y = *y + *vy ;

  // calculate distance from peg
  fix15 dx = *x - peg_x ;
  fix15 dy = *y - peg_y ;

  // quick check to justify full calculation
  if (absfix15(dx) < COLLISION_DISTANCE && absfix15(dy) < COLLISION_DISTANCE) {
    float dx_pixels = fix2float15(dx);
    float dy_pixels = fix2float15(dy);
    fix15 distance = float2fix15(sqrtf(dx_pixels * dx_pixels + dy_pixels * dy_pixels));
    if (distance < COLLISION_DISTANCE) { // peg hit - make sound!
      fix15 normal_x, normal_y;
      if (distance == 0) {
        normal_x = 0;
        normal_y = -int2fix15(1);
      }
      else {
        normal_x = divfix(dx, distance);
        normal_y = divfix(dy, distance);
      }
      
      // calculate direction of ball movement in relation to the peg
      // <0: moving towards peg, >0: moving away, =0: tangent to surface
      fix15 dir = multfix15(*vx, normal_x) + multfix15(*vy, normal_y); 
      if (dir < 0) { // reverse the part of velocity pointing into peg + preserve sideways motion
        fix15 impulse = -2 * dir;
        *vx += multfix15(normal_x, impulse);
        *vy += multfix15(normal_y, impulse);

        // bounciness!
        *vx = multfix15(*vx, BOUNCINESS);
        *vy = multfix15(*vy, BOUNCINESS);

        // start playback
        triggerSound();
      }
      // reposition ball outside of the peg, even if moving away 
      fix15 separation = int2fix15(BALL_RADIUS + PEG_RADIUS + 1);
      *x = peg_x + multfix15(normal_x, separation);
      *y = peg_y + multfix15(normal_y, separation);
    }
  }

  // wall collision logic
  if (hitTop(*y)) {
    *y = int2fix15(100 + BALL_RADIUS);
    if (*vy < 0) {*vy = -*vy;}
  }
  if (hitBottom(*y)) {
    spawnBall(x, y, vx, vy);
    return;
  } 
  if (hitRight(*x)) {
    *x = int2fix15(540 - BALL_RADIUS);
    if (*vx > 0) {*vx = -*vx;}
  }
  if (hitLeft(*x)) {
    *x = int2fix15(100 + BALL_RADIUS);
    if (*vx < 0) {*vx = -*vx;}
  } 

  *vy += GRAVITY;

}

// ==================================================
// === users serial input thread
// ==================================================
static PT_THREAD (protothread_serial(struct pt *pt))
{
    PT_BEGIN(pt);
    // stores user input
    static int user_input ;
    // wait for 0.1 sec
    PT_YIELD_usec(1000000) ;
    // announce the threader version
    sprintf(pt_serial_out_buffer, "Protothreads RP2040 v1.4\n\r");
    // non-blocking write
    serial_write ;
      while(1) {
        // print prompt
        sprintf(pt_serial_out_buffer, "input a number in the range 1-15: ");
        // non-blocking write
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%d", &user_input) ;
        // update ball color
        if ((user_input > 0) && (user_input < 16)) {
          ball_color = (char)user_input ;
        }
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// Animation on main core
static PT_THREAD (protothread_anim(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    // Spawn a ball
    spawnBall(&ball_x, &ball_y, &ball_vx, &ball_vy);

    while(1) {
      // Wait for the signal that the buffer's changed
      PT_YIELD_UNTIL(pt, draw_start_signal()) ;
      // Clear the buffer
      clearLowFrame(0, BLACK);
      // update ball's position and velocity
      updateBall(&ball_x, &ball_y, &ball_vx, &ball_vy) ;
      // draw the ball at its new position
      fillCircle(fix2int15(ball_x), fix2int15(ball_y), BALL_RADIUS, ball_color); 
      // draw the peg
      fillCircle(fix2int15(peg_x), fix2int15(peg_y), PEG_RADIUS, peg_color); 
      // draw the boundaries
      drawArena() ;
      // Let the count thread finish this frame before starting another.
      PT_SEM_SDK_SIGNAL(pt, &draw_count_start) ;
      PT_SEM_SDK_WAIT(pt, &draw_count_done) ;
     // NEVER exit while
    } // END WHILE(1)
  PT_END(pt);
} // animation thread

static PT_THREAD (protothread_draw_count(struct pt *pt))
{
    // Mark beginning of thread
    PT_BEGIN(pt);

    while(1) {
      PT_SEM_SDK_WAIT(pt, &draw_count_start);
      print_to_vga(count);
      PT_SEM_SDK_SIGNAL(pt, &draw_count_done);

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

  // initialize VGA + audio
  initVGA() ;
  initAudio();

  // initialize random seed generator
  srand((unsigned int)time(NULL));

  // Initialize the per-frame drawing handshake.
  sem_init(&draw_count_start, 0, 1) ;
  sem_init(&draw_count_done, 0, 1) ;

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


  // add threads
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);
  pt_add_thread(protothread_draw_count);

  // start scheduler
  pt_schedule_start ;
} 
