//------------------------------------------------------------------------------------------------------------------
#ifndef __RPROFILE_H__
#define __RPROFILE_H__
//------------------------------------------------------------------------------------------------------------------
#include "Arduino.h"
//------------------------------------------------------------------------------------------------------------------
// Named radio work-profiles persisted on LittleFS.
//
// A profile is simply a sequence of radio sub-commands (the part after
// 'radio <iface>'), one per line, e.g.:
//
//     set mod ook
//     set freq 433920000
//     set rxbw 203000
//     set drate 5000
//     rx
//
// Applying a profile replays those lines against a chosen interface.
// Each profile is stored as a flat file "/prof_<name>".
//------------------------------------------------------------------------------------------------------------------
#define RPROFILE_NAME_MAXLEN 16
#define RPROFILE_LINE_MAXLEN 96
//------------------------------------------------------------------------------------------------------------------
// Callback invoked for every non-empty, non-comment line while applying.
// 'ctx' is an opaque pointer passed through (the target interface).
typedef void (*rprofile_apply_cb)(void* ctx, const char* line);
//------------------------------------------------------------------------------------------------------------------
bool rprofile_valid_name(const char* name);
bool rprofile_exists(const char* name);
bool rprofile_new(const char* name);                       // create or truncate to empty
bool rprofile_add_line(const char* name, const char* line);// append one command line
bool rprofile_del(const char* name);
int  rprofile_list(Stream& out);                           // print names, return count
bool rprofile_show(const char* name, Stream& out);
bool rprofile_apply(const char* name, rprofile_apply_cb cb, void* ctx);
//------------------------------------------------------------------------------------------------------------------
void rprofile_cmd_init(void);                              // register the 'profile' CLI command
//------------------------------------------------------------------------------------------------------------------
#endif// __RPROFILE_H__
//------------------------------------------------------------------------------------------------------------------
