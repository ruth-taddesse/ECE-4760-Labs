/* Desktop adapter for the actual collection firmware renderer and commands.
 * Stubs verify screen layout, not physical VGA or I2C timing.
 */
#include <setjmp.h>
#include <stdlib.h>
#define main unused_pico_entry
#include "../imu_training_data.c"
#undef main
#include "../VGA/font_glcd.c"
#include "../VGA/font_Arial_round_16x24.h"
static unsigned char pixels[640*480];
static jmp_buf draw_done;
static int frames;
static uint64_t mock_time;
static bool calibration_ok;
static unsigned out_of_bounds;
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
void drawPixel(short x,short y,char color){
    if(x>=0&&x<640&&y>=0&&y<480)pixels[y*640+x]=(unsigned char)color;
    else out_of_bounds++;
}
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
void fillRect(short x,short y,short w,short h,char c){for(int i=0;i<h;i++)drawHLine(x,y+i,w,c);}
int drawTextArial24(short x,short y,char *s,char c,char bg){
    int start=x;
    for(;*s;s++,x+=16)for(int row=0;row<24;row++)for(int col=0;col<16;col++) {
        unsigned char bits=Arial_round_16x24[((unsigned char)*s-32)*48+row*2+col/8];
        drawPixel(x+col,y+row,(bits&(1u<<(7-col%8)))?c:bg);
    }
    return x-start;
}
// Synthetic signals exercise the actual renderer; these are not training recordings.
unsigned char *collection_render(int state,unsigned phase,int flags,int sport) {
    memset(&capture,0,sizeof(capture));
    capture.state=state;capture.phase=phase;capture.pre_ms=500;capture.motion_ms=2000;capture.post_ms=500;
    capture.sport=sports[sport];capture.category=sport==3?2:0;
    capture.usb_connected=(flags&1)!=0;capture.calibrated=(flags&2)!=0;
    capture.calibration_failed=(flags&4)!=0;capture.stopped=(flags&8)!=0;
    capture.countdown=3;
    if(state==COUNTDOWN || state==RECORDING || state==DONE)strcpy(capture.trial,"preview-001");
    if(state==LIVE || state==RECORDING || state==DONE) {
        capture.count=state==RECORDING?(phase==0?25:phase==1?160:280):300;
        capture.elapsed_us=capture.count*10000;
        for(unsigned i=0;i<capture.count;i++) {
            Sample *s=&capture.samples[i];s->seq=i;s->elapsed_us=i*10000;s->pico_us=1000000+i*10000;
            float t=i/100.0f,activity=t>0.5f&&t<2.5f?sinf((t-.5f)*1.5707963f):0;
            for(int j=0;j<3;j++) {
                s->value.a[j]=(j==2?GRAVITY:0)+activity*18*sinf(12*t+j);
                s->value.g[j]=activity*6*cosf(10*t+j);
            }
            capture.peak_a=fmaxf(capture.peak_a,mag(s->value.a));
            capture.peak_g=fmaxf(capture.peak_g,mag(s->value.g));
        }
    }
    if(flags&16){capture.errors=1;capture.missed=2;capture.clips=1;}
    out_of_bounds=0;draw_capture(&capture);return pixels;
}
unsigned collection_bounds_errors(void){return out_of_bounds;}
int collection_check_commands(void) {
    // Exercise snapshot flags through real CAL, START and STOP commands.
    memset(&capture,0,sizeof(capture));capture.sport=&bowling;capture.usb_connected=true;
    healthy=true;calibrated=false;calibration_ok=false;
    command("CAL");if(capture.calibrated || !capture.calibration_failed)return 1;
    calibration_ok=true;command("CAL");
    if(!capture.calibrated || capture.calibration_failed || capture.trial[0])return 2;
    command("START preview-002 3 2 500 2000 500 3000");
    if(capture.state!=COUNTDOWN || !capture.calibrated || !capture.usb_connected || capture.category!=2)return 3;
    command("STOP");if(capture.state!=DONE || !capture.stopped)return 4;
    return 0;
}
