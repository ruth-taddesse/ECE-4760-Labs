/* Pico Sports collection entry point. VGA derived from Hunter Adams' IMU demo.
 * Newline commands: INFO, CAL, START id sport category pre_ms motion_ms post_ms countdown_ms,
 * STOP, DUMP. Host owns randomized experiments; Pico owns sample timing.
 */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/clocks.h"
#include "imu.h"
#include "sports.h"
#include "VGA/vga16_graphics_v3.h"
#define CAPACITY 1000
#define PERIOD_US 10000u
#define LEFT 52
#define WIDTH 530

typedef struct { uint64_t pico_us; uint32_t elapsed_us, seq; ImuReading value; } Sample;
enum { LIVE, COUNTDOWN, RECORDING, DONE, SENSOR_ERROR, CALIBRATING };
typedef struct {
    Sample samples[CAPACITY];
    unsigned count, head, errors, missed, clips, pre_ms, motion_ms, post_ms;
    unsigned countdown, phase;
    int state;
    char trial[49];
    const Sport *sport;
    unsigned category;
    float peak_a, peak_g;
    bool stopped;
} Capture;
static Capture capture, screen;
static mutex_t data_lock;
static uint64_t start_us, countdown_end;
static bool healthy, calibrated;
static ImuBias bias;
static const Sport *sports[] = {&bowling, &basketball, &cup_pong, &cup_pong_aim};
static const char *phases[] = {"PRE: HOLD STILL", "MOTION: THROW NOW", "POST: HOLD STILL"};
static float mag(const float v[3]) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static unsigned duration_ms(const Capture *c) { return c->pre_ms+c->motion_ms+c->post_ms; }
static unsigned phase_at(const Capture *c, uint32_t us) {
    return us < c->pre_ms*1000u ? 0 : us < (c->pre_ms+c->motion_ms)*1000u ? 1 : 2;
}
static void text_at(int x,int y,char color,const char *text) {
    drawTextGLCD(x,y,(char *)text,color,BLACK);
}
static int plot_y(float v,float low,float high,int top) {
    return top+90-(int)((fmaxf(low,fminf(high,v))-low)*90/(high-low));
}
static void display_core(void) {
    const char *states[] = {"LIVE", "GET READY", "RECORDING", "CAPTURE COMPLETE", "IMU ERROR: check wiring/reboot", "CALIBRATING: +Z UP, STILL"};
    char line[105];
    while (true) {
        if (!draw_start_signal()) { tight_loop_contents(); continue; }
        mutex_enter_blocking(&data_lock); memcpy(&screen,&capture,sizeof(screen)); mutex_exit(&data_lock);
        clearLowFrame(0,BLACK);
        snprintf(line,sizeof(line),"%s  Countdown %u  Samples %u",states[screen.state],screen.countdown,screen.count);
        text_at(10,8,YELLOW,line);
        snprintf(line,sizeof(line),"%s / %s  %s",screen.sport->name,
                 screen.sport->categories[screen.category],screen.state==RECORDING ? phases[screen.phase] : "");
        text_at(10,25,YELLOW,line);
        text_at(52,43,WHITE,"X white"); text_at(160,43,RED,"Y red"); text_at(260,43,GREEN,"Z green");
        text_at(52,60,CYAN,"Raw acceleration (m/s^2, gravity retained), +/-78.45");
        text_at(52,176,CYAN,"Raw angular velocity (rad/s), +/-34.91");
        text_at(52,292,YELLOW,"Magnitudes: accel yellow 0..136 m/s^2; gyro cyan 0..61 rad/s");
        unsigned duration = duration_ms(&screen);
        for (int p=0;p<3;p++) {
            int top=76+p*116;
            drawRect(LEFT,top,WIDTH+1,91,DARK_GREEN);
            if (p<2) drawHLine(LEFT,top+45,WIDTH,DARK_GREEN);
            if (screen.state!=LIVE) {
                drawVLine(LEFT+screen.pre_ms*(WIDTH-1)/duration,top,91,DARK_BLUE);
                drawVLine(LEFT+(screen.pre_ms+screen.motion_ms)*(WIDTH-1)/duration,top,91,DARK_BLUE);
            }
        }
        unsigned first = screen.count==CAPACITY ? screen.head : 0;
        uint64_t base = screen.count ? screen.samples[first].pico_us : 0;
        const char colors[] = {WHITE,RED,GREEN};
        for (unsigned i=0;i<screen.count;i++) {
            Sample *s=&screen.samples[(first+i)%CAPACITY];
            uint64_t elapsed = screen.state==LIVE ? s->pico_us-base : s->elapsed_us;
            uint64_t span = screen.state==LIVE ? 10000000ull : duration*1000ull;
            int x=LEFT+(int)(elapsed*(WIDTH-1)/span);
            if (x>=LEFT+WIDTH) continue;
            for (int a=0;a<3;a++) {
                drawPixel(x,plot_y(s->value.a[a],-8*GRAVITY,8*GRAVITY,76),colors[a]);
                drawPixel(x,plot_y(s->value.g[a],-34.91f,34.91f,192),colors[a]);
            }
            drawPixel(x,plot_y(mag(s->value.a),0,136,308),YELLOW);
            drawPixel(x,plot_y(mag(s->value.g),0,61,308),CYAN);
        }
        snprintf(line,sizeof(line),"0s                    Time (s)                      %.2fs",screen.state==LIVE?10.0:duration/1000.0);
        text_at(52,402,WHITE,line);
        snprintf(line,sizeof(line),"Peak a %.2f m/s^2  gyro %.2f rad/s  Rail %u  I2C %u  Missed %u",
                 screen.peak_a,screen.peak_g,screen.clips,screen.errors,screen.missed);
        text_at(10,419,YELLOW,line);
        snprintf(line,sizeof(line),"Trial %s",screen.trial); text_at(10,437,WHITE,line);
        text_at(10,457,CYAN,"Use host/collect.py. Phase cues are not measured ball release.");
    }
}
static void dump(void) {
    if (capture.state!=DONE) { puts("ERR,no_capture"); return; }
    printf("BEGIN,%s,%u,%u,%u,%u\n",capture.trial,capture.count,capture.errors,capture.missed,capture.stopped);
    puts("seq,pico_us,elapsed_us,phase,ax_mps2,ay_mps2,az_mps2,gx_rads,gy_rads,gz_rads,near_rail");
    for (unsigned i=0;i<capture.count;i++) {
        Sample *s=&capture.samples[i];
        printf("%lu,%llu,%lu,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u\n",
               (unsigned long)s->seq,(unsigned long long)s->pico_us,(unsigned long)s->elapsed_us,
               phase_at(&capture,s->elapsed_us),s->value.a[0],s->value.a[1],s->value.a[2],
               s->value.g[0],s->value.g[1],s->value.g[2],s->value.near_rail);
    }
    printf("END,%s\n",capture.trial); stdio_flush();
}
static void serial_help(void) {
    puts("# Pico Sports: USB serial connected. Firmware ready.");
    puts(healthy ? "# MPU6050 detected: 100 Hz, GPIO 8 SDA / GPIO 9 SCL." :
                   "# MPU6050 initialization FAILED. Check power/wiring/address and reboot.");
    puts("# To save throws: stop this Serial Monitor, then run in the VS Code TERMINAL:");
    puts("# & ./.venv/Scripts/python.exe host/collect.py --port COM3 --sport bowling");
    puts("# Replace COM3 with your Pico's port. Follow the calibration prompt (+Z up, still).");
    puts("# When Python shows the next throw, press ENTER in that TERMINAL to start.");
    puts("# Follow VGA: countdown -> HOLD STILL -> THROW NOW -> HOLD STILL.");
    puts("# After capture, press ENTER to accept/save, r to repeat, or p to pause.");
    puts("# Accepted bowling throws append to data/bowling.csv (or your --data folder).");
    puts("# Serial Monitor alone does not save a CSV. INFO checks status; HELP repeats this message.");
    stdio_flush();
}
static void command(const char *line) {
    bool busy=capture.state==COUNTDOWN || capture.state==RECORDING;
    if (!strcmp(line,"STOP") && busy) {
        mutex_enter_blocking(&data_lock); capture.state=DONE; capture.stopped=true; mutex_exit(&data_lock);
        dump(); return;
    }
    // Never print help/errors while sampling an active trial.
    if (busy) return;
    if (!strcmp(line,"HELP")) { serial_help(); return; }
    if (!strcmp(line,"CAPS")) { puts("CAPS,cup_pong_aim"); return; }
    if (!strcmp(line,"INFO")) {
        printf("INFO,2,MPU6050,100,8,2000,%u,%u\n",healthy,calibrated); return;
    }
    if (!strcmp(line,"CAL")) {
        if (!healthy) { puts("ERR,sensor"); return; }
        mutex_enter_blocking(&data_lock); capture.state=CALIBRATING; mutex_exit(&data_lock);
        calibrated=imu_calibrate(&bias);
        mutex_enter_blocking(&data_lock); capture.state=LIVE; capture.count=capture.head=0; mutex_exit(&data_lock);
        if (!calibrated) { printf("ERR,%s\n",imu_calibration_report()); return; }
        printf("CAL,%.8f,%.8f,%.8f,%.8f,%.8f,%.8f\n",bias.a[0],bias.a[1],bias.a[2],bias.g[0],bias.g[1],bias.g[2]); return;
    }
    if (!strcmp(line,"DUMP")) { dump(); return; }
    char id[49],extra;
    unsigned sport,category,pre,motion,post,delay;
    int fields=sscanf(line,"START %48s %u %u %u %u %u %u %c",id,&sport,&category,&pre,&motion,&post,&delay,&extra);
    if (fields!=7 || sport>3 || category>4 || (sport==3 && category>2) || pre>3000 || post>3000 || motion<100 || motion>8000 ||
        pre+motion+post>10000 || delay>10000 || !healthy || !calibrated) { puts("ERR,command_or_not_calibrated"); return; }
    for (const char *p=id;*p;p++) if (!((*p>='0'&&*p<='9') || (*p>='a'&&*p<='z') || *p=='-' || *p=='_')) { puts("ERR,id"); return; }
    mutex_enter_blocking(&data_lock);
    memset(&capture,0,sizeof(capture)); capture.state=COUNTDOWN;
    capture.pre_ms=pre; capture.motion_ms=motion; capture.post_ms=post;
    capture.sport=sports[sport]; capture.category=category; strcpy(capture.trial,id);
    capture.countdown=(delay+999)/1000;
    mutex_exit(&data_lock);
    printf("ACK,%s\n",id); stdio_flush();
    countdown_end=time_us_64()+delay*1000ull;
}
int main(void) {
    set_sys_clock_khz(150000,true); stdio_init_all(); mutex_init(&data_lock); initVGA();
    healthy=imu_init(); capture.state=healthy?LIVE:SENSOR_ERROR;
    capture.sport=&bowling; capture.pre_ms=500; capture.motion_ms=2000; capture.post_ms=500;
    multicore_launch_core1(display_core);
    uint64_t next=time_us_64(); unsigned seq=0;
    char input[160]; unsigned length=0; bool overflow=false;
    bool usb_was_connected=false, banner_pending=false;
    while (true) {
        bool usb_connected=stdio_usb_connected();
        if (usb_connected && !usb_was_connected) banner_pending=true;
        usb_was_connected=usb_connected;
        // Wait for an open USB connection; never interrupt a capture/export with text.
        if (usb_connected && banner_pending && capture.state!=COUNTDOWN && capture.state!=RECORDING) {
            serial_help(); banner_pending=false;
            next=time_us_64();
        }
        // Limit input processing per loop so noisy serial input cannot starve sampling.
        for (int n=0;n<32;n++) {
            int ch=getchar_timeout_us(0); if (ch==PICO_ERROR_TIMEOUT) break;
            if (ch=='\r' || ch=='\n') {
                if (length && !overflow) { input[length]=0; command(input); }
                length=0; overflow=false;
            } else if (length<sizeof(input)-1) input[length++]=(char)ch;
            else overflow=true;
        }
        uint64_t now=time_us_64();
        mutex_enter_blocking(&data_lock);
        if (capture.state==COUNTDOWN) {
            if (now>=countdown_end) { capture.state=RECORDING; capture.countdown=0; start_us=now; next=now; seq=0; }
            else capture.countdown=(unsigned)((countdown_end-now+999999)/1000000);
        }
        bool finished=capture.state==RECORDING && (now-start_us>=duration_ms(&capture)*1000ull || capture.count==CAPACITY);
        if (finished) capture.state=DONE;
        if (capture.state==RECORDING) capture.phase=phase_at(&capture,(uint32_t)(now-start_us));
        int state=capture.state;
        mutex_exit(&data_lock);
        if (finished) dump();
        if ((state!=LIVE && state!=RECORDING) || now<next) { sleep_us(100); continue; }
        unsigned missed=(unsigned)((now-next)/PERIOD_US);
        next+=((uint64_t)missed+1)*PERIOD_US; seq+=missed;
        Sample s={.pico_us=now,.elapsed_us=state==RECORDING?(uint32_t)(now-start_us):0,.seq=seq++};
        bool valid=imu_read(&s.value);
        mutex_enter_blocking(&data_lock);
        capture.missed+=missed;
        if (!valid) capture.errors++;
        else {
            capture.samples[capture.head]=s; capture.head=(capture.head+1)%CAPACITY;
            if (capture.count<CAPACITY) capture.count++;
            capture.clips+=s.value.near_rail;
            capture.peak_a=fmaxf(capture.peak_a,mag(s.value.a)); capture.peak_g=fmaxf(capture.peak_g,mag(s.value.g));
        }
        mutex_exit(&data_lock);
    }
}
