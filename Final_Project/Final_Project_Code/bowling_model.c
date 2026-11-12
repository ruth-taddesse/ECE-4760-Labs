/* Pure C math shared by Pico and offline replay tests. Game units, not recovered SI velocity. */
#include "bowling_model.h"
#include "bowling_trained.h"
#include <math.h>
#include <string.h>
#include <float.h>
const char *bowl_labels[5]={"Straight slow","Straight normal","Straight fast","Hook left","Hook right"};
static float clamp(float v,float lo,float hi) { return fmaxf(lo,fminf(hi,v)); }
void bowl_features_reset(BowlFeatures *f) {
    memset(f,0,sizeof(*f));
    for(int j=0;j<3;j++){ f->low[j]=FLT_MAX; f->high[j]=-FLT_MAX; }
}
void bowl_features_add(BowlFeatures *f,const float a[3],const float g[3],float t) {
    f->peak=fmaxf(f->peak,sqrtf(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]));
    for(int j=0;j<3;j++) {
        f->low[j]=fminf(f->low[j],g[j]);f->high[j]=fmaxf(f->high[j],g[j]);
        if(f->count) f->integral[j]+=(g[j]+f->previous_g[j])*0.5f*(t-f->previous_t);
        f->sum_a[j]+=a[j];f->previous_g[j]=g[j];
    }
    f->previous_t=t;f->count++;
}
bool bowl_classify(const BowlFeatures *f,BowlThrow *r) {
    if(f->count<150) return false;
    float activity=0;
    for(int j=0;j<3;j++)activity=fmaxf(activity,fmaxf(fabsf(f->low[j]),fabsf(f->high[j])));
    if(f->peak<11.5f || activity<0.6f)return false;
    memset(r,0,sizeof(*r));r->peak=r->features[0]=f->peak;
    for(int j=0;j<3;j++) {
        r->features[1+j]=f->low[j];r->features[4+j]=f->high[j];
        r->features[7+j]=f->integral[j];r->features[10+j]=f->sum_a[j]/f->count;
    }
    float d[5],best=FLT_MAX;
    for(int c=0;c<5;c++) {
        d[c]=0;
        for(int j=0;j<13;j++) {
            float z=(r->features[j]-class_centers[c][j])/feature_scale[j];d[c]+=z*z/13.0f;
        }
        if(d[c]<best){best=d[c];r->label=c;}
    }
    float w[5],total=0;
    for(int c=0;c<5;c++){w[c]=expf(-(d[c]-best)/0.35f);total+=w[c];}
    // Similarity weights, not calibrated probabilities. Straight classes contribute zero hook.
    r->certainty=w[r->label]/total;
    float hook=(w[4]-w[3])/total;
    r->hook=fabsf(hook)<0.12f ? 0 : copysignf((fabsf(hook)-0.12f)/0.88f,hook);
    r->power=clamp((f->peak-POWER_SLOW)/(POWER_FAST-POWER_SLOW),0,1);
    return true;
}
BowlTuning bowl_defaults(void) { return (BowlTuning){0,0,1,1,0.12f,0.10f,0}; }
bool bowl_set(BowlTuning *t,const char *key,float v) {
    if(!isfinite(v)) return false;
    #define SET(name,lo,hi) if(!strcmp(key,#name)){if(v<lo||v>hi)return false;t->name=v;return true;}
    SET(start_x,-1.3f,1.3f) SET(aim_deg,-10,10) SET(speed_gain,0.5f,2)
    SET(hook_gain,0,3) SET(friction,0,1) SET(hook_start,0,0.9f) SET(randomness,0,1)
    #undef SET
    return false;
}
void bowl_pin(int index,float *x,float *y) {
    int row=0;while(index>=row+1){index-=row+1;row++;}
    *x=(index-0.5f*row)*0.55f;*y=5.7f+row*0.476314f;
}
static float random_signed(unsigned *seed) {
    *seed=*seed*1664525u+1013904223u;
    return ((float)((*seed>>8)&0xffffff)/16777215.0f)*2-1;
}
void bowl_simulate(const BowlThrow *s,const BowlTuning *t,unsigned seed,BowlPath *p) {
    memset(p,0,sizeof(*p));p->pin=-1;
    // At randomness=1: at most +/-0.5 degree aim and +/-2% initial speed.
    float angle=(t->aim_deg+0.5f*t->randomness*random_signed(&seed))*0.01745329252f;
    p->speed=(3.0f+3.0f*s->power)*t->speed_gain*(1+0.02f*t->randomness*random_signed(&seed));
    p->hook_accel=1.8f*s->hook*t->hook_gain;
    float x=t->start_x,y=0,vx=sinf(angle)*p->speed,vy=cosf(angle)*p->speed,time=0;
    const float dt=1.0f/120;
    p->points[p->count++]=(BowlPoint){x,y,time};
    for(unsigned n=1;n<BOWL_PATH_MAX;n++) {
        float progress=clamp((y/BOWL_LENGTH-t->hook_start)/(1-t->hook_start),0,1);
        float ramp=progress*progress*(3-2*progress);
        float speed=sqrtf(vx*vx+vy*vy);
        if(speed<0.1f || vy<=0){p->stopped=true;break;}
        float drag=fmaxf(0,1-t->friction*dt/speed);
        vx=vx*drag+p->hook_accel*ramp*dt;vy*=drag;
        // Small steps plus a swept segment test prevent skipping pins at high speeds.
        float nx=x+vx*dt,ny=y+vy*dt,dx=nx-x,dy=ny-y;
        float earliest=1;int hit=-1;bool gutter=false,end=false;
        float edge=BOWL_HALF_WIDTH-BOWL_BALL_RADIUS;
        if(nx>edge || nx<-edge) { earliest=clamp(((nx>0?edge:-edge)-x)/dx,0,1);gutter=true; }
        if(ny>=BOWL_LENGTH) {
            float u=(BOWL_LENGTH-y)/dy;if(u<earliest){earliest=u;gutter=false;end=true;}
        }
        for(int i=0;i<10;i++) {
            float px,py;bowl_pin(i,&px,&py);
            float ox=x-px,oy=y-py,r=BOWL_BALL_RADIUS+BOWL_PIN_RADIUS;
            float A=dx*dx+dy*dy,B=2*(ox*dx+oy*dy),C=ox*ox+oy*oy-r*r;
            float discriminant=B*B-4*A*C;
            if(A>0 && discriminant>=0) {
                float u=(-B-sqrtf(discriminant))/(2*A);
                if(u>=0 && u<=earliest){earliest=u;hit=i;gutter=false;end=false;}
            }
        }
        time+=dt*earliest;x+=dx*earliest;y+=dy*earliest;
        p->points[p->count++]=(BowlPoint){x,y,time};
        if(hit>=0 || gutter || end){p->pin=hit;p->gutter=gutter;break;}
    }
}
