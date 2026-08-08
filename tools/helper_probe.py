#!/usr/bin/env python3
"""helper_probe.py -- talk CSUH to a CardSatUsbHelper Stick from a Mac/PC.

WHY THIS EXISTS
---------------
When the helper misbehaves on the bench there are three things it could be: the
Stick's firmware, the Grove wiring, or CardSat's client. With CardSat in the loop
those are indistinguishable -- a silent link looks the same as a reversed Grove
pair, a wrong link baud, or a radio that has gone deaf. This script speaks the
protocol directly, so the Stick can be exercised with CardSat removed from the
picture entirely. Same role `thd75_probe.py` plays for the radio.

WIRING
------
A USB<->serial adapter (3.3 V!) to the Stick's Grove pins:

    adapter TX  -> Stick GPIO9  (Grove RX)
    adapter RX  <- Stick GPIO10 (Grove TX)
    adapter GND -- Stick GND

Do NOT connect the adapter's 5 V/VCC to the Grove power pin. The Stick's Grove
rail is 5 V INPUT and the firmware forces EXT_5V to input, but there is no reason
to feed it here -- power the Stick from its own USB-C.

If nothing is ever received, swap TX and RX. A reversed pair is SILENT, not
noisy, so it looks exactly like a dead cable or a wrong baud.

USAGE
-----
    ./helper_probe.py --port /dev/cu.usbserial-XXXX             # auto-baud + status
    ./helper_probe.py --port ... --baud 230400 --enum
    ./helper_probe.py --port ... --open '' --cat-baud 19200 --civ A4
    ./helper_probe.py --port ... --monitor                       # dump all frames
    ./helper_probe.py --port ... --stats
    ./helper_probe.py --port ... --rescan                        # reboot the helper

Requires pyserial (`pip3 install pyserial`).

The protocol constants below MUST match src/csuh_proto.h. They are duplicated
rather than parsed out of the header because this script has to run on a machine
with no checkout of the firmware -- and because a probe that silently followed a
changed header would stop being an independent check. tools/check_csuh_parity.py
does not police this file; if the protocol version bumps, update PROTO_VER here
and re-read the frame table.
"""

import argparse
import sys
import time

try:
    import serial
except ImportError:
    sys.exit("pyserial not found:  pip3 install pyserial")

# ---- protocol (mirror of src/csuh_proto.h) --------------------------------
PROTO_VER = 1
MAX_PAYLOAD = 128
MAX_FRAME = 2 + MAX_PAYLOAD + 2
CREDIT_INIT = 8
BAUDS = [230400, 115200, 460800]

T_HELLO_REQ, T_ENUM_REQ, T_OPEN, T_CLOSE, T_DATA_OUT = 0x01, 0x02, 0x03, 0x04, 0x05
T_MODEM, T_PING, T_CREDIT_OUT, T_STAT_REQ, T_RESCAN = 0x06, 0x07, 0x08, 0x09, 0x0A
T_LINE, T_WAKE = 0x0B, 0x0C

T_HELLO, T_ENUM, T_OPENED, T_DATA_IN = 0x81, 0x82, 0x83, 0x84
T_EVENT, T_PONG, T_CREDIT_IN, T_STAT = 0x85, 0x86, 0x87, 0x88

RX_NAME = {T_HELLO: "HELLO", T_ENUM: "ENUM", T_OPENED: "OPENED", T_DATA_IN: "DATA",
           T_EVENT: "EVENT", T_PONG: "PONG", T_CREDIT_IN: "CREDIT", T_STAT: "STAT"}

ERR_NAME = {0: "ok", 1: "no such device", 2: "ambiguous - nominate one",
            3: "busy", 4: "USB host not running", 5: "bad arguments",
            6: "device has no serial port"}

EV_NAME = {1: "attach", 2: "detach", 3: "port lost", 4: "usb error",
           5: "OVERRUN", 6: "usb host down", 7: "re-bound", 8: "restarting"}

STAT_FIELDS = [("frames rx", 0), ("frames tx", 4), ("crc err", 8), ("cobs err", 12),
               ("usb rx", 16), ("usb tx", 20), ("OVERRUN", 24),
               ("heap", 28), ("uptime s", 32)]
# 0.9.73 trailing extension (single bytes; absent from pre-extension firmware)
STAT_EXT = [("devs seen", 36), ("usable", 37), ("usb host up", 38)]


def crc16(d: bytes) -> int:
    c = 0xFFFF
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else (c << 1) & 0xFFFF
    return c


def cobs_encode(src: bytes) -> bytes:
    out = bytearray([0])
    code_at, code = 0, 1
    n = len(src)
    for i, b in enumerate(src):
        if b == 0:
            out[code_at] = code
            code_at, code = len(out), 1
            out.append(0)
        else:
            out.append(b)
            code += 1
            # Only open a new block if input REMAINS. Without the `i + 1 < n`
            # guard, a run of exactly 254 non-zero bytes ending at the input end
            # emits a trailing empty block -- which still decodes, but is not what
            # csuh_proto.h produces, so a byte comparison against the firmware
            # would disagree. (Unreachable at CSUH frame sizes; caught by the
            # cross-check against the same reference vectors the C++ test uses,
            # and fixed rather than left as a latent difference.)
            if code == 0xFF and i + 1 < n:
                out[code_at] = code
                code_at, code = len(out), 1
                out.append(0)
    out[code_at] = code
    return bytes(out)


def cobs_decode(src: bytes):
    out, i = bytearray(), 0
    while i < len(src):
        code = src[i]
        i += 1
        if code == 0:
            return None
        for _ in range(code - 1):
            if i >= len(src):
                return None
            out.append(src[i])
            i += 1
        if code != 0xFF and i < len(src):
            out.append(0)
    return bytes(out)


def build(ftype: int, payload: bytes = b"") -> bytes:
    raw = bytes([ftype, 0]) + payload
    raw += crc16(raw).to_bytes(2, "little")
    return cobs_encode(raw) + b"\x00"


def parse(block: bytes):
    raw = cobs_decode(block)
    if not raw or len(raw) < 4 or len(raw) > MAX_FRAME:
        return None
    if crc16(raw[:-2]) != int.from_bytes(raw[-2:], "little"):
        return None
    if raw[1] != 0:
        return None
    return raw[0], raw[2:-2]


class Link:
    def __init__(self, port, baud, verbose=False):
        self.ser = serial.Serial(port, baud, timeout=0.05)
        self.acc = bytearray()
        self.verbose = verbose
        self.credit_owed = 0

    def send(self, ftype, payload=b""):
        self.ser.write(build(ftype, payload))

    def poll(self, seconds=0.5):
        """Read for `seconds`, yielding (type, payload). Returns credit as it goes."""
        end = time.time() + seconds
        while time.time() < end:
            chunk = self.ser.read(256)
            if not chunk:
                continue
            for b in chunk:
                if b != 0:
                    if len(self.acc) < MAX_FRAME + 8:
                        self.acc.append(b)
                    continue
                if self.acc:
                    got = parse(bytes(self.acc))
                    self.acc.clear()
                    if got is None:
                        if self.verbose:
                            print("   [bad frame - crc/cobs]")
                        continue
                    ftype, payload = got
                    # Keep the helper's send window open, or it stops after 8 frames.
                    if ftype == T_DATA_IN:
                        self.credit_owed += 1
                        if self.credit_owed >= 4:
                            self.send(T_CREDIT_OUT, bytes([self.credit_owed]))
                            self.credit_owed = 0
                    yield ftype, payload
                else:
                    self.acc.clear()

    def drain_credit(self):
        if self.credit_owed:
            self.send(T_CREDIT_OUT, bytes([self.credit_owed]))
            self.credit_owed = 0


def show(ftype, payload, raw_data=False):
    name = RX_NAME.get(ftype, f"0x{ftype:02X}")
    if ftype == T_HELLO:
        ver = payload[0]
        epoch = int.from_bytes(payload[1:5], "little")
        fwlen = payload[7] if len(payload) > 7 else 0
        fw = payload[8:8 + fwlen].decode("ascii", "replace")
        warn = "" if ver == PROTO_VER else f"   *** expected v{PROTO_VER} ***"
        print(f"HELLO   proto v{ver}  epoch {epoch:#010x}  fw {fw}{warn}")
    elif ftype == T_ENUM:
        idx, cnt, flags = payload[0], payload[1], payload[2]
        if cnt == 0:
            print("ENUM    (no devices attached)")
            return
        o = 3
        klen = payload[o]; o += 1
        key = payload[o:o + klen].decode("ascii", "replace"); o += klen
        label = ""
        if o < len(payload):
            llen = payload[o]; o += 1
            label = payload[o:o + llen].decode("ascii", "replace")
        mark = "*" if flags & 0x02 else " "
        print(f"ENUM  {mark}[{idx + 1}/{cnt}] {label}   key={key}")
    elif ftype == T_OPENED:
        ok, err = payload[0], payload[1]
        nlen = payload[2] if len(payload) > 2 else 0
        nm = payload[3:3 + nlen].decode("ascii", "replace")
        print(f"OPENED  {'OK' if ok else 'FAILED'}  {nm}"
              f"{'' if ok else '  -- ' + ERR_NAME.get(err, str(err))}")
    elif ftype == T_EVENT:
        code = payload[0]
        dlen = payload[1] if len(payload) > 1 else 0
        det = payload[2:2 + dlen].decode("ascii", "replace")
        print(f"EVENT   {EV_NAME.get(code, code)}  {det}")
    elif ftype == T_STAT:
        print("STAT")
        for label, off in STAT_FIELDS:
            print(f"          {label:>11} : {int.from_bytes(payload[off:off + 4], 'little')}")
        for label, off in STAT_EXT:
            if off < len(payload):
                print(f"          {label:>11} : {payload[off]}")
    elif ftype == T_DATA_IN:
        if raw_data:
            print(f"DATA    {len(payload):3d} B  {payload.hex(' ')}")
        else:
            print(f"DATA    {len(payload):3d} B from the radio")
    elif ftype == T_CREDIT_IN:
        print(f"CREDIT  +{payload[0]}")
    elif ftype == T_PONG:
        print("PONG")
    else:
        print(f"{name}  {payload.hex(' ')}")


def autobaud(port, verbose):
    """Find the rate the helper has locked onto by asking at each candidate."""
    for baud in BAUDS:
        print(f"trying {baud} ...", end=" ", flush=True)
        link = Link(port, baud, verbose)
        # The helper scans CSUH_BAUD_TRY_MS per rate, so give it more than one
        # full sweep before deciding this rate is wrong.
        for _ in range(6):
            link.send(T_HELLO_REQ)
            for ftype, payload in link.poll(0.35):
                if ftype == T_HELLO:
                    print("linked")
                    show(ftype, payload)
                    return link
        print("no answer")
        link.ser.close()
    return None


def main():
    ap = argparse.ArgumentParser(description="CSUH probe for CardSatUsbHelper")
    ap.add_argument("--port", required=True, help="serial device on the Grove pins")
    ap.add_argument("--baud", type=int, help="skip auto-baud and use this rate")
    ap.add_argument("--enum", action="store_true", help="list devices on the Stick")
    ap.add_argument("--open", metavar="KEY",
                    help="open a device; '' = the only one attached")
    ap.add_argument("--cat-baud", type=int, default=19200, help="the RADIO's baud")
    ap.add_argument("--civ", metavar="HEX",
                    help="after opening, send a CI-V read-frequency to this address "
                         "(e.g. A4 for an IC-705) and show the reply")
    ap.add_argument("--send", metavar="HEX", help="send raw hex bytes to the device")
    ap.add_argument("--monitor", action="store_true", help="dump frames until Ctrl-C")
    ap.add_argument("--stats", action="store_true", help="request the counters")
    ap.add_argument("--rescan", action="store_true", help="reboot the helper")
    ap.add_argument("--raw", action="store_true", help="hex-dump DATA payloads")
    ap.add_argument("-v", "--verbose", action="store_true")
    a = ap.parse_args()

    if a.baud:
        link = Link(a.port, a.baud, a.verbose)
        link.send(T_HELLO_REQ)
        got = False
        for ftype, payload in link.poll(1.5):
            show(ftype, payload, a.raw)
            got = got or ftype == T_HELLO
        if not got:
            print(f"no HELLO at {a.baud}. Check the Grove pair (TX<->RX are easy to "
                  f"swap; a reversed pair is silent) and that the Stick is powered.")
    else:
        link = autobaud(a.port, a.verbose)
        if not link:
            print("\nNo answer at any supported rate. In order of likelihood:\n"
                  "  1. Grove TX/RX swapped (silent, looks like a dead cable)\n"
                  "  2. Stick not powered / not running CardSatUsbHelper\n"
                  "  3. GND not connected\n"
                  "  4. adapter is 5 V, not 3.3 V")
            return 1

    if a.rescan:
        print("\n-- rescan (the helper reboots; it is stateless, so this is safe)")
        link.send(T_RESCAN)
        for ftype, payload in link.poll(2.0):
            show(ftype, payload, a.raw)
        return 0

    if a.enum or a.open is not None:
        print("\n-- devices")
        link.send(T_ENUM_REQ)
        for ftype, payload in link.poll(1.0):
            show(ftype, payload, a.raw)

    if a.open is not None:
        print(f"\n-- open (radio baud {a.cat_baud})")
        key = a.open.encode("ascii")
        pay = (a.cat_baud.to_bytes(4, "little") + bytes([8, 0, 1, 1, 1, len(key)]) + key)
        link.send(T_OPEN, pay)
        for ftype, payload in link.poll(1.5):
            show(ftype, payload, a.raw)

    if a.civ:
        addr = int(a.civ, 16)
        # CI-V read operating frequency: FE FE <radio> E0 03 FD
        frame = bytes([0xFE, 0xFE, addr, 0xE0, 0x03, 0xFD])
        print(f"\n-- CI-V read-freq to {addr:#04x}: {frame.hex(' ')}")
        link.send(T_DATA_OUT, frame)
        for ftype, payload in link.poll(1.5):
            show(ftype, payload, True)
        link.drain_credit()

    if a.send:
        data = bytes.fromhex(a.send.replace(" ", ""))
        print(f"\n-- send {len(data)} B: {data.hex(' ')}")
        for i in range(0, len(data), MAX_PAYLOAD):
            link.send(T_DATA_OUT, data[i:i + MAX_PAYLOAD])
        for ftype, payload in link.poll(1.5):
            show(ftype, payload, True)
        link.drain_credit()

    if a.stats:
        print("\n-- counters")
        link.send(T_STAT_REQ)
        for ftype, payload in link.poll(1.0):
            show(ftype, payload, a.raw)

    if a.monitor:
        print("\n-- monitoring (Ctrl-C to stop)")
        last_ping = 0.0
        try:
            while True:
                for ftype, payload in link.poll(0.5):
                    show(ftype, payload, a.raw)
                if time.time() - last_ping > 3.0:
                    last_ping = time.time()
                    link.send(T_PING, b"\x00\x00")
        except KeyboardInterrupt:
            print("\nstopped")
    return 0


if __name__ == "__main__":
    sys.exit(main())
