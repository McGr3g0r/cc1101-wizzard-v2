//------------------------------------------------------------------------------------------------------------------
#ifndef __RADIO_IFACE_H__
#define __RADIO_IFACE_H__
//------------------------------------------------------------------------------------------------------------------
#include "cc1101.h"
//------------------------------------------------------------------------------------------------------------------
// Named radio interfaces: r0, r1, ...  For now only r0 is populated (single
// RadioDev on the SPI bus from the original project). Adding more modules later
// means registering extra RadioDev instances in radio_iface_init().
//------------------------------------------------------------------------------------------------------------------
#define RADIO_IFACE_MAX 4
//------------------------------------------------------------------------------------------------------------------
typedef struct radio_iface_s
{
   char    name[4];   // "r0".. "r3"
   RadioDev* radio;     // bound driver instance
   bool    used;      // slot in use
   bool    present;   // RadioDev answered on the bus
} radio_iface_t;
//------------------------------------------------------------------------------------------------------------------
void            radio_iface_init(void);                    // register & probe interfaces
int             radio_iface_count(void);
radio_iface_t*  radio_iface_get(int idx);
radio_iface_t*  radio_iface_find(const char* name);        // by "r0".. returns 0 if unknown
radio_iface_t*  radio_iface_default(void);                 // r0
//------------------------------------------------------------------------------------------------------------------
#endif// __RADIO_IFACE_H__
//------------------------------------------------------------------------------------------------------------------
