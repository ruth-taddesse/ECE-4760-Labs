/* Player-view bowling prototype. USB terminal: menu, CAL, THROW, SET, REPLAY.
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
#include "hardware/adc.h"
#include "imu.h"
#include "bowling_model.h"
#include "sports_views.h"
#include "VGA/vga16_graphics_v3.h"
enum { NEED_CAL, READY, CALIBRATING, COUNTDOWN, RECORDING, PLAYING, BAD_CAPTURE, SENSOR_ERROR };
enum { HOME_VIEW, GAME_VIEW, SETTINGS_VIEW, UNAVAILABLE_VIEW, QUIT_VIEW };
typedef struct {
    int state,view,return_view;unsigned menu_index,unavailable_game;
    unsigned countdown,phase,errors,missed,rail,samples;
    bool sensor_ok,is_calibrated;
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
// Vy -> ADC0/GP26 (horizontal), Vx -> ADC1/GP27 (vertical).
// SW -> GP28, active low to ground; joystick supply must be 3.3 V.
#define JOY_CENTER 2048.0f
#define JOY_DEADZONE 256.0f
#define JOY_SPEED 1.5f
#define JOY_DIRECTION 1.0f
#define JOY_Y_DIRECTION -1.0f // mounted joystick: lower ADC values move down
#define JOY_SW 28
static uint64_t joystick_last,button_changed,menu_repeat;
static bool button_raw,button_stable;
static int menu_direction;
static void command(const char *line);
static float joystick_axis(unsigned channel){
    adc_select_input(channel);(void)adc_read(); // discard first sample after switching
    unsigned sum=0;for(int i=0;i<8;i++)sum+=adc_read();
    return sum/8.0f-JOY_CENTER;
}
static void update_joystick(uint64_t now) {
    if(now-joystick_last<10000)return;
    float dt=fminf((now-joystick_last)*1e-6f,0.02f);
    joystick_last=now;
    bool pressed=!gpio_get(JOY_SW);
    if(pressed!=button_raw){button_raw=pressed;button_changed=now;}
    bool select=false;
    if(now-button_changed>=30000 && button_stable!=button_raw){
        button_stable=button_raw;select=button_stable;
    }
    // Selection is menu-only: never accidentally start/cancel a throw by gripping SW.
    if(select){
        if(demo.view==HOME_VIEW||demo.view==UNAVAILABLE_VIEW||demo.view==QUIT_VIEW){command("");return;}
        if(demo.view==SETTINGS_VIEW){command("BACK");return;}
    }
    if(demo.view==HOME_VIEW){
        float x=JOY_DIRECTION*joystick_axis(0),y=JOY_Y_DIRECTION*joystick_axis(1);
        int direction=0;
        if(fmaxf(fabsf(x),fabsf(y))>900)
            direction=fabsf(x)>fabsf(y)?(x>0?1:2):(y>0?3:4);
        if(!direction){menu_direction=0;return;}
        if(direction!=menu_direction || now>=menu_repeat){
            bool first=direction!=menu_direction;
            menu_direction=direction;menu_repeat=now+(first?400000:180000);
            // Spatial navigation: three game tiles above Settings and Quit.
            static const unsigned next[4][5]={
                {1,2,2,4,4}, {0,0,1,3,3}, {3,3,4,3,4}, {0,1,2,0,2}
            };
            mutex_enter_blocking(&lock);demo.menu_index=next[direction-1][demo.menu_index];mutex_exit(&lock);
        }
        return;
    }
    menu_direction=0;
    if(demo.view!=GAME_VIEW)return;
    bool finished=demo.state==PLAYING && demo.path.count &&
        (now-demo.animation_start)*1e-6f>=demo.path.points[demo.path.count-1].t;
    if(demo.state!=READY && demo.state!=BAD_CAPTURE && !finished)return;
    float delta=joystick_axis(0);
    if(fabsf(delta)<=JOY_DEADZONE)return;
    float velocity=JOY_DIRECTION*copysignf((fabsf(delta)-JOY_DEADZONE)/(2048.0f-JOY_DEADZONE),delta);
    mutex_enter_blocking(&lock);
    demo.tuning.start_x=fmaxf(-1.3f,fminf(1.3f,demo.tuning.start_x+velocity*JOY_SPEED*dt));
    demo.has_shot=false;demo.state=READY;
    mutex_exit(&lock);
}
// A fixed camera at the foul line. Physics stays in the original lane coordinates.
// Keep the useful picture inside a small safe area because some VGA monitors crop
// the outermost scan lines. The wider horizontal scale also makes the pin deck
// readable from the player's viewpoint.
#define CAMERA_CENTER_X 320
#define CAMERA_X_SCALE 190.0f
#define CAMERA_Y_SCALE 145.0f
#define CAMERA_DEPTH 0.38f
#define BOTTOM_SAFE_MARGIN 8
#define FOUL_LINE_Y (438-BOTTOM_SAFE_MARGIN)
#define DISPLAY_X_CORRECTION 0.75f // 640x480 stretched by monitor to 16:9
#define PIN_RACK_X_SCALE 50.0f
#define PIN_RACK_FRONT_Y 201
#define PIN_RACK_ROW_GAP 13
static float perspective(float y){return 1.0f/(1.0f+CAMERA_DEPTH*y);}
static int sx(float x,float y){return CAMERA_CENTER_X+(int)lroundf(x*CAMERA_X_SCALE*perspective(y));}
static int sy(float y){return FOUL_LINE_Y-(int)lroundf(y*CAMERA_Y_SCALE*perspective(y));}
static int pin_row(int index){
    int row=0;while(index>=row+1){index-=row+1;row++;}return row;
}
static void fill_display_circle(int x,int y,int radius,char color){
    int rx=(int)lroundf(radius*DISPLAY_X_CORRECTION);
    if(rx<1)rx=1;
    for(int dy=-radius;dy<=radius;dy++){
        float normalized=(float)(dy*dy)/(float)(radius*radius);
        int dx=(int)lroundf(rx*sqrtf(fmaxf(0,1-normalized)));
        drawHLine(x-dx,y+dy,2*dx+1,color);
    }
}
static void draw_display_circle(int x,int y,int radius,char color){
    int rx=(int)lroundf(radius*DISPLAY_X_CORRECTION);
    if(rx<1)rx=1;
    for(int dy=-radius;dy<=radius;dy++){
        float normalized=(float)(dy*dy)/(float)(radius*radius);
        int dx=(int)lroundf(rx*sqrtf(fmaxf(0,1-normalized)));
        drawPixel(x-dx,y+dy,color);drawPixel(x+dx,y+dy,color);
    }
    for(int dx=-rx;dx<=rx;dx++){
        float normalized=(float)(dx*dx)/(float)(rx*rx);
        int dy=(int)lroundf(radius*sqrtf(fmaxf(0,1-normalized)));
        drawPixel(x+dx,y-dy,color);drawPixel(x+dx,y+dy,color);
    }
}
static void label(int x,int y,char color,const char *s){drawTextGLCD(x,y,(char *)s,color,WHITE);}
static void title(int x,int y,char color,const char *s){drawTextArial24(x,y,(char *)s,color,WHITE);}
static void button(int x,int y,int w,int h,const char *name,const char *detail,bool selected){
    fillRect(x,y,w,h,WHITE);
    drawRect(x,y,w,h,selected?BLUE:LIGHT_BLUE);
    if(selected)drawRect(x+2,y+2,w-4,h-4,BLUE);
    label(x+12,y+14,BLUE,name);label(x+12,y+h-22,BLACK,detail);
}
static void draw_menu(const Demo *d){
    clearLowFrame(0,WHITE);
    if(d->view==UNAVAILABLE_VIEW&&d->unavailable_game==1){draw_basketball_view();return;}
    if(d->view==UNAVAILABLE_VIEW&&d->unavailable_game==2){draw_cup_pong_view();return;}
    title(24,22,BLUE,"PICO SPORTS");
    label(26,56,BLACK,"because ECEs can have fun too!");
    drawHLine(24,78,592,LIGHT_BLUE);
    if(d->view==HOME_VIEW){
        label(26,100,BLUE,"CHOOSE YOUR GAME");
        const char *names[]={"1  BOWLING","2  BASKETBALL","3  CUP PONG"};
        for(int i=0;i<3;i++){
            int x=24+i*204;
            button(x,126,184,142,names[i],i==0?"":"Coming soon",d->menu_index==(unsigned)i);
            // Small sport icons use only the existing VGA primitives.
            if(i==0){
                fill_display_circle(x+76,186,16,BLUE);
                for(int j=0;j<3;j++){
                    fill_display_circle(x+117+j*10,178,4,BLUE);
                    fillRect(x+113+j*10,183,8,18,BLUE);
                }
            }else if(i==1){
                draw_display_circle(x+90,187,23,ORANGE);drawVLine(x+90,164,46,ORANGE);
                drawHLine(x+73,187,35,ORANGE);
            }else{
                for(int j=0;j<3;j++)drawRect(x+63+j*22,172,18,30,BLUE);
                fill_display_circle(x+95,158,5,ORANGE);
            }
        }
        button(24,292,286,64,"4  SETTINGS","Aim, power, curve, calibration",d->menu_index==3);
        button(330,292,286,64,"5  QUIT","Stop this session",d->menu_index==4);
        label(26,386,BLACK,"Joystick: left/right across tiles; up/down changes row.");
        label(26,400,BLACK,"Press joystick SW to select the highlighted option.");
        label(26,416,BLACK,"USB keyboard: W/S + Enter, or 1-5, also works.");
        label(26,434,d->sensor_ok?BLUE:RED,d->sensor_ok?
              d->is_calibrated?"Controller calibrated / ready":"Select Bowling, then CAL to calibrate":"IMU unavailable / check wiring and reboot");
    }else if(d->view==SETTINGS_VIEW){
        title(24,100,BLUE,"SETTINGS");
        label(26,136,BLACK,"Type SET name value in the USB terminal.");
        char line[80];
        const char *keys[]={"start_x","aim_deg","speed_gain","hook_gain","friction","hook_start","randomness"};
        const char *limits[]={"-1.3 to 1.3","-10 to 10 deg","0.5 to 2","0 to 3","0 to 1","0 to 0.9","0 to 1"};
        float values[]={d->tuning.start_x,d->tuning.aim_deg,d->tuning.speed_gain,d->tuning.hook_gain,d->tuning.friction,d->tuning.hook_start,d->tuning.randomness};
        for(int i=0;i<7;i++){
            int y=166+i*26;
            snprintf(line,sizeof(line),"%-12s  %+.2f",keys[i],values[i]);label(36,y,BLUE,line);
            label(320,y,BLACK,limits[i]);
        }
        label(26,368,BLACK,"Example: SET aim_deg 2 / SET hook_gain 1.5");
        label(26,390,BLACK,"Press joystick SW to go back. RESET restores defaults.");
        label(26,412,BLACK,"BACK returns. MENU chooses a game. QUIT ends session.");
    }else if(d->view==UNAVAILABLE_VIEW){
        title(24,110,BLUE,d->unavailable_game==1?"BASKETBALL":"CUP PONG");
        label(26,170,BLACK,"Game coming soon.");
        label(26,196,BLACK,"Gesture collection is implemented; this game is not yet playable.");
        label(26,222,BLACK,"To collect throws, use MPU6050_IMU.uf2 and host/collect.py.");
        button(24,290,592,64,"BACK TO GAME MENU","Press SW, Enter or type MENU",true);
    }else{
        title(24,110,BLUE,"THANKS FOR PLAYING");
        label(26,170,BLACK,"Session stopped. It is safe to close the USB terminal.");
        label(26,196,BLACK,"Settings remain in RAM until reboot.");
        button(24,290,592,64,"PLAY AGAIN","Press SW, Enter or type MENU",true);
    }
    drawHLine(24,454-BOTTOM_SAFE_MARGIN,592,LIGHT_BLUE);
    label(26,468-BOTTOM_SAFE_MARGIN,BLUE,"Pico 2 + MPU6050 / Joystick + USB keyboard / VGA");
}
static void draw_game(const Demo *d){
    char text[100];
    clearLowFrame(0,WHITE);
    title(24,10,BLUE,"BOWLING");
    label(418,14,BLUE,"MENU / SETTINGS / QUIT");
    label(418,28,BLACK,"Type in USB terminal");
    const char *status="";char color=BLUE;
    switch(d->state){
        case NEED_CAL:status="CALIBRATE CONTROLLER";break;
        case READY:status="READY TO BOWL";break;
        case CALIBRATING:status="KEEP STILL / +Z UP";break;
        case COUNTDOWN:snprintf(text,sizeof(text),"GET READY  %u",d->countdown);status=text;break;
        case RECORDING:status=d->phase==1?"THROW NOW":"HOLD STILL";color=d->phase==1?MED_GREEN:BLUE;break;
        case PLAYING:break;
        case BAD_CAPTURE:status="PLEASE TRY AGAIN";color=RED;break;
        case SENSOR_ERROR:status="CHECK IMU WIRING";color=RED;break;
    }
    title(24,52,color,status);
    label(26,84,BLACK,d->state==NEED_CAL?"Place sensor flat, +Z up. Type CAL and press Enter.":
          d->state==CALIBRATING?"Leave the controller still for three seconds.":
          d->state==BAD_CAPTURE?"Weak or incomplete capture. Press Enter to retry.":
          d->state==SENSOR_ERROR?"Check 3.3V, GND, GP8 SDA / GP9 SCL; reboot.":
          "Joystick: move start / Enter: bowl / MENU: choose game");
    drawHLine(24,104,592,LIGHT_BLUE);
    label(232,112,BLUE,"PLAYER VIEW / PIN DECK");
    // A dark back wall keeps the separated white pin silhouettes visible.
    fillRect(190,124,260,80,DARK_BLUE);
    // Fill trapezoid row by row: boards converge toward the far pin deck.
    for(int row=sy(BOWL_LENGTH);row<=FOUL_LINE_Y;row++){
        float distance=FOUL_LINE_Y-row;
        float y=distance/(CAMERA_Y_SCALE-CAMERA_DEPTH*distance);
        int left=sx(-BOWL_HALF_WIDTH,y),right=sx(BOWL_HALF_WIDTH,y);
        int gutter=(int)(12*perspective(y));
        drawHLine(left-gutter,row,right-left+2*gutter+1,DARK_BLUE);
        drawHLine(left,row,right-left+1,ORANGE);
    }
    for(int board=0;board<=16;board++){
        float x=-BOWL_HALF_WIDTH+board*0.2f;
        drawLine(sx(x,0),sy(0),sx(x,BOWL_LENGTH),sy(BOWL_LENGTH),DARK_ORANGE);
    }
    drawLine(sx(-BOWL_HALF_WIDTH,0),sy(0),sx(-BOWL_HALF_WIDTH,BOWL_LENGTH),sy(BOWL_LENGTH),WHITE);
    drawLine(sx(BOWL_HALF_WIDTH,0),sy(0),sx(BOWL_HALF_WIDTH,BOWL_LENGTH),sy(BOWL_LENGTH),WHITE);
    drawHLine(sx(-BOWL_HALF_WIDTH,0),FOUL_LINE_Y,sx(BOWL_HALF_WIDTH,0)-sx(-BOWL_HALF_WIDTH,0)+1,BLUE);
    for(int i=-3;i<=3;i++){
        int x=sx(i*.32f,1.3f),y=sy(1.3f);
        drawLine(x-3,y+4,x,y-3,BLUE);drawLine(x,y-3,x+3,y+4,BLUE);
    }
    float elapsed=(float)(time_us_64()-d->animation_start)/1000000.0f;
    unsigned last=0;
    if(d->has_shot){
        while(last+1<d->path.count&&d->path.points[last+1].t<=elapsed)last++;
        for(unsigned i=1;i<=last;i++){
            const BowlPoint *a=&d->path.points[i-1],*b=&d->path.points[i];
            drawLine(sx(a->x,a->y),sy(a->y),sx(b->x,b->y),sy(b->y),YELLOW);
        }
    }
    bool complete=d->has_shot&&last+1==d->path.count;
    // The collision model retains physical pin coordinates. For the small VGA
    // display, exaggerate rack depth and spacing so the four rows remain legible.
    // Draw far rows first so nearer pins correctly cover farther pins.
    for(int i=9;i>=0;i--){
        float x,y;bowl_pin(i,&x,&y);
        int row=pin_row(i);
        int px=CAMERA_CENTER_X+(int)lroundf(x*PIN_RACK_X_SCALE*DISPLAY_X_CORRECTION);
        int py=PIN_RACK_FRONT_Y-row*PIN_RACK_ROW_GAP;
        char pin_color=complete&&d->path.pin==i?RED:WHITE;
        fill_display_circle(px,py-3,5,pin_color);
        fill_display_circle(px,py-8,6,pin_color);
        fillRect(px-2,py-16,5,9,pin_color);
        fill_display_circle(px,py-19,4,pin_color);
        drawHLine(px-2,py-14,5,RED);
        drawHLine(px-2,py-13,5,RED);
    }
    float x=d->has_shot?d->path.points[last].x:d->tuning.start_x;
    float y=d->has_shot?d->path.points[last].y:0;
    int r=(int)lroundf(BOWL_BALL_RADIUS*CAMERA_X_SCALE*perspective(y));
    int bx=sx(x,y),by=sy(y)-r;
    fill_display_circle(bx,by,r,BLUE);
    fill_display_circle(bx-(int)lroundf(r*DISPLAY_X_CORRECTION/3),by-r/3,r/5,CYAN);
    if(complete){
        if(d->path.pin>=0)snprintf(text,sizeof(text),"FIRST CONTACT: PIN %d",d->path.pin+1);
        else snprintf(text,sizeof(text),"%s",d->path.gutter?"GUTTER":d->path.stopped?"BALL STOPPED":"MISSED PINS");
        label(232,112,BLUE,text);
    }
    if(d->has_shot){
        snprintf(text,sizeof(text),"%s / Power %.2f / Hook %+.2f",bowl_labels[d->shot.label],d->shot.power,d->shot.hook);
        label(14,446-BOTTOM_SAFE_MARGIN,BLUE,text);
    }else label(14,446-BOTTOM_SAFE_MARGIN,BLUE,"Start with the same grip used for your training throws.");
    snprintf(text,sizeof(text),"Samples %u / I2C %u / gaps %u / rail %u",d->samples,d->errors,d->missed,d->rail);
    label(350,446-BOTTOM_SAFE_MARGIN,BLACK,text);
    label(14,460-BOTTOM_SAFE_MARGIN,BLUE,"First contact only / pin scattering and scoring pending");
}
static void draw_core(void){
    while(true){
        if(!draw_start_signal()){tight_loop_contents();continue;}
        mutex_enter_blocking(&lock);memcpy(&screen,&demo,sizeof(screen));mutex_exit(&lock);
        if(screen.view==GAME_VIEW)draw_game(&screen);else draw_menu(&screen);
    }
}
static void help(void){
    puts("# PICO SPORTS: USB terminal controls the VGA menus. W/S then Enter, or 1-5.");
    puts("# 1 BOWLING | 2 BASKETBALL (coming soon) | 3 CUP PONG (coming soon) | 4 SETTINGS | 5 QUIT");
    puts("# MENU: game menu | BACK: previous screen | SETTINGS: tuning | QUIT: stop session");
    puts("# Bowling: CAL (+Z up, still 3s), then Enter/THROW. REPLAY repeats last path.");
    puts("# Capture: 3s countdown, 0.5s still, 2s THROW NOW, 0.5s still.");
    puts("# Joystick Vy ADC0: horizontal/start; Vx ADC1: vertical; SW GP28: menu select.");
    puts("# SET start_x -0.5 | SET aim_deg 1 | SET hook_gain 1.5 | SET speed_gain 1.1");
    puts("# SET friction 0.12 | SET hook_start 0.1 | SET randomness 0 | RESET defaults");
    puts("# STOP cancels capture. This prototype has no pin scattering or scoring.");
}
static void replay(void) {
    bowl_simulate(&demo.shot,&demo.tuning,shot_seed,&demo.path);
    demo.animation_start=time_us_64();demo.state=PLAYING;
}
static void command(const char *input){
    // Accept command words from Serial Monitor in uppercase, lowercase, or a
    // mixture of both. SET parameter names are normalized separately below.
    char normalized[128];
    size_t length=strlen(input);
    if(length>=sizeof(normalized))length=sizeof(normalized)-1;
    memcpy(normalized,input,length);normalized[length]=0;
    for(size_t i=0;i<length&&normalized[i]!=' ';i++)
        if(normalized[i]>='a'&&normalized[i]<='z')normalized[i]-='a'-'A';
    const char *line=normalized;
    bool busy=demo.state==RECORDING||demo.state==COUNTDOWN;
    // Navigation may cancel a pending capture; never leave acquisition running behind a menu.
    bool home=!strcmp(line,"MENU")||!strcmp(line,"HOME");
    bool quit=!strcmp(line,"QUIT")||!strcmp(line,"Q")||!strcmp(line,"5");
    bool settings=!strcmp(line,"SETTINGS")||(!busy&&!strcmp(line,"4"));
    if(home||quit||settings||!strcmp(line,"BACK")){
        mutex_enter_blocking(&lock);
        if(busy){demo.state=calibrated?READY:NEED_CAL;demo.has_shot=false;}
        if(settings){demo.return_view=demo.view==GAME_VIEW?GAME_VIEW:HOME_VIEW;demo.view=SETTINGS_VIEW;}
        else if(!strcmp(line,"BACK")&&demo.view==SETTINGS_VIEW)demo.view=demo.return_view;
        else demo.view=quit?QUIT_VIEW:HOME_VIEW;
        mutex_exit(&lock);puts("OK,navigation");return;
    }
    if(busy){
        if(!strcmp(line,"STOP")){
            mutex_enter_blocking(&lock);demo.state=READY;mutex_exit(&lock);
            puts("OK,capture_cancelled");
        }
        return;
    }
    if(demo.view==HOME_VIEW){
        if(!strcmp(line,"W")||!strcmp(line,"S")){
            mutex_enter_blocking(&lock);demo.menu_index=(demo.menu_index+(!strcmp(line,"W")?4:1))%5;mutex_exit(&lock);
            puts("OK,selection");return;
        }
        int choice=!line[0]?(int)demo.menu_index:strlen(line)==1&&line[0]>='1'&&line[0]<='5'?line[0]-'1':-1;
        if(choice>=0){
            mutex_enter_blocking(&lock);
            demo.menu_index=(unsigned)choice;
            if(choice==0){demo.view=GAME_VIEW;demo.state=!healthy?SENSOR_ERROR:calibrated?(demo.has_shot?PLAYING:READY):NEED_CAL;}
            else if(choice<3){demo.view=UNAVAILABLE_VIEW;demo.unavailable_game=(unsigned)choice;}
            else if(choice==3){demo.return_view=HOME_VIEW;demo.view=SETTINGS_VIEW;}
            else demo.view=QUIT_VIEW;
            mutex_exit(&lock);puts("OK,selected");return;
        }
    }else if(!line[0]&&(demo.view==UNAVAILABLE_VIEW||demo.view==QUIT_VIEW)){
        mutex_enter_blocking(&lock);demo.view=HOME_VIEW;mutex_exit(&lock);puts("OK,menu");return;
    }
    if(!strcmp(line,"INFO")){printf("BOWL,1,%u,%u\n",healthy,calibrated);return;}
    if(!strcmp(line,"STOP")){puts("OK,idle");return;}
    if(!strcmp(line,"HELP")){help();puts("OK,help");return;}
    if(!strcmp(line,"CAL")) {
        if(!healthy){puts("ERR,sensor");return;}
        mutex_enter_blocking(&lock);demo.view=GAME_VIEW;demo.state=CALIBRATING;demo.has_shot=false;demo.is_calibrated=false;mutex_exit(&lock);
        calibrated=imu_calibrate(&bias);
        mutex_enter_blocking(&lock);demo.is_calibrated=calibrated;demo.state=calibrated?READY:NEED_CAL;mutex_exit(&lock);
        if(calibrated)puts("CAL,OK - Press Enter to record a throw.");
        else printf("ERR,%s\n",imu_calibration_report());
        return;
    }
    if(!line[0]||!strcmp(line,"THROW")) {
        if(demo.view!=GAME_VIEW){puts("ERR,Select Bowling from MENU first");return;}
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
    if(set)for(char *p=key;*p;p++)if(*p>='A'&&*p<='Z')*p+='a'-'A';
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
    adc_init();adc_gpio_init(26);adc_gpio_init(27);
    gpio_init(JOY_SW);gpio_set_dir(JOY_SW,GPIO_IN);gpio_pull_up(JOY_SW);
    joystick_last=time_us_64();
    healthy=imu_init();demo.sensor_ok=healthy;demo.view=HOME_VIEW;demo.state=healthy?NEED_CAL:SENSOR_ERROR;demo.tuning=bowl_defaults();
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
        update_joystick(now);
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
