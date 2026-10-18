//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-18
//------------------------------------------------------------------------------------------------------------------
// RadioDev wrapper - RadioLib backend.
//
// Same public interface as the ELECHOUSE-based version; all hardware access
// goes through the flat shim in rl_cc1101.cpp (the only unit that includes
// RadioLib, to avoid the RadioDev class-name clash). Shadow variables are kept so
// the getters return exactly what was last set, as before.
//------------------------------------------------------------------------------------------------------------------
#include "cc1101.h"
#include "settings.h"
#include "rl_cc1101.h"
//------------------------------------------------------------------------------------------------------------------
RadioDev::RadioDev(void) {}
//------------------------------------------------------------------------------------------------------------------
void RadioDev::init(void) { default_init(); }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::default_init(void)
{
   // bring up RadioLib on the project's SPI0 pins (ss/gdo0/gdo2 from settings.h)
   rl_begin(ss, gdo0, gdo2, 433.92f, 9.6f, 47.6f, 812.5f, 10, 16);

   // establish the same baseline the ELECHOUSE build used (shadow + hardware)
   setCCMode(1);
   setModulation(2);          // ASK/OOK
   setFreq(433920);
   setDeviation(47600);
   setChan(0);
   setChanSpaceing(199950);
   setRxBw(812000);   // RadioLib caps rxBw at 812.0 kHz (812.5 is rejected)
   setDataRate(9600);
   setFifoDrateCal(1000);   // neutral (1.0x) until explicitly measured/set for this board
   setPA(10);
   setSyncMode(2);
   setSyncWord(145, 211);     // swordHi=211, swordLo=145 (as before)
   setAddressCheck(0);
   setAddress(0);
   setDataWhitening(0);
   setPacketFormat(0);
   setPacketLengthMode(1);
   setPacketLength(0);
   setCrcEnable(0);
   setCrcAf(0);
   setDCFilterOff(0);
   setManchester(0);
   setFEC(0);
   setPreambleLen(0);
   setPQT(0);
   setAppendStatus(0);
   rxen = 0;
   txen = 0;
   lastCrc = 0;
}
//------------------------------------------------------------------------------------------------------------------
bool RadioDev::check(void) { return rl_present(); }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setFreq(int khz)
{
   if (khz >= 300000 && khz <= 928000)
   {
      lastFreq = khz;
      rl_set_freq_mhz(khz / 1000.0f);
   }
}
int RadioDev::getFreq(void) { return lastFreq; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setModulation(int mod)
{
   this->mod = mod;
   if (mod == 2) rl_set_ook(true);
   else { rl_set_ook(false); rl_set_modformat(mod); }
}
int RadioDev::getModulation(void) { return mod; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setDeviation(int dev)       { this->dev = dev; rl_set_dev_khz(dev / 1000.0f); }
int  RadioDev::getDeviation(void)          { return dev; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setChanSpaceing(int sp)     { this->chanSp = sp; rl_set_chanspc_hz(sp); }
int  RadioDev::getChanSpaceing(void)       { return chanSp; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setChan(int chan)           { this->chan = chan; rl_set_channel(chan); }
int  RadioDev::getChan(void)               { return chan; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setRxBw(int val)            { this->rxbw = val; rl_set_rxbw_khz(val / 1000.0f); }
int  RadioDev::getRxBw(void)               { return rxbw; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setDataRate(int val)        { this->drate = val; rl_set_bitrate_kbps(val / 1000.0f); }
int  RadioDev::getDataRate(void)           { return drate; }
void RadioDev::setFifoDrateCal(int permille) { this->fifoDrateCalPermille = permille; }
int  RadioDev::getFifoDrateCal(void)         { return fifoDrateCalPermille; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPA(int val)              { this->pa = val; rl_set_power_dbm(val); }
int  RadioDev::getPA(void)                 { return pa; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setSyncMode(int val)        { this->smode = val; rl_set_sync_mode(val); }
int  RadioDev::getSyncMode(void)           { return smode; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setSyncWord(int low, int high)
{
   this->swordHi = high;
   this->swordLo = low;
   rl_set_syncword((uint8_t)high, (uint8_t)low);
}
int  RadioDev::getSyncWordHigh(void)       { return swordHi; }
int  RadioDev::getSyncWordLow(void)        { return swordLo; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setAddressCheck(int val)    { this->achk = val; rl_set_addr_chk(val); }
int  RadioDev::getAddressCheck(void)       { return achk; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setAddress(int val)         { this->addr = val; rl_set_addr(val); }
int  RadioDev::getAddress(void)            { return addr; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setDataWhitening(int dw)    { this->dataw = dw; rl_set_whitening(dw != 0); }
int  RadioDev::getDataWhitening(void)      { return dataw; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPacketFormat(int pktfmt) { this->pktfmt = pktfmt; rl_set_pktformat(pktfmt); }
int  RadioDev::getPacketFormat(void)       { return pktfmt; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPacketLengthMode(int m)  { this->pktlmode = m; rl_set_pktlenmode(m); }
int  RadioDev::getPacketLengthMode(void)   { return pktlmode; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPacketLength(int len)    { this->pktlen = len; rl_set_pktlen(len); }
int  RadioDev::getPacketLength(void)       { return pktlen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setCrcEnable(int crcen)     { this->crcen = crcen; rl_set_crc(crcen != 0); }
int  RadioDev::getCrcEnable(void)          { return crcen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setCrcAf(int af)            { this->crcaf = af; rl_set_crc_autoflush(af != 0); }
int  RadioDev::getCrcAf(void)              { return crcaf; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setDCFilterOff(int off)     { this->dcfilteroff = off; rl_set_dcfilt_off(off != 0); }
int  RadioDev::getDCFilterOff(void)        { return dcfilteroff; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setManchester(int en)       { this->manchen = en; rl_set_manchester(en != 0); }
int  RadioDev::getManchester(void)         { return manchen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setFEC(int en)              { this->fecen = en; rl_set_fec(en != 0); }
int  RadioDev::getFEC(void)                { return fecen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPreambleLen(int len)     { this->prelen = len; rl_set_num_preamble(len); }
int  RadioDev::getPreambleLen(void)        { return prelen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setPQT(int pqt)             { this->pqt = pqt; rl_set_pqt(pqt); }
int  RadioDev::getPQT(void)                { return pqt; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setAppendStatus(int en)     { this->asen = en; rl_set_append_status(en != 0); }
int  RadioDev::getAppendStatus(void)       { return asen; }
//------------------------------------------------------------------------------------------------------------------
int  RadioDev::getRSSI(void)               { return (int)rl_get_rssi(); }
int  RadioDev::getLQI(void)                { return rl_get_lqi(); }
//------------------------------------------------------------------------------------------------------------------
String RadioDev::modulation(int mod)
{
   if (mod == 0) return "2fsk";
   else if (mod == 1) return "gfsk";
   else if (mod == 2) return "ook";
   else if (mod == 3) return "4fsk";
   else if (mod == 4) return "msk";
   else return "unknown";
}
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setRX(void)
{
   this->rxen = true;
   this->txen = false;
   if (ccmode == 0 || pktfmt == 3) rl_set_rx_direct();   // OOK / async raw on GDO0
   else                            rl_set_rx_packet();   // normal packet RX
}
int RadioDev::getRX(void) { return rxen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setTX(void)
{
   this->txen = true;
   this->rxen = false;
   if (ccmode == 0 || pktfmt == 3) rl_set_tx_direct();   // async raw, MCU drives GDO0
   // packet-mode TX is performed by sendData()
}
int RadioDev::getTX(void) { return txen; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setCCMode(int mode) { this->ccmode = mode; }
int  RadioDev::getCCMode(void)     { return ccmode; }
//------------------------------------------------------------------------------------------------------------------
void RadioDev::armOokTx(void)
{
   // Only meaningful for OOK: forces FREND0=0x11 + PATABLE={0x00, power(pa)} so
   // the GDO0 bit-bang produces full 100% ASK (carrier off in the spaces). The
   // ELECHOUSE build did this implicitly via setCCMode(0)->setModulation; the
   // RadioLib path only set PATABLE once at init, so it is re-armed here.
   if (mod == 2) rl_arm_ook_tx(pa);
}
//------------------------------------------------------------------------------------------------------------------
void RadioDev::sendData(uint8_t* data, int len)
{
   if (len > 64) len = 64;
   rl_transmit(data, len);
}
//------------------------------------------------------------------------------------------------------------------
int RadioDev::sendRawBits(uint8_t* data, int nbytes)
{
   if (nbytes <= 0) return -1;
   return rl_transmit_raw_bits(data, (size_t)nbytes);
}
//------------------------------------------------------------------------------------------------------------------
void RadioDev::setIdle(void)
{
   this->rxen = false;
   this->txen = false;
   rl_idle();
}
//------------------------------------------------------------------------------------------------------------------
bool RadioDev::rxAvailable(void) { return rl_rx_available(); }
//------------------------------------------------------------------------------------------------------------------
int RadioDev::receive(uint8_t* buf)
{
   int crc = 0;
   int n = rl_receive(buf, 64, &crc);
   lastCrc = crc;
   return n;
}
//------------------------------------------------------------------------------------------------------------------
bool RadioDev::crcOk(void) { return lastCrc != 0; }
//------------------------------------------------------------------------------------------------------------------
int RadioDev::version(void) { return rl_version(); }
int RadioDev::partnum(void) { return rl_partnum(); }
//------------------------------------------------------------------------------------------------------------------
