//------------------------------------------------------------------------------------------------------------------
#include "radio_iface.h"
//------------------------------------------------------------------------------------------------------------------
RadioDev* getRadio(void);   // the single global instance, defined in the main sketch
//------------------------------------------------------------------------------------------------------------------
static radio_iface_t s_ifaces[RADIO_IFACE_MAX];
static int           s_iface_cnt = 0;
//------------------------------------------------------------------------------------------------------------------
static radio_iface_t* radio_iface_register(const char* name, RadioDev* radio)
{
   if (s_iface_cnt >= RADIO_IFACE_MAX || radio == 0)
      return 0;

   radio_iface_t* ifc = &s_ifaces[s_iface_cnt++];
   memset(ifc->name, 0, sizeof(ifc->name));
   strncpy(ifc->name, name, sizeof(ifc->name) - 1);
   ifc->radio   = radio;
   ifc->used    = true;
   ifc->present = radio->check();
   return ifc;
}
//------------------------------------------------------------------------------------------------------------------
void radio_iface_init(void)
{
   s_iface_cnt = 0;
   memset(s_ifaces, 0, sizeof(s_ifaces));

   // r0 -> the single RadioDev on the original SPI bus
   radio_iface_register("r0", getRadio());

   // To add a second module later:
   //   static RadioDev cc1;  cc1.init();  radio_iface_register("r1", &cc1);
}
//------------------------------------------------------------------------------------------------------------------
int radio_iface_count(void)
{
   return s_iface_cnt;
}
//------------------------------------------------------------------------------------------------------------------
radio_iface_t* radio_iface_get(int idx)
{
   if (idx >= 0 && idx < s_iface_cnt)
      return &s_ifaces[idx];
   return 0;
}
//------------------------------------------------------------------------------------------------------------------
radio_iface_t* radio_iface_find(const char* name)
{
   if (name == 0)
      return 0;
   for (int i = 0; i < s_iface_cnt; i++)
   {
      if (strcmp(s_ifaces[i].name, name) == 0)
         return &s_ifaces[i];
   }
   return 0;
}
//------------------------------------------------------------------------------------------------------------------
radio_iface_t* radio_iface_default(void)
{
   return s_iface_cnt > 0 ? &s_ifaces[0] : 0;
}
//------------------------------------------------------------------------------------------------------------------
