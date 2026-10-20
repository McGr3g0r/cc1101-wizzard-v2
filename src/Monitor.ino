//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-17
//------------------------------------------------------------------------------------------------------------------
// monitor command (v2) - raw capture only
//
//   monitor start <iface> <profile> [json|hex] [minUs] [maxGapUs] [rssiGate] [direct]
//   monitor stop
//   monitor status
//   monitor beginlog   - also append every emitted capture to monNNNN.txt on
//                        LittleFS (NNNN = next free number; a fresh number each
//                        beginlog). The file rotates to the next number once it
//                        passes 32 KB, only between records (never mid-line).
//   monitor endlog     - stop file logging ('monitor stop' also closes it)
//
// This firmware does NOT interpret protocols - it only captures and dumps raw
// data. Decoding is left to the host connected over the CLI.
//
// Monitoring is NON-BLOCKING: monitor_poll() runs from loop(), so the CLI stays
// responsive (type 'monitor stop' or any command at any time).
//
// Capture method depends on the modulation set by the profile, and on the
// 'direct' keyword:
//   OOK (ASK)                    -> always direct/pulse capture (forced)
//   2-FSK/GFSK/4-FSK/MSK, +'direct' -> ALSO direct/pulse capture
//   2-FSK/GFSK/4-FSK/MSK, no 'direct'-> packet mode (default for these)
//
// Direct mode is not actually OOK-specific: CC1101's async serial RX mode
// puts whatever the demodulator decided (envelope slicer for OOK, frequency
// slicer for FSK-family modes) onto GDO0 as a level, continuously, regardless
// of modulation - so the exact same pulse-timing capture that serves OOK also
// works for e.g. 2-FSK carrying a non-standard/AFSK-style bit framing the
// chip's own packet engine (sync word, fixed/variable length, CRC) would not
// decode correctly. Pass 'direct' as the last argument to use it:
//   monitor start r0 fsk433 json 100 2000 -85 direct
// In direct mode, the emitted "us" pulse-train format is identical to OOK's -
// only "mod" in the output differs, reporting the real modulation in use.
//
// Direct-mode capture params (optional; same meaning regardless of modulation):
//   minUs    - runt filter: leading/trailing edges shorter than this are
//              dropped                                           (default 100)
//   maxGapUs - guard / inter-frame gap: a continuous LOW this long marks a
//              frame boundary. Capture waits for an idle gap, starts on the
//              next rising edge, and ends one frame on the following gap, so
//              one capture = one complete, aligned frame. Set it just above the
//              longest intra-frame LOW (for a keyfob ~10-30x the bit period;
//              e.g. try 3000-5000 for HCS/KeeLoq).              (default 15000)
//------------------------------------------------------------------------------------------------------------------
#include "settings.h"
#include "CmdHandler.h"
#include "cc1101.h"
#include "radio_iface.h"
#include "rprofile.h"
#include "radio_pulses.h"
#include "utils.h"
#if USE_FILE_SYSTEM == 1
#include "LittleFS.h"
#endif
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_main(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_start_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_stop_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_status_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_beginlog_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_endlog_cmd(void* parent, int argc, char* argv[]);
CmdHandler* getRootCommandHandler(void);
void radio_profile_exec_line(void* ctx, const char* line);  // defined in radio_handler.ino
//------------------------------------------------------------------------------------------------------------------
#define MON_FMT_JSON 0
#define MON_FMT_HEX  1
#define MOD_OOK      2
// No MIN/MAX pulse-count filter here by design: the receiver is low-level and
// has no notion of what a "real" frame looks like for any given remote, so it
// does not guess which captures are noise - it reports every capture it makes
// (n > 0) and leaves filtering/interpretation entirely to the host. A capture
// can be as short as 1 pulse or as long as the buffer allows (see
// radio_pulses_buffer_len, currently RADIO_BUFFER_MAIN/2 pulses); a very long
// one takes proportionally longer to print over serial, which is the direct,
// visible cost of not dropping data silently.
#define MON_OOK_DEF_MINUS  100
#define MON_OOK_DEF_MAXGAP 15000
#define MON_OOK_RSSI_GATE  -85       // dBm: only capture when RSSI is above this (carrier present)
                                     // noise floor ~ -92..-99 dBm in testing; override per setup
// Direct/async GDO0 capture (pulse-train timing, same mechanism as the OOK
// path below) is not actually OOK-specific: CC1101's async serial RX mode
// just puts whatever the demodulator decided (envelope slicer for OOK/ASK,
// frequency slicer for 2-FSK/GFSK/4-FSK/MSK) onto GDO0 as a level, continuously,
// regardless of modulation. For OOK this firmware always captures this way
// (forced, below). For every other modulation it is opt-in via the 'direct'
// keyword to 'monitor start' - e.g. for 2-FSK, this lets a host decode a
// non-standard bit framing itself (AFSK-style) from raw symbol-timing, the
// same way it would decode an unknown OOK remote from raw pulses, instead of
// relying on CC1101's own packet engine (sync word, fixed/variable length,
// CRC) which assumes a standard framing the signal may not actually have.
//------------------------------------------------------------------------------------------------------------------
static struct monitor_state_s
{
   bool           active;
   radio_iface_t* iface;
   int            fmt;        // MON_FMT_*
   int            mod;        // modulation in effect
   bool           direct;     // true: async GDO0 pulse-timing capture (forced for OOK,
                               // opt-in via 'direct' for other modulations); false: packet mode
   int            minUs;      // direct mode: shortest pulse kept
   int            maxGapUs;   // direct mode: gap ending a frame
   int            rssiGate;   // direct mode: capture only when RSSI >= this (dBm)
   uint32_t       count;      // frames/packets emitted
   char           profile[RPROFILE_NAME_MAXLEN + 1];
} mon;
//------------------------------------------------------------------------------------------------------------------
// Cross-core handoff: the OOK pulse capture is a tight GDO0 busy-wait that would
// starve core0's USB/CLI (whether polled or interrupt-driven, because GDO0 toggles
// fast in async direct mode). So it runs on CORE1 (see loop1 -> monitor_core1_task),
// leaving core0 free for USB, CLI and SPI. Handshake via these flags.
static volatile bool mon_cap_enabled = false;   // OOK monitor active
static volatile bool mon_cap_request = false;   // core0 -> core1: capture a frame
static volatile bool mon_cap_done    = false;   // core1 -> core0: frame ready
static volatile int  mon_cap_count   = 0;       // result pulse count
static int           mon_cap_min     = 100;     // params (stable while running)
static int           mon_cap_maxgap  = 15000;
// RSSI at the moment capture was armed (core0-only; set then read within the
// same core0 polling loop, no cross-core access). A carrier is guaranteed
// present at THIS instant, since it is exactly what satisfied rssiGate to
// trigger the arm - unlike a fresh getRSSI() taken after capture completes,
// which lands in the trailing gap that, by definition, ended the frame (the
// carrier is reliably OFF there), so it would read close to the noise floor
// regardless of how strong the actual signal was.
static int           mon_cap_rssi    = 0;
//------------------------------------------------------------------------------------------------------------------
// Runs on core1 (from loop1). Blocking capture here is fine - core0 is untouched.
void monitor_core1_task(void)
{
   if (mon_cap_enabled && mon_cap_request && !mon_cap_done)
   {
      int n = radio_pulses_capture(0, mon_cap_min, mon_cap_maxgap);
      mon_cap_count = n;
      asm volatile("" ::: "memory");   // publish buffer+count before the flag
      mon_cap_request = false;
      mon_cap_done    = true;
   }
}
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t monitor_handler = { &monitor_main, NULL, NULL, "monitor", "non-blocking raw RX monitor, use: 'monitor help'", 0, "", "monitor help" };
//------------------------------------------------------------------------------------------------------------------
cmd_handler_t monitor_sub_cmd[] = {
   { &monitor_main,       &monitor_handler, NULL, "help",   "this help", 0, "", "monitor help" },
   { &monitor_start_cmd,  &monitor_handler, NULL, "start",  "start <iface> <profile> [json|hex] [minUs] [maxGapUs] [rssiGate dBm] [direct]", 2, "tt", "monitor start r0 ook433 json" },
   { &monitor_stop_cmd,   &monitor_handler, NULL, "stop",   "stop monitoring", 0, "", "monitor stop" },
   { &monitor_status_cmd, &monitor_handler, NULL, "status", "show monitor state", 0, "", "monitor status" },
   { &monitor_beginlog_cmd, &monitor_handler, NULL, "beginlog", "also log captures to monNNNN.txt (new file each time; rotates at 32KB)", 0, "", "monitor beginlog" },
   { &monitor_endlog_cmd,   &monitor_handler, NULL, "endlog",   "stop logging captures to file", 0, "", "monitor endlog" },
   { 0, 0, NULL, "", "", 0, "", "" }
};
//------------------------------------------------------------------------------------------------------------------
void monitor_cmd_init(void)
{
   memset(&mon, 0, sizeof(mon));
   getRootCommandHandler()->registerHandler(&monitor_handler, monitor_sub_cmd);
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_main(void* parent, int argc, char* argv[])
{
   STDOUT.println(monitor_handler.desc);
   int idx = 0;
   cmd_handler_t* item = &monitor_handler.sub_cmd[idx];
   while (item && item->handler != 0)
   {
      STDOUT.print("- "); STDOUT.print(item->cmd); STDOUT.print(" : "); STDOUT.println(item->desc);
      idx++;
      item = &monitor_handler.sub_cmd[idx];
   }
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_start_cmd(void* parent, int argc, char* argv[])
{
   CmdHandler* h = (CmdHandler*)parent;

   radio_iface_t* ifc = radio_iface_find(argv[2]);
   if (ifc == 0) { h->setHint("unknown interface, use 'radio list'"); return WRONG_PARAMS; }

   const char* pname = argv[3];
   if (!rprofile_exists(pname)) { h->setHint("profile not found, use 'profile list'"); return WRONG_PARAMS; }

   int fmt      = MON_FMT_JSON;
   int minUs    = MON_OOK_DEF_MINUS;
   int maxGapUs = MON_OOK_DEF_MAXGAP;
   int rssiGate = MON_OOK_RSSI_GATE;

   if (argc >= 5)
   {
      if      (!strcmp(argv[4], "json")) fmt = MON_FMT_JSON;
      else if (!strcmp(argv[4], "hex"))  fmt = MON_FMT_HEX;
      else { h->setHint("format: json | hex"); return WRONG_PARAMS; }
   }
   // 'direct' (forces async GDO0 pulse-timing capture for a non-OOK modulation
   // - see the mon.direct comment above) is recognized as the last argument,
   // wherever it falls, so it can follow either just <fmt> or the full
   // <minUs> <maxGapUs> <rssiGate> - the numeric params keep their normal
   // positions either way.
   bool wantDirect = (argc >= 6 && !strcmp(argv[argc - 1], "direct"));
   int numArgc = wantDirect ? argc - 1 : argc;   // numeric parsing ignores the trailing keyword
   if (numArgc >= 6) minUs    = atoi(argv[5]);
   if (numArgc >= 7) maxGapUs = atoi(argv[6]);
   if (numArgc >= 8) rssiGate = atoi(argv[7]);   // dBm carrier-sense gate (direct mode)
   if (minUs < 1 || maxGapUs <= minUs) { h->setHint("need 0 < minUs < maxGapUs"); return WRONG_PARAMS; }

   // apply the profile (sets freq, modulation, bandwidth, data rate, rx, ...)
   if (!rprofile_apply(pname, radio_profile_exec_line, ifc)) return FILE_ERROR;

   mon.active   = true;
   mon.iface    = ifc;
   mon.fmt      = fmt;
   mon.mod      = ifc->radio->getModulation();
   mon.direct   = (mon.mod == MOD_OOK) || wantDirect;
   mon.minUs    = minUs;
   mon.maxGapUs = maxGapUs;
   mon.rssiGate = rssiGate;
   mon.count    = 0;
   memset(mon.profile, 0, sizeof(mon.profile));
   strncpy(mon.profile, pname, sizeof(mon.profile) - 1);

   if (mon.direct)
   {
      // enter async direct RX ONCE (data on GDO0); core1 bit-bangs it. Works
      // the same regardless of modulation - see the mon.direct comment above.
      ifc->radio->setCCMode(0);
      ifc->radio->setPacketFormat(3);
      ifc->radio->setRX();
      mon_cap_min     = mon.minUs;
      mon_cap_maxgap  = mon.maxGapUs;
      mon_cap_request = false;
      mon_cap_done    = false;
      mon_cap_enabled = true;    // core1 may now capture on request
   }
   else
      ifc->radio->setRX();   // packet-mode RX

   STDOUT.print("monitor started on "); STDOUT.print(ifc->name);
   STDOUT.print(" freq="); STDOUT.print(ifc->radio->getFreq());
   STDOUT.print("KHz mod="); STDOUT.print(RadioDev::modulation(mon.mod));
   STDOUT.print(" fmt="); STDOUT.print(fmt == MON_FMT_JSON ? "json" : "hex");
   STDOUT.println(" (type 'monitor stop' to end)");
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_stop_cmd(void* parent, int argc, char* argv[])
{
   if (!mon.active) { STDOUT.println("monitor not running"); return OK; }
   mon.active = false;
   mon_cap_enabled = false;    // stop core1 capturing
   mon_cap_request = false;
   mon_cap_done    = false;
   monitor_endlog();           // close any open log file
   if (mon.iface) mon.iface->radio->setIdle();
   STDOUT.print("monitor stopped, captures="); STDOUT.println(mon.count);
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_beginlog_cmd(void* parent, int argc, char* argv[])
{
#if USE_FILE_SYSTEM == 1
   if (!monitor_beginlog())
   {
      ((CmdHandler*)parent)->setHint("could not create log file");
      return FILE_ERROR;
   }
   char name[16];
   snprintf(name, sizeof(name), "mon%04d.txt", mon_log_num);
   STDOUT.print("logging captures to "); STDOUT.println(name);
   return OK;
#else
   ((CmdHandler*)parent)->setHint("file system disabled");
   return FILE_ERROR;
#endif
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_endlog_cmd(void* parent, int argc, char* argv[])
{
   monitor_endlog();
   STDOUT.println("capture logging stopped");
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_status_cmd(void* parent, int argc, char* argv[])
{
   if (!mon.active) { STDOUT.println("monitor: idle"); return OK; }
   STDOUT.print("monitor: active iface="); STDOUT.print(mon.iface->name);
   STDOUT.print(" profile="); STDOUT.print(mon.profile);
   STDOUT.print(" mod="); STDOUT.print(RadioDev::modulation(mon.mod));
   STDOUT.print(" direct="); STDOUT.print(mon.direct ? 1 : 0);
   STDOUT.print(" fmt="); STDOUT.print(mon.fmt == MON_FMT_JSON ? "json" : "hex");
   STDOUT.print(" captures="); STDOUT.print(mon.count);
   if (mon_logging)
   {
      char name[16];
      snprintf(name, sizeof(name), "mon%04d.txt", mon_log_num);
      STDOUT.print(" log="); STDOUT.print(name);
   }
   else
   {
      STDOUT.print(" log=off");
   }
   STDOUT.println();
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
//------------------------------------------------------------------------------------------------------------------
// Optional file logging: 'monitor beginlog' tees every emitted record into a
// LittleFS file monNNNN.txt (NNNN zero-padded; the next free number, i.e.
// highest existing +1, is chosen at each beginlog). The file is rotated to the
// next number once it passes 32 KB, but only AFTER a full line is written, so a
// record is never split across files. 'monitor endlog' (and 'monitor stop')
// closes the file.
//------------------------------------------------------------------------------------------------------------------
#define MON_LOG_ROTATE_BYTES 32768
#if USE_FILE_SYSTEM == 1
static bool     mon_logging   = false;
static File     mon_logfile;
static uint32_t mon_log_bytes = 0;
static int      mon_log_num   = 0;
//------------------------------------------------------------------------------------------------------------------
static int mon_log_next_num(void)
{
   int maxn = 0;
   Dir dir = LittleFS.openDir("/");
   while (dir.next())
   {
      if (!dir.isFile()) continue;
      String fn = dir.fileName();
      if (fn.startsWith("/")) fn = fn.substring(1);
      if (fn.startsWith("mon") && fn.endsWith(".txt"))
      {
         String mid = fn.substring(3, fn.length() - 4);   // between 'mon' and '.txt'
         if (mid.length() == 0) continue;
         bool digits = true;
         for (unsigned i = 0; i < mid.length(); i++)
            if (mid[i] < '0' || mid[i] > '9') { digits = false; break; }
         if (digits) { int v = mid.toInt(); if (v > maxn) maxn = v; }
      }
   }
   return maxn + 1;
}
//------------------------------------------------------------------------------------------------------------------
static bool mon_log_open(int num)
{
   char path[20];
   snprintf(path, sizeof(path), "/mon%04d.txt", num);
   mon_logfile = LittleFS.open(path, "w");
   if (!mon_logfile) return false;
   mon_log_bytes = 0;
   mon_log_num   = num;
   return true;
}
//------------------------------------------------------------------------------------------------------------------
static bool monitor_beginlog(void)
{
   if (mon_logging) { mon_logfile.close(); mon_logging = false; }   // each beginlog starts a fresh number
   if (!mon_log_open(mon_log_next_num())) return false;
   mon_logging = true;
   return true;
}
//------------------------------------------------------------------------------------------------------------------
static void monitor_endlog(void)
{
   if (mon_logging) { mon_logfile.close(); mon_logging = false; }
}
//------------------------------------------------------------------------------------------------------------------
static void mon_log_append(const String& rec)
{
   if (!mon_logging || !mon_logfile) return;
   mon_log_bytes += mon_logfile.print(rec);
   mon_log_bytes += mon_logfile.print('\n');
   mon_logfile.flush();
   // rotate only here, after a full line is on disk -> never split a record
   if (mon_log_bytes >= MON_LOG_ROTATE_BYTES)
   {
      mon_logfile.close();
      mon_log_open(mon_log_next_num());   // current file now exists -> picks the next number
   }
}
#else
static const bool mon_logging = false;
static const int  mon_log_num = 0;
static bool monitor_beginlog(void)        { return false; }
static void monitor_endlog(void)          { }
static void mon_log_append(const String&) { }
#endif
//------------------------------------------------------------------------------------------------------------------
// Single exit point for every emitted record: to the console always, and to the
// log file when logging is active.
static void monitor_emit_record(const String& rec)
{
   STDOUT.println(rec);
   mon_log_append(rec);
   mon.count++;
}
//------------------------------------------------------------------------------------------------------------------
// Direct mode (any modulation - forced for OOK, opt-in for others, see the
// mon.direct comment above): dump the raw captured pulse-train (microsecond
// level durations straight off GDO0).
static void monitor_emit_pulses(RadioDev* r, uint16_t* pulses, int n)
{
   uint32_t ts = millis();
   // NOT a fresh r->getRSSI() here - see the mon_cap_rssi comment above: by
   // the time capture is done, the carrier is reliably OFF (we are in the
   // trailing gap that ended the frame), so a fresh read would be meaningless.
   int rssi = mon_cap_rssi;
   int lqi  = r->getLQI();

   String rec;
   rec.reserve((size_t)n * 6 + 96);   // avoid O(n^2) String growth for big frames
   if (mon.fmt == MON_FMT_JSON)
   {
      rec += "{ \"iface\":\""; rec += mon.iface->name;
      rec += "\", \"ts\":"; rec += (unsigned long)ts;
      rec += ", \"mod\":\""; rec += RadioDev::modulation(mon.mod);
      rec += "\", \"rssi\":"; rec += rssi;
      rec += ", \"lqi\":"; rec += lqi;
      rec += ", \"n\":"; rec += n;
      rec += ", \"us\":[";
      for (int i = 0; i < n; i++) { rec += (unsigned)pulses[i]; if (i < n - 1) rec += ','; }
      rec += "] }";
   }
   else
   {
      char hx[6];
      rec += mon.iface->name; rec += " ts="; rec += (unsigned long)ts;
      rec += ' '; rec += RadioDev::modulation(mon.mod);
      rec += " rssi="; rec += rssi; rec += " lqi="; rec += lqi;
      rec += " n="; rec += n; rec += ' ';
      for (int i = 0; i < n; i++) { snprintf(hx, sizeof(hx), "%04x", (unsigned)pulses[i]); rec += hx; }
   }
   monitor_emit_record(rec);
}
//------------------------------------------------------------------------------------------------------------------
// FSK/GFSK/4-FSK/MSK: dump the raw RX-FIFO bytes.
static void monitor_emit_pkt(RadioDev* r, uint8_t* buf, int len)
{
   uint32_t ts = millis();
   int rssi = r->getRSSI();
   int lqi  = r->getLQI();
   bool crc = r->crcOk();

   String rec;
   rec.reserve((size_t)len * 2 + 128);
   char hx[3];
   if (mon.fmt == MON_FMT_JSON)
   {
      rec += "{ \"iface\":\""; rec += mon.iface->name;
      rec += "\", \"ts\":"; rec += (unsigned long)ts;
      rec += ", \"mod\":\""; rec += RadioDev::modulation(mon.mod);
      rec += "\", \"rssi\":"; rec += rssi;
      rec += ", \"lqi\":"; rec += lqi;
      rec += ", \"crc\":"; rec += (crc ? "true" : "false");
      rec += ", \"len\":"; rec += len;
      rec += ", \"hex\":\"";
      for (int i = 0; i < len; i++) { snprintf(hx, sizeof(hx), "%02X", buf[i]); rec += hx; }
      rec += "\" }";
   }
   else
   {
      rec += mon.iface->name; rec += " ts="; rec += (unsigned long)ts;
      rec += ' '; rec += RadioDev::modulation(mon.mod);
      rec += " rssi="; rec += rssi; rec += " lqi="; rec += lqi;
      rec += " crc="; rec += (crc ? 1 : 0);
      rec += " len="; rec += len; rec += ' ';
      for (int i = 0; i < len; i++) { snprintf(hx, sizeof(hx), "%02X", buf[i]); rec += hx; }
   }
   monitor_emit_record(rec);
}
//------------------------------------------------------------------------------------------------------------------
// Called every loop() iteration. Does at most one capture/read, then returns so
// the CLI can run. Safe to call when inactive.
void monitor_poll(void)
{
   if (!mon.active || mon.iface == 0) return;

   RadioDev* r = mon.iface->radio;

   if (mon.direct)
   {
      // core0 side: never blocks. The actual capture happens on core1.
      static uint32_t lastCapMs = 0;

      // a frame captured by core1 is ready -> emit it
      if (mon_cap_done)
      {
         asm volatile("" ::: "memory");
         int n = mon_cap_count;
         mon_cap_done = false;
         lastCapMs = millis();
         if (n > 0)   // report every capture made; n==0 means grab() timed out before any edge arrived
            monitor_emit_pulses(r, radio_pulses_get_pulses(), n);   // emit the WHOLE frame (no truncation)
         return;
      }

      // not capturing -> carrier-sense; if a signal is present ask core1 to capture
      if (!mon_cap_request)
      {
         int rssi = r->getRSSI();
         if (rssi >= mon.rssiGate && (millis() - lastCapMs) >= 300)
         {
            mon_cap_rssi    = rssi;   // latch: see mon_cap_rssi comment above
            mon_cap_request = true;   // core1 picks this up and captures
         }
      }
   }
   else
   {
      // packet mode: poll the RX done flag, read the FIFO
      if (r->rxAvailable())
      {
         uint8_t buf[CC_PACKET_BUFFER_LEN];
         memset(buf, 0, sizeof(buf));
         int len = r->receive(buf);
         if (len > 0 && len <= CC_PACKET_BUFFER_LEN)
            monitor_emit_pkt(r, buf, len);
      }
   }
}
//------------------------------------------------------------------------------------------------------------------
bool monitor_active(void) { return mon.active; }
//------------------------------------------------------------------------------------------------------------------
