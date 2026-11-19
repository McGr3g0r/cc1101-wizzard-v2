//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-11-19
//------------------------------------------------------------------------------------------------------------------
#ifndef __RADIO_PAINT_H__
#define __RADIO_PAINT_H__
//------------------------------------------------------------------------------------------------------------------
// Single-carrier spectrum "painter". The CC1101 TXes one frequency at a time,
// so an image is drawn on an SDR waterfall (X=frequency, Y=time) by hopping the
// carrier. To light several columns within one FFT frame, each column frequency
// is calibrated once up front (FREQ+FSCAL saved, 6 bytes/column) and then
// re-selected without a per-hop recalibration. Memory cost is tiny: the whole
// table is PAINT_MAX_COLS*6 bytes, and the image is streamed one row at a time
// from the host (no full bitmap is ever stored on-chip).
//------------------------------------------------------------------------------------------------------------------
#include <stdint.h>
#include "cc1101.h"
//------------------------------------------------------------------------------------------------------------------
#define PAINT_MAX_COLS 128         // 128 * 6 = 768 bytes of RAM for the table
//------------------------------------------------------------------------------------------------------------------
typedef struct {
   uint8_t reg[6];   // FREQ2, FREQ1, FREQ0, FSCAL3, FSCAL2, FSCAL1
} paint_bin_t;
//------------------------------------------------------------------------------------------------------------------
// Calibrate 'ncols' evenly-spaced frequencies in [fstartHz, fstopHz] and store
// their register sets. Forces OOK modulation. Returns ncols, or <0 on error.
int  radio_paint_setup(RadioDev* r, long fstartHz, long fstopHz, int ncols);
// Paint one time-row: for dwellUs, fast-hop the carrier among the lit columns.
// 'mask' is a bit array, column c = bit (0x80 >> (c & 7)) of mask[c >> 3]
// (col 0 = MSB of byte 0 = lowest frequency). A blank row = dwellUs of silence.
void radio_paint_row(RadioDev* r, uint32_t dwellUs, const uint8_t* mask, int maskbytes);
// Stop any paint carrier and restore normal auto-calibration / idle.
void radio_paint_stop(RadioDev* r);
// Introspection (for 'paint show' / 'paint count').
int  radio_paint_ncols(void);
long radio_paint_fstart(void);
long radio_paint_fstop(void);
const uint8_t* radio_paint_bin(int col);   // 6-byte reg set, or nullptr if out of range
//------------------------------------------------------------------------------------------------------------------
#endif// __RADIO_PAINT_H__
//------------------------------------------------------------------------------------------------------------------
