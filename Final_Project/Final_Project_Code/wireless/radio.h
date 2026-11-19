#ifndef SPORTS_RADIO_H
#define SPORTS_RADIO_H
#include "wire.h"
bool radio_init(bool ground);
void radio_poll(void);
bool radio_take(WirePacket *packet);
void radio_send(const WirePacket *packet);
uint32_t radio_session(void);
#endif
