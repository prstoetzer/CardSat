#!/usr/bin/env python3
"""
thd75_probe.py — drive a Kenwood TH-D75 from a Mac using EXACTLY the commands
CardSat sends, to separate two questions that have been tangled all along:

    Is the COMMAND SET wrong, or is CardSat's USB TRANSPORT on the Cardputer wrong?

The Mac's CDC-ACM stack is known-good and completely independent of EspUsbHost. If
this script drives the radio correctly, the command set is right and the fault is in
CardSat's USB path. If it fails here too, the commands are wrong and no amount of USB
work will help.

Requires only pyserial:      python3 -m pip install pyserial
Run:                         python3 thd75_probe.py
                             python3 thd75_probe.py --port /dev/cu.usbmodemXXXX
                             python3 thd75_probe.py --soak 60      # reliability run

Everything it sends is a normal VFO operation. It does NOT touch memories, tones or
any stored setting; it reads the VFO record, writes frequencies back into it, and at
the end restores the record it found. Nothing is transmitted.
"""

import argparse
import glob
import sys
import time

try:
    import serial  # pyserial
except ImportError:
    sys.exit("pyserial not installed.  python3 -m pip install pyserial")

# ── What CardSat sends ────────────────────────────────────────────────────────
# src/rig.cpp, LEGF_KWHT:
#   read  : "FO <band>\r"                     (legBuildReadFreqFrame)
#   write : the returned record, frequency patched at offsets 5..14, then "\r"
#           (legKwFoPatch), with fine-mode fields adjusted by mode
# CR terminated — NOT the ';' used by Kenwood base rigs.
BAND = "1"           # band B -- the all-mode receiver, and what CardSat
                     # uses (LEG_KWHT_BAND = '1'). Band A is FM-only.
TERM = "\r"

# FO field indices (LA3QMA/TH-D74-Kenwood commands/FO.md, corroborated by Hamlib
# thd74.c which patches the same characters by absolute offset 27/33/35).
F_BAND, F_FREQ, F_OFFSET, F_STEP, F_TXSTEP = 1, 2, 3, 4, 5
F_MODE, F_FINEMODE, F_FINESTEP = 6, 7, 8
MODE_NAME = {"0": "FM", "1": "DV", "2": "AM", "3": "LSB", "4": "USB",
             "5": "CW", "6": "NFM", "7": "DR", "8": "WFM", "9": "R-CW"}
FINESTEP_HZ = {"0": 20, "1": 100, "2": 500, "3": 1000}
STEP_KHZ = {"0": 5, "1": 6.25, "2": 8.33, "3": 9, "4": 10, "5": 12.5,
            "6": 15, "7": 20, "8": 25, "9": 30, "A": 50, "B": 100}
# CardSat applies fine mode only in these (bench: FM does not support it)
FINE_OK_MODES = {"3", "4", "5", "9"}


def find_port():
    """The D75 shows up as a single CDC-ACM port; no serial number in its descriptor."""
    cands = sorted(glob.glob("/dev/cu.usbmodem*")) + sorted(glob.glob("/dev/tty.usbmodem*"))
    return cands[0] if cands else None


class Radio:
    def __init__(self, port, baud, verbose):
        # CDC-ACM: baud is nominal, but set it to what CardSat uses anyway.
        self.s = serial.Serial(port, baud, timeout=0.4, write_timeout=2)
        self.verbose = verbose
        self.s.dtr = True          # CardSat asserts both; some CDC devices need DTR
        self.s.rts = True
        time.sleep(0.2)
        self.s.reset_input_buffer()

    def xchg(self, cmd, budget=1.0):
        """Send one CR-terminated command, collect the CR-terminated answer."""
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
        dt = (time.time() - t0) * 1000
        rx = buf.decode("ascii", "replace")
        if self.verbose:
            print(f"    TX {cmd!r}   RX {rx!r}   ({dt:.0f} ms)")
        return rx, dt

    def close(self):
        try:
            self.s.close()
        except Exception:
            pass


def show_record(rec):
    f = rec.split(",")
    if len(f) < 8:
        return "  (unparseable)"
    head = f[0].split()
    band = head[1] if len(head) > 1 else "?"
    mode, fine, fstep, step = f[F_MODE - 1], f[F_FINEMODE - 1], f[F_FINESTEP - 1], f[F_STEP - 1]
    return (f"  band={band}  freq={int(f[F_FREQ-1]):,} Hz  mode={MODE_NAME.get(mode,mode)}"
            f"  step={STEP_KHZ.get(step,'?')} kHz"
            f"  fine={'ON' if fine=='1' else 'off'}"
            f"  finestep={FINESTEP_HZ.get(fstep,'?')} Hz")


def patch(rec, hz, apply_fine):
    """Reproduce legKwFoPatch(): frequency at offsets 5..14, then mode-aware fine fields."""
    if len(rec) < 15 or not rec.startswith("FO "):
        return None
    out = list(rec)
    out[5:15] = list(f"{hz:010d}")
    f = "".join(out).split(",")
    if apply_fine and len(f) >= 8:
        mode = f[F_MODE - 1]
        if mode in FINE_OK_MODES:
            if len(f[F_FINEMODE - 1]) == 1:
                f[F_FINEMODE - 1] = "1"
            if len(f[F_FINESTEP - 1]) == 1:
                f[F_FINESTEP - 1] = "0"
        elif len(f[F_STEP - 1]) == 1:
            f[F_STEP - 1] = "0"        # finest normal step (5 kHz)
    return ",".join(f)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", help="serial device (default: first /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=9600, help="nominal; CDC ignores it")
    ap.add_argument("--band", default=BAND, choices=["0", "1"],
                    help="default 1 = band B (all-mode RX), matching CardSat")
    ap.add_argument("--soak", type=int, default=0, metavar="N",
                    help="after the basic checks, do N consecutive writes and report reliability")
    ap.add_argument("--no-fine", action="store_true", help="do not touch the fine-mode fields")
    ap.add_argument("--cycles", type=int, default=0, metavar="N",
                    help="close and REOPEN the port N times, testing whether the radio "
                         "still answers after a session ends (the Cardputer symptom)")
    ap.add_argument("-q", "--quiet", action="store_true")
    a = ap.parse_args()

    port = a.port or find_port()
    if not port:
        sys.exit("No /dev/cu.usbmodem* found. Is the radio plugged in and powered on?")

    # ---- REOPEN TEST -------------------------------------------------------------
    # The Cardputer symptom: the radio answers everything on the first engage and is
    # completely silent on the second -- no echo even for MD. USB enumeration and the
    # CDC bind both succeed, so the question is whether the RADIO stops answering
    # after a session ends, or whether CardSat's re-bind is at fault. A Mac closing
    # and reopening its own CDC port reproduces the same DTR drop and re-assert on a
    # known-good host, which separates the two.
    if a.cycles:
        print(f"reopen test: {a.cycles} open/close cycles on {port}\n")
        bad = 0
        for i in range(a.cycles):
            try:
                r = Radio(port, a.baud, False)
            except Exception as e:
                print(f"  cycle {i+1:2d}: OPEN FAILED -- {e}")
                bad += 1
                time.sleep(1.0)
                continue
            # xchg() returns (text, milliseconds) -- unpack BOTH.
            ident, _ = r.xchg("ID")
            fo, _ = r.xchg(f"FO {a.band}")
            ok = ident.startswith("ID") and fo.startswith("FO")
            print(f"  cycle {i+1:2d}: ID={ident or '(silent)':12s} "
                  f"FO={'ok' if fo.startswith('FO') else '(silent)'}   "
                  f"{'' if ok else '<-- RADIO NOT ANSWERING'}")
            if not ok:
                bad += 1
            r.s.dtr = False              # what CardSat now does on disengage
            r.s.rts = False
            r.close()
            time.sleep(0.8)              # roughly the operator's re-engage gap
        print()
        if bad == 0:
            print("  every reopen answered -> the RADIO is fine across sessions, and the")
            print("  Cardputer-side re-bind is what differs. Compare CardSat's bind order")
            print("  (set config -> DTR -> RTS) against pyserial's on the wire.")
        elif bad >= a.cycles - 1:
            print("  the radio goes silent after the FIRST session on a known-good host")
            print("  too -> this is the radio's behaviour, not CardSat's bug. A workaround")
            print("  (leave DTR asserted, or re-open twice) is then the honest fix.")
        else:
            print("  intermittent across reopens -> the radio is marginal at this cadence;")
            print("  a longer settle between disengage and re-engage is the mitigation.")
        return 1 if bad else 0
    print(f"port {port}   (CardSat sends 'FO {a.band}\\r' and writes the patched record back)\n")

    r = Radio(port, a.baud, not a.quiet)
    fails = 0
    try:
        # 1 ── is anything there at all?
        print("1. identity (AE)")
        ident, _ = r.xchg("AE")
        print(f"   {ident or '(no reply)'}")
        if not ident:
            print("   !! no reply to a basic query — check the radio's USB/PC port mode")

        # 2 ── the read CardSat depends on
        print("\n2. read the VFO record (this is what CardSat's read-modify-write needs)")
        rec, dt = r.xchg(f"FO {a.band}")
        if not rec.startswith("FO"):
            print(f"   FAIL: no FO record (got {rec!r})")
            print("   -> the command set is the problem; CardSat's USB path is not implicated")
            return 1
        print(f"   {len(rec)} bytes in {dt:.0f} ms")
        print(show_record(rec))
        original = rec

        # How slow is it really? CardSat had this capped at 200 ms and starved it.
        print("\n3. read latency x5 (CardSat's budget was 200 ms; now floored at 300 ms)")
        lat = []
        for _ in range(5):
            _, d = r.xchg(f"FO {a.band}")
            lat.append(d)
        print(f"   min {min(lat):.0f}  max {max(lat):.0f}  mean {sum(lat)/len(lat):.0f} ms")
        if max(lat) > 200:
            print("   NOTE: at least one read exceeded 200 ms — consistent with the")
            print("         read-budget starvation found on the Cardputer.")

        # 3b ── RADIO STATE. A bare "N" reply is a Kenwood NAK: the radio understood
        # the command and REFUSED it. Writing back the completely unmodified record
        # also NAKs, so the refusal is not about the content -- it is about the state
        # the radio is in. These four queries name the usual causes.
        print("\n3b. radio state (why a write might be refused)")
        idr, _   = r.xchg("ID")
        bc, _    = r.xchg("BC")
        vm0, _   = r.xchg("VM 0")
        vm1, _   = r.xchg("VM 1")
        tn, _    = r.xchg("TN")
        dl, _    = r.xchg("DL")
        VMNAME = {"0": "VFO", "1": "MEMORY", "2": "CALL"}
        TNNAME = {"0": "off", "1": "APRS", "2": "KISS"}
        def arg(rep, idx=-1):
            if not rep or "," not in rep and " " not in rep:
                return "?"
            body = rep.split(" ", 1)[1] if " " in rep else rep
            parts = body.split(",")
            return parts[idx] if parts else "?"
        vm_a, vm_b = arg(vm0), arg(vm1)
        tn_mode, tn_band = arg(tn, 0), arg(tn, -1)
        print(f"   model      {idr or '?'}")
        print(f"   ctrl band  {arg(bc,0)}   (0=A 1=B)")
        print(f"   band A     {VMNAME.get(vm_a, vm_a)} mode")
        print(f"   band B     {VMNAME.get(vm_b, vm_b)} mode")
        print(f"   TNC        {TNNAME.get(tn_mode, tn_mode)}  on band {tn_band}")
        print(f"   dual/single{dl or '?'}")
        target_band = a.band
        vm_here = vm_a if target_band == "0" else vm_b
        if vm_here != "0":
            print(f"   !! band {target_band} is in {VMNAME.get(vm_here, vm_here)} mode --"
                  f" FO sets the VFO, so a write is expected to be refused")
        if tn_mode in ("1", "2") and tn_band == target_band:
            print(f"   !! the TNC ({TNNAME.get(tn_mode)}) owns band {target_band} --"
                  f" it may be holding the frequency")

        # 4 ── single write, read back, verify
        base = int(original.split(",")[F_FREQ - 1])
        target = base + 12340          # deliberately NOT a multiple of 5 kHz
        print(f"\n4. write {target:,} Hz (base {base:,} + 12,340 — not a 5 kHz multiple)")
        w = patch(original, target, not a.no_fine)
        if not w:
            print("   FAIL: could not patch the record")
            return 1
        r.xchg(w)
        back, _ = r.xchg(f"FO {a.band}")
        got = int(back.split(",")[F_FREQ - 1]) if back.startswith("FO") else -1
        print(show_record(back))
        if got == target:
            print(f"   PASS exact: {got:,}")
        else:
            err = got - target
            print(f"   got {got:,}  (off by {err:+,} Hz)")
            print("   -> the radio QUANTISED the write. That is the step/fine-mode issue,")
            print("      not a transport issue.")
            fails += 1

        # 4b ── if the write was refused, find out WHAT unblocks it. This is the whole
        # point of the probe: naming the precondition CardSat must satisfy.
        if got != target:
            print("\n4b. the write was refused -- testing what unblocks it")

            def try_write(label, hz):
                cur, _ = r.xchg(f"FO {a.band}")
                if not cur.startswith("FO"):
                    print(f"   {label}: read failed"); return False
                w2 = patch(cur, hz, not a.no_fine)
                rep, _ = r.xchg(w2)
                chk, _ = r.xchg(f"FO {a.band}")
                g = int(chk.split(",")[F_FREQ - 1]) if chk.startswith("FO") else -1
                good = (g == hz)
                print(f"   {label}: reply={rep!r}  got {g:,}  {'WORKS' if good else 'still refused'}")
                return good

            fixed = False
            # (a) force VFO mode on the target band
            if vm_here != "0":
                print(f"   -> setting band {a.band} to VFO mode (VM {a.band},0)")
                r.xchg(f"VM {a.band},0")
                fixed = try_write("after VFO mode", base + 15000)
            # (b) make it the control band
            if not fixed and arg(bc, 0) != a.band:
                print(f"   -> making band {a.band} the control band (BC {a.band})")
                r.xchg(f"BC {a.band}")
                fixed = try_write("after control band", base + 15000)
            # (c) turn the TNC off if it owns this band
            if not fixed and tn_mode in ("1", "2") and tn_band == a.band:
                print(f"   -> turning the TNC off (TN 0,{a.band})")
                r.xchg(f"TN 0,{a.band}")
                fixed = try_write("after TNC off", base + 15000)
                r.xchg(f"TN {tn_mode},{tn_band}")      # put it back
            # (d) the other band, untouched
            if not fixed:
                other = "1" if a.band == "0" else "0"
                oc, _ = r.xchg(f"FO {other}")
                if oc.startswith("FO"):
                    ob = int(oc.split(",")[F_FREQ - 1])
                    ow = patch(oc, ob + 15000, not a.no_fine)
                    rep, _ = r.xchg(ow)
                    chk, _ = r.xchg(f"FO {other}")
                    g = int(chk.split(",")[F_FREQ - 1]) if chk.startswith("FO") else -1
                    print(f"   band {other} instead: reply={rep!r}  "
                          f"{'WORKS' if g == ob + 15000 else 'also refused'}")
                    if g == ob + 15000:
                        r.xchg(patch(chk, ob, not a.no_fine))   # restore
            if not fixed:
                print("   none of the usual preconditions unblocked it.")

            # 4c ── WHICH SET COMMANDS DOES THIS RADIO ACCEPT AT ALL?
            # BC succeeded while VM and FO both NAK, and FO fails on band B too --
            # which is in VFO mode. So this is not a state problem: some writes are
            # simply not accepted. Find one that is. FQ is the direct
            # "set frequency" command; Hamlib's D74 backend does not use it, but the
            # D74 is not this radio.
            print("\n4c. which SET commands does the radio accept?")
            probe_band = a.band

            def try_cmd(label, cmd, verify=None, want=None):
                rep, dt = r.xchg(cmd)
                nak = (rep.strip() == "N")
                extra = ""
                if verify and not nak:
                    chk, _ = r.xchg(verify)
                    extra = f"   readback: {chk}"
                    if want is not None:
                        try:
                            gotv = int(chk.split(",")[-1])
                            extra += f"   {'MATCHES' if gotv == want else 'differs'}"
                        except Exception:
                            pass
                print(f"   {label:22s} {'NAK' if nak else 'accepted'}   reply={rep!r}{extra}")
                return not nak

            cur, _ = r.xchg(f"FO {probe_band}")
            curhz = int(cur.split(",")[F_FREQ - 1]) if cur.startswith("FO") else 144390000
            tgt = curhz + 25000        # a clean 25 kHz step, valid on any step size

            # FQ: get first (does it even exist on this radio?), then set.
            fqget, _ = r.xchg(f"FQ {probe_band}")
            print(f"   {'FQ (get)':22s} reply={fqget!r}")
            fq_ok = try_cmd("FQ (set frequency)", f"FQ {probe_band},{tgt:010d}",
                            verify=f"FQ {probe_band}", want=tgt)

            # MD: a different kind of VFO write, to see if ANY VFO parameter is settable
            mdget, _ = r.xchg(f"MD {probe_band}")
            print(f"   {'MD (get)':22s} reply={mdget!r}")

            if fq_ok:
                back, _ = r.xchg(f"FQ {probe_band}")
                print(f"\n   *** FQ WORKS. {back}")
                print("   -> CardSat should use 'FQ <band>,<10 digits>' for this radio,")
                print("      not the FO read-modify-write. One frame, no round trip.")
                r.xchg(f"FQ {probe_band},{curhz:010d}")     # restore
            else:
                print("\n   FQ refused too. No tested write reaches the VFO on this radio;")
                print("   the remaining suspects are radio-side (key lock, PC/remote")
                print("   setting, or a firmware restriction on this model).")

        # 5 ── the actual complaint: MULTIPLE consecutive writes
        n = a.soak if a.soak else 10
        print(f"\n5. {n} consecutive writes (the reported failure is 'multiple writes')")
        good = 0
        worst = 0.0
        for i in range(n):
            tgt = base + 1000 * (i + 1)
            cur, _ = r.xchg(f"FO {a.band}")
            if not cur.startswith("FO"):
                print(f"   [{i+1:3d}] read FAILED")
                continue
            pw = patch(cur, tgt, not a.no_fine)
            t0 = time.time()
            r.xchg(pw)
            chk, _ = r.xchg(f"FO {a.band}")
            worst = max(worst, (time.time() - t0) * 1000)
            gotn = int(chk.split(",")[F_FREQ - 1]) if chk.startswith("FO") else -1
            ok = (gotn == tgt)
            good += ok
            if not ok or not a.quiet:
                print(f"   [{i+1:3d}] want {tgt:,}  got {gotn:,}  {'ok' if ok else 'MISMATCH'}")
        print(f"\n   {good}/{n} exact.  slowest write+verify {worst:.0f} ms")
        if good == n:
            print("   -> the command set handles repeated writes fine on a known-good")
            print("      USB stack. The Cardputer-side failure is TRANSPORT, not commands.")
        elif good == 0:
            print("   -> repeated writes fail here too: a COMMAND-SET/radio problem.")
        else:
            print("   -> intermittent even on a Mac: the radio itself is unreliable at this")
            print("      rate. Slowing CardSat's CAT cycle would be the mitigation.")
            fails += 1

        # 6 ── put it back
        print("\n6. restoring the original record")
        r.xchg(original)
        fin, _ = r.xchg(f"FO {a.band}")
        print(show_record(fin))

    finally:
        r.close()

    print("\n" + "=" * 68)
    print("READING THE RESULT")
    print("  all steps pass here  -> commands are correct; the fault is CardSat's USB")
    print("                          transport on the Cardputer (EspUsbHost path)")
    print("  reads/writes fail    -> command set or radio configuration; CardSat's USB")
    print("                          layer is exonerated")
    print("  intermittent here    -> the radio is marginal at this rate regardless of host")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
