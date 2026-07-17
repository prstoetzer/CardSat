#!/usr/bin/env python3
"""Guard against `while (x.available())` on a Stream that can return -1.

Arduino's Stream::available() returns a COUNT, but HWCDC (and USBCDC) return **-1**
when the port has been closed -- and `while (-1)` is TRUE. This froze USB CAT for an
entire debugging session: UsbSerial::begin() calls Serial.end() to hand the USB PHY
to the host stack, after which

    while (Serial.available()) { char c = (char)Serial.read(); ... }

spins forever on -1/0xFF with no yield. No exception, no starvation on the other
core -- invisible to every watchdog until the loop-task WDT was armed. It took a
coredump backtrace to find (App::serviceSerialCli+79).

Any Stream that can be closed at runtime needs `available() > 0`. fs::File returns 0
and is exempt.
"""
import re, sys, os

# Streams that can be torn down while code still polls them.
RISKY = re.compile(r'while\s*\(\s*(Serial\d?|_stream->|s_cdc->|cdc->|\w*[Ss]erial\w*\.)\s*available\(\)\s*\)')
files, bad = [], 0
for root, _, names in os.walk('src'):
    for n in names:
        if n.endswith(('.cpp','.h')): files.append(os.path.join(root,n))
files.append('CardSat.ino')

for path in files:
    if not os.path.exists(path): continue
    for i, line in enumerate(open(path, encoding='utf-8', errors='replace'), 1):
        if line.lstrip().startswith('//'): continue
        m = RISKY.search(line.split('//')[0])
        if m:
            print(f"UNGUARDED available(): {path}:{i}")
            print(f"    {line.strip()[:88]}")
            print(f"    -> use `available() > 0`: a closed HWCDC/USBCDC returns -1, and while(-1) is TRUE")
            bad += 1
print('  stream-guard gate: no unguarded available() loops.' if not bad
      else f'  stream-guard gate: {bad} problem(s).')
sys.exit(1 if bad else 0)
