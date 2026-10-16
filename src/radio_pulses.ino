//------------------------------------------------------------------------------------------------------------------
#include "radio_pulses.h"
#include "radio_buffer.h"
#include "settings.h"
#include "cc1101.h"
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
   memset(radio_pulses_buffer, 0, sizeof(radio_pulses_buffer)); 
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
    RadioDev* radio = getRadio();

    int idx = startOffset;
    uint32_t s2;
    int tdiff;

    radio->setCCMode(0);
    radio->setPacketFormat(3);
    radio->setRX();

    pinMode(gdo0, INPUT);

    // absolute cap so continuous sub-minPulse activity (RF noise on GDO0 in
    // async mode) can never spin this loop forever and freeze the CLI
    uint32_t grabStart = micros();
    uint32_t s = micros();
    while ( digitalRead(gdo0) == LOW && micros() -s < maxSpaceUs);
    if ( micros() -s < maxSpaceUs)
    {
       idx = startOffset;
       uint8_t curr_val = 1;

       s = micros();
       while ((idx < radio_pulses_buffer_len) && (micros() -s < maxSpaceUs) && (micros() - grabStart < RADIO_PULSES_MAX_GRAB_US))
       {
            while((digitalRead(gdo0) == curr_val) && (micros() -s < maxSpaceUs));
            s2 = micros();
            tdiff = s2 -s;
            if (tdiff < maxSpaceUs)
            {  
               if (tdiff >= minPulseUs) {
                   radio_pulses_buffer[idx++] = tdiff;
               }
               s = micros();
               curr_val++;
               curr_val &= 0x01;
            }     
       }

       radio_pulses_cnt = idx;
        // setting normal pkt format again
       radio->setCCMode(1); 
       radio->setPacketFormat(0);
       radio->setRX();
    };

    return idx - startOffset;
}
//------------------------------------------------------------------------------------------------------------------
// Pure GPIO pulse capture: NO radio reconfiguration, NO RadioLib calls. The
// caller must have already put the radio into async direct RX (data on gdo0).
// Hard-capped in time so RF noise can never spin this forever.
//
// Frame-boundary ("gap -> frame -> gap") triggering so one capture is exactly
// one complete, aligned frame regardless of when it started:
//   1) wait for an idle period (continuous LOW >= maxSpaceUs) -> we are in the
//      inter-frame gap, not mid-frame;
//   2) start on the first rising edge -> the real frame start (preamble);
//   3) record edge intervals until the next long LOW gap (>= maxSpaceUs) ends
//      the frame;
//   4) trim leading/trailing runts (edge glitches shorter than minPulseUs).
// maxSpaceUs is the guard/inter-frame gap threshold (set it just above the
// longest intra-frame LOW, e.g. ~10-30x the bit period for a keyfob).
int radio_pulses_capture(int startOffset, int minPulseUs, int maxSpaceUs)
{
    pinMode(gdo0, INPUT);

    const uint32_t guardUs    = (uint32_t)maxSpaceUs;
    const uint32_t deadlineUs = (uint32_t)RADIO_PULSES_MAX_GRAB_US;
    const uint32_t grabStart  = micros();

    // Phase 1: wait for an idle period (continuous LOW >= guardUs). Any HIGH
    // restarts the idle timer, so we only pass once we are between frames.
    uint32_t idleStart = micros();
    for (;;)
    {
        if ((uint32_t)(micros() - grabStart) >= deadlineUs) { radio_pulses_cnt = startOffset; return 0; }
        if (digitalRead(gdo0) != LOW) idleStart = micros();
        else if ((uint32_t)(micros() - idleStart) >= guardUs) break;
    }

    // Phase 2: wait for the first rising edge = clean frame start.
    while (digitalRead(gdo0) == LOW)
        if ((uint32_t)(micros() - grabStart) >= deadlineUs) { radio_pulses_cnt = startOffset; return 0; }

    // Phase 3: record edge intervals until the next long LOW gap (frame end).
    int idx = startOffset;
    uint8_t curr_val = 1;          // line is HIGH now (we just saw the rising edge)
    uint32_t s = micros();
    while (idx < radio_pulses_buffer_len)
    {
        while (digitalRead(gdo0) == curr_val)
        {
            if ((uint32_t)(micros() - s) >= guardUs) break;
            if ((uint32_t)(micros() - grabStart) >= deadlineUs) break;
        }
        uint32_t now   = micros();
        uint32_t tdiff = now - s;

        if (curr_val == 0 && tdiff >= guardUs) break;                     // guard on LOW -> frame end
        if ((uint32_t)(micros() - grabStart) >= deadlineUs) break;        // overall time cap

        radio_pulses_buffer[idx++] = (tdiff > 0xFFFF) ? 0xFFFF : (uint16_t)tdiff;
        s = now;
        curr_val ^= 0x01;
    }

    // Phase 4: trim leading/trailing runts (edge glitches < minPulseUs).
    int begin = startOffset;
    int end   = idx;                                   // exclusive
    while (end > begin && radio_pulses_buffer[begin]     < (uint16_t)minPulseUs) begin++;
    while (end > begin && radio_pulses_buffer[end - 1]   < (uint16_t)minPulseUs) end--;

    int outN = end - begin;
    if (begin != startOffset && outN > 0)
        memmove(&radio_pulses_buffer[startOffset], &radio_pulses_buffer[begin], (size_t)outN * sizeof(uint16_t));

    radio_pulses_cnt = startOffset + outN;
    return outN;
}
//------------------------------------------------------------------------------------------------------------------
int radio_pulses_send(int startOffset, int pulses)
{
    int total_us = 0; 
    uint32_t s;
    if (pulses > 0 && startOffset >=0 && startOffset < radio_pulses_buffer_len)
    { 
        RadioDev* radio = getRadio();        
        radio->setCCMode(0); 
        radio->setPacketFormat(3);
        radio->setTX();

        pinMode(gdo0, OUTPUT);
        if (radio_pulses_buffer[startOffset] == 0xffff) /*hack*/
        {
            digitalWrite(gdo0, LOW);
            delayMicroseconds(1000);
        }
        
        digitalWrite(RXLED, LOW);   // set the RX LED ON
        uint8_t curr_val = 1;
        for (int idx = startOffset; idx < startOffset + pulses; idx++) {
           if (idx >= radio_pulses_buffer_len)
               break;
           
            digitalWrite(gdo0, curr_val);
            s = micros();
            while (micros() -s < radio_pulses_buffer[idx]);
            //delayMicroseconds(radio_pulses_buffer[idx]);
            curr_val++;
            curr_val &= 0x01;              
        }
            
        digitalWrite(gdo0, 0);
        digitalWrite(RXLED, HIGH);   // set the RX LED OFF

        // setting normal pkt format again
        radio->setCCMode(1); 
        radio->setPacketFormat(0);
        radio->setRX();

        for (int idx = startOffset; idx < startOffset + pulses; idx++) {
           if (idx >= radio_pulses_buffer_len)
               break;
               
            total_us += radio_pulses_buffer[idx];
        }
        radio_pulses_cnt = startOffset + pulses;
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
