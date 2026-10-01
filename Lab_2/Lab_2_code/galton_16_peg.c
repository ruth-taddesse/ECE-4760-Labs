
/**
Lab 2, Milestone 1 - simple Galton Board with 1 peg
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

// ball-related constants
#define BALL_SPAWN_Y 20
#define BALL_EXIT_Y 400

// rotary encoder pins
#define state_pin 13
#define a_pin 14
#define b_pin 15

#define MAX_BALLS 1000
#define INITIAL_BALLS 10

// histogram location + layout constants
#define HISTOGRAM_BASELINE 470
#define HISTOGRAM_HEIGHT 90
#define BAR_WIDTH 20       // width of each bar in pixels
#define BAR_SPACING PEG_HORIZONTAL_SPACING // spacing between adjacent bins matches row 16 pegs

// lab-defined constants for ball / peg physics
#define BALL_RADIUS 4
#define PEG_RADIUS 6
#define COLLISION_DISTANCE int2fix15(BALL_RADIUS + PEG_RADIUS)
#define GRAVITY float2fix15(0.37)

// 16-row Galton board geometry: row n contains n + 1 pegs
#define PEG_ROWS 16
#define NUM_PEGS ((PEG_ROWS * (PEG_ROWS + 1)) / 2)
#define PEG_HORIZONTAL_SPACING 38
#define PEG_VERTICAL_SPACING 19
#define FIRST_PEG_X 320
#define FIRST_PEG_Y 75
#define NUM_BINS (PEG_ROWS + 1)
#define BOTTOM_ROW_START ((PEG_ROWS * (PEG_ROWS - 1)) / 2)

#define MIN_BOUNCE float2fix15(0.1f)
#define MAX_BOUNCE float2fix15(1.5f)
#define BOUNCE_STEP float2fix15(0.1f)

// audio playback configuration + DMA storage
#define SOUND_SAMPLES 1000
#define SAMPLE_RATE 25000

typedef struct {
  fix15 x;
  fix15 y;
  fix15 vx;
  fix15 vy;
} Ball;

typedef struct {
  fix15 x;
  fix15 y;
} Peg;

// coordinate drawing between animation and count threads
semaphore_t draw_count_start ;
semaphore_t draw_count_done ;

// initialize balls, pegs, and other global variables
static Ball balls[MAX_BALLS];
static uint32_t bin_counts[NUM_BINS];
static Peg pegs[NUM_PEGS];
static int bottom_left_x;
char color = WHITE ;
char text[64];
volatile int number_of_balls = INITIAL_BALLS;
volatile int animated_balls = INITIAL_BALLS;
volatile uint32_t total_fallen = 0;
volatile bool state = true; // true = adjusting # of balls, false = adjusting bounciness
volatile uint32_t bounciness = float2fix15(0.5);

static uint16_t DAC_data[SOUND_SAMPLES];
static uint16_t *address_pointer = DAC_data;
static int data_chan, ctrl_chan;

void print_to_vga(int num_balls){
  uint32_t seconds = to_ms_since_boot(get_absolute_time()) / 1000;

  // draw stats
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  setCursor(10, 20);
  snprintf(text, sizeof(text), "# of balls animated: %d", num_balls);
  writeString(text);

  setCursor(10, 30);
  snprintf(text, sizeof(text), "# of balls fallen: %lu", (unsigned long)total_fallen);
  writeString(text);

  setCursor(10, 40);
  snprintf(text, sizeof(text), "bounciness: %.2f", fix2float15(bounciness));
  writeString(text);

  setCursor(10, 50);
  snprintf(text, sizeof(text), "time since boot: %lus", (unsigned long)seconds);
  writeString(text);

  // draw histogram
  uint32_t max_count = 1;

  for (int i = 0; i < NUM_BINS; i++) {
    if (bin_counts[i] > max_count) {
      max_count = bin_counts[i];
    }
  }

  for (int i = 0; i < NUM_BINS; i++) {
    int height = (bin_counts[i] * HISTOGRAM_HEIGHT) / max_count;
    int bin_x = 10 + i * BAR_SPACING;

    fillRect(bin_x,
            HISTOGRAM_BASELINE - height,
            BAR_WIDTH,
            height,
            CYAN);
  }
}

void gpio_callback_encoder(uint gpio, uint32_t event_mask) {
    static uint32_t last_event_us = 0;
    uint32_t now = time_us_32();

    // ignore repeated edges caused by mechanical contact bounce
    if ((uint32_t)(now - last_event_us) < 2000) {
      return;
    }
    last_event_us = now;

    // clear parameters
    total_fallen = 0;
    memset(bin_counts, 0, sizeof(bin_counts));


    if (!state) {

      if (gpio_get(b_pin)){
        if (bounciness < MAX_BOUNCE) {
          bounciness += BOUNCE_STEP;
        }
      }

      else{
        if (bounciness > MIN_BOUNCE) {
          bounciness -= BOUNCE_STEP;
        }
      }

    }

    else {
      if (gpio_get(b_pin)){
        if (number_of_balls < MAX_BALLS) {
          number_of_balls += 10;
        }
      }

      else{
        if (number_of_balls > 1) {
          number_of_balls -= 10;
        }
      }
    }

}

void gpio_callback_state(uint gpio, uint32_t event_mask) {

  state = !state; // toggle between adjusting # of balls (true) vs bounciness (false)

}

void gpio_callback(uint gpio, uint32_t event_mask) {
  if (gpio == state_pin) {
    gpio_callback_state(gpio, event_mask);
  } else if (gpio == a_pin) {
    gpio_callback_encoder(gpio, event_mask);
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

// generate galton board with rows of 1 + 2 + ... + 16 pegs
void generatePegs(void)
{
  int peg_index = 0;

  for (int row = 0; row < PEG_ROWS; row++) {
    int row_start_x = FIRST_PEG_X -
                      (row * PEG_HORIZONTAL_SPACING) / 2;
    int row_y = FIRST_PEG_Y + row * PEG_VERTICAL_SPACING;

    for (int column = 0; column <= row; column++) {
      pegs[peg_index].x = int2fix15(
          row_start_x + column * PEG_HORIZONTAL_SPACING);
      pegs[peg_index].y = int2fix15(row_y);
      peg_index++;
    }
  }
}

void drawPegs(void)
{
  for (int i = 0; i < NUM_PEGS; i++) {
    fillCircle(fix2int15(pegs[i].x),
               fix2int15(pegs[i].y),
               PEG_RADIUS,
               peg_color);
  }
}

// create a ball
void spawnBall(fix15* x, fix15* y, fix15* vx, fix15* vy)
{
  // start in top of screen
  *x = int2fix15(320) ;
  *y = int2fix15(BALL_SPAWN_Y);
  // small, randomized horizontal velocity: -1 to +1 pixels/frame
  *vx = float2fix15(0.25f * ((float)rand() / RAND_MAX) - 0.125f) ;
  // zero vertical velocity
  *vy = int2fix15(0) ;
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

  // check the ball against each generated peg - only one collision is
  // resolved per frame as peg collision regions don't overlap
  for (int i = 0; i < NUM_PEGS; i++) {
    fix15 dx = *x - pegs[i].x;
    fix15 dy = *y - pegs[i].y;

    // cheap bounding-box rejection before calculating exact distance
    if (absfix15(dx) >= COLLISION_DISTANCE ||
        absfix15(dy) >= COLLISION_DISTANCE) {
      continue;
    }

    float dx_pixels = fix2float15(dx);
    float dy_pixels = fix2float15(dy);
    fix15 distance = float2fix15(
        sqrtf(dx_pixels * dx_pixels + dy_pixels * dy_pixels));

    if (distance >= COLLISION_DISTANCE) {
      continue;
    }

    fix15 normal_x;
    fix15 normal_y;
    if (distance == 0) {
      normal_x = 0;
      normal_y = -int2fix15(1);
    }
    else {
      normal_x = divfix(dx, distance);
      normal_y = divfix(dy, distance);
    }

    // reverse the velocity component pointing into peg
    fix15 dir = multfix15(*vx, normal_x) +
                multfix15(*vy, normal_y);
    if (dir < 0) {
      fix15 impulse = -2 * dir;
      *vx += multfix15(normal_x, impulse);
      *vy += multfix15(normal_y, impulse);

      *vx = multfix15(*vx, bounciness);
      *vy = multfix15(*vy, bounciness);
      triggerSound();
    }

    // move the ball just outside the peg to prevent repeated overlap
    fix15 separation = int2fix15(BALL_RADIUS + PEG_RADIUS + 1);
    *x = pegs[i].x + multfix15(normal_x, separation);
    *y = pegs[i].y + multfix15(normal_y, separation);
    break;
  }

  // ball exit logic
  if (*y > int2fix15(BALL_EXIT_Y)) {
    int bin = (fix2int15(*x) - bottom_left_x + PEG_HORIZONTAL_SPACING / 2) / PEG_HORIZONTAL_SPACING;
    if (bin < 0) bin = 0;
    if (bin >= NUM_BINS) bin = NUM_BINS - 1;
    bin_counts[bin]++;

    total_fallen++;
    spawnBall(x, y, vx, vy);
    return;
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
    static int active_balls;
    static int requested_balls;

    // Mark beginning of thread
    PT_BEGIN(pt);

    // Spawn every ball that is active at startup.
    active_balls = number_of_balls;
    animated_balls = active_balls;
    for (int i = 0; i < active_balls; i++) {
      spawnBall(&balls[i].x, &balls[i].y,
                &balls[i].vx, &balls[i].vy);
    }

    while(1) {
      // Wait for the signal that the buffer's changed
      PT_YIELD_UNTIL(pt, draw_start_signal()) ;
      // Clear the buffer
      clearLowFrame(0, BLACK);

      // Capture the encoder-controlled value once for this frame. Spawn any
      // newly enabled balls at the top of the board.
      requested_balls = number_of_balls;
      if (requested_balls > active_balls) {
        for (int i = active_balls; i < requested_balls; i++) {
          spawnBall(&balls[i].x, &balls[i].y,
                    &balls[i].vx, &balls[i].vy);
        }
      }
      active_balls = requested_balls;
      animated_balls = active_balls;

      // Update and draw every active ball.
      for (int i = 0; i < active_balls; i++) {
        updateBall(&balls[i].x, &balls[i].y,
                   &balls[i].vx, &balls[i].vy);
        fillCircle(fix2int15(balls[i].x),
                   fix2int15(balls[i].y),
                   BALL_RADIUS,
                   ball_color);
      }

      // draw all 136 pegs in the 16-row board
      drawPegs();

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
      print_to_vga(animated_balls);
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

  // Generate the fixed peg positions once at startup.
  generatePegs();
  bottom_left_x = fix2int15(pegs[BOTTOM_ROW_START].x);

  // initialize random seed generator
  srand((unsigned int)time(NULL));

  // Initialize the per-frame drawing handshake.
  sem_init(&draw_count_start, 0, 1) ;
  sem_init(&draw_count_done, 0, 1) ;

  gpio_init(state_pin);
  gpio_set_dir(state_pin, GPIO_IN) ;
  gpio_pull_up(state_pin) ;

  //initialize rotary A
  gpio_init(a_pin) ;
  gpio_set_dir(a_pin, GPIO_IN) ;
  //gpio_pull_up(a_pin) ;

  // initialize rotary B
  gpio_init(b_pin);
  gpio_pull_up(b_pin) ;
  //gpio_set_dir(b_pin, GPIO_IN);

  // interrupt on A fall
  gpio_set_irq_enabled_with_callback(a_pin, GPIO_IRQ_EDGE_FALL, true, gpio_callback);

  // rotary encoder state switch pin
  gpio_set_irq_enabled(state_pin, GPIO_IRQ_EDGE_FALL, true);


  // add threads
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);
  pt_add_thread(protothread_draw_count);

  // start scheduler
  pt_schedule_start ;
} 
