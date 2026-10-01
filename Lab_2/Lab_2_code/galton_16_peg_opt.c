
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
#include "hardware/sync.h"

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

#define MAX_BALLS 5000
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

#define ALPHA float2fix15(0.96043387f)
#define BETA float2fix15(0.397824734f)

// audio playback configuration + DMA storage
#define SOUND_SAMPLES 1000
#define SAMPLE_RATE 25000

// 1 = physics every frame, 2 = every other frame, etc.
#define PHYSICS_EVERY_N_FRAMES 2

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

// Core 0 publishes one physics job per frame.
static semaphore_t physics_start;
static semaphore_t physics_done;

static int worker_begin;
static int worker_end;
static fix15 frame_bounce;

// Each core writes only the entries for its assigned balls.
static bool ball_hit_peg[MAX_BALLS];

// Written by the encoder ISR, consumed by core 0 between frames.
static volatile bool reset_stats_requested = false;

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
static const int8_t ball_half_width[9] = {
    0, 2, 3, 3, 4, 3, 3, 2, 0
};

static uint16_t DAC_data[SOUND_SAMPLES];
static uint16_t *address_pointer = DAC_data;
static int data_chan, ctrl_chan;

//physics frame countdown
static unsigned int physics_countdown = 0;

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

    // Let the animation thread reset statistics between frames.
    reset_stats_requested = true;


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
    int next = number_of_balls +
               (gpio_get(b_pin) ? 50 : -50);

    if (next < 1) next = 1;
    if (next > MAX_BALLS) next = MAX_BALLS;

    number_of_balls = next;
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

static void drawBall(int x, int y, char color) {
  for (int row = 0; row < 9; row++) {
        int half_width = ball_half_width[row];
        drawHLine(
            x - half_width,
            y + row - BALL_RADIUS,
            2 * half_width + 1,
            color
        );
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
  
  // DMA timer 0 at 25 kHz [180 MHz system clock × (1 / 7200)]
  // OPT: overclocking to 300Mhz
  dma_timer_set_fraction(0, 1, 10000);
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
static bool updateBallPhysics(Ball *ball, fix15 bounce)
{
    bool hit = false;

    const int n = PHYSICS_EVERY_N_FRAMES;

    // Advance N frames using the original position-then-gravity order.
    // Integer multiplication preserves the fixed-point scale.
    ball->x += ball->vx * n;
    ball->y += ball->vy * n +
              GRAVITY * ((n * (n - 1)) / 2);

    // Velocity entering the final frame's collision check.
    // The remaining gravity step is applied at the function's end.
    ball->vy += GRAVITY * (n - 1);

    for (int i = 0; i < NUM_PEGS; i++) {
        fix15 dx = ball->x - pegs[i].x;
        fix15 dy = ball->y - pegs[i].y;

        if (absfix15(dx) >= COLLISION_DISTANCE ||
            absfix15(dy) >= COLLISION_DISTANCE) {
            continue;
        }

        // OPT: alpha max + beta min algo
        fix15 abs_dx = abs(dx);
        fix15 abs_dy = abs(dy);
        fix15 maximum = abs_dx > abs_dy ? abs_dx : abs_dy;
        fix15 minimum = (abs_dx + abs_dy) - maximum;
        fix15 distance = multfix15(ALPHA, maximum) + multfix15(BETA, minimum);

        if (distance >= COLLISION_DISTANCE) {
            continue;
        }

        fix15 normal_x;
        fix15 normal_y;

        if (distance == 0) {
            normal_x = 0;
            normal_y = -int2fix15(1);
        } else {
            normal_x = divfix(dx, distance);
            normal_y = divfix(dy, distance);
        }

        fix15 dir = multfix15(ball->vx, normal_x) +
                    multfix15(ball->vy, normal_y);

        if (dir < 0) {
            fix15 impulse = -2 * dir;

            ball->vx += multfix15(normal_x, impulse);
            ball->vy += multfix15(normal_y, impulse);

            ball->vx = multfix15(ball->vx, bounce);
            ball->vy = multfix15(ball->vy, bounce);

            hit = true;
        }

        fix15 separation =
            int2fix15(BALL_RADIUS + PEG_RADIUS + 1);

        ball->x = pegs[i].x + multfix15(normal_x, separation);
        ball->y = pegs[i].y + multfix15(normal_y, separation);
        break;
    }

    // Core 0 will respawn exiting balls after both cores finish.
    if (ball->y <= int2fix15(BALL_EXIT_Y)) {
        ball->vy += GRAVITY;
    }

    return hit;
}

// Runs only on core 1.
static void core1_entry(void)
{
    while (true) {
        sem_acquire_blocking(&physics_start);

        for (int i = worker_begin; i < worker_end; i++) {
            ball_hit_peg[i] =
                updateBallPhysics(&balls[i], frame_bounce);
        }

        sem_release(&physics_done);
    }
}

// Runs only on core 0, after both cores finish physics.
static void finishBallUpdates(int count)
{
    bool play_sound = false;

    for (int i = 0; i < count; i++) {
        Ball *ball = &balls[i];

        if (ball_hit_peg[i]) {
            play_sound = true;
        }

        if (ball->y > int2fix15(BALL_EXIT_Y)) {
            int bin =
                (fix2int15(ball->x) - bottom_left_x +
                 PEG_HORIZONTAL_SPACING / 2) /
                PEG_HORIZONTAL_SPACING;

            if (bin < 0) {
                bin = 0;
            }
            if (bin >= NUM_BINS) {
                bin = NUM_BINS - 1;
            }

            bin_counts[bin]++;
            total_fallen++;

            // Keep rand() and spawning on core 0.
            spawnBall(&ball->x, &ball->y,
                      &ball->vx, &ball->vy);
        }
    }

    // Only core 0 touches the audio DMA.
    if (play_sound) {
        triggerSound();
    }
}

static PT_THREAD(protothread_draw_count(struct pt *pt))
{
    PT_BEGIN(pt);

    while (1) {
        PT_SEM_SDK_WAIT(pt, &draw_count_start);
        print_to_vga(animated_balls);
        PT_SEM_SDK_SIGNAL(pt, &draw_count_done);
    }

    PT_END(pt);
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
static PT_THREAD(protothread_anim(struct pt *pt))
{
    static int active_balls;
    static int requested_balls;

    PT_BEGIN(pt);

    active_balls = number_of_balls;
    animated_balls = active_balls;

    for (int i = 0; i < active_balls; i++) {
        spawnBall(&balls[i].x, &balls[i].y,
                  &balls[i].vx, &balls[i].vy);
    }

    while (1) {
        PT_YIELD_UNTIL(pt, draw_start_signal());

        clearLowFrame(0, BLACK);

        // Atomically consume the ISR's reset request.
        // Interrupts are disabled only for this short exchange.
        {
            uint32_t irq_state = save_and_disable_interrupts();
            bool reset_now = reset_stats_requested;
            reset_stats_requested = false;
            restore_interrupts(irq_state);

            if (reset_now) {
                total_fallen = 0;
                memset(bin_counts, 0, sizeof(bin_counts));
            }
        }

        requested_balls = number_of_balls;

        if (requested_balls > active_balls) {
            for (int i = active_balls;
                 i < requested_balls;
                 i++) {
                spawnBall(&balls[i].x, &balls[i].y,
                          &balls[i].vx, &balls[i].vy);
            }
        }

        active_balls = requested_balls;
        animated_balls = active_balls;

        if (physics_countdown == 0) {
          // Publish this update's settings before waking core 1.
          frame_bounce = (fix15)bounciness;

          worker_begin = active_balls / 2;
          worker_end = active_balls;

          sem_release(&physics_start);

          // Core 0 updates the first half concurrently.
          for (int i = 0; i < worker_begin; i++) {
              ball_hit_peg[i] =
                  updateBallPhysics(&balls[i], frame_bounce);
          }

          // Only wait when we actually submitted work.
          PT_SEM_SDK_WAIT(pt, &physics_done);

          // Process collision flags and exits once per physics update.
          finishBallUpdates(active_balls);

          physics_countdown = PHYSICS_EVERY_N_FRAMES - 1;
      } else {
           physics_countdown--;
      }

        for (int i = 0; i < active_balls; i++) {
            drawBall(
                fix2int15(balls[i].x),
                fix2int15(balls[i].y),
                ball_color);
        }

        drawPegs();

        PT_SEM_SDK_SIGNAL(pt, &draw_count_start);
        PT_SEM_SDK_WAIT(pt, &draw_count_done);
    }

    PT_END(pt);
}

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
  set_sys_clock_khz(250000, true) ; // 
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
  sem_init(&physics_start, 0, 1);
  sem_init(&physics_done, 0, 1);

  multicore_launch_core1(core1_entry);

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
