
/**
Lab 2, Milestone 1 - simple Dalton Board with 1 peg
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
// avoiding negative displacement - multiplication instead of left-shifting negative integers
#define divfix(a,b) ((fix15)div_s64s64((long long)(a) * 32768LL, (long long)(b)))

// Wall detection
#define hitBottom(b) (b>int2fix15(380 - BALL_RADIUS))
#define hitTop(b) (b<int2fix15(100 + BALL_RADIUS))
#define hitLeft(a) (a<int2fix15(100 + BALL_RADIUS))
#define hitRight(a) (a>int2fix15(540 - BALL_RADIUS))

// uS per frame
#define FRAME_RATE 33000 // not being used..?

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
static const fix15 peg_y = int2fix15(135);

// create a ball
void spawnBall(fix15* x, fix15* y, fix15* vx, fix15* vy)
{
  // start in top of screen
  *x = int2fix15(320) ;
  *y = int2fix15(100 + BALL_RADIUS) ;
  // small, randomized horizontal velocity: -1 to +1 pixels/frame
  *vx = float2fix15(2.0f * ((float)rand() / RAND_MAX) - 1.0f) ;
  // zero vertical velocity
  *vy = int2fix15(0) ;
}

// Draw the boundaries
void drawArena() {
  drawVLine(100, 100, 280, WHITE) ;
  drawVLine(540, 100, 280, WHITE) ;
  drawHLine(100, 100, 440, WHITE) ;
  drawHLine(100, 380, 440, WHITE) ;
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
      fix15 new_x, new_y;
      if (distance == 0) {
        new_x = 0;
        new_y = -int2fix15(1);
      }
      else {
        new_x = divfix(dx, distance);
        new_y = divfix(dy, distance);
      }
      
      // calculate direction of ball movement in relation to the peg
      // <0: moving towards peg, >0: moving away, =0: tangent to surface
      fix15 dir = multfix15(*vx, new_x) + multfix15(*vy, new_y); 
      if (dir < 0) { // reverse the part of velocity pointing into peg + preserve sideways motion
        fix15 impulse = -2 * dir;
        *vx += multfix15(new_x, impulse);
        *vy += multfix15(new_y, impulse);

        // bounciness!
        *vx = multfix15(*vx, BOUNCINESS);
        *vy = multfix15(*vy, BOUNCINESS);
      }
      // reposition ball outside of the peg, even if moving away 
      fix15 separation = int2fix15(BALL_RADIUS + PEG_RADIUS + 1);
      *x = peg_x + multfix15(new_x, separation);
      *y = peg_y + multfix15(new_y, separation);
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
     // NEVER exit while
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

  // initialize random seed generator
  srand((unsigned int)time(NULL));

  // add threads
  pt_add_thread(protothread_serial);
  pt_add_thread(protothread_anim);

  // start scheduler
  pt_schedule_start ;
} 
