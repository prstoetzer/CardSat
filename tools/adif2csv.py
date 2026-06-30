#!/usr/bin/env python3
"""
adif2csv.py  --  Convert an ADIF log into the CSV format CardSat stores on its
device (/CardSat/qso_log.csv), keeping only satellite QSOs and only the fields
CardSat actually uses.

CardSat logs each QSO as one CSV line with these 13 columns, in this order:

    utc,call,sat,mode,dlHz,ulHz,rstS,rstR,myGrid,grid,myCall,notes,uploaded

      utc       Unix epoch seconds (UTC) of the QSO start
      call      worked station callsign
      sat       satellite name (ADIF SAT_NAME)
      mode      mode (SSB/CW/FM/...)
      dlHz      downlink (receive) frequency in Hz   (ADIF FREQ_RX, MHz -> Hz)
      ulHz      uplink (transmit) frequency in Hz    (ADIF FREQ,    MHz -> Hz)
      rstS      RST sent
      rstR      RST received
      myGrid    your Maidenhead grid (ADIF MY_GRIDSQUARE)
      grid      worked station's grid (ADIF GRIDSQUARE)
      myCall    your station callsign (ADIF STATION_CALLSIGN)
      notes     free text (ADIF COMMENT, else NOTES)
      uploaded  upload flags bitfield: bit0 = LoTW, bit1 = Cloudlog

Only QSOs whose ADIF PROP_MODE is SAT are written; everything else (terrestrial
HF/VHF contacts, meteor scatter, etc.) is dropped. Any ADIF field CardSat does
not use is ignored. Callsigns and grids are upper-cased to match CardSat's own
entry rule, and commas/newlines in free text are replaced with spaces so they
cannot break the CSV.

Usage:
    python3 adif2csv.py input.adi               # writes qso_log.csv next to it
    python3 adif2csv.py input.adi out.csv       # explicit output path
    python3 adif2csv.py input.adi -             # write to stdout

Then copy the resulting qso_log.csv onto the device's LittleFS at
/CardSat/qso_log.csv (see the manual for transferring files).

Note on awards: CardSat derives Worked-States and DXCC counts from each QSO's
grid square by geographic calculation, not from any STATE/DXCC field in your
ADIF. Records without a grid contribute to the QSO/sat/band tallies but cannot
be placed in a state or DXCC entity. This converter does not read or emit those
fields.
"""

import sys
import re
import os
from datetime import datetime, timezone


def parse_adif(text):
    """Yield one dict per ADIF record (the fields after the header, up to <EOR>).

    ADIF fields look like <NAME:LENGTH[:TYPE]>value, case-insensitive on the
    name, and records are separated by <EOR>. The header (everything before the
    first <EOH>, if present) is skipped.
    """
    # Drop the header if an <EOH> marker is present.
    m = re.search(r'<eoh>', text, re.IGNORECASE)
    if m:
        text = text[m.end():]

    field_re = re.compile(r'<([A-Za-z0-9_]+):(\d+)(?::[^>]*)?>', re.IGNORECASE)
    rec = {}
    pos = 0
    while pos < len(text):
        # End-of-record?
        eor = re.match(r'\s*<eor>', text[pos:], re.IGNORECASE)
        if eor:
            if rec:
                yield rec
                rec = {}
            pos += eor.end()
            continue
        fm = field_re.search(text, pos)
        if not fm:
            break
        name = fm.group(1).upper()
        length = int(fm.group(2))
        vstart = fm.end()
        value = text[vstart:vstart + length]
        rec[name] = value
        pos = vstart + length
    if rec:
        yield rec


def mhz_to_hz(s):
    """ADIF FREQ is MHz (a decimal string). Return integer Hz, or 0 if absent."""
    if not s:
        return 0
    try:
        return int(round(float(s) * 1_000_000))
    except ValueError:
        return 0


def to_epoch(qso_date, time_on):
    """ADIF QSO_DATE is YYYYMMDD and TIME_ON is HHMM or HHMMSS, both UTC."""
    if not qso_date or len(qso_date) < 8:
        return 0
    t = (time_on or "0000").strip()
    if len(t) < 4:
        t = t.ljust(4, "0")
    hh = int(t[0:2])
    mm = int(t[2:4])
    ss = int(t[4:6]) if len(t) >= 6 else 0
    try:
        dt = datetime(int(qso_date[0:4]), int(qso_date[4:6]), int(qso_date[6:8]),
                      hh, mm, ss, tzinfo=timezone.utc)
    except ValueError:
        return 0
    return int(dt.timestamp())


def clean(s):
    """Strip commas/newlines that would corrupt the CSV; collapse whitespace."""
    if not s:
        return ""
    s = s.replace(",", " ").replace("\n", " ").replace("\r", " ")
    return " ".join(s.split())


def convert(records):
    """Turn parsed ADIF records into CardSat CSV lines (satellite QSOs only)."""
    out = []
    kept = 0
    skipped_nonsat = 0
    for r in records:
        # Satellite QSOs only. ADIF marks these with PROP_MODE = SAT. (A SAT_NAME
        # alone is not sufficient/required, but if PROP_MODE is missing and a
        # SAT_NAME is present we treat it as satellite to be forgiving of loggers
        # that omit PROP_MODE.)
        prop = (r.get("PROP_MODE", "") or "").strip().upper()
        if prop:
            if prop != "SAT":
                skipped_nonsat += 1
                continue
        elif not (r.get("SAT_NAME", "") or "").strip():
            skipped_nonsat += 1
            continue

        utc = to_epoch(r.get("QSO_DATE", ""), r.get("TIME_ON", "") or r.get("TIME_OFF", ""))
        call = clean(r.get("CALL", "")).upper()
        sat = clean(r.get("SAT_NAME", ""))
        mode = clean(r.get("MODE", "")).upper()
        dl = mhz_to_hz(r.get("FREQ_RX", ""))
        ul = mhz_to_hz(r.get("FREQ", ""))
        rst_s = clean(r.get("RST_SENT", ""))
        rst_r = clean(r.get("RST_RCVD", ""))
        my_grid = clean(r.get("MY_GRIDSQUARE", "")).upper()
        grid = clean(r.get("GRIDSQUARE", "")).upper()
        my_call = clean(r.get("STATION_CALLSIGN", "") or r.get("OPERATOR", "")).upper()
        notes = clean(r.get("COMMENT", "") or r.get("NOTES", ""))

        # uploaded flags: bit0 LoTW, bit1 Cloudlog. Map LoTW QSL state if present;
        # leave Cloudlog unset. (Y/uppercase => uploaded.)
        uploaded = 0
        if (r.get("LOTW_QSL_SENT", "") or "").strip().upper() in ("Y", "U", "C"):
            uploaded |= 1

        out.append("%d,%s,%s,%s,%d,%d,%s,%s,%s,%s,%s,%s,%d" % (
            utc, call, sat, mode, dl, ul, rst_s, rst_r,
            my_grid, grid, my_call, notes, uploaded))
        kept += 1
    return out, kept, skipped_nonsat


def main(argv):
    if len(argv) < 2 or argv[1] in ("-h", "--help"):
        print(__doc__)
        return 0
    in_path = argv[1]
    if len(argv) >= 3:
        out_path = argv[2]
    else:
        d = os.path.dirname(os.path.abspath(in_path))
        out_path = os.path.join(d, "qso_log.csv")

    try:
        with open(in_path, "r", encoding="utf-8", errors="replace") as f:
            text = f.read()
    except OSError as e:
        print("Cannot read %s: %s" % (in_path, e), file=sys.stderr)
        return 1

    lines, kept, skipped = convert(parse_adif(text))

    body = "\n".join(lines) + ("\n" if lines else "")
    if out_path == "-":
        sys.stdout.write(body)
    else:
        with open(out_path, "w", encoding="utf-8", newline="") as f:
            f.write(body)
        print("Wrote %d satellite QSO(s) to %s" % (kept, out_path), file=sys.stderr)
        print("Skipped %d non-satellite/!SAT record(s)." % skipped, file=sys.stderr)
        if kept:
            print("Copy it to the device at /CardSat/qso_log.csv.", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
