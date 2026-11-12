/* Desktop test adapter. Runs the actual firmware draw function into a pixel buffer.
 * Pico APIs are stubbed; it cannot verify physical VGA timing or hardware I/O.
 */
#include <setjmp.h>
#include <stdlib.h>
#define main unused_pico_entry
#include "../bowling_demo.c"
#undef main
#include "../VGA/font_glcd.c"
static unsigned char pixels[640*480];
static jmp_buf draw_done;
static int frames;
static uint64_t mock_time;
uint64_t time_us_64(void){return mock_time;}
void stdio_init_all(void){} void stdio_flush(void){fflush(stdout);}
void sleep_us(uint64_t us){mock_time+=us;} void tight_loop_contents(void){}
int getchar_timeout_us(uint32_t us){(void)us;return PICO_ERROR_TIMEOUT;}
bool stdio_usb_connected(void){return false;}
void multicore_launch_core1(void(*entry)(void)){(void)entry;}
void mutex_init(mutex_t*m){(void)m;} void mutex_enter_blocking(mutex_t*m){(void)m;} void mutex_exit(mutex_t*m){(void)m;}
bool set_sys_clock_khz(unsigned khz,bool required){(void)khz;(void)required;return true;}
bool imu_init(void){return false;} bool imu_read(ImuReading*r){(void)r;return false;}
bool imu_calibrate(ImuBias*b){(void)b;return false;}
const char *imu_calibration_report(void){return "test";}
void initVGA(void){}
int draw_start_signal(void){if(frames++)longjmp(draw_done,1);return 1;}
void drawPixel(short x,short y,char color){if(x>=0&&x<640&&y>=0&&y<480)pixels[y*640+x]=(unsigned char)color;}
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
    memset(&demo,0,sizeof(demo));demo.state=PLAYING;demo.tuning=*tuning;demo.shot=*shot;demo.has_shot=true;demo.samples=300;
    bowl_simulate(shot,tuning,seed,&demo.path);mock_time=(uint64_t)(seconds*1000000);frames=0;
    if(!setjmp(draw_done))draw_core();
    return pixels;
}
