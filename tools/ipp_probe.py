#!/usr/bin/env python3
# CardSat IPP probe -- run from a machine ON THE SAME LAN as the printer.
#   python3 ipp_probe.py 10.0.0.59
# It (1) asks the printer what it supports (Get-Printer-Attributes), then
# (2) sends a one-line PCL test page via IPP Print-Job. Prints the HTTP status.
import sys, struct, urllib.request

host = sys.argv[1] if len(sys.argv) > 1 else "10.0.0.59"
uri  = f"ipp://{host}/ipp/print"
http = f"http://{host}:631/ipp/print"

def attr(tag, name, value):
    return bytes([tag]) + struct.pack(">H", len(name)) + name.encode() \
         + struct.pack(">H", len(value)) + value.encode()

def post(body):
    req = urllib.request.Request(http, data=body,
            headers={"Content-Type": "application/ipp"})
    try:
        r = urllib.request.urlopen(req, timeout=8)
        return r.status, r.read()
    except Exception as e:
        return None, str(e).encode()

# 1) Get-Printer-Attributes (operation 0x000B) -- what does it support?
q  = struct.pack(">H", 0x0101) + struct.pack(">H", 0x000B) + struct.pack(">I", 1)
q += bytes([0x01])
q += attr(0x47, "attributes-charset", "utf-8")
q += attr(0x48, "attributes-natural-language", "en")
q += attr(0x45, "printer-uri", uri)
q += bytes([0x03])
st, data = post(q)
print(f"[query] Get-Printer-Attributes -> HTTP {st}")
if data and st:
    # crude scan for document-format-supported values in the response
    for fmt in [b"application/postscript", b"application/vnd.hp-PCL",
                b"application/octet-stream", b"image/pwg-raster",
                b"image/urf", b"text/plain"]:
        if fmt in data:
            print("   supports:", fmt.decode())

# 2) Print-Job (operation 0x0002) with a tiny PCL page
for docfmt, doc, label in [
    ("application/vnd.hp-PCL", b"\x1bE" + b"CardSat IPP PCL test\r\n\x0c", "PCL"),
    ("application/postscript",
     b"%!PS-Adobe-3.0\n/Courier findfont 12 scalefont setfont\n"
     b"72 700 moveto (CardSat IPP PostScript test) show showpage\n", "PostScript"),
    ("application/octet-stream", b"\x1bE" + b"CardSat IPP raw test\r\n\x0c", "octet-stream"),
]:
    body  = struct.pack(">H", 0x0101) + struct.pack(">H", 0x0002) + struct.pack(">I", 2)
    body += bytes([0x01])
    body += attr(0x47, "attributes-charset", "utf-8")
    body += attr(0x48, "attributes-natural-language", "en")
    body += attr(0x45, "printer-uri", uri)
    body += attr(0x42, "requesting-user-name", "cardsat")
    body += attr(0x42, "job-name", "CardSat test")
    body += attr(0x49, "document-format", docfmt)
    body += bytes([0x03])
    body += doc
    st, resp = post(body)
    print(f"[print] {label:12s} ({docfmt}) -> HTTP {st}")
    if st == 200:
        print(f"   >>> {label} job accepted -- check the printer for a page. <<<")
