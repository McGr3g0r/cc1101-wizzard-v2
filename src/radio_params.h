//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-16
//------------------------------------------------------------------------------------------------------------------
#ifndef __RADIO_PARAMS_H__
#define __RADIO_PARAMS_H__
//------------------------------------------------------------------------------------------------------------------
#include "cc1101.h"
//------------------------------------------------------------------------------------------------------------------
// A single, generic RadioDev parameter that can be set/read by name from the CLI.
// Adding a new settable parameter == adding one row to the table in radio_params.ino.
//------------------------------------------------------------------------------------------------------------------
typedef void (*radio_param_set_fn)(RadioDev* r, long v);
typedef long (*radio_param_get_fn)(RadioDev* r);
//------------------------------------------------------------------------------------------------------------------
typedef struct radio_param_s
{
   const char*        name;   // canonical name, e.g. "freq"
   const char*        unit;   // human unit, e.g. "Hz", "Baud", "dBm", "" for enums/flags
   radio_param_set_fn set;    // write to the radio
   radio_param_get_fn get;    // read back the shadow value
   long               vmin;   // inclusive range for validation
   long               vmax;
   const char*        help;   // short description
} radio_param_t;
//------------------------------------------------------------------------------------------------------------------
const radio_param_t* radio_params_table(int* count);
const radio_param_t* radio_params_find(const char* name);
//------------------------------------------------------------------------------------------------------------------
#endif// __RADIO_PARAMS_H__
//------------------------------------------------------------------------------------------------------------------
