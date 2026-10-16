//------------------------------------------------------------------------------------------------------------------
#include "cc1101.h"
#include "platform.h"
#include "Parser.h"
#include "Script.h"
#include "Terminal.h"
#include "CmdHandler.h"
#include "settings.h"
#include "radio_pulses.h"
#include "radio_iface.h"
#include "radio_params.h"
#include "rprofile.h"
#if USE_FILE_SYSTEM == 1
#include "env.h"
#endif
//------------------------------------------------------------------------------------------------------------------
#define CMD_MAX_BUFFER_LEN 256
//------------------------------------------------------------------------------------------------------------------
RadioDev cc;
Parser parser;
Terminal term(CMD_MAX_BUFFER_LEN);
CmdHandler handler;
Script script;
//------------------------------------------------------------------------------------------------------------------
void radio_cmd_init(void);
void help_cmd_init(void);
void info_cmd_init(void);
void monitor_cmd_init(void);
void monitor_poll(void);
void monitor_core1_task(void);
void env_cmd_init(void);
void radio_iface_init(void);
void rprofile_cmd_init(void);
#if USE_FILE_SYSTEM == 1
void files_cmd_init(void);
#endif
//------------------------------------------------------------------------------------------------------------------
CmdHandler* getRootCommandHandler(void)
{
  return &handler;
}
//------------------------------------------------------------------------------------------------------------------
void commands_init(void)
{
  help_cmd_init();
  info_cmd_init();
  radio_cmd_init();
  rprofile_cmd_init();
  monitor_cmd_init();
  #if USE_FILE_SYSTEM == 1
  files_cmd_init();
  #endif
  env_cmd_init();
}
//------------------------------------------------------------------------------------------------------------------
RadioDev* getRadio() 
{
  return &cc;
}
//------------------------------------------------------------------------------------------------------------------
void setup() {
     String tmp;
     String verStr = String("CC1101 Wizard tool v")+ String(FW_VER_HI) + String(".") + String(FW_VER_LO) + String(" connected, use 'help' for list of commands...\n\r");
     uint32_t s = millis();
     Serial.begin(115200);

     while (!Serial && millis() -s < 1000);
     delay(1000);
     
     Serial.println(verStr.c_str());
     Serial.println(F("(C) Grzegorz Rajtar grzegorz.rajtar@gmail.com 2023\n\r"));
     Serial.print(F("Plarform:"));Serial.println(platform_get_name());
     Serial.println();

     platform_init();

     radio_pulses_init();

     commands_init();

     cc.init();

     radio_iface_init();

     if (cc.check()) {
        Serial.println(F("CC1101 [OK].\n\r"));
     } else {
        Serial.println(F("CC1101 [connection error]!\n\r"));
     };
     
     term.setEcho(true);
     term.clear();

     #if USE_FILE_SYSTEM == 1
     env_load();
     tmp = env_get("init", "none");
     if (tmp == "none")
         script.load("/init.scr");
     else
         script.load(String("/"+ tmp).c_str());
     #endif
     
}
//------------------------------------------------------------------------------------------------------------------
void loop() {
#if USE_FILE_SYSTEM == 1
  if (script.available())
  {
    term.input(script.getLine());
  } else 
#endif  
  {
    term.input(Serial);
  }
  if (term.available())
  {
      bool cmdOk = false;
      if (parser.parse(term.getLine()))
      {
          cmdOk = handler.handleCommand(parser.getArgc(), parser.getArgv());
          term.clear();
      }
      if (!cmdOk)
      {
           handler.print_error(Serial);
      } 
      else 
      {
        if (handler.getResultMessage().length()> 0)
        {
            Serial.println(handler.getResultMessage());
            handler.setResultMessage("");
        }
        else
            Serial.println("OK");
      }
  }

  // non-blocking background RX monitor; runs concurrently with the CLI
  monitor_poll();
}
//------------------------------------------------------------------------------------------------------------------
// Second core: runs the OOK pulse capture so its tight GDO0 busy-wait never
// blocks core0's USB/CLI. arduino-pico runs setup1()/loop1() on core1.
void setup1() {}
void loop1()
{
  monitor_core1_task();
}
//------------------------------------------------------------------------------------------------------------------
