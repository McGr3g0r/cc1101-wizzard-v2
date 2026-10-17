//------------------------------------------------------------------------------------------------------------------
#ifndef _SETTINGS_H__
#define _SETTINGS_H__
//------------------------------------------------------------------------------------------------------------------
#define FW_VER_LO 01
#define FW_VER_HI 2
//------------------------------------------------------------------------------------------------------------------
// Firmware / device identity (reported by the 'info' CLI command)
#define FW_NAME     "cc1101-wizzard"
#define FW_VARIANT  "v2"
#define FW_BUILD_NO 12         // bump on each release build
#define CLI_API_VER 1          // CLI protocol version for host negotiation
//------------------------------------------------------------------------------------------------------------------
//RP2040 pins SPI0
byte sck = 2;   
byte miso = 4;
byte mosi = 3;
byte ss = 5;
int gdo0 = 7;
int gdo2 = 6;
//------------------------------------------------------------------------------------------------------------------
#define RADIO_BUFFER_MAIN (8192)
#define CC_PACKET_BUFFER_LEN 64
// hard cap for one OOK pulse capture (us) so RF noise can't spin the grab loop forever
#define RADIO_PULSES_MAX_GRAB_US 250000
//------------------------------------------------------------------------------------------------------------------
#define STDOUT Serial
#define STDIN Serial

#define DBG 1
#if DBG == 1
#define DBG_PRINTLN(x) Serial.println(x)
#define DBG_PRINT(x) Serial.print(x)
#else
#define DBG_PRINTLN(x)
#define DBG_PRINT(x)
#endif
//------------------------------------------------------------------------------------------------------------------
#define USE_FILE_SYSTEM 1
#define FS_FILE_NAME_MAXLEN 12

#define CTRL_C 0x03
#define CTRL_F 0x06
#define CTRL_D 0x04
//------------------------------------------------------------------------------------------------------------------
#define ENV_MAX_ITEMS  16
#define ENV_NAME_MAXLEN 8
#define ENV_VALUE_MAXLEN 32

#define OS_ITERATION_FILE_AUTO_DUMP 2048
//------------------------------------------------------------------------------------------------------------------
#endif// _SETTINGS_H__
//------------------------------------------------------------------------------------------------------------------
