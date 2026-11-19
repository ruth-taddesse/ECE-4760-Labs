// Included by bowling_demo.c only for the wireless ground-station target.
static WirePacket remote_packet;
static uint32_t remote_session,remote_sequence,remote_request=1,remote_operation=OP_STOP;
static uint32_t remote_select_seen,remote_menu_seen,remote_applied;
static uint64_t remote_seen,remote_send_at,remote_request_since;
static bool remote_acked;
static bool remote_connected,remote_press;
static void remote_request_op(unsigned op){
    remote_request++;remote_operation=op;remote_applied=0;remote_acked=false;remote_request_since=time_us_64();
}
static void remote_tick(void){
    radio_poll();uint64_t now=time_us_64();WirePacket p;
    if(radio_take(&p)){
        bool fresh=!remote_connected||p.session!=remote_session;
        if(fresh||wire_newer(p.sequence,remote_sequence)){
            if(fresh){
                remote_request_op(OP_STOP);remote_select_seen=p.select_count;remote_menu_seen=p.menu_count;
                mutex_enter_blocking(&lock);demo.has_shot=false;mutex_exit(&lock);
            }
            remote_session=p.session;remote_sequence=p.sequence;remote_seen=now;remote_connected=true;remote_packet=p;
            healthy=p.healthy;calibrated=p.calibrated;
            mutex_enter_blocking(&lock);
            demo.sensor_ok=healthy;demo.is_calibrated=calibrated;demo.wireless_connected=true;
            if(p.target==radio_session()&&p.request==remote_request){
                remote_acked=true;
                if(p.state==PLAYING){
                    if(remote_applied!=p.request){demo.shot=p.shot;demo.has_shot=true;shot_seed=p.sequence;replay();remote_applied=p.request;
                        printf("RESULT,%s,power=%.3f,hook=%.3f,peak=%.3f,first_pin=%d\n",bowl_labels[demo.shot.label],demo.shot.power,demo.shot.hook,demo.shot.peak,demo.path.pin+1);}
                }else demo.state=p.state;
                demo.countdown=p.countdown;demo.phase=p.phase;demo.samples=p.samples;
                demo.errors=p.errors;demo.missed=p.missed;demo.rail=p.rail;
            }
            mutex_exit(&lock);
            if(p.target==radio_session()&&p.request==remote_request&&remote_applied!=p.request){
                if(remote_operation==OP_CAL&&(p.state==READY||p.state==NEED_CAL||p.state==SENSOR_ERROR)){
                    remote_applied=p.request;
                    if(p.calibrated)puts("CAL,OK - Controller calibrated. Press SW or Enter to bowl.");
                    else printf("ERR,Calibration failed: %s\n",p.calibration_error);
                }else if(remote_operation==OP_THROW&&(p.state==BAD_CAPTURE||p.state==NEED_CAL||p.state==SENSOR_ERROR)){
                    remote_applied=p.request;printf("ERR,Controller capture failed: I2C=%u gaps=%u rail=%u\n",p.errors,p.missed,p.rail);
                }
            }
            if(p.menu_count!=remote_menu_seen){remote_menu_seen=p.menu_count;remote_select_seen=p.select_count;command("MENU");}
            else if(p.select_count!=remote_select_seen){remote_select_seen=p.select_count;remote_press=true;}
        }
    }
    if(remote_connected&&now-remote_seen>1000000){
        remote_connected=false;remote_press=false;healthy=calibrated=false;remote_request_op(OP_STOP);
        mutex_enter_blocking(&lock);demo.wireless_connected=false;demo.sensor_ok=false;demo.is_calibrated=false;
        demo.state=NEED_CAL;demo.has_shot=false;mutex_exit(&lock);
        puts("# Controller disconnected; throw cancelled. Waiting for reconnect.");
    }
    if(remote_connected&&!remote_acked&&now-remote_request_since>2000000){
        puts("ERR,Controller did not acknowledge command; cancelling and resynchronizing.");
        remote_request_op(OP_STOP);
        mutex_enter_blocking(&lock);demo.has_shot=false;demo.state=NEED_CAL;mutex_exit(&lock);
    }
    if(remote_session&&now>=remote_send_at){
        WirePacket c={0};c.magic=WIRE_MAGIC;c.version=WIRE_VERSION;c.kind=WIRE_COMMAND;
        c.session=radio_session();c.sequence=(uint32_t)(now/1000);c.target=remote_session;
        c.request=remote_request;c.operation=remote_operation;radio_send(&c);remote_send_at=now+100000;
    }
}
