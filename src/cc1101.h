//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-18
//------------------------------------------------------------------------------------------------------------------
#ifndef __CC1101_H__
#define __CC1101_H__
//------------------------------------------------------------------------------------------------------------------
class RadioDev {
    public:
        RadioDev(void);
        void init(void);
        bool check(void);
        void setFreq(int khz);
        int getFreq(void);
        void setModulation(int mod);
        int getModulation(void);
        void setDeviation(int dev);
        int getDeviation(void);
        void setChan(int chan);
        int getChan(void);

        void setChanSpaceing(int space);
        int getChanSpaceing(void);

        void setRxBw(int val);
        int getRxBw(void);

        void setDataRate(int val);
        int getDataRate(void);
        // See fifoDrateCalPermille above.
        void setFifoDrateCal(int permille);
        int getFifoDrateCal(void);

        void setPA(int pa);
        int getPA(void);

        void setSyncMode(int val);
        int getSyncMode(void);

        void setSyncWord(int low, int high);
        int getSyncWordHigh(void);
        int getSyncWordLow(void);
        void setAddressCheck(int chk);
        int getAddressCheck(void);

        void setAddress(int addr);
        int getAddress(void);

        
        void setDataWhitening(int wdata);
        int getDataWhitening(void);

        void setPacketFormat(int pktfmt);
        int getPacketFormat(void);

        void setPacketLengthMode(int mode);
        int getPacketLengthMode(void);
        void setPacketLength(int len);
        int getPacketLength(void);  

        void setCrcEnable(int enable);
        int getCrcEnable(void);  

        void setCrcAf(int af);
        int getCrcAf(void); 

        void setDCFilterOff(int off);
        int getDCFilterOff(void);

        void setManchester(int enable);
        int getManchester(void);

        void setFEC(int enable);
        int getFEC(void);

        void setPQT(int pqt);
        int getPQT(void);

        void setPreambleLen(int len);
        int getPreambleLen(void);

        void setAppendStatus(int enable);
        int getAppendStatus(void);

        int getRSSI(void);
        int getLQI(void);

        void setRX();
        int getRX(void);
        void setTX();
        int getTX(void);

        // Arm a clean two-level OOK PA table (FREND0=0x11, PATABLE={0x00,power})
        // right before a GDO0-driven direct transmit. No-op unless modulation is
        // OOK. Call after setCCMode(0)/setPacketFormat(3) and before setTX().
        void armOokTx(void);

        static String modulation(int mod);

        void setCCMode(int mode);
        int getCCMode(void);

        void sendData(uint8_t* data, int len);

        // Hardware-clocked alternative to the GDO0 bit-bang path in
        // radio_pulses.ino: streams 'nbytes' through the TX FIFO with bit
        // timing set by the radio's own bit-rate generator, unframed (no
        // preamble/sync/length byte), via rl_transmit_raw_bits(). Caller must
        // call armOokTx() first for OOK, same as before setTX() on the GDO0
        // path. Returns 0 on success.
        int sendRawBits(uint8_t* data, int nbytes);

        // v2: packet-mode receive (FSK/GFSK/MSK/4-FSK and OOK in packet mode)
        void setIdle(void);                 // put transceiver in IDLE state
        bool rxAvailable(void);             // true when a packet is waiting (GDO0 flag)
        int  receive(uint8_t* buf);         // read packet from RX FIFO, returns length
        bool crcOk(void);                   // CRC status of last received packet

        int  version(void);                 // CC1101 VERSION status register (chip id)
        int  partnum(void);                 // CC1101 PARTNUM status register

    private:
        void default_init(void);
        int lastFreq;
        int mod;
        int chan;
        int chanSp;
        int dev;
        int rxbw;
        int drate;
        // Per-board empirical correction for radio_pulses_send_fifo(): on at
        // least one real CC1101 module/clone, the actual over-the-air bit
        // period measured via SDR was ~2x shorter than MDMCFG3/MDMCFG4
        // (verified correct against the TI datasheet DRATE formula) implied -
        // a hardware-specific discrepancy, not a computation bug. In permille
        // (1000 = 1.0x, no change). Applied as a multiplier on the requested
        // baud: targetBaud = naiveTargetBaud * fifoDrateCalPermille / 1000.
        // Default 1000 so boards without this quirk are unaffected; set via
        // 'radio r0 set fifocal <permille>' after measuring the real
        // discrepancy on your own hardware (e.g. via SDR), never assumed.
        int fifoDrateCalPermille;
        int ccmode;
        int pa;
        int smode;
        int swordHi;
        int swordLo;
        int achk;
        int addr;
        int dataw;
        int pktfmt;
        int pktlmode;
        int pktlen;
        int crcen;
        int crcaf;
        int dcfilteroff;
        int manchen;
        int fecen;
        int prelen;
        int pqt;
        int asen;
        int rxen;
        int txen;
        int lastCrc;
};
//------------------------------------------------------------------------------------------------------------------
#endif// __CC1101_H__
//------------------------------------------------------------------------------------------------------------------
