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
//------------------------------------------------------------------------------------------------------------------
CmdStatus_e monitor_main(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_start_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_stop_cmd(void* parent, int argc, char* argv[]);
CmdStatus_e monitor_status_cmd(void* parent, int argc, char* argv[]);
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
   if (mon.iface) mon.iface->radio->setIdle();
   STDOUT.print("monitor stopped, captures="); STDOUT.println(mon.count);
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
   STDOUT.print(" captures="); STDOUT.println(mon.count);
   return OK;
}
//------------------------------------------------------------------------------------------------------------------
static void monitor_print_hexbytes(uint8_t* buf, int len)
{
   for (int i = 0; i < len; i++)
   {
      if (buf[i] < 0x10) STDOUT.print("0");
      STDOUT.print(buf[i], HEX);
   }
}
//------------------------------------------------------------------------------------------------------------------
static void monitor_print_hex16(uint16_t v)
{
   // big-endian, 4 hex digits
   char t[5];
   snprintf(t, sizeof(t), "%04x", (unsigned)v);
   STDOUT.print(t);
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

   if (mon.fmt == MON_FMT_JSON)
   {
      STDOUT.print("{ \"iface\":\""); STDOUT.print(mon.iface->name);
      STDOUT.print("\", \"ts\":"); STDOUT.print(ts);
      STDOUT.print(", \"mod\":\""); STDOUT.print(RadioDev::modulation(mon.mod));
      STDOUT.print("\", \"rssi\":"); STDOUT.print(rssi);
      STDOUT.print(", \"lqi\":"); STDOUT.print(lqi);
      STDOUT.print(", \"n\":"); STDOUT.print(n);
      STDOUT.print(", \"us\":[");
      for (int i = 0; i < n; i++)
      {
         STDOUT.print(pulses[i]);
         if (i < n - 1) STDOUT.print(",");
      }
      STDOUT.println("] }");
   }
   else
   {
      STDOUT.print(mon.iface->name); STDOUT.print(" ts="); STDOUT.print(ts);
      STDOUT.print(" "); STDOUT.print(RadioDev::modulation(mon.mod));
      STDOUT.print(" rssi="); STDOUT.print(rssi); STDOUT.print(" lqi="); STDOUT.print(lqi);
      STDOUT.print(" n="); STDOUT.print(n); STDOUT.print(" ");
      for (int i = 0; i < n; i++) monitor_print_hex16(pulses[i]);
      STDOUT.println();
   }
   mon.count++;
}
//------------------------------------------------------------------------------------------------------------------
// FSK/GFSK/4-FSK/MSK: dump the raw RX-FIFO bytes.
static void monitor_emit_pkt(RadioDev* r, uint8_t* buf, int len)
{
   uint32_t ts = millis();
   int rssi = r->getRSSI();
   int lqi  = r->getLQI();
   bool crc = r->crcOk();

   if (mon.fmt == MON_FMT_JSON)
   {
      STDOUT.print("{ \"iface\":\""); STDOUT.print(mon.iface->name);
      STDOUT.print("\", \"ts\":"); STDOUT.print(ts);
      STDOUT.print(", \"mod\":\""); STDOUT.print(RadioDev::modulation(mon.mod));
      STDOUT.print("\", \"rssi\":"); STDOUT.print(rssi);
      STDOUT.print(", \"lqi\":"); STDOUT.print(lqi);
      STDOUT.print(", \"crc\":"); STDOUT.print(crc ? "true" : "false");
      STDOUT.print(", \"len\":"); STDOUT.print(len);
      STDOUT.print(", \"hex\":\""); monitor_print_hexbytes(buf, len); STDOUT.println("\" }");
   }
   else
   {
      STDOUT.print(mon.iface->name); STDOUT.print(" ts="); STDOUT.print(ts);
      STDOUT.print(" "); STDOUT.print(RadioDev::modulation(mon.mod));
      STDOUT.print(" rssi="); STDOUT.print(rssi); STDOUT.print(" lqi="); STDOUT.print(lqi);
      STDOUT.print(" crc="); STDOUT.print(crc ? 1 : 0);
      STDOUT.print(" len="); STDOUT.print(len); STDOUT.print(" ");
      monitor_print_hexbytes(buf, len);
      STDOUT.println();
   }
   mon.count++;
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
