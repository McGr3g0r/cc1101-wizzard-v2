//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-20
//------------------------------------------------------------------------------------------------------------------
#include "radio_pulses.h"
#include "radio_buffer.h"
#include "settings.h"
#include "cc1101.h"
#include <hardware/gpio.h>
#include <hardware/sync.h>
#include <hardware/timer.h>
//------------------------------------------------------------------------------------------------------------------
RadioDev* getRadio(void);
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_cnt;
uint16_t* radio_pulses_buffer;
int radio_pulses_buffer_len;
//------------------------------------------------------------------------------------------------------------------
void radio_pulses_init(void)
{
  radio_pulses_buffer = (uint16_t*)radio_buffer_get(0);
  radio_pulses_buffer_len = radio_buffer_size(0) / 2; /*buffer is uint8_t, pulses are uint16_t*/
  radio_pulses_flush();
}
//------------------------------------------------------------------------------------------------------------------
void radio_pulses_flush(void)
{
   radio_pulses_cnt = 0;
   // clear the whole pulse buffer: radio_pulses_buffer is a pointer, so
   // sizeof() on it would only cover the pointer itself (4 bytes)
   memset(radio_pulses_buffer, 0, (size_t)radio_pulses_buffer_len * sizeof(radio_pulses_buffer[0]));
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_get_count(void)
{
  return radio_pulses_cnt;
}
//------------------------------------------------------------------------------------------------------------------
uint16_t* radio_pulses_get_pulses(void)
{
   return &radio_pulses_buffer[0];
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_grab(Stream& input, int startOffset, int minPulseUs, int maxSpaceUs)
{
    (void)input;
    RadioDev* radio = getRadio();

    // put the radio into async direct RX (raw demod data on gdo0)
    radio->setCCMode(0);
    radio->setPacketFormat(3);
    radio->setRX();

    // one capture on the current config; time-capped so it can't freeze the CLI
    int n = radio_pulses_capture(startOffset, minPulseUs, maxSpaceUs);

    // restore normal packet format / RX
    radio->setCCMode(1);
    radio->setPacketFormat(0);
    radio->setRX();

    return n;
}
//------------------------------------------------------------------------------------------------------------------
// Pure GPIO pulse capture: NO radio reconfiguration, NO RadioLib calls. The
// caller must have already put the radio into async direct RX (data on gdo0).
// Proven fast logic: wait up to maxSpaceUs for the signal to start, then record
// edge intervals until a gap >= maxSpaceUs (or buffer full, or the absolute time
// cap). A key from the CLI also aborts. Bounded so it can never freeze the CLI.
int radio_pulses_capture(int startOffset, int minPulseUs, int maxSpaceUs)
{
    int idx = startOffset;
    uint64_t s2;
    int tdiff;

    pinMode(gdo0, INPUT);

    // time_us_64() (raw Pico SDK, reads the hardware timer directly) instead
    // of Arduino's micros(): avoids the Arduino-core wrapper overhead and any
    // cross-core contention with core0's own hardware-timer-based TX loop
    // (see radio_pulses_send()), for lower jitter in this precision-critical
    // capture loop - consistent across the whole TX/RX timing path.
    uint64_t grabStart = time_us_64();
    uint64_t s = time_us_64();
    // wait for the signal to appear (line goes HIGH), bounded by maxSpaceUs
    while (digitalRead(gdo0) == LOW && time_us_64() - s < (uint64_t)maxSpaceUs)
        if (STDIN.available()) { STDIN.read(); radio_pulses_cnt = startOffset; return 0; }

    if (time_us_64() - s < (uint64_t)maxSpaceUs)
    {
       idx = startOffset;
       uint8_t curr_val = 1;
       s = time_us_64();
       while ((idx < radio_pulses_buffer_len) && (time_us_64() - s < (uint64_t)maxSpaceUs) && (time_us_64() - grabStart < (uint64_t)RADIO_PULSES_MAX_GRAB_US))
       {
            // Precision-critical: exactly ONE time_us_64() call per poll, same
            // as the original ELECHOUSE-era capture had for micros(). The
            // 250ms absolute-grab-time cap is enforced once per EDGE by the
            // outer loop above, not here - adding a second timer call to this
            // innermost busy-wait biases every measured pulse width long by
            // its extra per-iteration overhead, which can push captured
            // timing outside a strict receiver's tolerance even though a
            // lenient receiver never notices.
            while ((digitalRead(gdo0) == curr_val) && (time_us_64() - s < (uint64_t)maxSpaceUs));
            s2 = time_us_64();
            tdiff = (int)(s2 - s);
            if (tdiff < maxSpaceUs)
            {
               if (tdiff >= minPulseUs)
                   radio_pulses_buffer[idx++] = tdiff;
               s = time_us_64();
               curr_val++;
               curr_val &= 0x01;
            }
       }
       radio_pulses_cnt = idx;
    }
    return idx - startOffset;
}
//------------------------------------------------------------------------------------------------------------------
// GDO0 direct-TX timing constants.
//
// In asynchronous serial mode the CC1101 modulator does NOT follow GDO0
// continuously: it samples the pin at 8x the programmed data rate (DRATE) and
// every edge is snapped to that grid. With a profile DRATE of e.g. 2400 Bd the
// grid is ~52us, at 9600 Bd ~13us - each pulse can come out up to one grid step
// shorter (or longer) than requested, no matter how precise the MCU loop is.
// During a GDO0 transmit DRATE is therefore raised to at least this value
// (grid = 1 / (8 * 250 kBd) = 0.5us) and restored afterwards, so the profile
// data rate stays what it was for RX / packet mode.
#define RADIO_PULSES_TX_DRATE            250000L
// Upper bound for waiting until the chip is in TX after STX (the FS_AUTOCAL
// calibration RadioLib configures takes ~800us). Generous on purpose.
#define RADIO_PULSES_TX_READY_TIMEOUT_US 5000
// Trains up to this length are sent with interrupts disabled on this core, so
// no ISR (USB, timers) can stretch one pulse and - with absolute deadlines -
// shorten the next one. Longer trains run with interrupts enabled to keep USB
// serviced; their per-edge jitter is then bounded by the ISR latency.
#define RADIO_PULSES_TX_IRQ_OFF_MAX_US   500000L
//------------------------------------------------------------------------------------------------------------------
// The actual pulse train, executed from RAM (no XIP cache-miss stalls), with
// direct SIO access (gpio_put) instead of digitalWrite(), and with ABSOLUTE
// deadlines: edge k is placed at t0 + sum(p[0..k-1]). The previous loop
// restarted its time base after every digitalWrite(), so each pulse was
// lengthened by the write/loop overhead and that error accumulated over the
// frame. time_us_32() is a single read of the hardware timer register; the
// signed difference below is wrap-safe.
// Placed in RAM via the same .time_critical section the Pico SDK's
// __not_in_flash_func() uses (written out as a plain attribute so the .ino
// prototype generator sees an ordinary function definition).
static void __attribute__((noinline, section(".time_critical.radio_pulses_tx_train"))) radio_pulses_tx_train(const uint16_t* p, int n, uint pin)
{
    uint32_t level = 1;
    uint32_t t = time_us_32();
    for (int i = 0; i < n; i++)
    {
        gpio_put(pin, level);
        t += p[i];
        while ((int32_t)(time_us_32() - t) < 0) { }
        level ^= 1u;
    }
    gpio_put(pin, 0);
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_send(int startOffset, int pulses)
{
    int total_us = 0;
    if (pulses > 0 && startOffset >= 0 && startOffset < radio_pulses_buffer_len)
    {
        if (startOffset + pulses > radio_pulses_buffer_len)
            pulses = radio_pulses_buffer_len - startOffset;

        for (int idx = startOffset; idx < startOffset + pulses; idx++)
            total_us += radio_pulses_buffer[idx];

        RadioDev* radio = getRadio();
        int savedDrate = radio->getDataRate();
        bool raiseDrate = (savedDrate < RADIO_PULSES_TX_DRATE);

        radio->setCCMode(0);
        radio->setPacketFormat(3);
        // Fine GDO0 sampling grid for the duration of the transmit (see
        // RADIO_PULSES_TX_DRATE). setDataRate() strobes IDLE, so it must come
        // before armOokTx()/setTX().
        if (raiseDrate)
            radio->setDataRate((int)RADIO_PULSES_TX_DRATE);
        // Re-arm the OOK PA table (FREND0=0x11, PATABLE={0x00, power}) so the
        // carrier is fully off between marks. Without this, RadioLib's init-time
        // table was relied on and the "space" level could leak carrier, which
        // professional (superhet/AGC) receivers fail to slice. Must precede the
        // STX issued by setTX(); PATABLE survives the standby inside direct TX.
        radio->armOokTx();
        radio->setTX();

        // Hold the line at "space" while the synthesizer calibrates. STX from
        // IDLE triggers FS calibration (~800us); edges driven before MARCSTATE
        // reaches TX are not transmitted, so the first pulse(s) of the frame
        // used to come out shortened or not at all.
        pinMode(gdo0, OUTPUT);
        gpio_put(gdo0, 0);
        radio->waitTxReady(RADIO_PULSES_TX_READY_TIMEOUT_US);

        if (radio_pulses_buffer[startOffset] == 0xffff) /*hack*/
        {
            delayMicroseconds(1000);
        }

        digitalWrite(RXLED, LOW);   // set the RX LED ON

        if ((long)total_us <= RADIO_PULSES_TX_IRQ_OFF_MAX_US)
        {
            uint32_t irq = save_and_disable_interrupts();
            radio_pulses_tx_train(&radio_pulses_buffer[startOffset], pulses, gdo0);
            restore_interrupts(irq);
        }
        else
        {
            radio_pulses_tx_train(&radio_pulses_buffer[startOffset], pulses, gdo0);
        }

        digitalWrite(RXLED, HIGH);   // set the RX LED OFF

        // restore the profile data rate, then the normal packet format / RX
        if (raiseDrate)
            radio->setDataRate(savedDrate);
        radio->setCCMode(1);
        radio->setPacketFormat(0);
        radio->setRX();

        // NOTE: transmitting must NOT change the stored buffer length; the
        // captured frame (and its count) stays intact so it can be replayed.
    }

    return total_us;
}
//------------------------------------------------------------------------------------------------------------------
// Scratch space for the FIFO/hardware-clocked send path below. Deliberately
// separate from radio_pulses_buffer/radio_buffer (which alias the SAME
// physical RAM - see radio_buffer.ino) so packing a bitstream here can never
// overwrite the stored pulse train it was built from; radio_pulses_send_fifo()
// leaves the stored pulses untouched, same invariant as radio_pulses_send().
#define RADIO_PULSES_FIFO_SCRATCH_BYTES 2048
static uint8_t radio_pulses_fifo_scratch[RADIO_PULSES_FIFO_SCRATCH_BYTES];
//------------------------------------------------------------------------------------------------------------------
// Convert 'pulses' stored microsecond durations (from startOffset) into a
// packed, MSB-first bitstream for RadioDev::sendRawBits(): each pulse becomes
// round(duration_us / unitUs) bits of the same level, alternating level the
// same way radio_pulses_send() does (starts HIGH('1'), toggles after every
// pulse). The last byte is zero-padded (trailing pad = carrier off - harmless
// silence appended after the real data, same as the GDO0 path ending low).
// Returns the number of packed bytes written, or a negative value: -1 bad
// args, -2 result does not fit RADIO_PULSES_FIFO_SCRATCH_BYTES (too many
// pulses/too fine a unit for one call - split into smaller play ranges, or
// use a coarser unitUs).
static int radio_pulses_pack_bits(int startOffset, int pulses, int unitUs)
{
   if (unitUs < 1 || pulses <= 0 || startOffset < 0 || startOffset >= radio_pulses_buffer_len)
      return -1;

   int     bitpos  = 0;      // 0..7 within the byte being built
   int     bytepos = 0;
   uint8_t curByte = 0;
   uint8_t curr_val = 1;     // matches radio_pulses_send(): starts HIGH, toggles each pulse

   for (int idx = startOffset; idx < startOffset + pulses; idx++)
   {
      if (idx >= radio_pulses_buffer_len) break;

      long nbits = ((long)radio_pulses_buffer[idx] + (unitUs / 2)) / unitUs;   // round to nearest
      if (nbits < 1) nbits = 1;   // never drop an edge entirely

      for (long b = 0; b < nbits; b++)
      {
         curByte = (uint8_t)((curByte << 1) | (curr_val & 0x01));
         bitpos++;
         if (bitpos == 8)
         {
            if (bytepos >= RADIO_PULSES_FIFO_SCRATCH_BYTES) return -2;
            radio_pulses_fifo_scratch[bytepos++] = curByte;
            curByte = 0;
            bitpos = 0;
         }
      }
      curr_val ^= 0x01;
   }
   if (bitpos > 0)
   {
      curByte = (uint8_t)(curByte << (8 - bitpos));   // zero-pad the remaining low bits
      if (bytepos >= RADIO_PULSES_FIFO_SCRATCH_BYTES) return -2;
      radio_pulses_fifo_scratch[bytepos++] = curByte;
   }
   return bytepos;
}
//------------------------------------------------------------------------------------------------------------------
// Hardware-clocked alternative to radio_pulses_send(): packs the given stored
// pulses into a bitstream at a fixed 'unitUs' quantum (see
// radio_pulses_pack_bits()) and streams it through the TX FIFO
// (RadioDev::sendRawBits -> rl_transmit_raw_bits), instead of bit-banging
// GDO0. Bit timing is then set by the radio's own bit-rate generator, not by
// an MCU busy-wait loop. Trade-off: every pulse is rounded to a whole number
// of 'unitUs' quanta, so pick unitUs no larger than ~1/8 of the shortest real
// pulse you need to reproduce to keep the rounding error well under what a
// receiver's own timing tolerance absorbs. DRATE (the register that actually
// sets how long each bit is on air) is derived from unitUs and set/restored
// automatically around the transmit - the caller does not set it separately.
// Returns the represented duration in microseconds (post-quantization) on
// success, or a negative value on error: -1 bad args, -2 does not fit the
// scratch buffer, -3 FIFO underrun/stall during transmit, -4 the radio never
// reached IDLE before transmit. Does not touch the stored pulse buffer (same
// invariant as radio_pulses_send()).
long radio_pulses_send_fifo(int startOffset, int pulses, int unitUs)
{
   int nbytes = radio_pulses_pack_bits(startOffset, pulses, unitUs);
   if (nbytes <= 0) return nbytes;

   RadioDev* radio = getRadio();

   // Each packed bit is meant to occupy exactly unitUs on air - that is set by
   // DRATE, not by anything in rl_transmit_raw_bits(). Deriving it here from
   // unitUs (rather than asking the caller to set DRATE separately beforehand)
   // means there is one knob, not two that have to be kept in sync by hand -
   // whatever unitUs was used to pack the bits is exactly what the radio is
   // clocking them out at. Restored afterwards, same as PKTCTRL0/MDMCFG2 are
   // restored inside rl_transmit_raw_bits() - a self-contained burst, not a
   // lasting change to the active profile's data rate.
   int savedDrate = radio->getDataRate();
   long targetBaud = (1000000L + (unitUs / 2)) / unitUs;   // round to nearest
   // Per-board empirical correction (see RadioDev::fifoDrateCalPermille):
   // on at least one real module, SDR measurement showed the actual on-air
   // bit period running ~2x shorter than this naive value, even though the
   // written MDMCFG3/MDMCFG4 bytes were independently verified correct
   // against the TI datasheet DRATE formula - i.e. a hardware-specific
   // quirk, not a bug in this calculation. Defaults to 1000 (no change).
   long cal = radio->getFifoDrateCal();
   targetBaud = (targetBaud * cal + 500) / 1000;   // round to nearest
   if (targetBaud < 20L)      targetBaud = 20L;             // RadioLib/CC1101 floor
   if (targetBaud > 1621830L) targetBaud = 1621830L;        // RadioLib/CC1101 ceiling
   radio->setDataRate((int)targetBaud);

   // Same OOK PA-table arming as the GDO0 path - how the bits reach the
   // modulator (FIFO vs GDO0) is unrelated to how OOK keying itself works.
   radio->armOokTx();

   digitalWrite(RXLED, LOW);
   int st = radio->sendRawBits(radio_pulses_fifo_scratch, nbytes);
   digitalWrite(RXLED, HIGH);

   radio->setDataRate(savedDrate);   // restore the profile's own data rate

   // same end-state as radio_pulses_send(), regardless of which path was used
   radio->setCCMode(1);
   radio->setPacketFormat(0);
   radio->setRX();

   if (st == -4) return -4;   // chip never reached IDLE - distinct from an underrun/stall
   if (st != 0)  return -3;   // FIFO underrun/stall during transmit

   long total_us = 0;
   for (int idx = startOffset; idx < startOffset + pulses; idx++)
   {
      if (idx >= radio_pulses_buffer_len) break;
      long nbits = ((long)radio_pulses_buffer[idx] + (unitUs / 2)) / unitUs;
      if (nbits < 1) nbits = 1;
      total_us += nbits * unitUs;
   }
   return total_us;
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_add(uint16_t pulse)
{
    if ( radio_pulses_cnt + 1< radio_pulses_buffer_len)
    {
        radio_pulses_buffer[radio_pulses_cnt] = pulse;
        radio_pulses_cnt++;
    }

    return radio_pulses_cnt;  
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_set(int offset, uint16_t pulse)
{
    if (offset < radio_pulses_buffer_len)
    {
        radio_pulses_buffer[offset] = pulse;
        if (offset >= radio_pulses_cnt)
        radio_pulses_cnt = offset + 1;
    }

    return radio_pulses_cnt;
}
//------------------------------------------------------------------------------------------------------------------
int  radio_pulses_max_count(void)
{
   return radio_pulses_buffer_len - radio_pulses_cnt;
}
//------------------------------------------------------------------------------------------------------------------
