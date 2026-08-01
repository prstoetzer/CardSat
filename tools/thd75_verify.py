#!/usr/bin/env python3
"""
thd75_verify.py — validate CardSat's NEW TH-D75 command set on a Mac, BEFORE flashing.

The previous probe established that CardSat's old approach could never work: FO is a
valid query on this radio but its WRITE is always refused. The replacement is:

    session, once:   VM <band>,0        put the band in VFO mode
                     BC <band>          make it the CONTROL band
    per mode:        MD <band>,<code>   set mode
                     FT 0|1             fine mode  (SSB/CW only — FM refuses it)
                     FS 0..3            fine step  (0=20 Hz 1=100 2=500 3=1000)
    per frequency:   FQ <band>,<10 digits>          single frame, no round trip

This script exercises exactly that, on **band B** (the all-mode receiver — CardSat's
LEG_KWHT_BAND is '1'), and MEASURES the real frequency grid in each mode instead of
assuming it. The grid matters because the radio REFUSES an off-grid write rather than
rounding: it echoes the old frequency back.

    python3 -m pip install pyserial
    python3 thd75_verify.py                     # full sweep on band B
    python3 thd75_verify.py --band 0            # band A instead
    python3 thd75_verify.py --modes USB,FM      # just these
    python3 thd75_verify.py --doppler 60        # simulate a 60-step Doppler run

It saves the band's mode/frequency at the start and restores them at the end. Nothing
is transmitted; band B on a D75 is a receiver.
"""

import argparse
import glob
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not installed.  python3 -m pip install pyserial")

TERM = "\r"

# tables/mode.md
MODES = {"FM": "0", "DV": "1", "AM": "2", "LSB": "3", "USB": "4",
         "CW": "5", "NFM": "6", "DR": "7", "WFM": "8", "R-CW": "9"}
# tables/finestep.md
FINE_STEP_HZ = {"0": 20, "1": 100, "2": 500, "3": 1000}
# CardSat applies fine mode only in these (src/rig.cpp kwApplyStepForMode)
CARDSAT_FINE_MODES = {"LSB", "USB", "CW", "AM"}   # AM added on measurement

# Offsets probed to find the real grid, smallest first.
GRID_PROBES = [20, 100, 500, 1000, 5000, 6250, 10000]


def find_port():
    c = sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/tty.usbmodem*"))
    return c[0] if c else None


class Radio:
    def __init__(self, port, verbose):
        self.s = serial.Serial(port, 9600, timeout=0.5, write_timeout=2)
        self.s.dtr = True
        self.s.rts = True
        self.verbose = verbose
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def xchg(self, cmd, budget=1.2):
        self.s.reset_input_buffer()
        self.s.write((cmd + TERM).encode("ascii"))
        self.s.flush()
        t0, buf = time.time(), bytearray()
        while time.time() - t0 < budget:
            b = self.s.read(1)
            if not b:
                continue
            if b in (b"\r", b"\n"):
                if buf:
                    break
                continue
            buf += b
        rx = buf.decode("ascii", "replace")
        if self.verbose:
            print(f"      TX {cmd!r} -> {rx!r}")
        return rx

    def ok(self, cmd):
        """True when the radio ACCEPTS a set command ('N' is a Kenwood NAK)."""
        return self.xchg(cmd).strip() != "N"

    def freq(self, band):
        r = self.xchg(f"FQ {band}")
        try:
            return int(r.split(",")[1])
        except Exception:
            return -1

    def set_freq(self, band, hz):
        """Write via FQ and confirm by reading back. Returns the frequency actually set."""
        self.xchg(f"FQ {band},{hz:010d}")
        return self.freq(band)

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def measure_grid(r, band, base):
    """Smallest offset the radio actually accepts = the usable Doppler resolution."""
    for off in GRID_PROBES:
        got = r.set_freq(band, base + off)
        r.set_freq(band, base)                      # back to the anchor each time
        if got == base + off:
            return off
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--band", default="1", choices=["0", "1"],
                    help="default 1 = band B, the all-mode receiver CardSat uses")
    ap.add_argument("--modes", default="FM,NFM,USB,LSB,CW,AM,DV",
                    help="comma list from " + ",".join(MODES))
    ap.add_argument("--freq", type=int, default=145900000,
                    help="anchor frequency for the sweep (default 145.900 MHz)")
    ap.add_argument("--doppler", type=int, default=0, metavar="N",
                    help="after the sweep, simulate N Doppler steps at the measured grid")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    port = a.port or find_port()
    if not port:
        sys.exit("No /dev/cu.usbmodem* found. Radio plugged in and on?")
    band = a.band
    print(f"port {port}   band {band} ({'B — all-mode RX' if band == '1' else 'A'})")
    print(f"CardSat sends: VM {band},0 / BC {band} once, then MD/FT/FS per mode, "
          f"FQ {band},<10 digits> per frequency\n")

    r = Radio(port, a.verbose)
    fails = 0
    try:
        print(f"identity: {r.xchg('ID')}")

        # ── save state ────────────────────────────────────────────────────────
        orig_fo = r.xchg(f"FO {band}")
        orig_bc = r.xchg("BC")
        orig_vm = r.xchg(f"VM {band}")
        orig_freq = r.freq(band)
        orig_mode = r.xchg(f"MD {band}")
        print(f"saved: {orig_fo}")
        print(f"       {orig_bc} | {orig_vm} | {orig_mode}\n")

        # ── 1. session preconditions ──────────────────────────────────────────
        print("1. session setup (CardSat sends these once per attached stream)")
        vm_ok = r.ok(f"VM {band},0")
        bc_ok = r.ok(f"BC {band}")
        print(f"   VM {band},0  (VFO mode)     {'accepted' if vm_ok else 'NAK'}")
        print(f"   BC {band}    (control band) {'accepted' if bc_ok else 'NAK'}")
        if not bc_ok:
            print("   !! without the control band every frequency write will be refused")
            fails += 1

        # ── 2. can we write at all on this band? ──────────────────────────────
        print(f"\n2. baseline write on band {band}")
        anchor = a.freq
        got = r.set_freq(band, anchor)
        if got == anchor:
            print(f"   FQ set to {anchor:,} -> OK")
        else:
            print(f"   FQ set to {anchor:,} -> got {got:,}")
            print("   (if this is off-grid for the current mode/step it will be refused;")
            print("    the per-mode sweep below measures the real grid)")

        # ── 3. per-mode sweep: fine mode, fine steps, real grid ───────────────
        print(f"\n3. per-mode sweep on band {band}")
        print(f"   {'mode':5} {'MD':>4} {'FT 1':>6} {'accepted FS':>14} {'grid':>9}   CardSat")
        print("   " + "-" * 62)
        results = {}
        for name in [m.strip().upper() for m in a.modes.split(",") if m.strip()]:
            if name not in MODES:
                print(f"   {name}: unknown mode, skipped")
                continue
            code = MODES[name]
            md_ok = r.ok(f"MD {band},{code}")
            if not md_ok:
                print(f"   {name:5} {'NAK':>4}  {'-':>5} {'-':>14} {'-':>9}   (mode refused)")
                results[name] = None
                continue

            ft_ok = r.ok("FT 1")                       # fine mode on?
            steps = []
            if ft_ok:
                for s in "0123":
                    if r.ok(f"FS {s}"):
                        steps.append(f"{FINE_STEP_HZ[s]}Hz")
                r.xchg("FS 0")                         # finest for the grid measurement
            else:
                r.xchg("FT 0")

            base = r.freq(band)
            if base < 0:
                base = anchor
            grid = measure_grid(r, band, base)
            gtxt = f"{grid} Hz" if grid else ">10k"
            cs = "fine" if name in CARDSAT_FINE_MODES else "5 kHz"
            print(f"   {name:5} {'ok':>4}  {'yes' if ft_ok else 'NAK':>5} "
                  f"{(','.join(steps) if steps else '-'):>14} {gtxt:>9}   {cs}")
            results[name] = (ft_ok, steps, grid)

        # ── 4. does CardSat's whitelist match the radio? ──────────────────────
        print("\n4. CardSat's fine-mode whitelist vs. what the radio accepts")
        mismatch = 0
        for name, res in results.items():
            if not res:
                continue
            ft_ok = res[0]
            expects = name in CARDSAT_FINE_MODES
            if ft_ok != expects:
                mismatch += 1
                print(f"   {name}: radio {'accepts' if ft_ok else 'refuses'} fine mode, "
                      f"CardSat {'applies' if expects else 'skips'} it  <-- MISMATCH")
        if not mismatch:
            print("   matches on every mode tested.")
        else:
            fails += 1
            print("   -> adjust CARDSAT_FINE_MODES / kwApplyStepForMode to match.")

        # ── 5. Doppler simulation at the measured grid ────────────────────────
        if a.doppler:
            print(f"\n5. Doppler simulation: {a.doppler} steps")
            r.ok(f"MD {band},{MODES['USB']}")
            r.ok("FT 1")
            r.ok("FS 0")
            g = measure_grid(r, band, r.freq(band)) or 5000
            print(f"   using the measured grid: {g} Hz")
            base = anchor
            good, t0 = 0, time.time()
            for i in range(a.doppler):
                # a realistic Doppler walk, rounded to the grid the way CardSat does
                hz = base + int(round((3000 * (1 - 2 * i / a.doppler)) / g)) * g
                if r.set_freq(band, hz) == hz:
                    good += 1
                elif a.verbose:
                    print(f"   step {i+1}: wanted {hz:,} refused")
            dt = time.time() - t0
            print(f"   {good}/{a.doppler} exact   {dt/a.doppler*1000:.0f} ms per step")
            if good != a.doppler:
                fails += 1
                print("   -> not reliable at this rate; CardSat's CAT cycle needs slowing")
            else:
                print("   -> the new command set is reliable for Doppler on this radio")

        # ── restore ───────────────────────────────────────────────────────────
        print("\nrestoring")
        try:
            r.ok(f"MD {band},{orig_mode.split(',')[1]}")
        except Exception:
            pass
        if orig_freq > 0:
            r.set_freq(band, orig_freq)
        if orig_bc.startswith("BC"):
            r.ok(f"BC {orig_bc.split()[1]}")
        print(f"   {r.xchg(f'FO {band}')}")

    finally:
        r.close()

    print("\n" + "=" * 68)
    print("WHAT TO DO WITH THIS")
    print("  BAND B NOTE   = this band refuses FM (code 0) and DV (code 1). CardSat")
    print("                  maps RM_FM and RM_DATA to NFM (6), which this band accepts.")
    print("  grid column   = the Doppler resolution actually achievable per mode.")
    print("                  Anything coarser than ~1 kHz on a linear bird is a problem;")
    print("                  on FM, 5 kHz is fine.")
    print("  MISMATCH rows = CardSat's mode whitelist disagrees with the radio and")
    print("                  should be corrected before flashing.")
    print("  Doppler run   = if that is not N/N, the radio is the limit, not the code.")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
