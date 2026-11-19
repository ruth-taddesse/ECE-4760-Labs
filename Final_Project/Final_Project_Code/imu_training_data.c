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
#define WIDTH 564
#define PLOT_HEIGHT 46

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
    bool stopped, calibrated, usb_connected, calibration_failed;
    uint32_t elapsed_us;
} Capture;
static Capture capture, screen;
static mutex_t data_lock;
static uint64_t start_us, countdown_end;
static bool healthy, calibrated;
static ImuBias bias;
static const Sport *sports[] = {&bowling, &basketball, &cup_pong, &cup_pong_aim};
static float mag(const float v[3]) { return sqrtf(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]); }
static unsigned duration_ms(const Capture *c) { return c->pre_ms+c->motion_ms+c->post_ms; }
static unsigned phase_at(const Capture *c, uint32_t us) {
    return us < c->pre_ms*1000u ? 0 : us < (c->pre_ms+c->motion_ms)*1000u ? 1 : 2;
}
static void text_at(int x,int y,char color,const char *text) {
    drawTextGLCD(x,y,(char *)text,color,BLACK);
}
static int plot_y(float v,float low,float high,int top) {
    return top+PLOT_HEIGHT-(int)((fmaxf(low,fminf(high,v))-low)*PLOT_HEIGHT/(high-low));
}
// Draw only the locked snapshot. Host-side acceptance/saving is not visible here.
static void draw_capture(const Capture *c) {
    char line[105];
    bool trial=c->trial[0]!=0;
    bool warning=c->errors || c->missed || c->clips;
    unsigned duration=duration_ms(c);
    const char *title, *hint;
    char accent=CYAN;
    switch(c->state) {
        case SENSOR_ERROR:
            title="IMU NOT DETECTED"; accent=RED;
            hint="Check 3.3V, GND, SDA GP8 / SCL GP9; then reboot."; break;
        case CALIBRATING:
            title="CALIBRATING"; accent=YELLOW;
            hint="Place controller flat, +Z up. Keep still for 3 seconds."; break;
        case COUNTDOWN:
            snprintf(line,sizeof(line),"GET READY  %u",c->countdown);
            title=line; accent=YELLOW;
            hint="Assume your throwing grip. Wait for THROW NOW."; break;
        case RECORDING:
            accent=c->phase==1?GREEN:YELLOW;
            title=c->phase==1?"THROW NOW":"HOLD STILL";
            hint=c->phase==0?"Starting pose: hold still until the motion phase.":
                 c->phase==1?"Perform one throw toward the requested target.":
                             "Motion ended. Hold still while capture finishes."; break;
        case DONE:
            accent=c->stopped?RED:warning?ORANGE:GREEN;
            title=c->stopped?"CAPTURE STOPPED":warning?"REVIEW CAPTURE":"CAPTURE COMPLETE";
            hint=c->stopped?"Incomplete capture. Repeat this throw from the host.":
                 warning?"Check quality warnings in host; repeat recommended.":
                         "Review in host: Enter accepts; r repeats; p pauses."; break;
        default:
            accent=c->calibrated?CYAN:c->calibration_failed?RED:YELLOW;
            title=c->calibrated?"READY TO CAPTURE":
                  c->calibration_failed?"CALIBRATION FAILED":"CALIBRATION NEEDED";
            hint=!c->usb_connected?"Connect USB and open host/collect.py to continue.":
                 c->calibrated?"Choose sport in host; press Enter there to start.":
                 c->calibration_failed?"See terminal details. Lay flat, +Z up, then retry.":
                                       "Follow host calibration prompt: flat, +Z up, still."; break;
    }
    clearLowFrame(0,BLACK);
    text_at(12,12,WHITE,"PICO SPORTS / THROW CAPTURE");
    text_at(328,12,CYAN,"MPU6050 / 100 Hz");
    text_at(490,12,c->usb_connected?GREEN:ORANGE,c->usb_connected?"USB CONNECTED":"USB OFFLINE");
    drawHLine(12,30,616,DARK_GREEN);
    // Keep the countdown headline in its own storage while formatting metadata.
    char headline[40]; snprintf(headline,sizeof(headline),"%s",title);
    snprintf(line,sizeof(line),"SPORT  %s",trial?c->sport->name:"select in host");
    text_at(12,42,CYAN,line);
    snprintf(line,sizeof(line),"TARGET  %s",trial?c->sport->categories[c->category]:"awaiting trial");
    text_at(244,42,WHITE,line);
    snprintf(line,sizeof(line),"TRIAL  %s",trial?c->trial:"--"); text_at(12,58,WHITE,line);
    drawRect(12,76,616,74,accent);
    drawTextArial24(24,86,headline,accent,BLACK);
    text_at(24,126,WHITE,hint);
    const char *steps[]={"01  HOLD STILL", "02  THROW NOW", "03  HOLD STILL"};
    for(int i=0;i<3;i++) {
        char color=c->state==RECORDING && c->phase==(unsigned)i?accent:DARK_GREEN;
        int x=12+i*208;
        drawRect(x,158,200,24,color);
        text_at(x+12,166,color==DARK_GREEN?WHITE:color,steps[i]);
    }
    drawRect(12,188,616,5,DARK_GREEN);
    if(trial && (c->state==RECORDING || c->state==DONE) && duration) {
        unsigned elapsed=c->state==DONE && !c->stopped?duration*1000u:c->elapsed_us;
        if(elapsed>duration*1000u)elapsed=duration*1000u;
        int width=(int)((uint64_t)elapsed*612/(duration*1000u));
        if(width)fillRect(14,190,width,1,accent);
    }
    text_at(12,202,CYAN,"ACCEL / m/s^2 (gravity retained)");
    text_at(378,202,WHITE,"X");text_at(414,202,RED,"Y");text_at(450,202,GREEN,"Z");
    text_at(12,268,CYAN,"GYRO / rad/s");
    text_at(12,334,YELLOW,"MAGNITUDE / accel yellow; gyro cyan");
    text_at(12,218,WHITE,"+78.5");text_at(12,252,WHITE,"-78.5");
    text_at(12,284,WHITE,"+34.9");text_at(12,318,WHITE,"-34.9");
    text_at(12,350,YELLOW,"136");text_at(12,384,WHITE,"0");
    text_at(514,334,CYAN,"gyro 0..61");
    for(int p=0;p<3;p++) {
        int top=216+p*66;
        drawRect(LEFT,top,WIDTH+1,PLOT_HEIGHT+1,DARK_GREEN);
        if(p<2)drawHLine(LEFT,top+PLOT_HEIGHT/2,WIDTH,DARK_GREEN);
        if(trial && duration) {
            drawVLine(LEFT+c->pre_ms*(WIDTH-1)/duration,top,PLOT_HEIGHT,DARK_BLUE);
            drawVLine(LEFT+(c->pre_ms+c->motion_ms)*(WIDTH-1)/duration,top,PLOT_HEIGHT,DARK_BLUE);
        }
    }
    unsigned first=c->count==CAPACITY?c->head:0;
    uint64_t base=c->count?c->samples[first].pico_us:0;
    uint64_t span=trial?duration*1000ull:10000000ull;
    const char colors[]={WHITE,RED,GREEN,YELLOW,CYAN};
    int previous_x=0, previous_y[8]={0};
    for(unsigned i=0;i<c->count && span;i++) {
        const Sample *s=&c->samples[(first+i)%CAPACITY];
        uint64_t elapsed=trial?s->elapsed_us:s->pico_us-base;
        if(elapsed>=span)continue;
        int x=LEFT+(int)(elapsed*(WIDTH-1)/span),y[8];
        for(int a=0;a<3;a++) {
            y[a]=plot_y(s->value.a[a],-8*GRAVITY,8*GRAVITY,216);
            y[a+3]=plot_y(s->value.g[a],-34.91f,34.91f,282);
        }
        y[6]=plot_y(mag(s->value.a),0,136,348);
        y[7]=plot_y(mag(s->value.g),0,61,348);
        bool connect=i && s->seq==c->samples[(first+i-1)%CAPACITY].seq+1;
        for(int channel=0;channel<8;channel++) {
            char color=colors[channel<6?channel%3:channel-3];
            if(connect)drawLine(previous_x,previous_y[channel],x,y[channel],color);
            else drawPixel(x,y[channel],color);
            previous_y[channel]=y[channel];
        }
        previous_x=x;
    }
    text_at(LEFT,400,WHITE,"0s");
    snprintf(line,sizeof(line),"%s / %.2f s",trial?"TRIAL":"LIVE WINDOW",span/1000000.0);
    text_at(250,400,WHITE,line);
    snprintf(line,sizeof(line),"Samples %u   Peak accel %.2f m/s^2   Peak gyro %.2f rad/s",c->count,c->peak_a,c->peak_g);
    text_at(12,418,WHITE,line);
    snprintf(line,sizeof(line),"%s   I2C errors %u   Missed %u   Near rail %u",
             c->calibrated?"CAL OK":"NOT CALIBRATED",c->errors,c->missed,c->clips);
    text_at(12,434,warning?ORANGE:CYAN,line);
    drawHLine(12,450,616,DARK_GREEN);
    text_at(12,458,WHITE,"Controls: host/collect.py terminal / VGA is display only");
    text_at(12,470,CYAN,"Raw SI plots / phase cues are not measured ball release");
}
static void display_core(void) {
    while(true) {
        if(!draw_start_signal()){tight_loop_contents();continue;}
        mutex_enter_blocking(&data_lock); memcpy(&screen,&capture,sizeof(screen)); mutex_exit(&data_lock);
        draw_capture(&screen);
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
        mutex_enter_blocking(&data_lock);
        capture.state=CALIBRATING; capture.calibrated=false; capture.calibration_failed=false;
        mutex_exit(&data_lock);
        calibrated=imu_calibrate(&bias);
        mutex_enter_blocking(&data_lock); capture.state=LIVE; capture.count=capture.head=0;
        capture.calibrated=calibrated; capture.calibration_failed=!calibrated;
        capture.errors=capture.missed=capture.clips=0; capture.peak_a=capture.peak_g=0;
        capture.trial[0]=0; capture.elapsed_us=0; mutex_exit(&data_lock);
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
    bool usb_connected=capture.usb_connected;
    memset(&capture,0,sizeof(capture)); capture.state=COUNTDOWN;
    capture.calibrated=calibrated; capture.usb_connected=usb_connected;
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
        mutex_enter_blocking(&data_lock); capture.usb_connected=usb_connected; mutex_exit(&data_lock);
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
                if (length && !overflow) {
                    input[length]=0; command(input);
                    // Calibration pauses polling; do not count it as missed LIVE samples.
                    if (capture.state==LIVE) next=time_us_64();
                }
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
        if (capture.state==RECORDING) {
            capture.elapsed_us=(uint32_t)(now-start_us);
            capture.phase=phase_at(&capture,capture.elapsed_us);
        }
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
