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
static uint32_t tx_ok,tx_failed,rx_ok,rx_rejected,rx_stale;
static uint64_t last_rx;
static int last_link_status=100;
static int last_join_error;
static void receive(void *arg,struct udp_pcb *u,struct pbuf *p,const ip_addr_t *addr,u16_t port){
    (void)arg;(void)u;
    WirePacket packet;
    bool accepted=false,stale=false;
    if(p && port==SPORTS_UDP_PORT && ip_addr_cmp(addr,&peer) && p->tot_len==sizeof(packet)){
        pbuf_copy_partial(p,&packet,sizeof(packet),0);
        if(wire_valid(&packet,sizeof(packet),is_ground?WIRE_STATUS:WIRE_COMMAND)){
            // Reject reordered duplicates before replacing the latest-state mailbox.
            stale=packet.session==retired_session ||
                (packet.session==rx_session&&!wire_newer(packet.sequence,rx_sequence));
            if(!stale){
                if(packet.session!=rx_session){retired_session=rx_session;rx_session=packet.session;}
                rx_sequence=packet.sequence;incoming=packet;available=true;
                rx_ok++;last_rx=time_us_64();accepted=true;
            }
        }
    }
    if(p){if(stale)rx_stale++;else if(!accepted)rx_rejected++;pbuf_free(p);}
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
    if(!is_ground){
        int status=cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA);
        if(status!=last_link_status){
            last_link_status=status;
            printf("# Wi-Fi state=%d (0=down 1=joined -1=failed -2=no AP -3=bad password)\n",status);
        }
        if(time_us_64()>=reconnect_at){
            if(status!=CYW43_LINK_JOIN && status!=CYW43_LINK_UP){
                last_join_error=cyw43_arch_wifi_connect_async(SPORTS_WIFI_SSID,SPORTS_WIFI_PASSWORD,CYW43_AUTH_WPA2_AES_PSK);
                printf("# Wi-Fi join requested / result=%d\n",last_join_error);
            }
            reconnect_at=time_us_64()+5000000;
        }
    }
}
bool radio_take(WirePacket *p){if(!available)return false;*p=incoming;available=false;return true;}
void radio_send(const WirePacket *packet){
    struct pbuf *p=pbuf_alloc(PBUF_TRANSPORT,sizeof(*packet),PBUF_RAM);
    if(!p){tx_failed++;return;}
    err_t result=pbuf_take(p,packet,sizeof(*packet));
    if(result==ERR_OK)result=udp_sendto(pcb,p,&peer,SPORTS_UDP_PORT);
    if(result==ERR_OK)tx_ok++;else tx_failed++;
    pbuf_free(p);
}
void radio_debug_print(void){
    struct netif *n=&cyw43_state.netif[is_ground?CYW43_ITF_AP:CYW43_ITF_STA];
    printf("# RADIO role=%s ip=%s netif_up=%u link_up=%u wifi=%d join_error=%d "
           "tx_ok=%lu tx_failed=%lu rx_ok=%lu rejected=%lu stale=%lu rx_age_ms=%lld\n",
           is_ground?"GROUND":"CONTROLLER",ip4addr_ntoa(netif_ip4_addr(n)),
           !!netif_is_up(n),!!netif_is_link_up(n),
           cyw43_wifi_link_status(&cyw43_state,CYW43_ITF_STA),last_join_error,
           (unsigned long)tx_ok,(unsigned long)tx_failed,(unsigned long)rx_ok,
           (unsigned long)rx_rejected,(unsigned long)rx_stale,
           rx_ok?(long long)((time_us_64()-last_rx)/1000):-1LL);
}
