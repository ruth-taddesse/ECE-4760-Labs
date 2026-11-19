#include "wire.h"
#include <math.h>
bool wire_newer(uint32_t n,uint32_t p){return (int32_t)(n-p)>0;}
bool wire_valid(const WirePacket *p,unsigned n,unsigned kind){
    if(n!=sizeof(*p)||p->magic!=WIRE_MAGIC||p->version!=WIRE_VERSION||p->kind!=kind||!p->session)return false;
    if(kind==WIRE_COMMAND)return p->operation<=OP_THROW;
    if(p->calibration_error[127]!=0)return false;
    if(p->state>7||p->phase>2||p->countdown>3||p->healthy>1||p->calibrated>1||p->joy_x>4095||p->joy_y>4095)return false;
    if(p->state==5){
        if(p->shot.label<0||p->shot.label>4||!isfinite(p->shot.power)||p->shot.power<0||p->shot.power>1||
           !isfinite(p->shot.hook)||fabsf(p->shot.hook)>1||!isfinite(p->shot.peak)||!isfinite(p->shot.certainty)||p->shot.certainty<0||p->shot.certainty>1)return false;
        for(unsigned i=0;i<13;i++)if(!isfinite(p->shot.features[i]))return false;
    }
    return true;
}
