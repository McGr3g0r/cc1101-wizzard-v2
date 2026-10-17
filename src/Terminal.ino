//------------------------------------------------------------------------------------------------------------------
#include "Terminal.h"
//------------------------------------------------------------------------------------------------------------------
Terminal::Terminal(int lineSize)
{
   maxLineLength = lineSize;
   echoEnabled = false;
}
//------------------------------------------------------------------------------------------------------------------
void Terminal::clear(void)
{
  buffer = "";
  avail = false;
}
//------------------------------------------------------------------------------------------------------------------
void Terminal::input(String line)
{
  buffer = line;
  avail = true;
}
//------------------------------------------------------------------------------------------------------------------
void Terminal::input(Stream& term)
{
    // drain the whole RX buffer each call (not one byte), so commands like
    // 'monitor stop' are always picked up promptly even while a background
    // task (the monitor) is busy between loop() iterations
    while (term.available())
    {
        int data = term.read();
        if (data == '\b' || data == '\177') {  // BS and DEL
            avail = false;
            if (buffer.length()) {
                 if(buffer.length() > 1)
                    buffer = buffer.substring(0, buffer.length()-1);
                 else
                    buffer = "";
                    
                if (echoEnabled && Serial.availableForWrite() >= 3) term.write("\b \b");

            }
        }
        else if (data == '\r' || data == '\n' ) {
            // non-blocking CRLF echo (see note below): a full TX buffer must not
            // block core0 at end-of-line on a long command
            if (echoEnabled && Serial.availableForWrite() >= 2) term.write("\r\n");    // output CRLF

            if (buffer.length() > 0)
            {
                //term.println(buffer);
                avail = true;
                return;
            }
        }
        else if (buffer.length() < CMD_MAX_BUFFER_LEN - 1) {
            buffer += (char)data;
            // NON-BLOCKING echo: only echo if the USB TX buffer has room. A long
            // line (e.g. 'pulses add <many>') would otherwise fill the TX FIFO
            // while the host is still sending (not reading), and term.write()
            // would block core0 -> USB stalls -> connection drops. Echo is a
            // convenience, so skipping it under back-pressure is safe.
            if (echoEnabled && Serial.availableForWrite() > 0) term.write(data);
        }
    }
}
//------------------------------------------------------------------------------------------------------------------    
 void Terminal::setEcho(bool enabled)
 {
    echoEnabled = enabled;
 }
//------------------------------------------------------------------------------------------------------------------
String Terminal::getLine(void)
{
   return buffer;
}
//------------------------------------------------------------------------------------------------------------------
bool Terminal::available()
{
     return avail;
}
//------------------------------------------------------------------------------------------------------------------
