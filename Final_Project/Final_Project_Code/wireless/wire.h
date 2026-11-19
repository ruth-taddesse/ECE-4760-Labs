#ifndef SPORTS_WIRE_H
#define SPORTS_WIRE_H
#include <stdint.h>
#include <stdbool.h>
#include "bowling_model.h"
#define WIRE_MAGIC 0x50535031u
#define WIRE_VERSION 1u
enum { WIRE_COMMAND=1, WIRE_STATUS=2 };
enum { OP_STOP, OP_CAL, OP_THROW };
// State values match bowling_demo.c. Native IEEE754 little-endian RP2040 peers only.
typedef struct {
    uint32_t magic,version,kind,session,sequence,target,request,operation;
    uint32_t state,phase,countdown,healthy,calibrated,joy_x,joy_y,select_count,menu_count;
    uint32_t samples,errors,missed,rail;
    BowlThrow shot;
    char calibration_error[128];
} WirePacket;
_Static_assert(sizeof(float)==4 && sizeof(BowlThrow)==72 && sizeof(WirePacket)==284,"Wire ABI changed: bump version");
bool wire_valid(const WirePacket *p, unsigned length, unsigned kind);
bool wire_newer(uint32_t sequence,uint32_t previous);
#endif
