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
   s_radio->getMod()->SPIreadRegisterBurst(RADIOLIB_CC1101_REG_PATABLE, 8, t);
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
   s_radio->getMod()->SPIwriteRegisterBurst(RADIOLIB_CC1101_REG_PATABLE, pa, 2);
   uint8_t t[8] = {0};
   s_radio->getMod()->SPIreadRegisterBurst(RADIOLIB_CC1101_REG_PATABLE, 8, t);
   return t[1];
}
//------------------------------------------------------------------------------------------------------------------
void rl_set_freq_mhz(float mhz)     { if (s_radio) s_radio->setFrequency(mhz); }
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
