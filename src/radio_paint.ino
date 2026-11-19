//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-11-19
//------------------------------------------------------------------------------------------------------------------
#include "radio_paint.h"
#include "settings.h"     // gdo0 pin
#include "cc1101.h"
//------------------------------------------------------------------------------------------------------------------
// How long the carrier dwells at each lit column before hopping to the next
// (multi-column rows only). This must be >> the synth settle (~75us) so the
// clean-carrier portion dominates over the brief retune transient; too short
// and the on/off keying of hopping sprays OOK sidebands and smears the column.
#define PAINT_HOLD_US 1200
//------------------------------------------------------------------------------------------------------------------
static paint_bin_t g_bins[PAINT_MAX_COLS];
static int  g_ncols  = 0;
static long g_fstart = 0;
static long g_fstop  = 0;
//------------------------------------------------------------------------------------------------------------------
int  radio_paint_ncols(void)  { return g_ncols; }
long radio_paint_fstart(void) { return g_fstart; }
long radio_paint_fstop(void)  { return g_fstop; }

const uint8_t* radio_paint_bin(int col)
{
   if (col < 0 || col >= g_ncols) return nullptr;
   return g_bins[col].reg;
}
//------------------------------------------------------------------------------------------------------------------
int radio_paint_setup(RadioDev* r, long fstartHz, long fstopHz, int ncols)
{
   if (!r || ncols < 1 || ncols > PAINT_MAX_COLS) return -1;
   if (fstopHz <= fstartHz)                        return -2;

   g_fstart = fstartHz;
   g_fstop  = fstopHz;
   g_ncols  = ncols;

   r->setModulation(2);   // OOK: a clean on/off carrier is what paints crisp columns

   for (int c = 0; c < ncols; c++)
   {
      double hz = (ncols == 1)
                  ? (double)fstartHz
                  : (double)fstartHz + (double)(fstopHz - fstartHz) * (double)c / (double)(ncols - 1);
      r->paintCal((float)(hz / 1000000.0), g_bins[c].reg);
   }
   r->setIdle();
   return ncols;
}
//------------------------------------------------------------------------------------------------------------------
void radio_paint_row(RadioDev* r, uint32_t dwellUs, const uint8_t* mask, int maskbytes)
{
   if (!r || g_ncols < 1) return;

   // Collect the lit column indices for this row.
   uint8_t lit[PAINT_MAX_COLS];
   int k = 0;
   for (int c = 0; c < g_ncols; c++)
   {
      int b = c >> 3;
      if (b < maskbytes && (mask[b] & (0x80 >> (c & 7)))) lit[k++] = (uint8_t)c;
   }

   // A blank row is just silence for the dwell (keeps vertical timing).
   if (k == 0)
   {
      uint32_t t0 = micros();
      while ((uint32_t)(micros() - t0) < dwellUs) { }
      return;
   }

   r->armOokTx();              // OOK PA table: mark = power, space = carrier off
   r->paintBegin();            // FS_AUTOCAL=never + async direct TX
   pinMode(gdo0, OUTPUT);
   digitalWrite(gdo0, HIGH);   // hold the OOK mark: carrier stays ON while we hop

   uint32_t t0 = micros();
   if (k == 1)
   {
      // Single column: tune once and HOLD the carrier for the whole dwell.
      // No re-keying -> a clean, narrow CW line (no OOK sidebands).
      r->paintHop(g_bins[lit[0]].reg);
      while ((uint32_t)(micros() - t0) < dwellUs) { }
   }
   else
   {
      // Multiple columns: interleave them, holding each for PAINT_HOLD_US so the
      // SDR's FFT frame sees them all lit, while keeping transitions rare enough
      // that the keying sidebands stay small.
      int i = 0;
      while ((uint32_t)(micros() - t0) < dwellUs)
      {
         r->paintHop(g_bins[lit[i]].reg);   // IDLE -> load FREQ+FSCAL -> STX (no recal)
         uint32_t hs = micros();
         while ((uint32_t)(micros() - hs) < PAINT_HOLD_US &&
                (uint32_t)(micros() - t0) < dwellUs) { }
         if (++i >= k) i = 0;
      }
   }

   digitalWrite(gdo0, LOW);    // carrier off
   r->paintEnd();              // idle + restore normal auto-calibration
}
//------------------------------------------------------------------------------------------------------------------
void radio_paint_stop(RadioDev* r)
{
   if (!r) return;
   digitalWrite(gdo0, LOW);
   r->paintEnd();
   r->setIdle();
}
//------------------------------------------------------------------------------------------------------------------
