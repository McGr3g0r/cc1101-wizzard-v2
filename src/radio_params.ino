//------------------------------------------------------------------------------------------------------------------
#include "radio_params.h"
//------------------------------------------------------------------------------------------------------------------
// Generic RadioDev parameter table.
//
// NOTE on units:
//   freq is expressed in Hz on the CLI (e.g. 433920000). The underlying driver
//   works in kHz, so the set/get wrappers convert Hz<->kHz here.
//   dev / chsp / rxbw are in Hz, drate in Baud, pa in dBm; these match the
//   values cached inside the RadioDev class.
//
// The 'mod' / 'syncword' / 'rx' / 'tx' / 'idle' commands are handled specially
// in radio_handler.ino (modulation accepts names, syncword takes two values,
// rx/tx/idle are actions), so they are not single-value rows here.
//------------------------------------------------------------------------------------------------------------------
static const radio_param_t s_params[] = {
  { "freq",       "Hz",   [](RadioDev* r, long v){ r->setFreq((int)(v / 1000)); }, [](RadioDev* r)->long{ return (long)r->getFreq() * 1000L; }, 300000000L, 920000000L, "carrier frequency (driver clamps 300-920 MHz)" },
  { "dev",        "Hz",   [](RadioDev* r, long v){ r->setDeviation((int)v); },     [](RadioDev* r)->long{ return r->getDeviation(); },               1580L,     380850L,    "FSK frequency deviation" },
  { "chan",       "",     [](RadioDev* r, long v){ r->setChan((int)v); },          [](RadioDev* r)->long{ return r->getChan(); },                    0L,        255L,       "channel number" },
  { "chsp",       "Hz",   [](RadioDev* r, long v){ r->setChanSpaceing((int)v); },  [](RadioDev* r)->long{ return r->getChanSpaceing(); },            25390L,    405450L,    "channel spacing" },
  { "rxbw",       "Hz",   [](RadioDev* r, long v){ r->setRxBw((int)v); },          [](RadioDev* r)->long{ return r->getRxBw(); },                    58000L,    812000L,    "receive bandwidth (RadioLib range 58-812 kHz)" },
  { "drate",      "Baud", [](RadioDev* r, long v){ r->setDataRate((int)v); },      [](RadioDev* r)->long{ return r->getDataRate(); },                20L,       1621830L,   "data rate" },
  { "pa",         "dBm",  [](RadioDev* r, long v){ r->setPA((int)v); },            [](RadioDev* r)->long{ return r->getPA(); },                      -30L,      12L,        "TX power (use one of -30,-20,-15,-10,-6,0,5,7,10,11,12)" },
  { "syncmode",   "",     [](RadioDev* r, long v){ r->setSyncMode((int)v); },      [](RadioDev* r)->long{ return r->getSyncMode(); },                0L,        7L,         "sync-word qualifier mode" },
  { "addrchk",    "",     [](RadioDev* r, long v){ r->setAddressCheck((int)v); },  [](RadioDev* r)->long{ return r->getAddressCheck(); },            0L,        3L,         "address check mode" },
  { "addr",       "",     [](RadioDev* r, long v){ r->setAddress((int)v); },       [](RadioDev* r)->long{ return r->getAddress(); },                 0L,        255L,       "device address for packet filtering" },
  { "whitening",  "",     [](RadioDev* r, long v){ r->setDataWhitening((int)v); }, [](RadioDev* r)->long{ return r->getDataWhitening(); },           0L,        1L,         "data whitening on/off" },
  { "pktfmt",     "",     [](RadioDev* r, long v){ r->setPacketFormat((int)v); },  [](RadioDev* r)->long{ return r->getPacketFormat(); },            0L,        3L,         "packet format (0 FIFO,1 sync serial,2 random,3 async serial)" },
  { "pktlenmode", "",     [](RadioDev* r, long v){ r->setPacketLengthMode((int)v); },[](RadioDev* r)->long{ return r->getPacketLengthMode(); },      0L,        3L,         "packet length mode (0 fixed,1 variable,2 infinite)" },
  { "pktlen",     "",     [](RadioDev* r, long v){ r->setPacketLength((int)v); },  [](RadioDev* r)->long{ return r->getPacketLength(); },            0L,        255L,       "packet length / max length" },
  { "crc",        "",     [](RadioDev* r, long v){ r->setCrcEnable((int)v); },     [](RadioDev* r)->long{ return r->getCrcEnable(); },               0L,        1L,         "CRC calculation/check on/off" },
  { "crcaf",      "",     [](RadioDev* r, long v){ r->setCrcAf((int)v); },         [](RadioDev* r)->long{ return r->getCrcAf(); },                   0L,        1L,         "auto-flush RX FIFO on CRC error" },
  { "dcfilteroff","",     [](RadioDev* r, long v){ r->setDCFilterOff((int)v); },   [](RadioDev* r)->long{ return r->getDCFilterOff(); },             0L,        1L,         "disable DC blocking filter" },
  { "manchester", "",     [](RadioDev* r, long v){ r->setManchester((int)v); },    [](RadioDev* r)->long{ return r->getManchester(); },              0L,        1L,         "Manchester encoding on/off" },
  { "fec",        "",     [](RadioDev* r, long v){ r->setFEC((int)v); },           [](RadioDev* r)->long{ return r->getFEC(); },                     0L,        1L,         "forward error correction on/off" },
  { "preamble",   "",     [](RadioDev* r, long v){ r->setPreambleLen((int)v); },   [](RadioDev* r)->long{ return r->getPreambleLen(); },             0L,        7L,         "min preamble bytes (0:2..7:24)" },
  { "pqt",        "",     [](RadioDev* r, long v){ r->setPQT((int)v); },           [](RadioDev* r)->long{ return r->getPQT(); },                     0L,        7L,         "preamble quality threshold" },
  { "appendstatus","",    [](RadioDev* r, long v){ r->setAppendStatus((int)v); },  [](RadioDev* r)->long{ return r->getAppendStatus(); },            0L,        1L,         "append RSSI/LQI status bytes" },
  { "ccmode",     "",     [](RadioDev* r, long v){ r->setCCMode((int)v); },        [](RadioDev* r)->long{ return r->getCCMode(); },                  0L,        1L,         "driver config mode (1 packet, 0 raw)" },
};
//------------------------------------------------------------------------------------------------------------------
const radio_param_t* radio_params_table(int* count)
{
   if (count) *count = (int)(sizeof(s_params) / sizeof(radio_param_t));
   return s_params;
}
//------------------------------------------------------------------------------------------------------------------
const radio_param_t* radio_params_find(const char* name)
{
   int n = (int)(sizeof(s_params) / sizeof(radio_param_t));
   for (int i = 0; i < n; i++)
   {
      if (strcmp(s_params[i].name, name) == 0)
         return &s_params[i];
   }
   return 0;
}
//------------------------------------------------------------------------------------------------------------------
