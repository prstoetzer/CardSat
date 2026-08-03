#!/usr/bin/env python3
"""
usb_probe.py — dump everything CardSat needs to know about a USB device, from a Mac.

Why: the IC-705 does not appear at all in CardSat's USB adapter list — not even as an
unrecognised device, and not when a powered hub is in the path. CardSat registers every
non-hub device it enumerates regardless of class, so an empty list means enumeration
itself is not completing. This script gathers the facts that decide why.

The single most important number is the **configuration descriptor total length**.
Arduino's prebuilt ESP-IDF sets CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE=256, and a
device whose configuration descriptor exceeds 256 bytes cannot be fetched during
enumeration — it simply never appears, with no error the application layer can see.
A CDC + USB Audio composite is exactly the shape that overruns it. For reference, the
TH-D75 (which works) reports 174 bytes.

`system_profiler` cannot report that number, so this script prefers libusb and falls
back to system_profiler for what it can still tell us.

    python3 -m pip install pyusb
    brew install libusb            # the native library pyusb drives
    python3 usb_probe.py           # everything it can see
    python3 usb_probe.py --icom    # just Icom devices
    python3 usb_probe.py --vid 0x0c26

No configuration is changed and nothing is written to the device: descriptors are read
from the cached configuration, and no interface is claimed.
"""

import argparse
import json
import subprocess
import sys

# CardSat / IDF limits worth checking every device against.
IDF_CONTROL_XFER_MAX = 256       # CONFIG_USB_HOST_CONTROL_TRANSFER_MAX_SIZE
ESP_USB_HOST_MAX_ENDPOINTS = 16  # per-device endpoint table in EspUsbHost

# Chips EspUsbHost recognises as vendor-class serial (isKnownVendorSerial).
KNOWN_VENDOR_SERIAL = {
    0x0403: ("FTDI", {0x6001, 0x6010, 0x6011, 0x6014, 0x6015}),
    0x10c4: ("CP210x", {0xea60, 0xea70, 0xea71}),
    0x1a86: ("CH34x", {0x5523, 0x55d3, 0x7522, 0x7523}),
    0x067b: ("Prolific", {0x2303, 0x23a3}),
}

CLASS_NAMES = {
    0x00: "(per-interface)", 0x01: "Audio", 0x02: "CDC control", 0x03: "HID",
    0x05: "Physical", 0x06: "Image", 0x07: "Printer", 0x08: "Mass storage",
    0x09: "Hub", 0x0a: "CDC data", 0x0b: "Smart card", 0x0d: "Content security",
    0x0e: "Video", 0x0f: "Personal healthcare", 0x10: "Audio/video",
    0xdc: "Diagnostic", 0xe0: "Wireless", 0xef: "Miscellaneous",
    0xfe: "Application specific", 0xff: "Vendor specific",
}

XFER_NAMES = {0: "control", 1: "isochronous", 2: "bulk", 3: "interrupt"}


def cname(c):
    return f"0x{c:02x} {CLASS_NAMES.get(c, '?')}"


# ---------------------------------------------------------------- libusb path
def find_libusb():
    """Locate libusb-1.0 ourselves.

    pyusb reports "No backend available" whenever it cannot find the native library,
    which on Apple Silicon is the normal case even after `brew install libusb`:
    Homebrew installs under /opt/homebrew, and pyusb's search does not look there.
    Returns (path, backend) or (None, None).
    """
    import ctypes.util
    import glob
    import os

    candidates = []
    # Whatever Homebrew itself says, if it is on PATH -- correct on both architectures.
    for prefix_cmd in (["brew", "--prefix", "libusb"], ["brew", "--prefix"]):
        try:
            pre = subprocess.run(prefix_cmd, capture_output=True, text=True,
                                 timeout=15).stdout.strip()
            if pre:
                candidates += glob.glob(os.path.join(pre, "lib", "libusb-1.0*.dylib"))
        except Exception:
            pass
    # The usual locations, Apple Silicon first, then Intel, then Linux.
    for pat in ("/opt/homebrew/lib/libusb-1.0*.dylib",
                "/opt/homebrew/opt/libusb/lib/libusb-1.0*.dylib",
                "/usr/local/lib/libusb-1.0*.dylib",
                "/usr/local/opt/libusb/lib/libusb-1.0*.dylib",
                "/opt/local/lib/libusb-1.0*.dylib",           # MacPorts
                "/usr/lib/*/libusb-1.0.so*", "/usr/lib/libusb-1.0.so*"):
        candidates += sorted(glob.glob(pat))
    found = ctypes.util.find_library("usb-1.0")
    if found:
        candidates.append(found)

    seen = set()
    for path in [c for c in candidates if not (c in seen or seen.add(c))]:
        try:
            import usb.backend.libusb1
            # NOTE the signature: pyusb calls find_library(name) with one positional
            # argument. A zero-argument lambda raises TypeError, which get_backend()
            # swallows and reports as the generic "No backend available" -- the exact
            # misleading message this function exists to avoid.
            backend = usb.backend.libusb1.get_backend(
                find_library=lambda _name, _p=path: _p)
            if backend is not None:
                return path, backend
        except Exception:
            continue
    return None, None


def dump_libusb(match):
    try:
        import usb.core
        import usb.util
    except ImportError:
        return None, "pyusb not installed  (python3 -m pip install pyusb)"

    path, backend = find_libusb()
    if backend is None:
        return None, ("no libusb backend found.\n"
                      "     Install it:            brew install libusb\n"
                      "     If already installed, this is almost always an ARCHITECTURE\n"
                      "     MISMATCH -- an x86_64 Python cannot load an arm64 libusb.\n"
                      "     Check both:            python3 -c 'import platform;"
                      " print(platform.machine())'\n"
                      "                            file $(brew --prefix libusb)/lib/libusb-1.0.dylib\n"
                      "     If they differ, use the Homebrew python3 (/opt/homebrew/bin/python3)\n"
                      "     or reinstall libusb for your Python's architecture.")
    print(f"  using libusb: {path}\n")

    try:
        devices = list(usb.core.find(find_all=True, backend=backend))
    except Exception as e:
        return None, f"libusb found at {path} but enumeration failed: {e}"

    out = []
    for dev in devices:
        if not match(dev.idVendor, dev.idProduct):
            continue
        info = {
            "vid": dev.idVendor, "pid": dev.idProduct,
            "class": dev.bDeviceClass, "subclass": dev.bDeviceSubClass,
            "protocol": dev.bDeviceProtocol,
            "ep0_size": dev.bMaxPacketSize0,
            "num_configs": dev.bNumConfigurations,
            "usb_ver": f"{dev.bcdUSB >> 8:x}.{(dev.bcdUSB >> 4) & 0xf:x}",
            "speed": getattr(dev, "speed", None),
            "configs": [],
        }
        for name in ("manufacturer", "product", "serial_number"):
            try:
                info[name] = usb.util.get_string(dev, getattr(dev, f"i{ 'Serial' if name=='serial_number' else name.capitalize() }", 0)) or ""
            except Exception:
                info[name] = "(unreadable)"
        for cfg in dev:
            c = {
                "value": cfg.bConfigurationValue,
                "total_length": cfg.wTotalLength,      # THE number that matters
                "num_interfaces": cfg.bNumInterfaces,
                "max_power_ma": cfg.bMaxPower * 2,
                "self_powered": bool(cfg.bmAttributes & 0x40),
                "interfaces": [],
            }
            for intf in cfg:
                c["interfaces"].append({
                    "number": intf.bInterfaceNumber,
                    "alt": intf.bAlternateSetting,
                    "class": intf.bInterfaceClass,
                    "subclass": intf.bInterfaceSubClass,
                    "protocol": intf.bInterfaceProtocol,
                    "endpoints": [{
                        "address": ep.bEndpointAddress,
                        "attributes": ep.bmAttributes,
                        "max_packet": ep.wMaxPacketSize,
                        "interval": ep.bInterval,
                    } for ep in intf],
                })
            info["configs"].append(c)
        out.append(info)
    return out, None


def print_device(d):
    vid, pid = d["vid"], d["pid"]
    print("=" * 74)
    print(f"  {vid:04x}:{pid:04x}   {d.get('manufacturer','')} {d.get('product','')}".rstrip())
    print("=" * 74)
    print(f"  serial            {d.get('serial_number','') or '(none)'}")
    print(f"  USB version       {d['usb_ver']}    EP0 max packet {d['ep0_size']}")
    print(f"  device class      {cname(d['class'])}  sub 0x{d['subclass']:02x}  proto 0x{d['protocol']:02x}")
    print(f"  configurations    {d['num_configs']}")

    for c in d["configs"]:
        over = c["total_length"] > IDF_CONTROL_XFER_MAX
        flag = "   <<< EXCEEDS THE IDF 256-BYTE LIMIT" if over else ""
        print()
        print(f"  --- configuration {c['value']} ---")
        print(f"  CONFIG DESCRIPTOR TOTAL LENGTH   {c['total_length']} bytes{flag}")
        print(f"  interfaces        {c['num_interfaces']}")
        print(f"  power             {'self-powered' if c['self_powered'] else 'bus-powered'}, "
              f"requests {c['max_power_ma']} mA")
        n_ep = 0
        for i in c["interfaces"]:
            n_ep += len(i["endpoints"])
            print(f"    iface {i['number']} alt {i['alt']}   class {cname(i['class'])} "
                  f"sub 0x{i['subclass']:02x} proto 0x{i['protocol']:02x}   "
                  f"{len(i['endpoints'])} endpoint(s)")
            for ep in i["endpoints"]:
                a = ep["address"]
                print(f"        ep 0x{a:02x} {'IN ' if a & 0x80 else 'OUT'}  "
                      f"{XFER_NAMES.get(ep['attributes'] & 3, '?'):11s} "
                      f"max {ep['max_packet']:4d}  interval {ep['interval']}")
        print(f"    total endpoints across all alt settings: {n_ep}"
              f"{'   <<< over EspUsbHost table of 16' if n_ep > ESP_USB_HOST_MAX_ENDPOINTS else ''}")
    verdict(d)


def verdict(d):
    print()
    print("  ---- what this means for CardSat ----")
    said = False
    for c in d["configs"]:
        if c["total_length"] > IDF_CONTROL_XFER_MAX:
            said = True
            print(f"  * Configuration descriptor is {c['total_length']} bytes, over the "
                  f"{IDF_CONTROL_XFER_MAX}-byte limit")
            print("    compiled into Arduino's prebuilt ESP-IDF. The descriptor cannot be")
            print("    fetched during enumeration, so the device never appears at all --")
            print("    which matches 'no adapters found'. This is an IDF build-configuration")
            print("    limit, NOT a CardSat bug, and cannot be raised without building a")
            print("    custom IDF. For reference the TH-D75, which works, reports 174 bytes.")
    for c in d["configs"]:
        n_ep = sum(len(i["endpoints"]) for i in c["interfaces"])
        if n_ep > ESP_USB_HOST_MAX_ENDPOINTS:
            said = True
            print(f"  * {n_ep} endpoints exceeds EspUsbHost's per-device table of "
                  f"{ESP_USB_HOST_MAX_ENDPOINTS}.")

    vid, pid = d["vid"], d["pid"]
    classes = {i["class"] for c in d["configs"] for i in c["interfaces"]}
    if 0x02 in classes or 0x0a in classes:
        print("  * Presents CDC-ACM, which is the transport CardSat drives directly.")
    elif 0xff in classes:
        name, pids = KNOWN_VENDOR_SERIAL.get(vid, (None, set()))
        if name and pid in pids:
            print(f"  * Vendor-class serial recognised as {name}.")
        else:
            said = True
            print(f"  * Vendor-class (0xff) but {vid:04x}:{pid:04x} is NOT in EspUsbHost's")
            print("    known vendor-serial table (FTDI 0403, SiLabs 10c4, CH34x 1a86,")
            print("    Prolific 067b). It would enumerate and be listed, but CAT would not")
            print("    work without adding support for its chip.")
    if not said:
        print("  * Nothing here explains a failure to enumerate. If CardSat still shows no")
        print("    devices, the problem is on the host side, not in this descriptor.")


# ------------------------------------------------------- system_profiler path
def dump_profiler(match):
    try:
        raw = subprocess.run(["system_profiler", "-json", "SPUSBDataType"],
                             capture_output=True, text=True, timeout=60).stdout
        data = json.loads(raw)
    except Exception as e:
        return None, f"system_profiler failed: {e}"

    found = []

    def walk(items):
        for it in items or []:
            vid = it.get("vendor_id", "")
            pid = it.get("product_id", "")
            try:
                v = int(vid.split()[0], 16)
                p = int(pid.split()[0], 16)
            except Exception:
                v = p = -1
            if v >= 0 and match(v, p):
                found.append((v, p, it))
            walk(it.get("_items"))

    walk(data.get("SPUSBDataType"))
    return found, None


def print_profiler(found):
    for v, p, it in found:
        print("=" * 74)
        print(f"  {v:04x}:{p:04x}   {it.get('_name','')}")
        print("=" * 74)
        for k in ("manufacturer", "bcd_device", "speed", "location_id",
                  "serial_num", "bus_power", "bus_power_used",
                  "extra_operating_current"):
            if k in it:
                print(f"  {k:24s} {it[k]}")
        print()
        print("  NOTE: system_profiler cannot report the configuration descriptor length,")
        print("  which is the number that decides this. Install libusb and pyusb and")
        print("  re-run for the full picture:  brew install libusb && pip3 install pyusb")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--vid", help="only this vendor id, e.g. 0x0c26")
    ap.add_argument("--pid", help="only this product id")
    ap.add_argument("--icom", action="store_true", help="shorthand for --vid 0x0c26")
    ap.add_argument("--all", action="store_true", help="every device, including hubs")
    a = ap.parse_args()

    want_vid = 0x0c26 if a.icom else (int(a.vid, 0) if a.vid else None)
    want_pid = int(a.pid, 0) if a.pid else None

    def match(v, p):
        if want_vid is not None and v != want_vid:
            return False
        if want_pid is not None and p != want_pid:
            return False
        return True

    print("CardSat USB probe — descriptor dump for diagnosing enumeration failures")
    print(f"(IDF control-transfer limit {IDF_CONTROL_XFER_MAX} bytes; "
          f"TH-D75 reference = 174 bytes)\n")

    devs, err = dump_libusb(match)
    if devs is not None:
        if not devs:
            print("  No matching device. Is the radio plugged in and powered on?")
            print("  Run without --icom/--vid to list everything.")
            return 1
        for d in devs:
            print_device(d)
            print()
        return 0

    print(f"  libusb unavailable — {err}")
    print("  Falling back to system_profiler, which gives less.\n")
    found, err2 = dump_profiler(match)
    if err2:
        print(f"  {err2}")
        return 1
    if not found:
        print("  No matching device found by system_profiler either.")
        return 1
    print_profiler(found)
    return 0


if __name__ == "__main__":
    sys.exit(main())
