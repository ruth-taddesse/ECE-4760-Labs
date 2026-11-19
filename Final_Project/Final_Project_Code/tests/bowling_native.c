/* Desktop test adapter. Runs the actual firmware draw function into a pixel buffer.
 * Pico APIs are stubbed; it cannot verify physical VGA timing or hardware I/O.
 */
#include <setjmp.h>
#include <stdlib.h>
#define main unused_pico_entry
#include "../bowling_demo.c"
#undef main
#include "../VGA/font_glcd.c"
#include "../VGA/font_Arial_round_16x24.h"
static unsigned char pixels[640*480];
static jmp_buf draw_done;
static int frames;
static uint64_t mock_time;
static unsigned bounds_errors;
static bool calibration_ok;
static uint16_t mock_adc=2048, mock_y=2048;
static unsigned mock_channel;
static bool mock_sw=true;
void gpio_init(unsigned p){(void)p;}
void gpio_set_dir(unsigned p,bool out){(void)p;(void)out;}
void gpio_pull_up(unsigned p){(void)p;}
bool gpio_get(unsigned p){(void)p;return mock_sw;}
void adc_init(void){} void adc_gpio_init(unsigned gpio){(void)gpio;}
void adc_select_input(unsigned input){mock_channel=input;}
uint16_t adc_read(void){return mock_channel==0?mock_adc:mock_y;}
float test_joystick(unsigned raw,int state,float start,unsigned ticks){
    memset(&demo,0,sizeof(demo));demo.view=GAME_VIEW;demo.state=state;demo.tuning=bowl_defaults();
    demo.tuning.start_x=start;mock_adc=(uint16_t)raw;joystick_last=0;
    for(unsigned i=1;i<=ticks;i++)update_joystick(i*10000ull);
    return demo.tuning.start_x;
}
uint64_t time_us_64(void){return mock_time;}
void stdio_init_all(void){} void stdio_flush(void){fflush(stdout);}
void sleep_us(uint64_t us){mock_time+=us;} void tight_loop_contents(void){}
int getchar_timeout_us(uint32_t us){(void)us;return PICO_ERROR_TIMEOUT;}
bool stdio_usb_connected(void){return false;}
void multicore_launch_core1(void(*entry)(void)){(void)entry;}
void mutex_init(mutex_t*m){(void)m;} void mutex_enter_blocking(mutex_t*m){(void)m;} void mutex_exit(mutex_t*m){(void)m;}
bool set_sys_clock_khz(unsigned khz,bool required){(void)khz;(void)required;return true;}
bool imu_init(void){return false;} bool imu_read(ImuReading*r){(void)r;return false;}
bool imu_calibrate(ImuBias*b){memset(b,0,sizeof(*b));return calibration_ok;}
const char *imu_calibration_report(void){return "test";}
void initVGA(void){}
int draw_start_signal(void){if(frames++)longjmp(draw_done,1);return 1;}
void drawPixel(short x,short y,char color){if(x>=0&&x<640&&y>=0&&y<480)pixels[y*640+x]=(unsigned char)color;else bounds_errors++;}
void drawHLine(int x,int y,int w,char c){for(int i=0;i<w;i++)drawPixel(x+i,y,c);}
void drawVLine(short x,short y,short h,char c){for(int i=0;i<h;i++)drawPixel(x,y+i,c);}
void drawRect(short x,short y,short w,short h,char c){drawHLine(x,y,w,c);drawHLine(x,y+h-1,w,c);drawVLine(x,y,h,c);drawVLine(x+w-1,y,h,c);}
void clearLowFrame(short top,short c){for(int y=top;y<480;y++)drawHLine(0,y,640,c);}
void fillCircle(short x,short y,short r,char c){for(int dy=-r;dy<=r;dy++)for(int dx=-r;dx<=r;dx++)if(dx*dx+dy*dy<=r*r)drawPixel(x+dx,y+dy,c);}
void drawLine(short x0,short y0,short x1,short y1,char c){
    int dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    while(1){drawPixel(x0,y0,c);if(x0==x1&&y0==y1)break;int e=2*err;if(e>=dy){err+=dy;x0+=sx;}if(e<=dx){err+=dx;y0+=sy;}}
}
int drawTextGLCD(short x,short y,char *s,char c,char bg){
    int start=x;
    for(;*s;s++,x+=6)for(int col=0;col<6;col++)for(int row=0;row<8;row++){
        unsigned char bits=col<5?font[(unsigned char)*s*5+col]:0;
        drawPixel(x+col,y+row,(bits&(1u<<row))?c:bg);
    }
    return x-start;
}
unsigned char *test_render(const BowlThrow *shot,const BowlTuning *tuning,unsigned seed,float seconds){
    memset(&demo,0,sizeof(demo));demo.view=GAME_VIEW;demo.state=PLAYING;demo.tuning=*tuning;demo.shot=*shot;demo.has_shot=true;demo.samples=300;
    bowl_simulate(shot,tuning,seed,&demo.path);mock_time=(uint64_t)(seconds*1000000);frames=0;
    if(!setjmp(draw_done))draw_core();
    return pixels;
}

void fillRect(short x,short y,short w,short h,char c){for(int i=0;i<h;i++)drawHLine(x,y+i,w,c);}
void drawCircle(short x,short y,short r,char c){
    // Same midpoint algorithm used by the VGA library.
    int f=1-r,dx=1,dy=-2*r,a=0,b=r;
    drawPixel(x,y+r,c);drawPixel(x,y-r,c);drawPixel(x+r,y,c);drawPixel(x-r,y,c);
    while(a<b){
        if(f>=0){b--;dy+=2;f+=dy;}a++;dx+=2;f+=dx;
        drawPixel(x+a,y+b,c);drawPixel(x-a,y+b,c);drawPixel(x+a,y-b,c);drawPixel(x-a,y-b,c);
        drawPixel(x+b,y+a,c);drawPixel(x-b,y+a,c);drawPixel(x+b,y-a,c);drawPixel(x-b,y-a,c);
    }
}
int drawTextArial24(short x,short y,char *s,char c,char bg){
    int start=x;
    for(;*s;s++,x+=16)for(int row=0;row<24;row++)for(int col=0;col<16;col++){
        unsigned char bits=Arial_round_16x24[((unsigned char)*s-32)*48+row*2+col/8];
        drawPixel(x+col,y+row,(bits&(1u<<(7-col%8)))?c:bg);
    }
    return x-start;
}
unsigned char *test_ui(int view,int state,unsigned selection,float seconds){
    memset(&demo,0,sizeof(demo));demo.view=view;demo.state=state;demo.menu_index=selection;
    demo.unavailable_game=selection;demo.sensor_ok=true;demo.tuning=bowl_defaults();demo.countdown=3;demo.phase=selection;
    if(state==PLAYING){
        demo.has_shot=true;demo.shot.power=.5f;demo.shot.hook=.35f;
        bowl_simulate(&demo.shot,&demo.tuning,42,&demo.path);demo.samples=300;
    }
    mock_time=(uint64_t)(seconds*1000000);bounds_errors=0;frames=0;
    if(!setjmp(draw_done))draw_core();return pixels;
}
unsigned test_bounds_errors(void){return bounds_errors;}
int test_navigation(void){
    memset(&demo,0,sizeof(demo));demo.tuning=bowl_defaults();healthy=true;calibrated=false;
    command("2");if(demo.view!=UNAVAILABLE_VIEW)return 1;
    command("");command("W");if(demo.menu_index!=0)return 2;
    command("");if(demo.view!=GAME_VIEW||demo.state!=NEED_CAL)return 3;
    calibration_ok=false;command("CAL");if(demo.state!=NEED_CAL||demo.is_calibrated)return 4;
    calibration_ok=true;command("CAL");if(demo.state!=READY||!demo.is_calibrated)return 5;
    command("THROW");if(demo.state!=COUNTDOWN)return 6;
    command("MENU");if(demo.view!=HOME_VIEW||demo.state!=READY)return 7;
    command("1");command("SETTINGS");command("SET aim_deg 2");
    if(demo.view!=SETTINGS_VIEW||demo.tuning.aim_deg!=2)return 8;
    command("BACK");if(demo.view!=GAME_VIEW)return 9;
    command("THROW");command("QUIT");if(demo.view!=QUIT_VIEW||demo.state!=READY)return 10;
    command("");if(demo.view!=HOME_VIEW)return 11;
    command("4");command("BACK");if(demo.view!=HOME_VIEW)return 12;
    return 0;
}

int test_joystick_menu(void){
    memset(&demo,0,sizeof(demo));demo.view=HOME_VIEW;demo.state=NEED_CAL;
    demo.tuning=bowl_defaults();joystick_last=button_changed=0;
    button_raw=button_stable=false;menu_direction=0;mock_sw=true;
    mock_adc=4095;mock_y=2048;update_joystick(10000);
    if(demo.menu_index!=1||demo.tuning.start_x!=0)return 1;
    update_joystick(20000);if(demo.menu_index!=1)return 2;
    mock_adc=2048;update_joystick(30000);
    mock_y=0;update_joystick(40000);if(demo.menu_index!=3)return 3;
    mock_y=2048;mock_sw=false;update_joystick(50000);
    mock_sw=true;update_joystick(60000); // bounce: no selection
    if(demo.view!=HOME_VIEW)return 4;
    mock_sw=false;update_joystick(70000);update_joystick(100000);
    if(demo.view!=SETTINGS_VIEW)return 5;
    update_joystick(200000);if(demo.view!=SETTINGS_VIEW)return 6;
    mock_sw=true;update_joystick(210000);update_joystick(240000);
    mock_sw=false;update_joystick(250000);update_joystick(280000);
    if(demo.view!=HOME_VIEW)return 7;
    mock_sw=true;button_raw=button_stable=false;
    return 0;
}
