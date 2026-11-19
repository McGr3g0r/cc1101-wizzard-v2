#!/usr/bin/env python3
"""png2paint.py - PoC: send a PNG to the cc1101-wizzard 'paint' feature.

Loads a PNG, flattens it to 1-bit (bright pixels = carrier ON), scales it to the
paint grid, prints an ASCII preview, and streams it to the device as
'paint setup' + one 'paint row' per image scan-line.

Usage:
  png2paint.py <file.png> [--cols N] [--rows M] [--thresh T|auto] [--invert]
               [--fstart Hz] [--fstop Hz] [--dwell us] [--noflip] [--dry]
Defaults: cols=32 rows=32 thresh=auto band=433.75-434.09MHz dwell=180000 flip=on
"""
import sys, time
from PIL import Image

def arg(name, default, cast=str):
    if name in sys.argv:
        return cast(sys.argv[sys.argv.index(name) + 1])
    return default

png    = sys.argv[1]
cols   = int(arg("--cols", 32))
rows   = int(arg("--rows", 32))
thresh = arg("--thresh", "auto")
invert = "--invert" in sys.argv
fstart = int(arg("--fstart", 433_750_000))
fstop  = int(arg("--fstop", 434_090_000))
dwell  = int(arg("--dwell", 180_000))
flip   = "--noflip" not in sys.argv
dry    = "--dry" in sys.argv

# load, flatten alpha over black (transparent -> dark -> off), to grayscale
im = Image.open(png).convert("RGBA")
bg = Image.new("RGBA", im.size, (0, 0, 0, 255))
im = Image.alpha_composite(bg, im).convert("L")
im = im.resize((cols, rows), Image.LANCZOS)
px = list(im.getdata())
grid = [px[r * cols:(r + 1) * cols] for r in range(rows)]

# threshold: fixed value, or auto = midpoint between dark and bright medians
if thresh == "auto":
    vals = sorted(px)
    lo = vals[len(vals) // 10]        # ~10th pct (background)
    hi = vals[len(vals) * 9 // 10]    # ~90th pct (bright strokes)
    T = int(lo + 0.55 * (hi - lo))
else:
    T = int(thresh)

def lit(v):
    return (v < T) if invert else (v > T)

bits = [[lit(v) for v in row] for row in grid]

# ASCII preview (as the image reads, top -> bottom)
nlit = sum(sum(r) for r in bits)
print(f"{png}: {cols}x{rows} grid, thresh={T}, {nlit} lit px "
      f"({100*nlit/(cols*rows):.0f}%) | band {fstart/1e6:.3f}-{fstop/1e6:.3f} MHz, "
      f"dwell {dwell}us, flip={flip}")
for r in bits:
    print("|" + "".join("#" if b else " " for b in r) + "|")

if dry:
    print("(dry run, nada transmitido)")
    sys.exit(0)

import serial
nbytes = (cols + 7) // 8
def mask_hex(rowbits):
    buf = bytearray(nbytes)
    for c, b in enumerate(rowbits):
        if b:
            buf[c >> 3] |= (0x80 >> (c & 7))
    return buf.hex()

order = bits[::-1] if flip else bits
ser = serial.Serial(); ser.port, ser.baudrate, ser.timeout = "/dev/ttyACM0", 115200, 0.05
ser.dtr = True; ser.rts = False
ser.open(); time.sleep(0.3); ser.reset_input_buffer()
def cmd(line, settle):
    ser.write((line + "\n").encode()); ser.flush(); time.sleep(settle); ser.read(8192)

print("transmitiendo...")
t0 = time.time()
cmd(f"radio r0 paint setup {fstart} {fstop} {cols}", 1.2)
for rb in order:
    cmd(f"radio r0 paint row {dwell} {mask_hex(rb)}", dwell / 1e6 + 0.04)
cmd("radio r0 paint end", 0.1)
print(f"done in {time.time()-t0:.1f}s")
ser.close()
