//------------------------------------------------------------------------------------------------------------------
#ifndef __RL_CC1101_H__
#define __RL_CC1101_H__
//------------------------------------------------------------------------------------------------------------------
// Flat C-style shim over RadioLib's CC1101 driver.
//
// RadioLib defines a class named CC1101 in the global namespace, which clashes
// with this firmware's own CC1101 wrapper class. To avoid the clash, RadioLib
// is included ONLY in rl_cc1101.cpp (its own translation unit); the rest of the
// firmware talks to it exclusively through these plain functions, which expose
// no RadioLib types. The CC1101 wrapper (cc1101.ino) calls these.
//------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
//------------------------------------------------------------------------------------------------------------------
// lifecycle
bool rl_begin(int csPin, int gdo0Pin, int gdo2Pin,
              float freqMhz, float brKbps, float devKhz, float rxbwKhz,
              int pwrDbm, int preambleBits);
void rl_reset(void);
bool rl_present(void);
int  rl_begin_status(void);   // RadioLib begin() return code (diagnostic)
int  rl_version(void);
int  rl_partnum(void);
int  rl_get_reg(int addr);            // read one CC1101 config register (diagnostic)
void rl_get_patable(uint8_t* out2);   // read PATABLE[0] and PATABLE[1]
int  rl_set_ook_patable(int powerByte); // IDLE->write FREND0+PATABLE{0,byte}->read back [1] (diagnostic)
//------------------------------------------------------------------------------------------------------------------
// parameters (high-level, handled by RadioLib)
void rl_set_freq_mhz(float mhz);
void rl_set_bitrate_kbps(float kbps);
void rl_set_dev_khz(float khz);
void rl_set_rxbw_khz(float khz);
void rl_set_power_dbm(int dbm);
void rl_set_syncword(uint8_t hi, uint8_t lo);
void rl_set_ook(bool en);
//------------------------------------------------------------------------------------------------------------------
// parameters (raw CC1101 registers, to preserve the firmware's full control)
void rl_set_modformat(int modIndex);   // 0=2FSK 1=GFSK 3=4FSK 4=MSK (OOK uses rl_set_ook)
void rl_set_channel(int ch);
void rl_set_chanspc_hz(long hz);
void rl_set_whitening(bool en);
void rl_set_manchester(bool en);
void rl_set_fec(bool en);
void rl_set_sync_mode(int mode0_7);
void rl_set_pktformat(int fmt0_3);
void rl_set_pktlenmode(int mode0_3);
void rl_set_pktlen(int len);
void rl_set_crc(bool en);
void rl_set_crc_autoflush(bool en);
void rl_set_dcfilt_off(bool off);
void rl_set_num_preamble(int idx0_7);
void rl_set_pqt(int pqt0_7);
void rl_set_append_status(bool en);
void rl_set_addr(int addr);
void rl_set_addr_chk(int chk0_3);
//------------------------------------------------------------------------------------------------------------------
// status
float rl_get_rssi(void);
int   rl_get_lqi(void);
//------------------------------------------------------------------------------------------------------------------
// RX/TX state
void rl_set_rx_packet(void);   // normal packet RX (arms the "packet received" interrupt)
void rl_set_rx_direct(void);   // async direct RX, raw data on GDO0 (for OOK pulse capture)
void rl_set_tx_direct(void);   // async direct TX, data driven on GDO0
void rl_idle(void);            // standby
//------------------------------------------------------------------------------------------------------------------
// data
bool rl_rx_available(void);                              // packet waiting?
int  rl_receive(uint8_t* buf, int maxlen, int* crcOk);   // read packet, returns length
int  rl_transmit(const uint8_t* buf, int len);           // blocking packet transmit, 0 = ok
//------------------------------------------------------------------------------------------------------------------
#endif// __RL_CC1101_H__
//------------------------------------------------------------------------------------------------------------------
