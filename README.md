# cc1101-wizzard v2 - PlatformIO (RP2040, RadioLib backend)

A serial-CLI tool for the TI **CC1101** sub-GHz radio, running on an **RP2040**
(Raspberry Pi Pico) under the Arduino framework. It lets a host (a terminal or
an application such as an Android app) drive the radio over a simple text
protocol: set any CC1101 register/parameter, store named work profiles, run
stored/startup scripts, monitor incoming frames non-blocking, and transmit raw
packets or OOK pulse trains.

The boot banner reports the firmware version, build number and build timestamp
(e.g. `CC1101 Wizard tool v2.1 build 33 (built Oct 21 2026 11:35:12) ...`); the
same identity is available machine-readable via `info`.

The firmware does **not** interpret protocols. The monitor emits only the raw
captured data (hex or JSON); decoding/encoding is the host's job.

## Radio driver layer

This variant drives the CC1101 through **[RadioLib](https://github.com/jgromes/RadioLib)**
instead of the ELECHOUSE `SmartRC-CC1101-Driver-Lib`. RadioLib's SPI works on
current arduino-pico cores, so this project builds on the **latest core with no
version/toolchain pinning** - it sidesteps the ELECHOUSE-vs-new-SDK SPI breakage
that forces the ELECHOUSE build back to arduino-pico 3.0.0.

RadioLib defines a class named `CC1101` in the global namespace, which would
clash with this firmware's own radio wrapper. To avoid the clash:

- `rl_cc1101.cpp` is the **only** translation unit that includes RadioLib. It
  holds the RadioLib `CC1101` instance and exposes a flat C-style API
  (`rl_cc1101.h`) with no RadioLib types.
- `cc1101.ino` keeps the firmware's radio wrapper class (named `RadioDev`,
  same public interface as before) and implements it by calling those `rl_*`
  functions.

So `radio_params`, `radio_handler`, `Monitor`, `radio_pulses`, `radio_raw` and
the rest stay driver-agnostic.

## Hardware / wiring

CC1101 is wired to **SPI0**. Pins are defined in `src/settings.h`:

| Signal | RP2040 GPIO |
|--------|-------------|
| SCK    | 2           |
| MOSI   | 3           |
| MISO   | 4           |
| CS/SS  | 5           |
| GDO2   | 6           |
| GDO0   | 7           |

RadioLib drives CS (`ss`) itself as a plain GPIO, so no hardware CS is set up
(see `src/platform_rp2040.ino`). GDO0 is used both as the RadioLib IRQ line
(packet mode) and as the async direct-mode data line (OOK capture / replay).

## Build / upload / monitor

```
pio run
pio run -t upload
pio device monitor      # 115200 baud
```

RadioLib is pulled automatically via `lib_deps` (`jgromes/RadioLib@7.8.1`).
A 0.5 MB LittleFS partition holds profiles, env variables and startup scripts.

## CLI overview

Line-based, one command per line, terminated by CR or LF. The firmware echoes
input and replies `OK` on success or an error hint otherwise.

Line limits (see "CLI limits" below): up to **255 characters** and **64
whitespace-separated tokens** per line.

### info

Device / firmware identification, so a host can reliably recognise the device
and its firmware version.

```
info          # JSON (default, host-parseable)
info text     # same data, human-readable
```

JSON fields: `device`, `variant`, `fw`, `build`, `built` (compile date/time),
`cli` (CLI protocol version), `driver` ("radiolib"), `begin` (RadioLib begin()
status), `platform`, `radios`, and an `interfaces` array with `name`, `present`,
`chip_version` and `partnum` per radio. A genuine CC1101 reports
`chip_version` = 20 (0x14).

### radio

```
radio list                         list radio interfaces
radio help                         command help
radio <iface>                      show interface info (all parameters)
radio <iface> info                 same as above
radio <iface> get <param>          read one parameter
radio <iface> set <param> <value>  set any parameter (generic)
radio <iface> set_<param> <value>  convenience form, e.g. radio r0 set_freq 433920000
radio <iface> get_<param>
radio <iface> set mod <2fsk|gfsk|ook|4fsk|msk|0..4>
radio <iface> set syncword <high> <low>
radio <iface> set pa <-30..12 dBm>
radio <iface> rx | tx | idle | reset | rssi
radio <iface> scan <startKHz> <stopKHz>
radio <iface> send <hexstring>
radio <iface> jam <ms>             (0 = until ctrl-c)
radio <iface> pulses ...           (see "pulses" below)
radio <iface> paint ...            (see "paint" below)
```

Only `r0` exists for now (one CC1101 on SPI0). Frequencies are in Hz for
`set_freq` / `set freq` (e.g. `433920000`).

### pulses (OOK raw timing)

Pulse values are durations in microseconds, stored in a shared buffer of up to
**4096** entries (`RADIO_BUFFER_MAIN` = 8192 bytes / 2).

```
radio <iface> pulses count                         number of stored pulses
radio <iface> pulses flush                          clear the buffer
radio <iface> pulses show                           print stored pulses
radio <iface> pulses grab <off> <minUs> <maxUs>     capture into the buffer
radio <iface> pulses add <v> <v> ...                append pulse values
radio <iface> pulses play <off> <n> [repeat] [gapUs]
```

`pulses play`:
- `off`, `n` - start offset and number of stored pulses to transmit. Bounds are
  checked against the **stored** pulse count (`off >= 0`, `off < count`,
  `off + n <= count`).
- `repeat` - optional, how many times to play the sequence (default 1).
- `gapUs` - optional, silent gap between repeats in microseconds (default 0).
  During the gap the radio is back in RX and does not emit.

Examples:
```
radio r0 pulses play 0 2395            # play once
radio r0 pulses play 0 2395 5 10000    # play 5 times, 10 ms gap between repeats
```

Note: `pulses play` is **blocking** - the whole sequence (all repeats plus
gaps) runs on core0, so the CLI is unresponsive for that duration. Keep the
total time short enough for the host's read timeout.

To load large pulse trains, send several `pulses add` lines (up to ~58 values
per line given the 255-char / 64-token line limits) and then `pulses play`.

### paint (spectrum painting)

Draws an image on an SDR **waterfall** by hopping the carrier: horizontal axis =
**frequency**, vertical axis = **time**, brightness = carrier on/off. The CC1101
is a single narrowband carrier, so a naive host-driven sweep can only draw
one-frequency-per-instant shapes (a sine, a diagonal) and each `set_freq`
re-triggers an ~800 us PLL recalibration - far too slow to light several
frequencies inside one FFT frame.

`paint` solves this on-device using the datasheet's fast-hopping recipe:
**calibrate each column frequency once up front, save its FREQ + FSCAL
registers, then on every hop restore those bytes with `FS_AUTOCAL = never`** so a
retune is only the synth settling (~75 us), not a full recalibration. Interleaving
the lit columns of a row then makes them appear simultaneous to the SDR - so a
real horizontal stroke (e.g. the crossbar of an "A") is possible, which a
single-carrier CLI sweep cannot do.

Memory is tiny and streaming: only the calibration table lives in RAM
(`PAINT_MAX_COLS` x 6 bytes = 768 bytes for 128 columns); the image is streamed
one row at a time from the host, so no full bitmap is ever stored on-chip.

```
radio <iface> paint setup <fstartHz> <fstopHz> <ncols>
      calibrate ncols evenly-spaced frequencies in [fstart, fstop] and store
      their FREQ+FSCAL register sets. Forces OOK modulation. ncols = 1..128.
radio <iface> paint row <dwellUs> <hexmask>
      paint one time-row: for dwellUs, fast-hop the carrier among the lit
      columns. hexmask is an ncols-bit array, MSB-first: column c = bit
      (0x80 >> (c & 7)) of mask byte (c >> 3), i.e. col 0 = MSB of byte 0 =
      lowest frequency. A blank mask = dwellUs of silence (keeps row timing).
radio <iface> paint show        print the calibration table (FREQ + FSCAL per column)
radio <iface> paint count       number of calibrated columns
radio <iface> paint end         stop the carrier, restore normal auto-cal, idle
```

- A row with a **single** lit column tunes once and holds a clean CW carrier for
  the whole dwell. A row with **several** lit columns interleaves them, holding
  each for `PAINT_HOLD_US` (default 1200 us) so transitions stay rare enough that
  the on/off keying sidebands do not smear the columns.
- `paint row` is **blocking** for its `dwellUs`, like `pulses play`.
- The carrier is keyed via GDO0 (OOK mark), so painting runs in OOK mode and
  uses the current `pa` power.

Example - paint a crossbar-style row of 8 adjacent columns (a horizontal line),
24-column grid over 433.80-434.04 MHz, held 40 ms:
```
radio r0 paint setup 433800000 434040000 24
radio r0 paint row 40000 00ff00     # cols 8..15 lit -> 8 frequencies at once
radio r0 paint end
```

A host script that converts ASCII-art / text into a sequence of `paint setup` +
`paint row` lines (one row per character scan-line) is the intended front end;
each image row is just a bit mask. Tested on RP2040 hardware and verified on an
RTL-SDR waterfall (clean CW per column, simultaneous multi-column rows, and a
legible "A" with a real horizontal crossbar).

### profile (named work profiles, stored on LittleFS)

A profile is a named sequence of radio commands (stripped of the `radio <iface>`
prefix) that configures the radio for a given mode. Stored as `/prof_<name>`.

```
profile list
profile show <name>
profile new  <name>
profile add  <name> <command...>      e.g. profile add ook433 set_freq 433920000
profile del  <name>
profile apply <name> <iface>          run the profile's commands on an interface
```

### script (stored scripts on LittleFS)

A script is a plain-text file `<name>.scr` on LittleFS; each line is a CLI
command, run in order. `/init.scr` (or the file named by the `init` env
variable) is executed automatically at boot, **before** the serial/USB loop
starts - handy to auto-configure the radio or apply a profile on power-up.
Blank lines and lines starting with `#` are skipped; while a script runs, each
line is echoed prefixed with `init> `.

```
script list                      list stored scripts
script show   <name>             print a script's lines
script new    <name>             create / clear an empty script
script add    <name> <command>   append a CLI command line, e.g. script add init profile apply ook433 r0
script del    <name>             delete a script
script rename <old> <new>        rename a script
script run    <name>             execute a script now
```

Script names use letters, digits, `_` and `-`, up to 24 characters; lines up to
256 characters. Example - make the radio come up on an OOK profile every boot:
```
profile new ook433
profile add ook433 set_freq 433920000
profile add ook433 set mod ook
script new init
script add init profile apply ook433 r0
# (reboot) -> init.scr runs 'profile apply ook433 r0' before the CLI starts
```

### monitor (non-blocking RX)

```
monitor start <iface> <profile> [json|hex] [minUs] [maxGapUs] [rssiGate]
monitor stop
monitor status
```

`monitor start` applies the profile (which sets frequency, modulation,
bandwidth, data rate, etc.) and then listens. The CLI stays live while the
monitor runs, so `monitor stop` and other commands work during monitoring.

- **Packet modes (2-FSK/GFSK/4-FSK/MSK)**: RadioLib `startReceive()` plus a
  "packet received" interrupt flag; the monitor reads the RX FIFO and prints
  each frame.
- **OOK**: the radio is put into asynchronous direct RX (raw demodulated data on
  **GDO0**). The pulse capture is a tight GDO0 busy-wait that would starve
  core0's USB/CLI, so it runs on the **RP2040 second core** (`loop1` ->
  `monitor_core1_task`); core0 stays free for USB, CLI and SPI. A carrier-sense
  RSSI gate (default -85 dBm, tunable via the `rssiGate` argument) only triggers
  a capture when a signal is present; completed frames are handed back to core0
  via volatile flags and printed.

OOK defaults: `minUs` = 100, `maxGapUs` = 15000, `rssiGate` = -85 dBm.
After `monitor stop`, no further frames are printed.

## Parameter mapping (CLI -> RadioLib)

High-level RadioLib calls: `freq` -> setFrequency, `drate` -> setBitRate,
`dev` -> setFrequencyDeviation, `rxbw` -> setRxBandwidth, `pa` -> setOutputPower,
`syncword` -> setSyncWord, `mod` OOK -> setOOK. Everything else (`mod` FSK
variants, `chan`, `chsp`, `whitening`, `manchester`, `fec`, `syncmode`,
`pktfmt`, `pktlenmode`, `pktlen`, `crc`, `crcaf`, `dcfilteroff`, `preamble`,
`pqt`, `appendstatus`, `addr`, `addrchk`) is written straight to the CC1101
registers via RadioLib's public `SPIsetRegValue`, so the firmware keeps full
control. Use `radio r0 info` for the current parameter list and values.

## RX / TX behaviour

- `radio <iface> send <hex>` transmits a packet in packet mode (ccmode=1,
  pktFormat=0) and then returns the radio to packet RX (`startReceive`), so it
  is ready to receive a reply. Received frames are only printed while the
  `monitor` is running.
- `pulses play` transmits in OOK async direct mode and then returns the radio to
  packet RX.

## CLI limits

- Command line: `CMD_MAX_BUFFER_LEN` = 256 (max 255 characters per line).
- Tokens per line: `PARSER_MAX_PARAMS` = 64.

These are deliberately large so a host can send long `pulses add` lines without
truncation or parser rejection. Buffers that join a whole line
(`Parser::buffer`, the `fs write/append` text buffer) are sized to match.

## Project layout

```
src/
  cc1101-wizzard-v2.ino   main sketch: setup/loop, core1 hook (loop1), command registration, init.scr at boot
  cc1101.{h,ino}          RadioDev radio wrapper (driver-agnostic public API)
  rl_cc1101.{h,cpp}       RadioLib shim (the only RadioLib TU), flat rl_* API
  radio_handler.ino       'radio' command dispatcher
  radio_params.{h,ino}    generic parameter table (name/range/get/set)
  radio_iface.{h,ino}     radio interface registry (r0)
  radio_pulses.{h,ino}    OOK pulse capture (core1) / replay / buffer
  radio_paint.{h,ino}     spectrum painting: per-column FREQ+FSCAL table + fast carrier hopping
  radio_raw.{h,ino}       raw packet TX helper
  Monitor.ino             non-blocking RX monitor (packet + OOK/core1)
  rprofile.{h,ino}        LittleFS-backed named profiles + 'profile' command
  script_handler.ino      LittleFS .scr scripts + 'script' command, init.scr runner
  info_handler.ino        'info' command (device/fw identification)
  platform_rp2040.ino     SPI0 bring-up, platform name, RX LED
  settings.h              pins, buffer sizes, firmware identity, feature flags
  Parser / Terminal / CmdHandler / Script / env / files   CLI plumbing
```

## Status

Built with PlatformIO and tested on RP2040 hardware: chip detection, `info`,
parameters, profiles, LittleFS scripts (incl. `init.scr` at boot), the
non-blocking OOK monitor (live CLI, real frames captured on core1), OOK
pulse replay, and the `paint` spectrum-painter (per-column FREQ+FSCAL
fast-hopping, verified on an RTL-SDR: clean CW per column, simultaneous
multi-column rows, and a legible "A" with a horizontal crossbar) all work.
Packet-mode (FSK) monitor and TX paths are implemented -
exercise them against your own signals. If the
chip is not detected, `info text` shows `chip_version` (a genuine CC1101
reports 20 / 0x14).
