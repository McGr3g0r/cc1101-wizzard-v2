//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-16
//------------------------------------------------------------------------------------------------------------------
// info command (v2)
//
//   info        - device / firmware identification as JSON (default, host-parseable)
//   info text   - same information in a human-readable form
//
// Lets a CLI host reliably identify the device, firmware version, build and
// radio interfaces.
//------------------------------------------------------------------------------------------------------------------
#include "settings.h"
#include "CmdHandler.h"
#include "cc1101.h"
#include "radio_iface.h"
#include "rl_cc1101.h"
//------------------------------------------------------------------------------------------------------------------
CmdHandler* getRootCommandHandler(void);
String platform_get_name(void);
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e info_main(void* parent, int argc, char* argv[]);
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t info_handler = { &info_main, NULL, NULL, "info", "device/firmware identification, use: 'info' (json) or 'info text'", 0, "", "info" };
//------------------------------------------------------------------------------------------------------------------
void info_cmd_init(void)
{
   getRootCommandHandler()->registerHandler(&info_handler, NULL);
}
//------------------------------------------------------------------------------------------------------------------
static void info_json(void)
{
   STDOUT.print("{ \"device\":\"");     STDOUT.print(FW_NAME);
   STDOUT.print("\", \"variant\":\"");  STDOUT.print(FW_VARIANT);
   STDOUT.print("\", \"fw\":\"");       STDOUT.print(FW_VER_HI); STDOUT.print("."); STDOUT.print(FW_VER_LO);
   STDOUT.print("\", \"build\":");      STDOUT.print(FW_BUILD_NO);
   STDOUT.print(", \"built\":\"");      STDOUT.print(F(__DATE__ " " __TIME__));
   STDOUT.print("\", \"cli\":");        STDOUT.print(CLI_API_VER);
   STDOUT.print(", \"driver\":\"radiolib\", \"begin\":"); STDOUT.print(rl_begin_status());
   STDOUT.print(", \"platform\":\"");   STDOUT.print(platform_get_name());
   STDOUT.print("\", \"radios\":");     STDOUT.print(radio_iface_count());
   STDOUT.print(", \"interfaces\":[");
   for (int i = 0; i < radio_iface_count(); i++)
   {
      radio_iface_t* ifc = radio_iface_get(i);
      if (i) STDOUT.print(",");
      STDOUT.print("{\"name\":\"");        STDOUT.print(ifc->name);
      STDOUT.print("\",\"present\":");     STDOUT.print(ifc->present ? "true" : "false");
      STDOUT.print(",\"chip_version\":");  STDOUT.print(ifc->radio->version());
      STDOUT.print(",\"partnum\":");       STDOUT.print(ifc->radio->partnum());
      STDOUT.print("}");
   }
   STDOUT.println("] }");
}
//------------------------------------------------------------------------------------------------------------------
static void info_text(void)
{
   STDOUT.print("device   : "); STDOUT.println(FW_NAME);
   STDOUT.print("variant  : "); STDOUT.println(FW_VARIANT);
   STDOUT.print("fw       : "); STDOUT.print(FW_VER_HI); STDOUT.print("."); STDOUT.println(FW_VER_LO);
   STDOUT.print("build    : "); STDOUT.println(FW_BUILD_NO);
   STDOUT.print("built    : "); STDOUT.println(F(__DATE__ " " __TIME__));
   STDOUT.print("cli api  : "); STDOUT.println(CLI_API_VER);
   STDOUT.print("driver   : radiolib (begin="); STDOUT.print(rl_begin_status()); STDOUT.println(")");
   STDOUT.print("platform : "); STDOUT.println(platform_get_name());
   STDOUT.print("radios   : "); STDOUT.println(radio_iface_count());
   for (int i = 0; i < radio_iface_count(); i++)
   {
      radio_iface_t* ifc = radio_iface_get(i);
      STDOUT.print("  "); STDOUT.print(ifc->name);
      STDOUT.print(" present="); STDOUT.print(ifc->present ? "yes" : "no");
      STDOUT.print(" chip_version="); STDOUT.print(ifc->radio->version());
      STDOUT.print(" partnum="); STDOUT.println(ifc->radio->partnum());
   }
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e info_main(void* parent, int argc, char* argv[])
{
   if (argc >= 2 && !strcmp(argv[1], "text"))
      info_text();
   else
      info_json();
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
