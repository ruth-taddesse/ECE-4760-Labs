
/**
Lab 2 - merged optimized 16-row Galton board
 */

// Include the VGA grahics library
#include "VGA/vga16_graphics_v3.h"
#include "galton_background.h"
_Static_assert(sizeof(galton_background) == VGA_FRAME_BYTES, "Background format mismatch");
// Include standard libraries
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <time.h>
// Include Pico libraries
#include "pico/stdlib.h"
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
#include "hardware/vreg.h"

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
#define fix2float15(a) ((float)(a) * 0.000030517578125f)
#define absfix15(a) abs(a) 
#define int2fix15(a) ((fix15)(a << 15))
#define fix2int15(a) ((int)(a >> 15))
#define char2fix15(a) (fix15)(((fix15)(a)) << 15)

// ball-related constants
#define BALL_SPAWN_Y 20
#define BALL_SPAWN_X_JITTER 1.0f // maximum horizontal spawn offset, in pixels
// Respawn once the ball center clears the bottom row's collision region.
#define BALL_EXIT_Y (FIRST_PEG_Y + (PEG_ROWS - 1) * PEG_VERTICAL_SPACING + PEG_RADIUS + BALL_RADIUS + 1)

// rotary encoder pins
#define state_pin 13
#define a_pin 14
#define b_pin 15

#define MAX_BALLS 44500
#define INITIAL_BALLS 20000

// histogram location + layout constants
#define HISTOGRAM_BASELINE 458
#define BIN_COUNT_Y (HISTOGRAM_BASELINE + 2)
#define HISTOGRAM_HEIGHT 90
#define BAR_WIDTH 20       // width of each bar in pixels
#define BAR_SPACING PEG_HORIZONTAL_SPACING // spacing between adjacent bins matches row 16 pegs

// lab-defined constants for ball / peg physics
#define BALL_RADIUS 1
#define PEG_RADIUS 6
#define COLLISION_DISTANCE int2fix15(BALL_RADIUS + PEG_RADIUS)
#define GRAVITY float2fix15(0.37)

// 16-row Galton board geometry: row n contains n + 1 pegs
#define PEG_ROWS 16
#define NUM_PEGS ((PEG_ROWS * (PEG_ROWS + 1)) >> 1)
#define PEG_HORIZONTAL_SPACING 38
#define PEG_VERTICAL_SPACING 19
#define FIRST_PEG_X 320
#define FIRST_PEG_Y 75
#define NUM_BINS (PEG_ROWS + 1)
#define BOTTOM_ROW_START ((PEG_ROWS * (PEG_ROWS - 1)) >> 1)

#define MIN_BOUNCE float2fix15(0.1f)
#define MAX_BOUNCE float2fix15(1.5f)
#define BOUNCE_STEP float2fix15(0.1f)

#define ALPHA float2fix15(0.96043387f)
#define BETA float2fix15(0.397824734f)

// audio playback configuration + DMA storage
#define SOUND_SAMPLES 1000
#define SAMPLE_RATE 25000

// 1 = physics every frame, 2 = every other frame, etc.
#define PHYSICS_EVERY_N_FRAMES 1

// Compute invariant physics values once, outside the per-ball updates.
static const fix15 spawn_x = int2fix15(FIRST_PEG_X);
static const fix15 spawn_y = int2fix15(BALL_SPAWN_Y);
static const fix15 exit_y = int2fix15(BALL_EXIT_Y);
static const fix15 collision_separation = int2fix15(BALL_RADIUS + PEG_RADIUS + 1);
static const fix15 fallback_normal_y = -int2fix15(1);
static const fix15 gravity_position_step =
    GRAVITY * ((PHYSICS_EVERY_N_FRAMES * (PHYSICS_EVERY_N_FRAMES - 1)) >> 1);
static const fix15 gravity_velocity_step = GRAVITY * (PHYSICS_EVERY_N_FRAMES - 1);
static const unsigned int physics_countdown_reset = PHYSICS_EVERY_N_FRAMES - 1;
static const fix15 first_peg_y = int2fix15(FIRST_PEG_Y);
static const fix15 peg_row_spacing = int2fix15(PEG_VERTICAL_SPACING);
static const fix15 peg_column_spacing = int2fix15(PEG_HORIZONTAL_SPACING);

// These divisions have constant operands and are evaluated at compile time.
// The integer reciprocal is rounded down; one remainder correction makes the
// quotient exact for every uint32_t input (divisor must be greater than one).
#define U32_RECIPROCAL(divisor) ((uint32_t)(UINT64_C(0x100000000) / (divisor)))
static const uint32_t row_reciprocal = U32_RECIPROCAL(PEG_VERTICAL_SPACING << 15);
static const uint32_t column_reciprocal = U32_RECIPROCAL(PEG_HORIZONTAL_SPACING << 15);
static const uint32_t bin_reciprocal = U32_RECIPROCAL(PEG_HORIZONTAL_SPACING);
static const float random_reciprocal = 1.0f / (float)RAND_MAX;
static const float envelope_reciprocal = 1.0f / (SOUND_SAMPLES - 1);
static const float audio_phase_step = 2.0f * 3.14159265f * 500.0f / SAMPLE_RATE;

static inline uint32_t quotient_constant(uint32_t value, uint32_t divisor,
                                         uint32_t reciprocal)
{
    uint32_t quotient = (uint32_t)(((uint64_t)value * reciprocal) >> 32);
    return quotient + (value - quotient * divisor >= divisor);
}

// Positive uint32_t inputs only. Seed the reciprocal from IEEE-754 float bits,
// then refine with Newton's method using the Pico 2's single-precision FPU.
// memcpy avoids aliasing violations. No floating-point divide is required.
static inline float reciprocal_positive(uint32_t divisor)
{
    float value = (float)divisor;
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    bits = UINT32_C(0x7ef311c3) - bits;
    float inverse;
    memcpy(&inverse, &bits, sizeof(inverse));
    inverse *= 2.0f - value * inverse;
    inverse *= 2.0f - value * inverse;
    inverse *= 2.0f - value * inverse;
    return inverse;
}

// Used for collision normals and histogram heights: their quotients are small.
// Correct the float estimate with integer products to preserve exact truncation.
static inline uint32_t quotient_small(uint64_t numerator, uint32_t divisor,
                                      float reciprocal)
{
    uint32_t quotient = (uint32_t)((float)numerator * reciprocal);
    uint64_t product = (uint64_t)quotient * divisor;
    while (product > numerator) {
        --quotient;
        product -= divisor;
    }
    while (numerator - product >= divisor) {
        ++quotient;
        product += divisor;
    }
    return quotient;
}

// Collision components are bounded by the 10-pixel collision box. After
// three reciprocal refinements, the estimated Q15 normal is within one unit.
// Convert the 32-bit component before scaling, avoiding uint64-to-float helpers.
static inline fix15 collision_normal(fix15 component, uint32_t distance,
                                      float scaled_reciprocal)
{
    uint32_t magnitude = (uint32_t)absfix15(component);
    uint32_t normal = (uint32_t)((float)magnitude * scaled_reciprocal);
    uint64_t numerator = (uint64_t)magnitude << 15;
    uint64_t product = (uint64_t)normal * distance;
    if (product > numerator) {
        --normal;
    } else if (numerator - product >= distance) {
        ++normal;
    }
    return component < 0 ? -(fix15)normal : (fix15)normal;
}

typedef struct {
  int16_t x; // signed fixed point: 5 fractional bits (1/32 pixel)
  int16_t y;
  int16_t vx; // signed fixed point: 10 fractional bits (1/1024 pixel per frame)
  int16_t vy;
} Ball;
_Static_assert(sizeof(Ball) == 8, "Ball storage must remain 8 bytes");
_Static_assert(BALL_EXIT_Y * 32 + 1 < INT16_MAX, "Exit marker must fit Q5");

// Q15 <-> signed Q5 positions. INT16_MIN in y is reserved for an escaped ball
// that core 0 must respawn without adding a false bottom-exit histogram count.
#define POSITION_ESCAPE INT16_MIN
static inline int16_t store_position(fix15 position)
{
    if (position >= INT16_MAX * 1024) return INT16_MAX;
    if (position <= INT16_MIN * 1024) return INT16_MIN;
    if (position >= 0) return (int16_t)((position + 512) >> 10);
    return (int16_t)(-((-position + 512) >> 10));
}

static inline fix15 load_position(int16_t position)
{
    return (fix15)position * 1024;
}

static inline int position_pixel(int16_t position)
{
    return fix2int15(load_position(position));
}

// Physics calculations stay Q15; stored positions use Q5 and velocities Q10.
// Round to nearest, with ties away from zero. Saturate instead of wrapping if
// energetic collisions exceed [-32, 31.9990234375] pixels per frame.
static inline int16_t store_velocity(fix15 velocity)
{
    if (velocity >= INT16_MAX * 32) return INT16_MAX;
    if (velocity <= INT16_MIN * 32) return INT16_MIN;
    if (velocity >= 0) return (int16_t)((velocity + 16) >> 5);
    return (int16_t)(-((-velocity + 16) >> 5));
}

static inline fix15 load_velocity(int16_t velocity)
{
    // Multiplication is defined for negative values; a signed left shift isn't.
    return (fix15)velocity * 32;
}

typedef struct {
  fix15 x;
  fix15 y;
  int pixel_x;
  int pixel_y;
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

// Core 1 publishes one aggregate hit flag before signaling physics_done.
// Core 0 keeps its own aggregate in the animation thread.
static bool worker_hit_peg;

// Written by the encoder ISR, consumed by core 0 between frames.
static volatile bool reset_stats_requested = false;

// initialize balls, pegs, and other global variables
static Ball balls[MAX_BALLS];
static uint32_t bin_counts[NUM_BINS];
// Counts only increase between resets, so maintain the maximum on each update.
// Core 0 owns these values and finishes updates before the drawing thread runs.
static uint32_t histogram_max_count = 1;
static Peg pegs[NUM_PEGS];
static int peg_row_start[PEG_ROWS];
static fix15 peg_row_left_x[PEG_ROWS];
static int bin_position_offset;
static int bin_pixel_x[NUM_BINS];
char color = WHITE ;
char text[64];
volatile int number_of_balls = INITIAL_BALLS;
volatile int animated_balls = INITIAL_BALLS;
volatile uint32_t total_fallen = 0;
volatile bool state = true; // true = adjusting # of balls, false = adjusting bounciness
volatile uint32_t bounciness = float2fix15(0.5);
// VGA stores four 2-bit palette indices per byte, leftmost in the low bits.
extern char * volatile current_draw_buffer;

static uint16_t DAC_data[SOUND_SAMPLES];
static uint16_t *address_pointer = DAC_data;
static int data_chan, ctrl_chan;
static uint32_t ctrl_channel_mask;

//physics frame countdown
static unsigned int physics_countdown = 0;

#if GALTON_TIMING_DISPLAY
static void draw_timing_overlay(void)
{
    static bool initialized;
    static VgaTimingSnapshot previous;
    static char lines[4][40] = {
        "Presented FPS: --",
        "Repeated frames/s: --",
        "Frame work avg: -- ms",
        "Worst (interval): -- ms"
    };
    const uint32_t now_us = time_us_32();
    if (!initialized) {
        vga_timing_snapshot(&previous);
        initialized = true;
    } else if ((uint32_t)(now_us - previous.sampled_us) >= 1000000u) {
        VgaTimingSnapshot current;
        vga_timing_snapshot(&current);
        const uint32_t elapsed_us = current.sampled_us - previous.sampled_us;
        const uint32_t presented = current.presented - previous.presented;
        const uint32_t repeated = current.repeated - previous.repeated;
        const uint32_t completed = current.completed - previous.completed;
        // Formatting and rate calculations run only once per second. Scale by
        // actual elapsed time so a slow renderer still reports accurate rates.
        const uint32_t fps10 = (uint32_t)((uint64_t)presented * 10000000u / elapsed_us);
        const uint32_t repeats10 = (uint32_t)((uint64_t)repeated * 10000000u / elapsed_us);
        const uint32_t average_us = completed ?
            (uint32_t)((current.total_work_us - previous.total_work_us) / completed) : 0;
        const uint32_t average_ms10 = (average_us + 50u) / 100u;
        const uint32_t worst_ms10 = (current.worst_work_us + 50u) / 100u;
        snprintf(lines[0], sizeof(lines[0]), "Presented FPS: %lu.%lu",
                 (unsigned long)(fps10 / 10), (unsigned long)(fps10 % 10));
        snprintf(lines[1], sizeof(lines[1]), "Repeated frames/s: %lu.%lu",
                 (unsigned long)(repeats10 / 10), (unsigned long)(repeats10 % 10));
        snprintf(lines[2], sizeof(lines[2]), "Frame work avg: %lu.%lu ms",
                 (unsigned long)(average_ms10 / 10), (unsigned long)(average_ms10 % 10));
        snprintf(lines[3], sizeof(lines[3]), "Worst (interval): %lu.%lu ms",
                 (unsigned long)(worst_ms10 / 10), (unsigned long)(worst_ms10 % 10));
        previous = current;
    }

    // The back buffer is restored every frame, so cached text must be redrawn.
    // This sits below the existing stats and left of the upper peg rows.
    fillRect(8, 63, 250, 42, BLACK);
    setTextColor2(WHITE, BLACK);
    setTextSize(1);
    for (int i = 0; i < 4; ++i) {
        setCursor(10, 65 + 10 * i);
        writeString(lines[i]);
    }
}
#endif

void print_to_vga(int num_balls){
  // Advance whole seconds without the SDK's 64-bit microsecond division.
  // Preserve fractional elapsed time; normally this loop runs zero or one time.
  static uint64_t last_second_us = 0;
  static uint32_t seconds = 0;
  const uint64_t now_us = time_us_64();
  while (now_us - last_second_us >= UINT64_C(1000000)) {
    last_second_us += UINT64_C(1000000);
    ++seconds;
  }

  // draw stats
  setTextColor2(WHITE, BLACK);
  setTextSize(1);

  // Static labels come from flash; only reformat numeric values when changed.
  // Still redraw values each frame because DMA restores the blank value fields.
  static char values[4][24];
  static bool values_valid;
  static int previous_balls;
  static uint32_t previous_fallen, previous_bounce, previous_seconds;
  const uint32_t fallen = total_fallen;
  const uint32_t bounce = bounciness;
  if (!values_valid || previous_balls != num_balls) {
    snprintf(values[0], sizeof(values[0]), "%d", num_balls);
    previous_balls = num_balls;
  }
  if (!values_valid || previous_fallen != fallen) {
    snprintf(values[1], sizeof(values[1]), "%lu", (unsigned long)fallen);
    previous_fallen = fallen;
  }
  if (!values_valid || previous_bounce != bounce) {
    snprintf(values[2], sizeof(values[2]), "%.2f", fix2float15(bounce));
    previous_bounce = bounce;
  }
  if (!values_valid || previous_seconds != seconds) {
    snprintf(values[3], sizeof(values[3]), "%lus", (unsigned long)seconds);
    previous_seconds = seconds;
  }
  values_valid = true;
  for (int i = 0; i < 4; ++i) {
    setCursor(background_value_x[i], 20 + 10 * i);
    writeString(values[i]);
  }

  // Reuse the scale until the maintained maximum changes.
  static uint32_t scaled_max_count = 1;
  static float histogram_reciprocal = 1.0f;
  if (scaled_max_count != histogram_max_count) {
    histogram_reciprocal = reciprocal_positive(histogram_max_count);
    scaled_max_count = histogram_max_count;
  }

  // DMA restores the background, so redraw compact counts every frame.
  for (int i = 0; i < NUM_BINS; i++) {
    int height = (int)quotient_small((uint64_t)bin_counts[i] * HISTOGRAM_HEIGHT,
                                     histogram_max_count, histogram_reciprocal);

    if (height > 0) {
      drawRect(bin_pixel_x[i],
            HISTOGRAM_BASELINE - height,
            BAR_WIDTH,
            height,
            CYAN);
    }
    // displaying count under histogram cleanly
    char count_text[8];
    uint32_t count = bin_counts[i];
    if (count < 1000u) {
      snprintf(count_text, sizeof(count_text), "%lu", (unsigned long)count);
    } else if (count < 1000000u) {
      snprintf(count_text, sizeof(count_text), "%luk",
               (unsigned long)(count / 1000u));
    } else {
      snprintf(count_text, sizeof(count_text), "%luM",
               (unsigned long)(count / 1000000u));
    }
    int text_width = 6 * (int)strlen(count_text);
    setCursor(bin_pixel_x[i] + (BAR_WIDTH >> 1) - (text_width >> 1), BIN_COUNT_Y);
    writeString(count_text);
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
               (gpio_get(b_pin) ? 500 : -500);

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
                      ((row * PEG_HORIZONTAL_SPACING) >> 1);
    int row_y = FIRST_PEG_Y + row * PEG_VERTICAL_SPACING;
    const fix15 fixed_row_y = int2fix15(row_y);
    peg_row_start[row] = peg_index;
    peg_row_left_x[row] = int2fix15(row_start_x);

    for (int column = 0; column <= row; column++) {
      int pixel_x = row_start_x + column * PEG_HORIZONTAL_SPACING;
      pegs[peg_index].x = int2fix15(pixel_x);
      pegs[peg_index].y = fixed_row_y;
      pegs[peg_index].pixel_x = pixel_x;
      pegs[peg_index].pixel_y = row_y;
      peg_index++;
    }
  }
}

// Visual-only 2x2 sprite spanning (x-1, y-1) through (x, y).
// BALL_RADIUS still controls collision physics independently of sprite size.
// Only core 0 renders, so overlapping balls cannot race on shared pixel bytes.
static void drawBall(int x, int y, char color) {
    if (x < 0 || x > 640 || y < 0 || y > 480) return;
    uint8_t *buffer = (uint8_t *)current_draw_buffer;
    const uint8_t pixel = vga_color_index(color);
    const uint8_t packed = pixel * 0x55u;
    const int left = x > 0 ? x - 1 : 0;
    const int right = x < 640 ? x : 639;
    const int top = y > 0 ? y - 1 : 0;
    const int bottom = y < 480 ? y : 479;
    const unsigned mask = ((1u << (2 * (right - left + 1))) - 1u)
                          << (2 * (left & 3));
    const uint8_t first_mask = (uint8_t)mask;
    const uint8_t second_mask = (uint8_t)(mask >> 8);
    for (int py = top; py <= bottom; ++py) {
        uint8_t *dest = buffer + VGA_ROW_BYTES * py + (left >> 2);
        dest[0] = (dest[0] & ~first_mask) | (packed & first_mask);
        // A pair starting at the last pixel in a byte crosses into the next.
        if (second_mask) {
            dest[1] = (dest[1] & ~second_mask) | (packed & second_mask);
        }
    }
}

void drawPegs(void)
{
  for (int i = 0; i < NUM_PEGS; i++) {
    fillCircle(pegs[i].pixel_x,
               pegs[i].pixel_y,
               PEG_RADIUS,
               peg_color);
  }
}

// create a ball
void spawnBall(Ball *ball)
{
  // Independent small position offset for both initial drops and respawns.
  fix15 spawn_offset = float2fix15(BALL_SPAWN_X_JITTER *
      (2.0f * ((float)rand() * random_reciprocal) - 1.0f));
  ball->x = store_position(spawn_x + spawn_offset);
  ball->y = store_position(spawn_y);
  // Small randomized horizontal velocity, approximately -0.125 to +0.125.
  // Q5 positions discard motion below half a position unit each update.
  // Keep at least one unit of sideways motion to avoid dropping exactly onto
  // the first peg's center and remaining in a vertical bouncing orbit.
  fix15 initial_vx = float2fix15(
      0.25f * ((float)rand() * random_reciprocal) - 0.125f);
  const fix15 minimum_spawn_vx = 1 << (15 - 5); // 1/32 pixel per frame
  if (absfix15(initial_vx) < minimum_spawn_vx) {
    initial_vx = initial_vx < 0 ? -minimum_spawn_vx : minimum_spawn_vx;
  }
  ball->vx = store_velocity(initial_vx);
  ball->vy = 0;
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
    float remaining = 1.0f - (float)i * envelope_reciprocal;
    float envelope = remaining * remaining;
    float phase = audio_phase_step * i;

    int sample = (int)(2048 + 1500 * envelope * sinf(phase));
    DAC_data[i] = DAC_config_chan_A | (sample & 0x0fff);
  }
  
  data_chan = dma_claim_unused_channel(true);
  ctrl_chan = dma_claim_unused_channel(true);
  ctrl_channel_mask = 1u << ctrl_chan;

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
  
  // DMA timer 0 at 25 kHz: 300 MHz system clock / 12000.
  dma_timer_set_fraction(0, 1, 12000);
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
    dma_start_channel_mask(ctrl_channel_mask);
}

// update velocity and position of ball
static bool updateBallPhysics(Ball *ball, fix15 bounce)
{
    bool hit = false;
    fix15 x = load_position(ball->x);
    fix15 y = load_position(ball->y);
    fix15 vx = load_velocity(ball->vx);
    fix15 vy = load_velocity(ball->vy);

    // Advance N frames using the original position-then-gravity order.
    // Integer multiplication preserves the fixed-point scale.
    x += vx * PHYSICS_EVERY_N_FRAMES;
    y += vy * PHYSICS_EVERY_N_FRAMES + gravity_position_step;

    // Velocity entering the final frame's collision check.
    // The remaining gravity step is applied at the function's end.
    vy += gravity_velocity_step;

    // Find rows intersecting the collision box. Clamp negative coordinates
    // before using unsigned reciprocal multiplication for exact grid indices.
    const fix15 row_low = y - COLLISION_DISTANCE - first_peg_y;
    const fix15 row_high = y + COLLISION_DISTANCE - first_peg_y;
    if (row_high < 0) {
        goto collision_done;
    }
    const int first_row = row_low > 0 ?
        (int)quotient_constant((uint32_t)(row_low + peg_row_spacing - 1),
                               peg_row_spacing, row_reciprocal) : 0;
    int last_row = (int)quotient_constant((uint32_t)row_high, peg_row_spacing, row_reciprocal);
    if (last_row >= PEG_ROWS) last_row = PEG_ROWS - 1;

    for (int row = first_row; row <= last_row; row++) {
      const fix15 column_low = x - COLLISION_DISTANCE - peg_row_left_x[row];
      const fix15 column_high = x + COLLISION_DISTANCE - peg_row_left_x[row];
      if (column_high < 0) continue;

      const int first_column = column_low > 0 ?
          (int)quotient_constant((uint32_t)(column_low + peg_column_spacing - 1),
                                 peg_column_spacing, column_reciprocal) : 0;
      int last_column = (int)quotient_constant((uint32_t)column_high, peg_column_spacing, column_reciprocal);
      if (last_column > row) last_column = row;

      for (int column = first_column; column <= last_column; column++) {
        const int i = peg_row_start[row] + column;
        fix15 dx = x - pegs[i].x;
        fix15 dy = y - pegs[i].y;

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
            normal_y = fallback_normal_y;
        } else {
            const float inverse_distance =
                reciprocal_positive((uint32_t)distance) * 32768.0f;
            normal_x = collision_normal(dx, (uint32_t)distance, inverse_distance);
            normal_y = collision_normal(dy, (uint32_t)distance, inverse_distance);
        }

        fix15 dir = multfix15(vx, normal_x) +
                    multfix15(vy, normal_y);

        if (dir < 0) {
            fix15 impulse = -2 * dir;

            vx += multfix15(normal_x, impulse);
            vy += multfix15(normal_y, impulse);

            vx = multfix15(vx, bounce);
            vy = multfix15(vy, bounce);

            hit = true;
        }

        x = pegs[i].x + multfix15(normal_x, collision_separation);
        y = pegs[i].y + multfix15(normal_y, collision_separation);
        // Preserve the original first-collision-only behavior across both loops.
        goto collision_done;
      }
    }

collision_done:
    // Core 0 will respawn exiting balls after both cores finish.
    if (y <= exit_y) {
        vy += GRAVITY;
    }

    if (y > exit_y) {
        // Preserve bottom-exit detection even if rounding returns y to the boundary.
        // Clamping an offscreen x still selects the same outer histogram bin.
        ball->x = store_position(x);
        ball->y = (int16_t)(BALL_EXIT_Y * 32 + 1);
    } else if (x < INT16_MIN * 1024 || x > INT16_MAX * 1024 ||
               y <= INT16_MIN * 1024) {
        ball->x = 0;
        ball->y = POSITION_ESCAPE;
    } else {
        ball->x = store_position(x);
        ball->y = store_position(y);
    }
    ball->vx = store_velocity(vx);
    ball->vy = store_velocity(vy);
    return hit;
}

// Runs only on core 1.
static void core1_entry(void)
{
    while (true) {
        sem_acquire_blocking(&physics_start);

        bool hit = false;
        for (int i = worker_begin; i < worker_end; i++) {
            // Bitwise OR evaluates every update, even after a previous hit.
            hit |= updateBallPhysics(&balls[i], frame_bounce);
        }
        worker_hit_peg = hit;
        sem_release(&physics_done);
    }
}

// Runs on core 0, only for a range whose physics has finished.
static void finishBallUpdates(int begin, int end)
{

    for (int i = begin; i < end; i++) {
        Ball *ball = &balls[i];

        if (ball->y == POSITION_ESCAPE) {
            // An energetic ball left the representable area; don't wrap or
            // pin it at an edge, and don't count it as a bottom exit.
            spawnBall(ball);
            continue;
        }
        if (ball->y > BALL_EXIT_Y * 32) {
            const int bin_position = position_pixel(ball->x) + bin_position_offset;
            int bin = bin_position > 0 ?
                (int)quotient_constant((uint32_t)bin_position,
                                      PEG_HORIZONTAL_SPACING, bin_reciprocal) : 0;

            if (bin < 0) {
                bin = 0;
            }
            if (bin >= NUM_BINS) {
                bin = NUM_BINS - 1;
            }

            bin_counts[bin]++;
            if (bin_counts[bin] > histogram_max_count) {
                histogram_max_count = bin_counts[bin];
            } else if (bin_counts[bin] == 0) {
                // A uint32_t counter wrapped. Only this rare decrease requires
                // rescanning; ordinary increments maintain the maximum in O(1).
                histogram_max_count = 1;
                for (int i = 0; i < NUM_BINS; i++) {
                    if (bin_counts[i] > histogram_max_count) {
                        histogram_max_count = bin_counts[i];
                    }
                }
            }
            total_fallen++;

            // Keep rand() and spawning on core 0.
            spawnBall(ball);
        }
    }

}

static PT_THREAD(protothread_draw_count(struct pt *pt))
{
    PT_BEGIN(pt);

    while (1) {
        PT_SEM_SDK_WAIT(pt, &draw_count_start);
        print_to_vga(animated_balls);
#if GALTON_TIMING_DISPLAY
        draw_timing_overlay();
#endif
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
        sprintf(pt_serial_out_buffer, "ball color: 1=white, 2=magenta, 3=cyan: ");
        // non-blocking write
        serial_write ;
        // spawn a thread to do the non-blocking serial read
        serial_read ;
        // convert input string to number
        sscanf(pt_serial_in_buffer,"%d", &user_input) ;
        // update ball color
        if (user_input == 1) ball_color = WHITE;
        else if (user_input == 2) ball_color = MAGENTA;
        else if (user_input == 3) ball_color = CYAN;
      } // END WHILE(1)
  PT_END(pt);
} // timer thread

// Animation on main core
static PT_THREAD(protothread_anim(struct pt *pt))
{
    static int active_balls;
    static int requested_balls;
    static int draw_begin;
    static bool play_sound;

    PT_BEGIN(pt);

    active_balls = number_of_balls;
    animated_balls = active_balls;

    for (int i = 0; i < active_balls; i++) {
        spawnBall(&balls[i]);
    }

    while (1) {
        PT_YIELD_UNTIL(pt, draw_start_signal());


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
                histogram_max_count = 1;
            }
        }

        requested_balls = number_of_balls;

        if (requested_balls > active_balls) {
            for (int i = active_balls;
                 i < requested_balls;
                 i++) {
                spawnBall(&balls[i]);
            }
        }

        active_balls = requested_balls;
        animated_balls = active_balls;

        draw_begin = 0;
        if (physics_countdown == 0) {
          // Publish this update's settings before waking core 1.
          frame_bounce = (fix15)bounciness;

          // Give core 1 more physics while core 0 also clears and renders.
          worker_begin = active_balls >> 2;
          worker_end = active_balls;

          sem_release(&physics_start);

          // Background DMA completed before draw_start_signal().

          // Reset the aggregate for each physics job, including empty ranges.
          play_sound = false;
          for (int i = 0; i < worker_begin; i++) {
              play_sound |= updateBallPhysics(&balls[i], frame_bounce);
          }

          // Draw only our completed range while core 1 updates its own range.
          finishBallUpdates(0, worker_begin);
          for (int i = 0; i < worker_begin; ++i) {
              drawBall(position_pixel(balls[i].x), position_pixel(balls[i].y), ball_color);
          }

          // Do not read core 1's positions until its semaphore is released.
          PT_SEM_SDK_WAIT(pt, &physics_done);

          play_sound |= worker_hit_peg;
          finishBallUpdates(worker_begin, active_balls);
          if (play_sound) triggerSound();
          draw_begin = worker_begin;

          physics_countdown = physics_countdown_reset;
      } else {
           physics_countdown--;
           // Background DMA completed before draw_start_signal().
      }

        for (int i = draw_begin; i < active_balls; i++) {
            drawBall(
                position_pixel(balls[i].x),
                position_pixel(balls[i].y),
                ball_color);
        }

        // Pegs and fixed labels are in the DMA-restored background.

        PT_SEM_SDK_SIGNAL(pt, &draw_count_start);
        PT_SEM_SDK_WAIT(pt, &draw_count_done);
        vga_frame_done(); // Publish only after both drawing threads finish.
    }

    PT_END(pt);
}

// ========================================
// === main
// ========================================
// USE ONLY C-sdk library
int main(){
    vreg_set_voltage(VREG_VOLTAGE_1_30);
    sleep_ms(10);
    set_sys_clock_khz(300000, true) ; // 
    // initialize stio
    stdio_init_all() ;

    // initialize VGA + audio
    initVGAWithBackground(galton_background);
    initAudio();

    // Generate the fixed peg positions once at startup.
    generatePegs();
    bin_position_offset = (PEG_HORIZONTAL_SPACING >> 1) - pegs[BOTTOM_ROW_START].pixel_x;
    for (int i = 0; i < NUM_BINS; i++) {
        bin_pixel_x[i] = 10 + i * BAR_SPACING;
  }

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
