//------------------------------------------------------------------------------------------------------------------
#ifndef __RADIO_PULSES_H__
#define __RADIO_PULSES_H__
//------------------------------------------------------------------------------------------------------------------
void radio_pulses_init(void);
void radio_pulses_flush(void);
int  radio_pulses_get_count(void);
int  radio_pulses_max_count(void);
uint16_t*  radio_pulses_get_pulses(void);
int radio_pulses_grab(Stream& input, int startOffset, int minPulseUs, int maxSpaceUs);
// like grab, but assumes the radio is ALREADY in async direct RX and gdo0 is an
// input: pure GPIO bit-bang, no radio/RadioLib reconfiguration. Runs on core1
// (see loop1) so its busy-wait never blocks core0's USB/CLI. For the monitor.
int radio_pulses_capture(int startOffset, int minPulseUs, int maxSpaceUs);
int radio_pulses_send(int startOffset, int pulses);
// Hardware-clocked alternative to radio_pulses_send() via the TX FIFO instead
// of GDO0 bit-banging - see the .ino for details and the quantization trade-off.
// DRATE is derived from unitUs and applied/restored automatically; do not set
// it separately beforehand. If SDR measurement shows your board's actual
// on-air bit period differs from unitUs by a consistent ratio, correct it
// with 'radio r0 set fifocal <permille>' (RadioDev::fifoDrateCalPermille) -
// e.g. 500 if the real period is running at ~2x the requested rate. Default
// 1000 (no correction); never assumed, only set after measuring your own
// board.
long radio_pulses_send_fifo(int startOffset, int pulses, int unitUs);
int radio_pulses_set(int startOffset, uint16_t pulse);
int radio_pulses_add(uint16_t pulse);
//------------------------------------------------------------------------------------------------------------------
#endif//__RADIO_PULSES_H__
//------------------------------------------------------------------------------------------------------------------
