//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-20
//------------------------------------------------------------------------------------------------------------------
// RadioLib shim - the ONLY translation unit that includes RadioLib. See rl_cc1101.h.
//------------------------------------------------------------------------------------------------------------------
#include <Arduino.h>
#include <SPI.h>
#include <RadioLib.h>
#include "rl_cc1101.h"
//------------------------------------------------------------------------------------------------------------------
// RadioLib's SPIsetRegValue/SPIgetRegValue are 'protected'. Subclass to expose
// them so the shim can do raw CC1101 register access for parameters that have
// no high-level RadioLib setter.
class RLCC1101 : public CC1101 {
  public:
    RLCC1101(Module* m) : CC1101(m) {}
    using CC1101::SPIsetRegValue;
    using CC1101::SPIgetRegValue;
    using CC1101::getMod;
};
//------------------------------------------------------------------------------------------------------------------
static Module*   s_mod    = nullptr;
static RLCC1101* s_radio  = nullptr;   // RadioLib's CC1101 (not the firmware's wrapper)
static bool     s_ok     = false;
static int      s_beginSt = 0;         // RadioLib begin() return code (diagnostic)
static volatile bool s_rxFlag = false;
static float    s_freqMhz = 433.92f;   // last frequency set; picks the PA band table
//------------------------------------------------------------------------------------------------------------------
// CC1101::SPIwriteRegisterBurst()/SPIreadRegisterBurst() are 'protected' (same
// situation as SPIsetRegValue/SPIgetRegValue above, and as SPIsendCommand used
// by rl_strobe() further down) - not reachable from this free function. Their
// own bodies just OR in RADIOLIB_CC1101_CMD_BURST and forward to the PUBLIC
// Module-level method of the same name, so that is replicated here directly.
// This must NOT be replaced with a plain call to the public Module method
// without the OR: that omits the burst bit, which is not a valid multi-byte
// burst access on real CC1101 silicon (confirmed the hard way - see the
// playfifo debugging history for rl_transmit_raw_bits() further down).
static void rl_burst_write(uint8_t reg, const uint8_t* data, size_t len)
{
   s_radio->getMod()->SPIwriteRegisterBurst((uint8_t)(reg | RADIOLIB_CC1101_CMD_BURST), data, len);
}
static void rl_burst_read(uint8_t reg, uint8_t numBytes, uint8_t* inBytes)
{
   s_radio->getMod()->SPIreadRegisterBurst((uint8_t)(reg | RADIOLIB_CC1101_CMD_BURST), numBytes, inBytes);
}
//------------------------------------------------------------------------------------------------------------------
static void rl_isr(void) { s_rxFlag = true; }
//------------------------------------------------------------------------------------------------------------------
static bool rl_known_version(int v)
{
   // accept any CC1101 version RadioLib recognises (incl. clones)
   return v == 0x14 || v == 0x04 || v == 0x17 || v == 0x03;
}
//------------------------------------------------------------------------------------------------------------------
bool rl_begin(int csPin, int gdo0Pin, int gdo2Pin,
              float freqMhz, float brKbps, float devKhz, float rxbwKhz,
              int pwrDbm, int preambleBits)
{
   if (s_radio == nullptr)
   {
      s_mod   = new Module((uint32_t)csPin, (uint32_t)gdo0Pin, RADIOLIB_NC, (uint32_t)gdo2Pin, SPI);
      s_radio = new RLCC1101(s_mod);
   }
   s_freqMhz = freqMhz;
   s_beginSt = s_radio->begin(freqMhz, brKbps, devKhz, rxbwKhz, (int8_t)pwrDbm, (uint8_t)preambleBits);
   // "present" means a CC1101 actually answered on SPI with a valid chip
   // version. begin() can still return a non-fatal error from one of its
   // range-checked setters even though the chip is there and config() ran;
   // default_init() reapplies the full configuration afterwards.
   int ver = s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_VERSION);
   s_ok = rl_known_version(ver);
   return s_ok;
}
//------------------------------------------------------------------------------------------------------------------
void rl_reset(void)    { if (s_radio) s_radio->reset(); }
bool rl_present(void)  { return s_ok; }
int  rl_begin_status(void) { return s_beginSt; }
int  rl_version(void)  { return s_radio ? s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_VERSION) : 0; }
int  rl_partnum(void)  { return s_radio ? s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_PARTNUM) : 0; }
//------------------------------------------------------------------------------------------------------------------
// Diagnostics: read a single config register, and the two OOK PATABLE entries.
int  rl_get_reg(int addr) { return s_radio ? (int)s_radio->SPIgetRegValue((uint8_t)addr) : -1; }
void rl_get_patable(uint8_t* out2)
{
   out2[0] = 0; out2[1] = 0;
   if (!s_radio) return;
   uint8_t t[8] = {0};
   // PATABLE is a special 8-byte register at 0x3E; burst-read it
   rl_burst_read(RADIOLIB_CC1101_REG_PATABLE, 8, t);
   out2[0] = t[0];
   out2[1] = t[1];
}
// Diagnostic: force IDLE, set FREND0=0x11 and PATABLE={0x00, powerByte}, then
// read PATABLE back and return entry [1], so we can tell if the write sticks.
int rl_set_ook_patable(int powerByte)
{
   if (!s_radio) return -1;
   s_radio->standby();
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_FREND0, 0x11);
   uint8_t pa[2] = { 0x00, (uint8_t)powerByte };
   rl_burst_write(RADIOLIB_CC1101_REG_PATABLE, pa, 2);
   uint8_t t[8] = {0};
   rl_burst_read(RADIOLIB_CC1101_REG_PATABLE, 8, t);
   return t[1];
}
//------------------------------------------------------------------------------------------------------------------
// TI/ELECHOUSE recommended PATABLE power bytes, per band. Index order matches
// the dBm breakpoints used in rl_power_byte_for_dbm() below. These are the same
// values the old ELECHOUSE build used, so OOK output level is preserved.
//                              -30   -20   -15   -10    0     5     7    >7
static const uint8_t PA_315[8] = {0x12,0x0D,0x1C,0x34,0x51,0x85,0xCB,0xC2};
static const uint8_t PA_433[8] = {0x12,0x0E,0x1D,0x34,0x60,0x84,0xC8,0xC0};
//                              -30   -20   -15   -10   -6    0     5     7    10   >10
static const uint8_t PA_868[10]= {0x03,0x17,0x1D,0x26,0x37,0x50,0x86,0xCD,0xC5,0xC0};
static const uint8_t PA_915[10]= {0x03,0x0E,0x1E,0x27,0x38,0x8E,0x84,0xCC,0xC3,0xC0};
//------------------------------------------------------------------------------------------------------------------
uint8_t rl_power_byte_for_dbm(int dbm)
{
   float f = s_freqMhz;
   // 300-348 and 387-464 MHz bands: 8-entry tables, coarse steps
   if (f <= 348.0f || (f >= 378.0f && f <= 464.0f))
   {
      const uint8_t* t = (f <= 348.0f) ? PA_315 : PA_433;
      int i;
      if      (dbm <= -30) i = 0;
      else if (dbm <= -20) i = 1;
      else if (dbm <= -15) i = 2;
      else if (dbm <= -10) i = 3;
      else if (dbm <=   0) i = 4;
      else if (dbm <=   5) i = 5;
      else if (dbm <=   7) i = 6;
      else                 i = 7;
      return t[i];
   }
   // 779-928 MHz: 10-entry tables, finer steps (split 868 vs 915 at 900 MHz)
   const uint8_t* t = (f < 900.0f) ? PA_868 : PA_915;
   int i;
   if      (dbm <= -30) i = 0;
   else if (dbm <= -20) i = 1;
   else if (dbm <= -15) i = 2;
   else if (dbm <= -10) i = 3;
   else if (dbm <=  -6) i = 4;
   else if (dbm <=   0) i = 5;
   else if (dbm <=   5) i = 6;
   else if (dbm <=   7) i = 7;
   else if (dbm <=  10) i = 8;
   else                 i = 9;
   return t[i];
}
//------------------------------------------------------------------------------------------------------------------
void rl_arm_ook_tx(int dbm)
{
   if (!s_radio) return;
   s_radio->standby();
   // force ASK/OOK modulation format (MDMCFG2[6:4]) in case something changed it
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2,
                           RADIOLIB_CC1101_MOD_FORMAT_ASK_OOK, 6, 4);
   // FREND0.PA_POWER=1 so the modulator keys between PATABLE[0] and PATABLE[1]
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_FREND0, 0x11);
   // two-level OOK table: space = 0x00 (carrier off), mark = band power byte
   uint8_t pa[2] = { 0x00, rl_power_byte_for_dbm(dbm) };
   rl_burst_write(RADIOLIB_CC1101_REG_PATABLE, pa, 2);
}
//------------------------------------------------------------------------------------------------------------------
// ELECHOUSE's per-band FSCTRL0 trim table (SmartRC_CC1101.h clb1..clb4 defaults).
// Index 0=315MHz band, 1=433MHz band, 2=868MHz band, 3=915MHz band.
static const uint8_t FSCTRL0_TRIM[4][2] = { {24,28}, {31,38}, {65,76}, {77,79} };
//------------------------------------------------------------------------------------------------------------------
static uint8_t rl_map_u8(long x, long inMin, long inMax, long outMin, long outMax)
{
   // Integer map(), matching Arduino's map() exactly (including the implicit
   // float->long truncation ELECHOUSE's setMHZ(float) applies to its MHz
   // argument before interpolating - replicated here via the (long) cast at
   // the call site, not inside this helper).
   return (uint8_t)((x - inMin) * (outMax - outMin) / (inMax - inMin) + outMin);
}
//------------------------------------------------------------------------------------------------------------------
void rl_apply_freq_calibration(float mhz)
{
   if (!s_radio) return;
   long f = (long)mhz;   // ELECHOUSE's setMHZ() truncates the same way via map(int,...)
   uint8_t fsctrl0;
   bool aboveThresh;
   int band;   // 0=315, 1=433, 2=868, 3=915

   if      (mhz >= 300.0f && mhz <= 348.0f)   { band = 0; fsctrl0 = rl_map_u8(f, 300, 348, FSCTRL0_TRIM[0][0], FSCTRL0_TRIM[0][1]); aboveThresh = (mhz >= 322.88f); }
   else if (mhz >= 378.0f && mhz <= 464.0f)   { band = 1; fsctrl0 = rl_map_u8(f, 378, 464, FSCTRL0_TRIM[1][0], FSCTRL0_TRIM[1][1]); aboveThresh = (mhz >= 430.5f);  }
   else if (mhz >= 779.0f && mhz <= 899.99f)  { band = 2; fsctrl0 = rl_map_u8(f, 779, 899, FSCTRL0_TRIM[2][0], FSCTRL0_TRIM[2][1]); aboveThresh = (mhz >= 861.0f);  }
   else if (mhz >= 900.0f && mhz <= 928.0f)   { band = 3; fsctrl0 = rl_map_u8(f, 900, 928, FSCTRL0_TRIM[3][0], FSCTRL0_TRIM[3][1]); aboveThresh = true; }
   else return;   // out of range, nothing to apply

   // FSCTRL0: manual frequency-offset trim. Auto-calibration never touches this
   // register, so without this write it silently stays at the chip's reset
   // value (0) under RadioLib - a fixed, uncorrected offset versus the old
   // ELECHOUSE build, on the order of tens of kHz depending on band/frequency.
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_FSCTRL0, fsctrl0);

   // documented CC1101 VCO/charge-pump calibration workaround for the upper
   // portion of each sub-band
   if (aboveThresh)
   {
      s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_TEST0, 0x09);
      uint8_t cur = (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_FSCAL2);
      if (cur < 32) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_FSCAL2, (uint8_t)(cur + 32));
   }
   else if (band == 0 || band == 1)   // 315/433 bands have a below-threshold branch too
   {
      s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_TEST0, 0x0B);
   }
}
//------------------------------------------------------------------------------------------------------------------
void rl_set_freq_mhz(float mhz)
{
   s_freqMhz = mhz;
   if (!s_radio) return;
   s_radio->setFrequency(mhz);
   rl_apply_freq_calibration(mhz);
}
void rl_set_bitrate_kbps(float k)   { if (s_radio) s_radio->setBitRate(k); }
void rl_set_dev_khz(float k)        { if (s_radio) s_radio->setFrequencyDeviation(k); }
void rl_set_rxbw_khz(float k)       { if (s_radio) s_radio->setRxBandwidth(k); }
void rl_set_power_dbm(int d)        { if (s_radio) s_radio->setOutputPower((int8_t)d); }
void rl_set_syncword(uint8_t h, uint8_t l) { if (s_radio) s_radio->setSyncWord(h, l, 0); }
void rl_set_ook(bool en)            { if (s_radio) s_radio->setOOK(en); }
//------------------------------------------------------------------------------------------------------------------
void rl_set_modformat(int modIndex)
{
   if (!s_radio) return;
   uint8_t v = RADIOLIB_CC1101_MOD_FORMAT_2_FSK;
   switch (modIndex)
   {
      case 0: v = RADIOLIB_CC1101_MOD_FORMAT_2_FSK; break;
      case 1: v = RADIOLIB_CC1101_MOD_FORMAT_GFSK;  break;
      case 3: v = RADIOLIB_CC1101_MOD_FORMAT_4_FSK; break;
      case 4: v = RADIOLIB_CC1101_MOD_FORMAT_MFSK;  break;  // CC1101 "MSK"
      default: v = RADIOLIB_CC1101_MOD_FORMAT_2_FSK; break;
   }
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, v, 6, 4);
}
//------------------------------------------------------------------------------------------------------------------
void rl_set_channel(int ch)         { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_CHANNR, (uint8_t)ch); }
void rl_set_whitening(bool en)      { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, en ? RADIOLIB_CC1101_WHITE_DATA_ON : RADIOLIB_CC1101_WHITE_DATA_OFF, 6, 6); }
void rl_set_manchester(bool en)     { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, en ? RADIOLIB_CC1101_MANCHESTER_EN_ON : RADIOLIB_CC1101_MANCHESTER_EN_OFF, 3, 3); }
void rl_set_fec(bool en)            { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG1, en ? RADIOLIB_CC1101_FEC_ON : RADIOLIB_CC1101_FEC_OFF, 7, 7); }
void rl_set_sync_mode(int m)        { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, (uint8_t)(m & 0x07), 2, 0); }
void rl_set_pktformat(int f)        { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, (uint8_t)((f & 0x03) << 4), 5, 4); }
void rl_set_pktlenmode(int m)       { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, (uint8_t)(m & 0x03), 1, 0); }
void rl_set_pktlen(int len)         { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTLEN, (uint8_t)len); }
void rl_set_crc(bool en)            { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, en ? RADIOLIB_CC1101_CRC_ON : RADIOLIB_CC1101_CRC_OFF, 2, 2); }
void rl_set_crc_autoflush(bool en)  { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL1, en ? RADIOLIB_CC1101_CRC_AUTOFLUSH_ON : RADIOLIB_CC1101_CRC_AUTOFLUSH_OFF, 3, 3); }
void rl_set_dcfilt_off(bool off)    { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, off ? RADIOLIB_CC1101_DEM_DCFILT_OFF : RADIOLIB_CC1101_DEM_DCFILT_ON, 7, 7); }
void rl_set_num_preamble(int idx)   { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG1, (uint8_t)((idx & 0x07) << 4), 6, 4); }
void rl_set_pqt(int pqt)            { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL1, (uint8_t)((pqt & 0x07) << 5), 7, 5); }
void rl_set_append_status(bool en)  { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL1, en ? RADIOLIB_CC1101_APPEND_STATUS_ON : RADIOLIB_CC1101_APPEND_STATUS_OFF, 2, 2); }
void rl_set_addr(int addr)          { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_ADDR, (uint8_t)addr); }
void rl_set_addr_chk(int chk)       { if (s_radio) s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL1, (uint8_t)(chk & 0x03), 1, 0); }
//------------------------------------------------------------------------------------------------------------------
void rl_set_chanspc_hz(long hz)
{
   if (!s_radio) return;
   // CHANSPC: df = (Fxosc / 2^18) * (256 + M) * 2^E,  Fxosc = 26 MHz
   const double FXOSC = 26000000.0;
   for (int e = 0; e <= 3; e++)
   {
      double m = ((double)hz * 262144.0) / (FXOSC * (double)(1UL << e)) - 256.0;
      long mi = (long)(m + 0.5);
      if (mi >= 0 && mi <= 255)
      {
         s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG1, (uint8_t)e, 1, 0);
         s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG0, (uint8_t)mi, 7, 0);
         return;
      }
   }
}
//------------------------------------------------------------------------------------------------------------------
float rl_get_rssi(void) { return s_radio ? s_radio->getRSSI() : -128.0f; }
int   rl_get_lqi(void)  { return s_radio ? (int)s_radio->getLQI() : 0; }
//------------------------------------------------------------------------------------------------------------------
void rl_set_rx_packet(void)
{
   if (!s_radio) return;
   s_radio->standby();
   s_radio->setPacketReceivedAction(rl_isr);
   s_rxFlag = false;
   s_radio->startReceive();
}
//------------------------------------------------------------------------------------------------------------------
void rl_set_rx_direct(void)
{
   if (!s_radio) return;
   s_radio->clearPacketReceivedAction();
   s_radio->receiveDirectAsync();    // async, raw demod data on GDO0
}
//------------------------------------------------------------------------------------------------------------------
void rl_set_tx_direct(void)
{
   if (!s_radio) return;
   s_radio->clearPacketReceivedAction();
   s_radio->transmitDirectAsync();   // async, GDO0 driven by the MCU
}
//------------------------------------------------------------------------------------------------------------------
static uint8_t rl_marcstate(void)
{
   return (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_MARCSTATE, 4, 0);
}
//------------------------------------------------------------------------------------------------------------------
bool rl_wait_tx_ready(uint32_t timeoutUs)
{
   if (!s_radio) return false;
   uint32_t start = micros();
   while (rl_marcstate() != RADIOLIB_CC1101_MARC_STATE_TX)
   {
      if ((uint32_t)(micros() - start) > timeoutUs) return false;
   }
   return true;
}
//------------------------------------------------------------------------------------------------------------------
void rl_idle(void) { if (s_radio) s_radio->standby(); }
//------------------------------------------------------------------------------------------------------------------
bool rl_rx_available(void) { return s_rxFlag; }
//------------------------------------------------------------------------------------------------------------------
int rl_receive(uint8_t* buf, int maxlen, int* crcOk)
{
   if (crcOk) *crcOk = 0;
   if (!s_radio || !s_rxFlag) return 0;
   s_rxFlag = false;

   size_t n = s_radio->getPacketLength(true);
   if ((int)n > maxlen) n = (size_t)maxlen;

   int st = s_radio->readData(buf, n);
   if (crcOk) *crcOk = (st != RADIOLIB_ERR_CRC_MISMATCH) ? 1 : 0;

   s_radio->startReceive();          // re-arm
   return (int)n;
}
//------------------------------------------------------------------------------------------------------------------
int rl_transmit(const uint8_t* buf, int len)
{
   if (!s_radio) return -1;
   return s_radio->transmit(buf, (size_t)len);
}
//------------------------------------------------------------------------------------------------------------------
// Stream an arbitrary-length raw bitstream through the TX FIFO, with every bit
// clocked out by the chip's own bit-rate generator (hardware timing, no MCU
// busy-wait jitter) - unlike the GDO0 direct-mode path in radio_pulses.ino.
//
// RadioLib's own transmit()/startTransmit() refuse anything over 255 bytes
// (RADIOLIB_CC1101_MAX_PACKET_LENGTH), so this bypasses them and refills the
// FIFO continuously from 'data' instead, using the same low-level SPI
// primitives as rl_arm_ook_tx()/rl_apply_freq_calibration() above.
//
// Framing is turned off entirely so the bytes given go out completely
// unframed - bit for bit what the caller put in, nothing prepended and
// nothing appended:
//   - PKTCTRL0.LENGTH_CONFIG = INFINITE (no length byte, no 255B cap)
//   - PKTCTRL0.PKT_FORMAT    = NORMAL (FIFO-fed, as opposed to GDO0 async)
//   - MDMCFG2.SYNC_MODE      = NONE (CC1101 normally auto-inserts a hardware
//     preamble + sync word before FIFO data in this format; NONE is the one
//     setting documented to suppress both, so our bytes are not preceded by
//     anything we did not put there ourselves)
// The caller's own packet-mode config (sync word, CRC, length mode, etc.) is
// saved and restored exactly, so this is a self-contained burst, not a
// lasting mode switch - callers using normal packet TX elsewhere are
// unaffected. FREND0/PATABLE OOK arming is unrelated to framing and must
// still be done by the caller (same armOokTx() as the GDO0 path) before TX.
//
// CC1101::SPIsendCommand() is protected (not reachable from this free
// function); it only forwards to Module::SPItransferStream(), which IS
// public, so strobe commands here go through that directly - same call RadioLib
// itself makes internally (verified in CC1101::SPIsendCommand's own body).
static void rl_strobe(uint8_t cmd)
{
   s_radio->getMod()->SPItransferStream(&cmd, 1, true, NULL, NULL, 0, false);
}
//------------------------------------------------------------------------------------------------------------------

// A single read of TXBYTES can be unreliable on real CC1101 silicon (a known
// errata - RadioLib's own startTransmit() works around it the same way, with
// the same comment): read it twice and retry until two consecutive reads
// agree, instead of trusting one read. Skipping this can under- or over-fill
// the TX FIFO on a glitched read, which stalls or truncates the transmission
// - a very plausible reason for "nothing coherent goes out at all".
static uint8_t rl_read_txbytes(void)
{
   uint8_t a, b;
   do
   {
      a = (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_TXBYTES, 6, 0);
      b = (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_TXBYTES, 6, 0);
   } while (a != b);
   return a;
}
//------------------------------------------------------------------------------------------------------------------
// TXBYTES is read through rl_read_txbytes() (double-read-until-stable, per a
// documented CC1101 errata - RadioLib's own startTransmit() does the same)
// rather than a single SPIgetRegValue() call, since a glitched single read
// can under/overfill the FIFO and silently stall the transmission.
// Returns 0 on success, -1 on bad args, -2 on a FIFO underrun/stall timeout,
// -4 if the chip never reached IDLE before transmit.
int rl_transmit_raw_bits(const uint8_t* data, size_t nbytes)
{
   if (!s_radio || data == NULL || nbytes == 0) return -1;

   if (s_radio->standby() != RADIOLIB_ERR_NONE) return -4;   // chip never reached IDLE

   uint8_t savedPktctrl0 = (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0);
   uint8_t savedMdmcfg2  = (uint8_t)s_radio->SPIgetRegValue(RADIOLIB_CC1101_REG_MDMCFG2);

   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, RADIOLIB_CC1101_LENGTH_CONFIG_INFINITE, 1, 0);
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, RADIOLIB_CC1101_PKT_FORMAT_NORMAL, 5, 4);
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, RADIOLIB_CC1101_SYNC_MODE_NONE, 2, 0);

   rl_strobe(RADIOLIB_CC1101_CMD_FLUSH_TX);

   // IMPORTANT: fill the FIFO through rl_burst_write() (near the top of this
   // file), never with a plain s_radio->getMod()->SPIwriteRegisterBurst() call.
   // CC1101's own SPIwriteRegisterBurst() ORs in RADIOLIB_CC1101_CMD_BURST
   // before forwarding to Module, but that method is protected on CC1101; a
   // bare Module-level call does not set that bit at all, and a multi-byte
   // FIFO write without it is not a valid burst write on real CC1101 silicon.
   size_t sent    = (nbytes < RADIOLIB_CC1101_FIFO_SIZE) ? nbytes : RADIOLIB_CC1101_FIFO_SIZE;
   rl_burst_write(RADIOLIB_CC1101_REG_FIFO, data, sent);

   rl_strobe(RADIOLIB_CC1101_CMD_TX);

   int ret = 0;
   uint32_t start = millis();
   while (sent < nbytes)
   {
      uint8_t txbytes = rl_read_txbytes();
      size_t  freeSp  = (txbytes < RADIOLIB_CC1101_FIFO_SIZE) ? (RADIOLIB_CC1101_FIFO_SIZE - txbytes) : 0;
      size_t  chunk   = nbytes - sent;
      if (chunk > freeSp) chunk = freeSp;
      if (chunk > 0)
      {
         rl_burst_write(RADIOLIB_CC1101_REG_FIFO, &data[sent], chunk);
         sent += chunk;
      }
      if (millis() - start > 2000) { ret = -2; break; }   // underrun/stall guard
   }

   if (ret == 0)
   {
      // Infinite-length mode never ends the packet by itself: wait for the
      // FIFO to actually drain to the modulator before leaving TX.
      start = millis();
      while (rl_read_txbytes() != 0)
      {
         if (millis() - start > 2000) { ret = -2; break; }
      }
   }

   if (ret == 0)
   {
      // TXBYTES == 0 only means the LAST byte has been moved from the FIFO
      // into the modulator's shift register - its 8 bits are still being
      // clocked out. Strobing IDLE at this point truncates the tail of the
      // frame (up to 8 bit periods = 8 * unitUs, i.e. the last pulse(s) come
      // out shorter or are lost). In infinite-length mode the chip enters
      // TXFIFO_UNDERFLOW exactly when the shift register needs a new byte and
      // the FIFO is empty, i.e. right after the final bit has left - so that
      // state is the precise "everything is on air" marker.
      start = millis();
      while (rl_marcstate() != RADIOLIB_CC1101_MARC_STATE_TXFIFO_UNDERFLOW)
      {
         if (millis() - start > 2000) { ret = -2; break; }
      }
   }

   // SFTX is the documented way out of TXFIFO_UNDERFLOW. It is only valid in
   // IDLE or TXFIFO_UNDERFLOW, so it is issued before SIDLE only when the
   // underflow state was actually reached; after standby() (IDLE) it is
   // always valid and leaves an empty FIFO for the next transmit.
   if (ret == 0) rl_strobe(RADIOLIB_CC1101_CMD_FLUSH_TX);
   s_radio->standby();
   rl_strobe(RADIOLIB_CC1101_CMD_FLUSH_TX);
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_PKTCTRL0, savedPktctrl0);
   s_radio->SPIsetRegValue(RADIOLIB_CC1101_REG_MDMCFG2, savedMdmcfg2);

   return ret;
}
//------------------------------------------------------------------------------------------------------------------
