#define SPORTS_WIRELESS_GROUND 1
void panic(const char *fmt,...);
#include "bowling_native.c"
static WirePacket queued,sent;
static bool queued_ready;
void panic(const char *fmt,...){(void)fmt;abort();}
bool radio_init(bool ground){(void)ground;return true;}
void radio_poll(void){}
uint32_t radio_session(void){return 123;}
void radio_debug_print(void){}
bool radio_take(WirePacket *p){if(!queued_ready)return false;*p=queued;queued_ready=false;return true;}
void radio_send(const WirePacket *p){sent=*p;}
static void deliver(WirePacket *p,uint64_t now){queued=*p;queued_ready=true;mock_time=now;remote_tick();}
int test_remote(void){
    memset(&demo,0,sizeof(demo));demo.tuning=bowl_defaults();demo.view=HOME_VIEW;
    remote_connected=false;remote_session=0;remote_request=1;remote_send_at=0;remote_press=false;
    WirePacket p={0};p.magic=WIRE_MAGIC;p.version=WIRE_VERSION;p.kind=WIRE_STATUS;
    p.session=77;p.sequence=1;p.state=READY;p.healthy=p.calibrated=1;p.joy_x=p.joy_y=2048;
    deliver(&p,100000);if(!remote_connected||sent.operation!=OP_STOP||sent.target!=77)return 1;
    p.target=123;p.request=remote_request;p.sequence++;deliver(&p,120000);
    if(demo.state!=READY)return 2;
    // The remote joystick reaches the original spatial navigation code.
    p.joy_x=4095;p.sequence++;deliver(&p,140000);update_joystick(mock_time);
    if(demo.menu_index!=1||demo.tuning.start_x!=0)return 3;
    p.joy_x=2048;p.select_count++;p.sequence++;deliver(&p,160000);update_joystick(mock_time);
    if(demo.view!=UNAVAILABLE_VIEW)return 4;
    p.sequence++;deliver(&p,180000);update_joystick(mock_time);
    if(demo.view!=UNAVAILABLE_VIEW)return 5; // repeated button count does not click again
    command("MENU");command("1");command("THROW");
    uint32_t requested=remote_request;
    if(remote_operation!=OP_THROW||demo.state!=COUNTDOWN)return 6;
    p.state=PLAYING;p.shot.power=.5f;p.shot.hook=0;p.shot.label=1;
    p.request=requested-1;p.sequence++;deliver(&p,200000);
    if(demo.has_shot)return 7; // stale result must not replay
    p.request=requested;p.sequence++;deliver(&p,220000);
    if(!demo.has_shot||demo.path.pin!=0)return 8;
    uint64_t animation=demo.animation_start;
    p.sequence++;deliver(&p,240000);if(demo.animation_start!=animation)return 9;
    p.sequence--;p.state=READY;deliver(&p,260000);if(demo.state!=PLAYING)return 10;
    // Hold-menu cancels a capture through the same command path.
    command("THROW");p.sequence+=2;p.menu_count++;deliver(&p,280000);
    if(demo.view!=HOME_VIEW||remote_operation!=OP_STOP)return 11;
    mock_time=1400000;remote_tick();if(remote_connected||demo.has_shot||healthy)return 12;
    p.session=88;p.sequence=1;p.select_count=100;p.menu_count=100;deliver(&p,1500000);
    if(!remote_connected||remote_press||remote_operation!=OP_STOP)return 13;
    // Calibration acknowledgement/completion is asynchronous.
    command("MENU");command("1");command("CAL");
    p.target=123;p.request=remote_request;p.sequence++;p.state=CALIBRATING;p.calibrated=0;
    deliver(&p,1520000);if(demo.state!=CALIBRATING||!remote_acked)return 14;
    p.sequence++;p.state=READY;p.calibrated=1;deliver(&p,1540000);
    if(demo.state!=READY||!calibrated||remote_applied!=p.request)return 15;
    return 0;
}
int test_remote_joystick_without_imu(void){
    memset(&demo,0,sizeof(demo));demo.tuning=bowl_defaults();demo.view=HOME_VIEW;
    remote_connected=false;remote_session=0;remote_request=1;remote_send_at=0;remote_press=false;
    joystick_last=menu_repeat=0;menu_direction=0;
    WirePacket p={0};p.magic=WIRE_MAGIC;p.version=WIRE_VERSION;p.kind=WIRE_STATUS;
    p.session=99;p.state=SENSOR_ERROR;p.joy_x=p.joy_y=2048;
    uint64_t now=2000000;
    deliver(&p,now);update_joystick(now);
    // Missing IMU must not gate menu input. Exercise all four directions.
    const unsigned moves[][3]={
        {4095,2048,1},{2048,2048,1},{0,2048,0},{2048,2048,0},
        {2048,0,3},{2048,2048,3},{2048,4095,0},{2200,1900,0}
    };
    for(unsigned i=0;i<sizeof(moves)/sizeof(moves[0]);i++){
        p.joy_x=moves[i][0];p.joy_y=moves[i][1];p.sequence++;
        now+=20000;deliver(&p,now);update_joystick(now);
        if(demo.menu_index!=moves[i][2]||healthy)return (int)i+1;
    }
    p.select_count++;p.sequence++;now+=20000;deliver(&p,now);update_joystick(now);
    if(demo.view!=GAME_VIEW||demo.state!=SENSOR_ERROR)return 10;
    // A duplicate count must not perform a second selection.
    p.sequence++;now+=20000;deliver(&p,now);update_joystick(now);
    if(remote_press)return 11;
    command("MENU");unsigned selection=demo.menu_index;
    mock_time=now+1000001;remote_tick();update_joystick(mock_time);
    if(remote_connected||demo.menu_index!=selection)return 12;
    // Old axis data must not navigate after disconnect.
    remote_packet.joy_x=4095;
    update_joystick(mock_time+20000);
    if(demo.menu_index!=selection)return 13;
    return 0;
}
