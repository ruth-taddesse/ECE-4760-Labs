/* Top-down path prototype. Serial Monitor: CAL then Enter/THROW, SET, REPLAY.
 * Same MPU6050/VGA wiring as the collection firmware. No host required for physics.
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
#include "bowling_model.h"
#include "VGA/vga16_graphics_v3.h"
enum { NEED_CAL, READY, CALIBRATING, COUNTDOWN, RECORDING, PLAYING, BAD_CAPTURE, SENSOR_ERROR };
typedef struct {
    int state;unsigned countdown,phase,errors,missed,rail,samples;
    bool has_shot;
    BowlTuning tuning;BowlThrow shot;BowlPath path;uint64_t animation_start;
} Demo;
static Demo demo,screen;
static mutex_t lock;
static ImuBias bias;
static BowlFeatures features;
static bool healthy,calibrated;
static uint64_t countdown_end,capture_start,next_sample;
static unsigned shot_seed=1;
static const int lane_x=150,lane_bottom=440;
#define SCALE 50.0f
static int sx(float x){return lane_x+(int)lroundf(x*SCALE);}
static int sy(float y){return lane_bottom-(int)lroundf(y*SCALE);}
static void label(int x,int y,char color,const char *s){drawTextGLCD(x,y,(char *)s,color,BLACK);}
static void draw_core(void) {
    char text[100];
    while(true) {
        if(!draw_start_signal()){tight_loop_contents();continue;}
        mutex_enter_blocking(&lock);memcpy(&screen,&demo,sizeof(screen));mutex_exit(&lock);
        clearLowFrame(0,BLACK);
        label(30,10,WHITE,"PICO SPORTS / BOWLING PATH DEMO");
        const char *status="";
        switch(screen.state) {
            case NEED_CAL:status="Place +Z up; type CAL + Enter";break;
            case READY:status="Press Enter to record a throw";break;
            case CALIBRATING:status="CALIBRATING - keep still";break;
            case COUNTDOWN:snprintf(text,sizeof(text),"GET READY: %u",screen.countdown);status=text;break;
            case RECORDING:status=screen.phase==0?"HOLD STILL":screen.phase==1?"THROW NOW":"HOLD STILL - finishing";break;
            case PLAYING:status="Path replay - Enter for next throw";break;
            case BAD_CAPTURE:status="Capture rejected; see terminal";break;
            case SENSOR_ERROR:status="IMU ERROR - check wiring / reboot";break;
        }
        label(30,30,YELLOW,status);
        // Deliberately schematic lane, equal X/Y display scale in game units.
        drawRect(sx(-BOWL_HALF_WIDTH),sy(BOWL_LENGTH),160,(int)(BOWL_LENGTH*SCALE)+1,WHITE);
        drawVLine(sx(-BOWL_HALF_WIDTH)-12,sy(BOWL_LENGTH),376,DARK_GREEN);
        drawVLine(sx(BOWL_HALF_WIDTH)+12,sy(BOWL_LENGTH),376,DARK_GREEN);
        for(int y=90;y<440;y+=20)drawVLine(lane_x,y,6,DARK_GREEN);
        drawHLine(sx(-BOWL_HALF_WIDTH),lane_bottom,160,CYAN);
        float elapsed=(float)(time_us_64()-screen.animation_start)/1000000.0f;
        unsigned last=0;
        if(screen.has_shot) {
            while(last+1<screen.path.count && screen.path.points[last+1].t<=elapsed)last++;
            for(unsigned i=1;i<=last;i++)drawLine(sx(screen.path.points[i-1].x),sy(screen.path.points[i-1].y),sx(screen.path.points[i].x),sy(screen.path.points[i].y),YELLOW);
        }
        bool complete=screen.has_shot && last+1==screen.path.count;
        for(int i=0;i<10;i++) {
            float x,y;bowl_pin(i,&x,&y);
            fillCircle(sx(x),sy(y),(int)(BOWL_PIN_RADIUS*SCALE),complete&&screen.path.pin==i?RED:WHITE);
        }
        float x=screen.has_shot?screen.path.points[last].x:screen.tuning.start_x;
        float y=screen.has_shot?screen.path.points[last].y:0;
        fillCircle(sx(x),sy(y),(int)(BOWL_BALL_RADIUS*SCALE),CYAN);
        label(80,458,CYAN,"START / FOUL LINE");
        label(280,65,WHITE,"Top-down schematic / game units");
        label(280,87,YELLOW,screen.has_shot?bowl_labels[screen.shot.label]:"Awaiting first throw");
        if(screen.has_shot) {
            snprintf(text,sizeof(text),"Power %.2f   Hook %+.2f",screen.shot.power,screen.shot.hook);label(280,107,WHITE,text);
            snprintf(text,sizeof(text),"Peak accel %.2f m/s^2",screen.shot.peak);label(280,125,WHITE,text);
            snprintf(text,sizeof(text),"Speed %.2f units/s",screen.path.speed);label(280,143,WHITE,text);
            snprintf(text,sizeof(text),"Curve accel %+.3f units/s^2",screen.path.hook_accel);label(280,161,WHITE,text);
            if(complete) {
                if(screen.path.pin>=0)snprintf(text,sizeof(text),"FIRST CONTACT: PIN %d",screen.path.pin+1);
                else snprintf(text,sizeof(text),"%s",screen.path.gutter?"GUTTER":screen.path.stopped?"BALL STOPPED":"MISSED PINS");
                label(280,185,YELLOW,text);
            }
        }
        label(280,213,CYAN,"SET name value (then Enter)");
        snprintf(text,sizeof(text),"start_x %+.2f    aim_deg %+.1f",screen.tuning.start_x,screen.tuning.aim_deg);label(280,235,WHITE,text);
        snprintf(text,sizeof(text),"speed_gain %.2f  hook_gain %.2f",screen.tuning.speed_gain,screen.tuning.hook_gain);label(280,253,WHITE,text);
        snprintf(text,sizeof(text),"friction %.2f    hook_start %.2f",screen.tuning.friction,screen.tuning.hook_start);label(280,271,WHITE,text);
        snprintf(text,sizeof(text),"randomness %.2f (0 = repeatable)",screen.tuning.randomness);label(280,289,WHITE,text);
        label(280,319,WHITE,"CAL: +Z-up calibration");
        label(280,337,WHITE,"Enter/THROW: countdown + capture");
        label(280,355,WHITE,"REPLAY: repeat last path");
        label(280,373,WHITE,"RESET: restore tuning defaults");
        snprintf(text,sizeof(text),"Samples %u  I2C %u  gaps %u",screen.samples,screen.errors,screen.missed);label(280,403,WHITE,text);
        label(280,425,CYAN,"Red pin = first ball contact only");
        label(280,443,CYAN,"No pin scattering or scoring yet");
    }
}
static void help(void) {
    puts("# BOWLING DEMO ready. CAL + Enter: place sensor +Z up and leave still 3 seconds.");
    puts("# Then press Enter or send THROW: 3s countdown, 0.5s still, 2s THROW NOW, 0.5s still.");
    puts("# Watch VGA cues. Path is displayed after recording; no measured release event.");
    puts("# SET start_x -0.5 | SET aim_deg 1 | SET hook_gain 1.5 | SET speed_gain 1.1");
    puts("# SET friction 0.12 | SET hook_start 0.1 | SET randomness 0");
    puts("# SET replays the last throw with new settings. REPLAY, RESET, INFO, HELP, STOP.");
}
static void replay(void) {
    bowl_simulate(&demo.shot,&demo.tuning,shot_seed,&demo.path);
    demo.animation_start=time_us_64();demo.state=PLAYING;
}
static void command(const char *line) {
    bool busy=demo.state==RECORDING||demo.state==COUNTDOWN;
    if(busy) {
        if(!strcmp(line,"STOP")) {
            mutex_enter_blocking(&lock);demo.state=READY;mutex_exit(&lock);
            puts("OK,capture_cancelled");
        }
        return;
    }
    if(!strcmp(line,"INFO")){printf("BOWL,1,%u,%u\n",healthy,calibrated);return;}
    if(!strcmp(line,"STOP")){puts("OK,idle");return;}
    if(!strcmp(line,"HELP")){help();puts("OK,help");return;}
    if(!strcmp(line,"CAL")) {
        if(!healthy){puts("ERR,sensor");return;}
        mutex_enter_blocking(&lock);demo.state=CALIBRATING;mutex_exit(&lock);
        calibrated=imu_calibrate(&bias);
        mutex_enter_blocking(&lock);demo.state=calibrated?READY:NEED_CAL;mutex_exit(&lock);
        if(calibrated)puts("CAL,OK - Press Enter to record a throw.");
        else printf("ERR,%s\n",imu_calibration_report());
        return;
    }
    if(!line[0]||!strcmp(line,"THROW")) {
        if(!healthy||!calibrated){puts("ERR,Send CAL first with sensor level +Z up");return;}
        bowl_features_reset(&features);
        mutex_enter_blocking(&lock);
        demo.has_shot=false;demo.samples=demo.errors=demo.missed=demo.rail=0;
        demo.state=COUNTDOWN;demo.countdown=3;
        mutex_exit(&lock);
        puts("ACK,THROW - Watch VGA. Countdown 3s, hold still 0.5s, then THROW NOW.");stdio_flush();
        countdown_end=time_us_64()+3000000;return;
    }
    char key[32],extra;float value;
    bool set=sscanf(line,"SET %31s %f %c",key,&value,&extra)==2;
    mutex_enter_blocking(&lock);
    bool ok=false;
    if(set)ok=bowl_set(&demo.tuning,key,value);
    else if(!strcmp(line,"RESET")){demo.tuning=bowl_defaults();ok=true;}
    else if(!strcmp(line,"REPLAY"))ok=demo.has_shot;
    if(ok&&demo.has_shot)replay();
    mutex_exit(&lock);
    puts(ok?"OK,tuning/replay updated":"ERR,unknown command, out-of-range value, or no recorded throw");
}
int main(void) {
    set_sys_clock_khz(150000,true);stdio_init_all();mutex_init(&lock);initVGA();
    healthy=imu_init();demo.state=healthy?NEED_CAL:SENSOR_ERROR;demo.tuning=bowl_defaults();
    multicore_launch_core1(draw_core);
    char input[128];unsigned used=0;bool overflow=false,last_cr=false,connected_before=false,pending=false;
    while(true) {
        bool connected=stdio_usb_connected();if(connected&&!connected_before)pending=true;connected_before=connected;
        if(pending&&connected&&demo.state!=RECORDING&&demo.state!=COUNTDOWN){help();pending=false;}
        for(int n=0;n<32;n++) {
            int c=getchar_timeout_us(0);if(c==PICO_ERROR_TIMEOUT)break;
            if(c=='\n'&&last_cr){last_cr=false;continue;}
            last_cr=c=='\r';
            if(c=='\r'||c=='\n') {
                if(!overflow){input[used]=0;command(input);}used=0;overflow=false;
            } else if(c==8||c==127){if(used)used--;}
            else if(used<sizeof(input)-1)input[used++]=(char)c;
            else overflow=true;
        }
        uint64_t now=time_us_64();
        mutex_enter_blocking(&lock);
        if(demo.state==COUNTDOWN) {
            if(now>=countdown_end){demo.state=RECORDING;demo.phase=0;demo.countdown=0;capture_start=next_sample=now;}
            else demo.countdown=(unsigned)((countdown_end-now+999999)/1000000);
        }
        bool active=demo.state==RECORDING;
        uint64_t elapsed=active?now-capture_start:0;
        if(active)demo.phase=elapsed<500000?0:elapsed<2500000?1:2;
        mutex_exit(&lock);
        if(active&&elapsed>=3000000) {
            BowlThrow result;
            bool valid=demo.errors==0&&demo.missed==0&&demo.rail==0&&bowl_classify(&features,&result);
            mutex_enter_blocking(&lock);
            demo.has_shot=valid;
            if(valid){demo.shot=result;shot_seed=(unsigned)now;replay();}else demo.state=BAD_CAPTURE;
            mutex_exit(&lock);
            if(valid)printf("RESULT,%s,power=%.3f,hook=%.3f,peak=%.3f,first_pin=%d\n",bowl_labels[demo.shot.label],demo.shot.power,demo.shot.hook,demo.shot.peak,demo.path.pin+1);
            else printf("ERR,Invalid/weak capture: I2C=%u gaps=%u rail=%u; retry Enter\n",demo.errors,demo.missed,demo.rail);
            continue;
        }
        if(!active||now<next_sample){sleep_us(100);continue;}
        unsigned missed=(unsigned)((now-next_sample)/10000);next_sample+=((uint64_t)missed+1)*10000;
        ImuReading r;bool ok=imu_read(&r);
        mutex_enter_blocking(&lock);demo.missed+=missed;
        if(!ok)demo.errors++;else{demo.samples++;demo.rail+=r.near_rail;}
        mutex_exit(&lock);
        if(ok&&elapsed>=500000&&elapsed<2500000) {
            for(int j=0;j<3;j++){r.a[j]-=bias.a[j];r.g[j]-=bias.g[j];}
            bowl_features_add(&features,r.a,r.g,(float)elapsed/1000000.0f);
        }
    }
}
