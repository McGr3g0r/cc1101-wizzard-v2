//------------------------------------------------------------------------------------------------------------------
// Author: Grzegorz Rajtar
// e-mail: mcgregor@blackmesaeast.com.pl
// (c) 2026
// Modified: 2026-10-16
//------------------------------------------------------------------------------------------------------------------
#include "platform.h"
#include "settings.h"
#include <SPI.h>
#include <RP2040.h>
//------------------------------------------------------------------------------------------------------------------
int RXLED = LED_BUILTIN;
//------------------------------------------------------------------------------------------------------------------
void platform_init(void)
{
    pinMode(RXLED, OUTPUT);

    // Configure SPI0 pins before RadioLib brings up the bus. Do NOT set a
    // hardware CS here: RadioLib drives CS (ss) as a plain GPIO itself, and a
    // hardware CS would fight it. GDO0/GDO2 are managed by RadioLib (IRQ) and
    // reconfigured on the fly by the OOK pulse code, so they are not touched here.
    SPI.setRX(miso);
    SPI.setTX(mosi);
    SPI.setSCK(sck);
    SPI.begin();   // bring up SPI0 with the pins above before RadioLib uses it
}

//------------------------------------------------------------------------------------------------------------------
String platform_get_name(void)
{
  return F("RP2040");
}

//------------------------------------------------------------------------------------------------------------------
