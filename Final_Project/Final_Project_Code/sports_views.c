#include <math.h>
#include "sports_views.h"
#include "VGA/vga16_graphics_v3.h"

#define DISPLAY_X_CORRECTION 0.75f
#define BOTTOM_SAFE_MARGIN 8

static void sports_label(int x,int y,char color,const char *s){
    drawTextGLCD(x,y,(char *)s,color,WHITE);
}
static void sports_title(int x,int y,char color,const char *s){
    drawTextArial24(x,y,(char *)s,color,WHITE);
}
static void sports_fill_circle(int x,int y,int radius,char color){
    int rx=(int)lroundf(radius*DISPLAY_X_CORRECTION);
    if(rx<1)rx=1;
    for(int dy=-radius;dy<=radius;dy++){
        float n=(float)(dy*dy)/(float)(radius*radius);
        int dx=(int)lroundf(rx*sqrtf(fmaxf(0,1-n)));
        drawHLine(x-dx,y+dy,2*dx+1,color);
    }
}
static void sports_circle(int x,int y,int radius,char color){
    int rx=(int)lroundf(radius*DISPLAY_X_CORRECTION);
    if(rx<1)rx=1;
    for(int dy=-radius;dy<=radius;dy++){
        float n=(float)(dy*dy)/(float)(radius*radius);
        int dx=(int)lroundf(rx*sqrtf(fmaxf(0,1-n)));
        drawPixel(x-dx,y+dy,color);drawPixel(x+dx,y+dy,color);
    }
    for(int dx=-rx;dx<=rx;dx++){
        float n=(float)(dx*dx)/(float)(rx*rx);
        int dy=(int)lroundf(radius*sqrtf(fmaxf(0,1-n)));
        drawPixel(x+dx,y-dy,color);drawPixel(x+dx,y+dy,color);
    }
}
static void sports_ellipse(int x,int y,int rx,int ry,char color){
    for(int dy=-ry;dy<=ry;dy++){
        float n=(float)(dy*dy)/(float)(ry*ry);
        int dx=(int)lroundf(rx*sqrtf(fmaxf(0,1-n)));
        drawPixel(x-dx,y+dy,color);drawPixel(x+dx,y+dy,color);
    }
}

void draw_basketball_view(void){
    sports_title(24,22,BLUE,"BASKETBALL");
    sports_label(442,28,BLUE,"PRELIMINARY VIEW");
    sports_label(24,58,BLACK,"Face the hoop. Aim upward and mimic a real shooting motion.");
    drawHLine(24,78,592,LIGHT_BLUE);
    fillRect(24,94,592,236,LIGHT_BLUE);
    fillRect(176,104,288,138,WHITE);
    drawRect(176,104,288,138,BLUE);drawRect(181,109,278,128,BLUE);
    drawRect(270,157,100,66,RED);drawRect(278,165,84,50,RED);
    fillRect(304,218,32,5,RED);
    sports_ellipse(320,232,69,12,RED);sports_ellipse(320,235,65,10,ORANGE);
    for(int i=-3;i<=3;i++)drawLine(320+i*18,241,320+i*8,305,WHITE);
    for(int y=253;y<=294;y+=13)drawLine(270+(y-241)/3,y,370-(y-241)/3,y,WHITE);
    fillRect(24,330,592,104,ORANGE);
    for(int x=96;x<=544;x+=64)drawLine(x,434,320+(x-320)/4,330,DARK_ORANGE);
    drawHLine(24,330,592,DARK_ORANGE);
    sports_fill_circle(320,402,38,DARK_ORANGE);sports_circle(320,402,38,BLACK);
    drawLine(320,364,320,440,BLACK);
    drawLine(292,376,348,428,BLACK);drawLine(348,376,292,428,BLACK);
    sports_label(24,344,BLUE,"PLAYER VIEW");sports_label(470,344,BLUE,"AIM UP");
    sports_label(24,446-BOTTOM_SAFE_MARGIN,BLACK,"Motion capture and shot feedback coming next.");
    sports_label(24,460-BOTTOM_SAFE_MARGIN,BLUE,"Press joystick SW, Enter, BACK, or MENU to return.");
}

static void draw_pong_cup(int x,int y,int w,int h){
    sports_ellipse(x,y,w/2,4,WHITE);sports_ellipse(x,y,w/2,4,RED);
    for(int row=1;row<h;row++){
        int half=w/2-(w/5)*row/h;
        drawHLine(x-half,y+row,2*half+1,RED);
    }
    drawHLine(x-w*3/10,y+h,w*3/5+1,BLACK);
}
void draw_cup_pong_view(void){
    sports_title(24,22,BLUE,"CUP PONG");
    sports_label(442,28,BLUE,"PRELIMINARY VIEW");
    sports_label(24,58,BLACK,"Face the cups. Aim forward and mimic an underhand toss.");
    drawHLine(24,78,592,LIGHT_BLUE);
    fillRect(24,94,592,70,LIGHT_BLUE);
    for(int y=164;y<=434;y++){
        int inset=(434-y)*140/270;
        drawHLine(24+inset,y,592-2*inset,BLUE);
    }
    drawLine(164,164,24,434,WHITE);drawLine(476,164,616,434,WHITE);
    drawLine(320,164,320,434,WHITE);drawHLine(164,164,313,WHITE);
    for(int row=0;row<4;row++){
        int count=4-row,y=176+row*38,w=29+row*3,h=27+row*3,gap=w+5;
        int start=320-(count-1)*gap/2;
        for(int col=0;col<count;col++)draw_pong_cup(start+col*gap,y,w,h);
    }
    sports_fill_circle(424,388,17,WHITE);sports_circle(424,388,17,BLACK);
    drawLine(410,362,366,314,YELLOW);drawLine(438,362,366,314,YELLOW);
    drawLine(366,314,366,332,YELLOW);
    sports_label(130,344,WHITE,"PLAYER VIEW");sports_label(470,344,WHITE,"AIM");
    sports_label(24,446-BOTTOM_SAFE_MARGIN,BLACK,"Motion capture, trajectory, and cup detection coming next.");
    sports_label(24,460-BOTTOM_SAFE_MARGIN,BLUE,"Press joystick SW, Enter, BACK, or MENU to return.");
}
