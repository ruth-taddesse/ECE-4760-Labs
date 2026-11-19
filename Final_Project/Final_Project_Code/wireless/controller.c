#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "hardware/adc.h"
#include "imu.h"
#include "radio.h"
static mutex_t guard;
static WirePacket shared;
static uint32_t job,job_op,job_owner;
static uint64_t last_command;
static ImuBias bias;
static bool current(uint32_t owner,uint32_t id){
    mutex_enter_blocking(&guard);bool ok=job==id&&job_owner==owner;mutex_exit(&guard);return ok;
}
static void worker(void){
    uint32_t handled=0,owner=0;
    for(;;){
        mutex_enter_blocking(&guard);
        uint32_t id=job,host=job_owner,op=job_op;
        bool ready=shared.healthy&&shared.calibrated;
        mutex_exit(&guard);
        if(id==handled&&host==owner){sleep_ms(1);continue;}
        handled=id;owner=host;
        mutex_enter_blocking(&guard);
        shared.request=id;shared.target=host;
        shared.state=op==OP_CAL?2:op==OP_THROW&&ready?3:shared.healthy?(shared.calibrated?1:0):7;
        shared.countdown=op==OP_THROW&&ready?3:0;
        if(op==OP_THROW){shared.samples=shared.errors=shared.missed=shared.rail=0;shared.phase=0;}
        mutex_exit(&guard);
        if(op==OP_CAL){
            bool ok=imu_calibrate(&bias);
            mutex_enter_blocking(&guard);
            if(job==id&&job_owner==host){shared.calibrated=ok;shared.state=ok?1:0;
                snprintf(shared.calibration_error,sizeof(shared.calibration_error),"%.127s",ok?"":imu_calibration_report());}
            mutex_exit(&guard);
            printf("# CAL %s: %s\n",ok?"OK":"FAILED",imu_calibration_report());
        }else if(op==OP_THROW&&ready){
            BowlFeatures features;bowl_features_reset(&features);
            uint32_t samples=0,errors=0,missed=0,rail=0;
            uint64_t begin=time_us_64()+3000000,next=begin;
            while(current(host,id)&&time_us_64()<begin){
                uint64_t now=time_us_64();
                mutex_enter_blocking(&guard);shared.countdown=now<begin?(uint32_t)((begin-now+999999)/1000000):0;mutex_exit(&guard);
                sleep_ms(5);
            }
            while(current(host,id)&&time_us_64()<begin+3000000){
                sleep_until(from_us_since_boot(next));
                uint64_t now=time_us_64();if(now>=begin+3000000)break;
                unsigned gaps=(unsigned)((now-next)/10000);missed+=gaps;next+=((uint64_t)gaps+1)*10000;
                uint64_t elapsed=now-begin;ImuReading r;bool ok=imu_read(&r);
                if(ok){samples++;rail+=r.near_rail;}else errors++;
                if(ok&&elapsed>=500000&&elapsed<2500000){
                    for(int j=0;j<3;j++){r.a[j]-=bias.a[j];r.g[j]-=bias.g[j];}
                    bowl_features_add(&features,r.a,r.g,elapsed*1e-6f);
                }
                mutex_enter_blocking(&guard);
                shared.state=4;shared.countdown=0;shared.phase=elapsed<500000?0:elapsed<2500000?1:2;
                shared.samples=samples;shared.errors=errors;shared.missed=missed;shared.rail=rail;
                mutex_exit(&guard);
            }
            BowlThrow shot;
            bool valid=!errors&&!missed&&!rail&&bowl_classify(&features,&shot);
            mutex_enter_blocking(&guard);
            if(job==id&&job_owner==host){shared.state=valid?5:6;if(valid)shared.shot=shot;}
            mutex_exit(&guard);
        }
    }
}
static unsigned axis(unsigned channel){adc_select_input(channel);adc_read();unsigned sum=0;for(int i=0;i<8;i++)sum+=adc_read();return sum/8;}
int main(void){
    stdio_init_all();mutex_init(&guard);
    shared.magic=WIRE_MAGIC;shared.version=WIRE_VERSION;shared.kind=WIRE_STATUS;
    shared.healthy=imu_init();shared.state=shared.healthy?0:7;
    adc_init();adc_gpio_init(26);adc_gpio_init(27);gpio_init(28);gpio_set_dir(28,GPIO_IN);gpio_pull_up(28);
    if(!radio_init(false))panic("Controller Wi-Fi init failed");
    shared.session=radio_session();multicore_launch_core1(worker);
    uint64_t send_at=0,changed=0,down_at=0;bool raw=false,stable=false,long_sent=false;
    uint32_t peer_seq=0,peer=0;
    for(;;){
        radio_poll();uint64_t now=time_us_64();WirePacket cmd;
        if(radio_take(&cmd)&&cmd.target==radio_session()){
            if(cmd.session!=peer||wire_newer(cmd.sequence,peer_seq)){
                peer=cmd.session;peer_seq=cmd.sequence;last_command=now;
                mutex_enter_blocking(&guard);
                if(cmd.session!=job_owner||cmd.request!=job){job_owner=cmd.session;job=cmd.request;job_op=cmd.operation;}
                mutex_exit(&guard);
            }
        }
        if(last_command&&now-last_command>1500000){
            mutex_enter_blocking(&guard);job_owner=0;job=0;job_op=OP_STOP;mutex_exit(&guard);
            last_command=0;peer=0;
        }
        bool pressed=!gpio_get(28);
        if(pressed!=raw){raw=pressed;changed=now;}
        if(now-changed>=30000&&stable!=raw){
            stable=raw;
            if(stable){down_at=now;long_sent=false;}
            else if(!long_sent){mutex_enter_blocking(&guard);shared.select_count++;mutex_exit(&guard);}
        }
        if(stable&&!long_sent&&now-down_at>=1000000){
            mutex_enter_blocking(&guard);shared.menu_count++;mutex_exit(&guard);long_sent=true;
        }
        if(now>=send_at){
            unsigned x=axis(0),y=axis(1);WirePacket packet;
            mutex_enter_blocking(&guard);shared.joy_x=x;shared.joy_y=y;shared.sequence++;packet=shared;mutex_exit(&guard);
            radio_send(&packet);send_at=now+20000;
        }
        sleep_us(500);
    }
}
