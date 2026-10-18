//------------------------------------------------------------------------------------------------------------------
// radio command (v2)
//
// Grammar:
//   radio list                         - list radio interfaces
//   radio help                         - this help
//   radio <iface>                      - show interface info (all parameters)
//   radio <iface> info                 - same as above
//   radio <iface> get <param>          - read one parameter
//   radio <iface> set <param> <value>  - set any parameter (generic)
//   radio <iface> set_<param> <value>  - convenience form, e.g. 'radio r0 set_freq 433920000'
//   radio <iface> get_<param>
//   radio <iface> set mod <2fsk|gfsk|ook|4fsk|msk|0..4>
//   radio <iface> set syncword <high> <low>
//   radio <iface> set pa <-30..12 dBm>
//   radio <iface> rx | tx | idle | reset | rssi
//   radio <iface> scan <startKHz> <stopKHz>
//   radio <iface> send <hexstring>
//   radio <iface> jam <ms>             (0 = until ctrl-c)
//   radio <iface> pulses count|flush|show|grab <off> <minUs> <maxUs>|play <off> <n> [repeat] [gapUs]|add <v>...
//------------------------------------------------------------------------------------------------------------------
#include "settings.h"
#include "CmdHandler.h"
#include "cc1101.h"
#include "radio_iface.h"
#include "radio_params.h"
#include "rprofile.h"
#include "radio_pulses.h"
#include "radio_raw.h"
#include "radio_buffer.h"
#include "rl_cc1101.h"
#include "utils.h"
//------------------------------------------------------------------------------------------------------------------
CmdHandler* getRootCommandHandler(void);
bool monitor_active(void);   // defined in Monitor.ino
extern int RXLED;
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e radio_main(void* parent, int argc, char* argv[]);
static CmdStatus_e radio_dispatch(radio_iface_t* ifc, int nsub, char** sub, CmdHandler* h);
void radio_profile_exec_line(void* ctx, const char* line);
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t radio_handler = { &radio_main, NULL, NULL, "radio", "CC1101 radio control, use: 'radio help'", 0, "", "radio help" };
//------------------------------------------------------------------------------------------------------------------
void radio_cmd_init(void)
{
   // registered with no fixed sub-command table -> radio_main gets the full argv[]
   getRootCommandHandler()->registerHandler(&radio_handler, NULL);
}
//------------------------------------------------------------------------------------------------------------------
static int radio_mod_from_token(const char* t)
{
   if (!strcmp(t, "2fsk")) return 0;
   if (!strcmp(t, "gfsk")) return 1;
   if (!strcmp(t, "ook"))  return 2;
   if (!strcmp(t, "4fsk")) return 3;
   if (!strcmp(t, "msk"))  return 4;
   if (isNumeric((char*)t))
   {
      int v = atoi(t);
      if (v >= 0 && v <= 4) return v;
   }
   return -1;
}
//------------------------------------------------------------------------------------------------------------------
static void radio_usage(void)
{
   STDOUT.println(radio_handler.desc);
   STDOUT.println("- list                      : list radio interfaces");
   STDOUT.println("- <iface>                   : show interface info");
   STDOUT.println("- <iface> get <param>       : read a parameter");
   STDOUT.println("- <iface> set <param> <val> : set any parameter (also set_<param> <val>)");
   STDOUT.println("- <iface> set mod <2fsk|gfsk|ook|4fsk|msk|0..4>");
   STDOUT.println("- <iface> set syncword <high> <low>");
   STDOUT.println("- <iface> rx | tx | idle | reset | rssi | regs");
   STDOUT.println("- <iface> scan <startKHz> <stopKHz>");
   STDOUT.println("- <iface> send <hex> | jam <ms>");
   STDOUT.println("- <iface> pulses count|flush|show|grab <off> <minUs> <maxUs>|play <off> <n> [repeat] [gapUs]|add <v>...");
   STDOUT.println("params:");
   int n = 0;
   const radio_param_t* t = radio_params_table(&n);
   for (int i = 0; i < n; i++)
   {
      STDOUT.print("  "); STDOUT.print(t[i].name);
      if (strlen(t[i].unit)) { STDOUT.print(" ["); STDOUT.print(t[i].unit); STDOUT.print("]"); }
      STDOUT.print(" : "); STDOUT.println(t[i].help);
   }
   STDOUT.println("  mod, syncword, pa (special-cased)");
}
//------------------------------------------------------------------------------------------------------------------
static void radio_info(radio_iface_t* ifc)
{
   RadioDev* r = ifc->radio;
   STDOUT.print("iface "); STDOUT.print(ifc->name);
   STDOUT.print(" present="); STDOUT.print(ifc->present ? "yes" : "no");
   STDOUT.print(" state="); STDOUT.println(r->getTX() ? "tx" : (r->getRX() ? "rx" : "idle"));
   STDOUT.print("  mod = "); STDOUT.print(RadioDev::modulation(r->getModulation())); STDOUT.print(" ("); STDOUT.print(r->getModulation()); STDOUT.println(")");
   STDOUT.print("  syncword = "); STDOUT.print(r->getSyncWordHigh()); STDOUT.print(" "); STDOUT.println(r->getSyncWordLow());
   STDOUT.print("  pa = "); STDOUT.print(r->getPA()); STDOUT.println(" dBm");
   int n = 0;
   const radio_param_t* t = radio_params_table(&n);
   for (int i = 0; i < n; i++)
   {
      STDOUT.print("  "); STDOUT.print(t[i].name); STDOUT.print(" = "); STDOUT.print(t[i].get(r));
      if (strlen(t[i].unit)) { STDOUT.print(" "); STDOUT.print(t[i].unit); }
      STDOUT.println();
   }
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_get(RadioDev* r, const char* name, CmdHandler* h)
{
   if (!strcmp(name, "mod"))
   {
      STDOUT.print("mod = "); STDOUT.print(RadioDev::modulation(r->getModulation()));
      STDOUT.print(" ("); STDOUT.print(r->getModulation()); STDOUT.println(")");
      return OK;
   }
   if (!strcmp(name, "syncword"))
   {
      STDOUT.print("syncword = "); STDOUT.print(r->getSyncWordHigh()); STDOUT.print(" "); STDOUT.println(r->getSyncWordLow());
      return OK;
   }
   if (!strcmp(name, "pa"))
   {
      STDOUT.print("pa = "); STDOUT.print(r->getPA()); STDOUT.println(" dBm");
      return OK;
   }
   const radio_param_t* p = radio_params_find(name);
   if (!p) { h->setHint("unknown parameter, use 'radio <iface> info'"); return WRONG_PARAMS; }
   STDOUT.print(name); STDOUT.print(" = "); STDOUT.print(p->get(r));
   if (strlen(p->unit)) { STDOUT.print(" "); STDOUT.print(p->unit); }
   STDOUT.println();
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_set(RadioDev* r, const char* name, int nval, char** vals, CmdHandler* h)
{
   if (nval < 1) { h->setHint("missing value"); return NOT_ENOUGH_PARAMS; }

   if (!strcmp(name, "mod"))
   {
      int mod = radio_mod_from_token(vals[0]);
      if (mod < 0) { h->setHint("mod: 2fsk|gfsk|ook|4fsk|msk or 0..4"); return WRONG_PARAMS; }
      r->setModulation(mod);
      return (r->getModulation() == mod) ? OK : CC_ERROR;
   }
   if (!strcmp(name, "syncword"))
   {
      if (nval < 2) { h->setHint("syncword needs <high> <low>"); return NOT_ENOUGH_PARAMS; }
      long hi = strtol(vals[0], 0, 0);
      long lo = strtol(vals[1], 0, 0);
      if (hi < 0 || hi > 255 || lo < 0 || lo > 255) { h->setHint("syncword bytes 0..255"); return WRONG_PARAMS; }
      r->setSyncWord((int)hi, (int)lo);
      return (r->getSyncWordHigh() == hi && r->getSyncWordLow() == lo) ? OK : CC_ERROR;
   }
   if (!strcmp(name, "pa"))
   {
      int allowed[] = { -30, -20, -15, -10, -6, 0, 5, 7, 10, 11, 12 };
      long pa = strtol(vals[0], 0, 0);
      if (!intInSet((int)pa, allowed, sizeof(allowed) / sizeof(int))) { h->setHint("pa: -30,-20,-15,-10,-6,0,5,7,10,11,12"); return WRONG_PARAMS; }
      r->setPA((int)pa);
      return (r->getPA() == pa) ? OK : CC_ERROR;
   }

   const radio_param_t* p = radio_params_find(name);
   if (!p) { h->setHint("unknown parameter, use 'radio <iface> info'"); return WRONG_PARAMS; }

   long v = strtol(vals[0], 0, 0);   // base 0 -> accepts decimal and 0x-hex
   if (v < p->vmin || v > p->vmax)
   {
      char msg[80];
      snprintf(msg, sizeof(msg), "%s range %ld..%ld %s", p->name, p->vmin, p->vmax, p->unit);
      h->setHint(msg);
      return WRONG_PARAMS;
   }
   p->set(r, v);
   long rb = p->get(r);
   bool ok = (!strcmp(name, "freq")) ? (rb / 1000 == v / 1000) : (rb == v);
   return ok ? OK : CC_ERROR;
}
//------------------------------------------------------------------------------------------------------------------
// Dump the CC1101 config registers that matter for OOK TX / decoding, so the
// live config can be compared against a known-good (e.g. ELECHOUSE) setup.
static void radio_regs(RadioDev* r)
{
   STDOUT.print("version=0x"); STDOUT.print(r->version(), HEX);
   STDOUT.print(" partnum=0x"); STDOUT.println(r->partnum(), HEX);

   static const struct { const char* n; uint8_t a; } REGS[] = {
      { "IOCFG2",  0x00 }, { "IOCFG0",  0x02 }, { "FIFOTHR", 0x03 },
      { "PKTCTRL1",0x07 }, { "PKTCTRL0",0x08 },
      { "FREND0",  0x22 },
      { "MDMCFG4", 0x10 }, { "MDMCFG3", 0x11 }, { "MDMCFG2", 0x12 },
      { "MDMCFG1", 0x13 }, { "MDMCFG0", 0x14 }, { "DEVIATN", 0x15 },
   };
   for (unsigned i = 0; i < sizeof(REGS) / sizeof(REGS[0]); i++)
   {
      int v = rl_get_reg(REGS[i].a) & 0xFF;
      STDOUT.print("  "); STDOUT.print(REGS[i].n);
      STDOUT.print("(0x"); if (REGS[i].a < 0x10) STDOUT.print("0"); STDOUT.print(REGS[i].a, HEX);
      STDOUT.print(")=0x"); if (v < 0x10) STDOUT.print("0"); STDOUT.println(v, HEX);
   }
   uint8_t pa[2];
   rl_get_patable(pa);
   STDOUT.print("  PATABLE[0]=0x"); if (pa[0] < 0x10) STDOUT.print("0"); STDOUT.print(pa[0], HEX);
   STDOUT.print(" PATABLE[1]=0x"); if (pa[1] < 0x10) STDOUT.print("0"); STDOUT.println(pa[1], HEX);
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_scan(RadioDev* r, int nval, char** vals, CmdHandler* h)
{
   if (nval < 2) { h->setHint("scan <startKHz> <stopKHz>"); return NOT_ENOUGH_PARAMS; }
   int fstart = atoi(vals[0]);
   int fstop  = atoi(vals[1]);
   if (fstart <= 0 || fstop <= fstart) { h->setHint("scan: 0 < start < stop (KHz)"); return WRONG_PARAMS; }

   int mrssi, mfreq = 0, rssi;
   int freq = fstart;
   long cfreq = fstart;

   r->setRxBw(58000);
   r->setRX();
   mrssi = -100;
   STDOUT.println("scanning... (press any key to stop)");

   while (!STDIN.available())
   {
      r->setFreq(freq);
      rssi = r->getRSSI();
      if (rssi > -75 && rssi > mrssi) { mrssi = rssi; mfreq = freq; }
      freq += 10;
      if (freq > fstop)
      {
         freq = fstart;
         if (mrssi > -75)
         {
            if (mfreq == cfreq)
            {
               STDOUT.print("\r\nSignal found at Freq: "); STDOUT.print(mfreq);
               STDOUT.print(" KHz  Rssi: "); STDOUT.println(mrssi);
               mrssi = -100; cfreq = 0; mfreq = 0;
            }
            else { cfreq = mfreq; freq = mfreq - 10; mfreq = 0; mrssi = -100; }
         }
      }
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_send(RadioDev* r, int nval, char** vals, CmdHandler* h)
{
   if (nval < 1 || !isHex(vals[0])) { h->setHint("send <hexstring>"); return WRONG_PARAMS; }
   radio_raw_init();
   radio_raw_add_hex(vals[0]);
   radio_raw_send(0, radio_raw_get_data_len());
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_jam(RadioDev* r, int nval, char** vals, CmdHandler* h)
{
   if (nval < 1) { h->setHint("jam <ms> (0 = until ctrl-c)"); return NOT_ENOUGH_PARAMS; }
   int timems = atoi(vals[0]);
   if (timems < 0) return WRONG_PARAMS;

   STDOUT.print("jamming ...");
   if (timems) { STDOUT.print(" for "); STDOUT.print(timems); STDOUT.println(" ms"); }
   else        STDOUT.println(" press ctrl-c/f/d to terminate...");

   bool done = false;
   uint32_t s = millis();
   while (!done)
   {
      randomSeed(analogRead(0));
      for (int i = 0; i < CC_PACKET_BUFFER_LEN; i++)
         radio_buffer_get(0)[i] = (byte)random(255);
      digitalWrite(RXLED, ~digitalRead(RXLED));
      r->sendData(radio_buffer_get(0), CC_PACKET_BUFFER_LEN - 4);
      if (timems) done = (millis() - s >= (uint32_t)timems);
      else if (STDIN.available() > 0)
      {
         byte c = STDIN.read();
         if (c == CTRL_C || c == CTRL_F || c == CTRL_D) done = true;
      }
   }
   digitalWrite(RXLED, LOW);
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_pulses_cmd(int nval, char** vals, CmdHandler* h)
{
   if (nval < 1) { h->setHint("pulses count|flush|show|grab|play|playfifo|add"); return NOT_ENOUGH_PARAMS; }
   const char* a = vals[0];

   if (!strcmp(a, "count"))
   {
      STDOUT.print("pulses cnt: "); STDOUT.println(radio_pulses_get_count());
      return OK;
   }
   if (!strcmp(a, "flush")) { radio_pulses_flush(); return OK; }
   if (!strcmp(a, "show"))
   {
      uint16_t* pulses = radio_pulses_get_pulses();
      int cnt = radio_pulses_get_count();
      STDOUT.println("pulses:");
      for (int idx = 0; idx < cnt; idx++)
      {
         STDOUT.print(pulses[idx]);
         if (idx < cnt - 1) STDOUT.print(",");
         if (idx > 0 && ((idx % 16) == 0)) STDOUT.print("\n\r");
      }
      STDOUT.print("\n\r");
      return OK;
   }
   if (!strcmp(a, "grab"))
   {
      if (monitor_active()) { h->setHint("monitor is running - stop it first ('monitor stop')"); return WRONG_PARAMS; }
      if (nval < 4) { h->setHint("pulses grab <off> <minUs> <maxUs>"); return NOT_ENOUGH_PARAMS; }
      int offs = atoi(vals[1]);
      int mint = atol(vals[2]);
      int maxt = atol(vals[3]);
      if (offs < 0 || offs > radio_pulses_max_count() || mint < 0 || maxt < mint) return WRONG_PARAMS;
      STDOUT.println("recording pulses...");
      int cnt = radio_pulses_grab(STDIN, offs, mint, maxt);
      STDOUT.print("pulses received: "); STDOUT.println(cnt);
      return OK;
   }
   if (!strcmp(a, "play"))
   {
      if (monitor_active()) { h->setHint("monitor is running - stop it first ('monitor stop')"); return WRONG_PARAMS; }
      if (nval < 3) { h->setHint("pulses play <off> <n> [repeat] [gapUs]"); return NOT_ENOUGH_PARAMS; }
      int offs = atoi(vals[1]);
      int total = atoi(vals[2]);
      // optional: repeat count (default 1) and inter-repeat gap in us (default 0)
      int  repeat = (nval >= 4) ? atoi(vals[3]) : 1;
      long gapUs  = (nval >= 5) ? atol(vals[4]) : 0;
      // bound against the number of STORED pulses, not the free space
      int stored = radio_pulses_get_count();
      if (offs < 0 || offs >= stored) { h->setHint("pulses play: off out of range"); return WRONG_PARAMS; }
      if (total <= 0 || (offs + total > stored)) { h->setHint("pulses play: off+n exceeds stored count"); return WRONG_PARAMS; }
      if (repeat < 1) { h->setHint("pulses play: repeat >= 1"); return WRONG_PARAMS; }
      if (gapUs < 0)  { h->setHint("pulses play: gapUs >= 0"); return WRONG_PARAMS; }

      long totalUs = 0;
      for (int r = 0; r < repeat; r++)
      {
         totalUs += radio_pulses_send(offs, total);
         // silent gap between repeats (radio is back in RX, not emitting)
         if (r < repeat - 1 && gapUs > 0)
         {
            uint64_t g = time_us_64();
            while ((uint64_t)(time_us_64() - g) < (uint64_t)gapUs);
         }
      }
      STDOUT.print("pulses sent: "); STDOUT.print(totalUs); STDOUT.print(" uS");
      if (repeat > 1) { STDOUT.print(" x"); STDOUT.print(repeat); }
      STDOUT.println(".");
      return OK;
   }
   if (!strcmp(a, "playfifo"))
   {
      // Hardware-clocked alternative to 'play': streams the pulses through the
      // TX FIFO at a fixed unitUs quantum instead of bit-banging gdo0 - see
      // radio_pulses_send_fifo(). unitUs has no sensible firmware-side default
      // (it depends entirely on the protocol being replayed), so it is
      // required, not optional.
      if (monitor_active()) { h->setHint("monitor is running - stop it first ('monitor stop')"); return WRONG_PARAMS; }
      if (nval < 4) { h->setHint("pulses playfifo <off> <n> <unitUs> [repeat] [gapUs]"); return NOT_ENOUGH_PARAMS; }
      int offs   = atoi(vals[1]);
      int total  = atoi(vals[2]);
      int unitUs = atoi(vals[3]);
      int  repeat = (nval >= 5) ? atoi(vals[4]) : 1;
      long gapUs  = (nval >= 6) ? atol(vals[5]) : 0;
      int stored = radio_pulses_get_count();
      if (offs < 0 || offs >= stored) { h->setHint("pulses playfifo: off out of range"); return WRONG_PARAMS; }
      if (total <= 0 || (offs + total > stored)) { h->setHint("pulses playfifo: off+n exceeds stored count"); return WRONG_PARAMS; }
      if (unitUs < 1) { h->setHint("pulses playfifo: unitUs must be > 0"); return WRONG_PARAMS; }
      if (repeat < 1) { h->setHint("pulses playfifo: repeat >= 1"); return WRONG_PARAMS; }
      if (gapUs < 0)  { h->setHint("pulses playfifo: gapUs >= 0"); return WRONG_PARAMS; }

      long totalUs = 0;
      for (int r = 0; r < repeat; r++)
      {
         long rc = radio_pulses_send_fifo(offs, total, unitUs);
         if (rc < 0)
         {
            const char* why = (rc == -1) ? "bad arguments"
                             : (rc == -2) ? "pulse train too long for the FIFO scratch buffer at this unitUs - use a coarser unitUs or fewer pulses"
                             : (rc == -4) ? "radio did not reach IDLE before transmit - check wiring/SPI"
                             : (rc == -3) ? "FIFO underrun/stall during transmit"
                             :              "transmit failed";
            h->setHint(why);
            return CC_ERROR;
         }
         totalUs += rc;
         if (r < repeat - 1 && gapUs > 0)
         {
            uint64_t g = time_us_64();
            while ((uint64_t)(time_us_64() - g) < (uint64_t)gapUs);
         }
      }
      STDOUT.print("pulses sent (fifo, unit="); STDOUT.print(unitUs); STDOUT.print("uS): ");
      STDOUT.print(totalUs); STDOUT.print(" uS");
      if (repeat > 1) { STDOUT.print(" x"); STDOUT.print(repeat); }
      STDOUT.println(".");
      return OK;
   }
   if (!strcmp(a, "add"))
   {
      if (nval < 2) { h->setHint("pulses add <v> <v> ..."); return NOT_ENOUGH_PARAMS; }
      for (int i = 1; i < nval; i++)
      {
         if (!isNumeric(vals[i])) return WRONG_PARAMS;
         uint16_t pulse = strtoul(vals[i], 0, 10);
         if (pulse < 1) return WRONG_PARAMS;
         radio_pulses_add(pulse);
      }
      return OK;
   }
   h->setHint("pulses count|flush|show|grab|play|add");
   return COMMAND_NOT_FOUND;
}
//------------------------------------------------------------------------------------------------------------------
static CmdStatus_e radio_dispatch(radio_iface_t* ifc, int nsub, char** sub, CmdHandler* h)
{
   const char* cmd = sub[0];
   RadioDev* r = ifc->radio;

   if (!strcmp(cmd, "help"))  { radio_usage(); return OK; }
   if (!strcmp(cmd, "info"))  { radio_info(ifc); return OK; }
   if (!strcmp(cmd, "reset")) { r->init(); return OK; }
   if (!strcmp(cmd, "rx"))    { r->setRX();   return OK; }
   if (!strcmp(cmd, "tx"))    { r->setTX();   return OK; }
   if (!strcmp(cmd, "idle"))  { r->setIdle(); return OK; }
   if (!strcmp(cmd, "rssi"))
   {
      STDOUT.print("rssi: "); STDOUT.print(r->getRSSI());
      STDOUT.print(" lqi: ");  STDOUT.println(r->getLQI());
      return OK;
   }
   if (!strcmp(cmd, "regs"))  { radio_regs(r); return OK; }
   if (!strcmp(cmd, "patable"))
   {
      if (nsub < 2) { h->setHint("patable <byte> e.g. 0xC0 (diagnostic: write OOK PATABLE[1] + read back)"); return NOT_ENOUGH_PARAMS; }
      int want = (int)strtol(sub[1], 0, 0) & 0xFF;
      int got  = rl_set_ook_patable(want) & 0xFF;
      STDOUT.print("PATABLE[1] wrote=0x"); if (want < 0x10) STDOUT.print("0"); STDOUT.print(want, HEX);
      STDOUT.print(" readback=0x");        if (got  < 0x10) STDOUT.print("0"); STDOUT.print(got, HEX);
      STDOUT.println(want == got ? "  (OK: write sticks)" : "  (MISMATCH: write ignored)");
      return OK;
   }
   if (!strcmp(cmd, "get"))
   {
      if (nsub < 2) { h->setHint("get <param>"); return NOT_ENOUGH_PARAMS; }
      return radio_get(r, sub[1], h);
   }
   if (!strcmp(cmd, "set"))
   {
      if (nsub < 3) { h->setHint("set <param> <value>"); return NOT_ENOUGH_PARAMS; }
      return radio_set(r, sub[1], nsub - 2, &sub[2], h);
   }
   if (!strncmp(cmd, "set_", 4))
   {
      if (nsub < 2) { h->setHint("set_<param> <value>"); return NOT_ENOUGH_PARAMS; }
      return radio_set(r, cmd + 4, nsub - 1, &sub[1], h);
   }
   if (!strncmp(cmd, "get_", 4))
      return radio_get(r, cmd + 4, h);

   if (!strcmp(cmd, "scan"))   return radio_scan(r, nsub - 1, &sub[1], h);
   if (!strcmp(cmd, "send"))   return radio_send(r, nsub - 1, &sub[1], h);
   if (!strcmp(cmd, "jam"))    return radio_jam(r, nsub - 1, &sub[1], h);
   if (!strcmp(cmd, "pulses")) return radio_pulses_cmd(nsub - 1, &sub[1], h);

   h->setHint("unknown radio command, use 'radio help'");
   return COMMAND_NOT_FOUND;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e radio_main(void* parent, int argc, char* argv[])
{
   CmdHandler* h = (CmdHandler*)parent;

   if (argc == 1 || (argc == 2 && !strcmp(argv[1], "help"))) { radio_usage(); return OK; }

   if (!strcmp(argv[1], "list"))
   {
      STDOUT.println("interfaces:");
      for (int i = 0; i < radio_iface_count(); i++)
      {
         radio_iface_t* ifc = radio_iface_get(i);
         STDOUT.print("- "); STDOUT.print(ifc->name);
         STDOUT.print(" present="); STDOUT.println(ifc->present ? "yes" : "no");
      }
      return OK;
   }

   radio_iface_t* ifc = radio_iface_find(argv[1]);
   if (ifc == 0) { h->setHint("unknown interface, use 'radio list'"); return WRONG_PARAMS; }

   if (argc == 2) { radio_info(ifc); return OK; }

   return radio_dispatch(ifc, argc - 2, &argv[2], h);
}
//------------------------------------------------------------------------------------------------------------------
// Replay a single profile line (already stripped of 'radio <iface>') on 'ctx'.
void radio_profile_exec_line(void* ctx, const char* line)
{
   radio_iface_t* ifc = (radio_iface_t*)ctx;
   if (ifc == 0) return;

   char buf[RPROFILE_LINE_MAXLEN];
   memset(buf, 0, sizeof(buf));
   strncpy(buf, line, sizeof(buf) - 1);

   char* sub[12];
   int nsub = 0;
   char* tok = strtok(buf, " ");
   while (tok != 0 && nsub < 12) { sub[nsub++] = tok; tok = strtok(0, " "); }
   if (nsub == 0) return;

   radio_dispatch(ifc, nsub, sub, getRootCommandHandler());
}
//------------------------------------------------------------------------------------------------------------------
