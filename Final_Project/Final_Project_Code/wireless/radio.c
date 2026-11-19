// Raw lwIP is confined to core 0 using pico_cyw43_arch_lwip_poll.
#include "radio.h"
#include "wifi_config.h"
#include "pico/cyw43_arch.h"
#include "pico/rand.h"
#include "lwip/udp.h"
#include "lwip/netif.h"
#include <string.h>
#include <stdio.h>
static struct udp_pcb *pcb;
static ip_addr_t peer;
static bool is_ground,available;
static WirePacket incoming;
static uint32_t boot_session,rx_session,retired_session,rx_sequence;
static uint64_t reconnect_at;
static void receive(void *arg,struct udp_pcb *u,struct pbuf *p,const ip_addr_t *addr,u16_t port){
    (void)arg;(void)u;
    WirePacket packet;
    if(p && port==SPORTS_UDP_PORT && ip_addr_cmp(addr,&peer) && p->tot_len==sizeof(packet)){
        pbuf_copy_partial(p,&packet,sizeof(packet),0);
        if(wire_valid(&packet,sizeof(packet),is_ground?WIRE_STATUS:WIRE_COMMAND)){
            // Reject reordered duplicates before replacing the latest-state mailbox.
            if(packet.session==retired_session){pbuf_free(p);return;}
            if(packet.session==rx_session&&!wire_newer(packet.sequence,rx_sequence)){pbuf_free(p);return;}
            if(packet.session!=rx_session){retired_session=rx_session;rx_session=packet.session;}
            rx_sequence=packet.sequence;incoming=packet;available=true;
        }
    }
    if(p)pbuf_free(p);
}
bool radio_init(bool ground){
    is_ground=ground;boot_session=get_rand_32();if(!boot_session)boot_session=1;
    if(cyw43_arch_init())return false;
    cyw43_wifi_pm(&cyw43_state,CYW43_NO_POWERSAVE_MODE);
    if(ground)cyw43_arch_enable_ap_mode(SPORTS_WIFI_SSID,SPORTS_WIFI_PASSWORD,CYW43_AUTH_WPA2_AES_PSK);
    else cyw43_arch_enable_sta_mode();
    ip4_addr_t ip,mask,gw;
    ip4addr_aton(ground?SPORTS_GROUND_IP:SPORTS_CONTROLLER_IP,&ip);
    ip4addr_aton("255.255.255.0",&mask);ip4addr_aton(SPORTS_GROUND_IP,&gw);
    netif_set_addr(&cyw43_state.netif[ground?CYW43_ITF_AP:CYW43_ITF_STA],&ip,&mask,&gw);
    ipaddr_aton(ground?SPORTS_CONTROLLER_IP:SPORTS_GROUND_IP,&peer);
    pcb=udp_new();if(!pcb)return false;
    if(udp_bind(pcb,IP_ADDR_ANY,SPORTS_UDP_PORT)!=ERR_OK)return false;
    udp_recv(pcb,receive,NULL);
    printf("# %s / Wi-Fi %s / static IP %s / UDP %u\n",ground?"GROUND STATION":"CONTROLLER",SPORTS_WIFI_SSID,ground?SPORTS_GROUND_IP:SPORTS_CONTROLLER_IP,SPORTS_UDP_PORT);
    return true;
}
uint32_t radio_session(void){return boot_session;}
void radio_poll(void){
    cyw43_arch_poll();
    if(!is_ground && time_us_64()>=reconnect_at){
        int status=cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA);
        if(status!=CYW43_LINK_JOIN && status!=CYW43_LINK_UP){
            cyw43_arch_wifi_connect_async(SPORTS_WIFI_SSID,SPORTS_WIFI_PASSWORD,CYW43_AUTH_WPA2_AES_PSK);
        }
        reconnect_at=time_us_64()+5000000;
    }
}
bool radio_take(WirePacket *p){if(!available)return false;*p=incoming;available=false;return true;}
void radio_send(const WirePacket *packet){
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,sizeof(*packet),PBUF_RAM);
    if(!p)return;
    pbuf_take(p,packet,sizeof(*packet));udp_sendto(pcb,p,&peer,SPORTS_UDP_PORT);pbuf_free(p);
}
