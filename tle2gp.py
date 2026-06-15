#!/usr/bin/env python3
"""
tle2gp.py  --  Convert a TLE (two-line element set) into the individual
GP / OMM mean orbital elements that CardSat asks for on its manual-entry
screen (and that AMSAT publishes as JSON).

CardSat consumes GP (General Perturbations / OMM) element sets. The fields
below are exactly the SGP4 mean elements encoded in a TLE, just decoded into
their human-readable GP form:

    EPOCH               UTC, ISO-8601
    MEAN_MOTION         revolutions / day
    ECCENTRICITY        dimensionless (0..1)
    INCLINATION         degrees
    RA_OF_ASC_NODE      degrees   (RAAN)
    ARG_OF_PERICENTER   degrees
    MEAN_ANOMALY        degrees
    BSTAR               1 / earth radii
    MEAN_MOTION_DOT     rev / day^2   (= 2 * TLE first derivative field)
    MEAN_MOTION_DDOT    rev / day^3   (= 6 * TLE second derivative field)
    NORAD_CAT_ID        integer
    ELEMENT_SET_NO      integer
    REV_AT_EPOCH        integer
    CLASSIFICATION_TYPE U / C / S
    EPHEMERIS_TYPE      integer (almost always 0)

No third-party libraries required.

Usage:
    python3 tle2gp.py "tle.txt"          # file with one or more TLEs
    python3 tle2gp.py                     # then paste 2 or 3 lines on stdin
    python3 tle2gp.py --json "tle.txt"    # emit AMSAT-style JSON records
"""

import sys
import json
import argparse
from datetime import datetime, timedelta, timezone


def _tle_epoch_to_iso(epoch_field: str) -> str:
    """Columns 19-32 of line 1: 'YYDDD.DDDDDDDD' -> ISO-8601 UTC."""
    yy = int(epoch_field[:2])
    year = 2000 + yy if yy < 57 else 1900 + yy   # standard TLE pivot
    day_of_year = float(epoch_field[2:])
    base = datetime(year, 1, 1, tzinfo=timezone.utc)
    dt = base + timedelta(days=day_of_year - 1.0)
    # round to whole seconds for CardSat's manual-entry format
    if dt.microsecond >= 500000:
        dt += timedelta(seconds=1)
    return dt.strftime("%Y-%m-%d %H:%M:%S")


def _decode_exp_field(field: str) -> float:
    """
    Decode a TLE implied-decimal exponential field such as ' 12345-3'
    (-> 0.12345e-3) used for BSTAR and the 2nd derivative of mean motion.
    """
    field = field.strip()
    if field in ("", "0", "+0", "-0", "00000-0", "00000+0"):
        return 0.0
    sign = 1.0
    if field[0] in "+-":
        if field[0] == "-":
            sign = -1.0
        field = field[1:]
    # mantissa and exponent: last sign char splits them
    for i in range(len(field) - 1, 0, -1):
        if field[i] in "+-":
            mant = field[:i]
            exp = int(field[i:])
            break
    else:
        mant, exp = field, 0
    mantissa = float("0." + mant)
    return sign * mantissa * (10.0 ** exp)


def parse_tle(line1: str, line2: str, name: str = "") -> dict:
    l1 = line1.rstrip("\n")
    l2 = line2.rstrip("\n")
    if not (l1.startswith("1 ") and l2.startswith("2 ")):
        raise ValueError("Lines must start with '1 ' and '2 ' (got "
                         f"{l1[:2]!r} / {l2[:2]!r})")

    norad = int(l1[2:7].replace(" ", "0").strip() or l1[2:7].strip())
    classification = l1[7].strip() or "U"
    epoch = _tle_epoch_to_iso(l1[18:32])

    # TLE line 1 stores n_dot/2 (rev/day^2) and n_ddot/6 (rev/day^3).
    # GP/OMM convention reports the full derivatives, so scale back up.
    ndot_half = float(l1[33:43])
    nddot = _decode_exp_field(l1[44:52])
    bstar = _decode_exp_field(l1[53:61])
    ephem_type = int(l1[62:63] or 0)
    elset_no = int(l1[64:68].strip() or 0)

    inclination = float(l2[8:16])
    raan = float(l2[17:25])
    eccentricity = float("0." + l2[26:33].strip())
    arg_perigee = float(l2[34:42])
    mean_anomaly = float(l2[43:51])
    mean_motion = float(l2[52:63])
    rev_at_epoch = int(l2[63:68].strip() or 0)

    return {
        "OBJECT_NAME": name.strip(),
        "NORAD_CAT_ID": norad,
        "CLASSIFICATION_TYPE": classification,
        "EPOCH": epoch,
        "MEAN_MOTION": mean_motion,                 # rev/day
        "ECCENTRICITY": eccentricity,               # dimensionless
        "INCLINATION": inclination,                 # deg
        "RA_OF_ASC_NODE": raan,                     # deg
        "ARG_OF_PERICENTER": arg_perigee,           # deg
        "MEAN_ANOMALY": mean_anomaly,               # deg
        "MEAN_MOTION_DOT": ndot_half * 2.0,         # rev/day^2
        "MEAN_MOTION_DDOT": nddot * 6.0,            # rev/day^3
        "BSTAR": bstar,                             # 1/earth-radii
        "EPHEMERIS_TYPE": ephem_type,
        "ELEMENT_SET_NO": elset_no,
        "REV_AT_EPOCH": rev_at_epoch,
        "REF_FRAME": "TEME",
        "TIME_SYSTEM": "UTC",
        "MEAN_ELEMENT_THEORY": "SGP4",
    }


def iter_tles(lines):
    """Yield (name, line1, line2) tuples from a list of text lines."""
    buf = []
    pending_name = ""
    for raw in lines:
        s = raw.rstrip("\n")
        if not s.strip():
            continue
        if s.startswith("1 ") and len(s) >= 60:
            buf = [s]
        elif s.startswith("2 ") and len(s) >= 60 and buf:
            yield pending_name, buf[0], s
            buf = []
            pending_name = ""
        else:
            # name / title line precedes a TLE pair
            pending_name = s.strip()
            buf = []


def _fixed(value, places):
    """Format a float as a plain decimal string (no scientific notation)."""
    return f"{value:.{places}f}"


def to_json(records):
    """Serialize GP records with fixed-point notation for the small fields."""
    formatted = []
    for gp in records:
        g = dict(gp)
        g["MEAN_MOTION"] = _fixed(g["MEAN_MOTION"], 8)
        g["ECCENTRICITY"] = _fixed(g["ECCENTRICITY"], 7)
        g["MEAN_MOTION_DOT"] = _fixed(g["MEAN_MOTION_DOT"], 12)
        g["MEAN_MOTION_DDOT"] = _fixed(g["MEAN_MOTION_DDOT"], 12)
        g["BSTAR"] = _fixed(g["BSTAR"], 10)
        formatted.append(g)
    payload = formatted if len(formatted) > 1 else formatted[0]
    # dump, then strip the quotes we added around the numeric strings so the
    # JSON stays numeric but never switches to scientific notation
    text = json.dumps(payload, indent=2)
    import re
    text = re.sub(r'"(-?\d+\.\d+)"', r'\1', text)
    return text


def print_human(gp: dict):
    name = gp["OBJECT_NAME"] or f"NORAD {gp['NORAD_CAT_ID']}"
    print(f"=== {name} ===")
    rows = [
        ("EPOCH (UTC)",        gp["EPOCH"]),
        ("INCLINATION (deg)",  f"{gp['INCLINATION']:.4f}"),
        ("RA_OF_ASC_NODE (deg)", f"{gp['RA_OF_ASC_NODE']:.4f}"),
        ("ECCENTRICITY",       f"{gp['ECCENTRICITY']:.7f}"),
        ("ARG_OF_PERICENTER (deg)", f"{gp['ARG_OF_PERICENTER']:.4f}"),
        ("MEAN_ANOMALY (deg)", f"{gp['MEAN_ANOMALY']:.4f}"),
        ("MEAN_MOTION (rev/day)", f"{gp['MEAN_MOTION']:.8f}"),
        ("MEAN_MOTION_DOT (rev/day^2)",  f"{gp['MEAN_MOTION_DOT']:.12f}"),
        ("MEAN_MOTION_DDOT (rev/day^3)", f"{gp['MEAN_MOTION_DDOT']:.12f}"),
        ("BSTAR (1/ER)",       f"{gp['BSTAR']:.10f}"),
        ("NORAD_CAT_ID",       gp["NORAD_CAT_ID"]),
        ("CLASSIFICATION",     gp["CLASSIFICATION_TYPE"]),
        ("ELEMENT_SET_NO",     gp["ELEMENT_SET_NO"]),
        ("REV_AT_EPOCH",       gp["REV_AT_EPOCH"]),
        ("EPHEMERIS_TYPE",     gp["EPHEMERIS_TYPE"]),
    ]
    width = max(len(k) for k, _ in rows)
    for k, v in rows:
        print(f"  {k:<{width}} : {v}")
    print()


def main():
    ap = argparse.ArgumentParser(description="Convert TLE -> GP/OMM elements for CardSat")
    ap.add_argument("file", nargs="?", help="text file containing TLE(s); omit to read stdin")
    ap.add_argument("--json", action="store_true", help="emit AMSAT-style JSON records")
    args = ap.parse_args()

    if args.file:
        with open(args.file, "r", encoding="utf-8") as fh:
            lines = fh.readlines()
    else:
        print("Paste TLE (2 or 3 lines), then Ctrl-D:", file=sys.stderr)
        lines = sys.stdin.readlines()

    records = [parse_tle(l1, l2, name) for name, l1, l2 in iter_tles(lines)]
    if not records:
        print("No valid TLE found.", file=sys.stderr)
        sys.exit(1)

    if args.json:
        print(to_json(records))
    else:
        for gp in records:
            print_human(gp)


if __name__ == "__main__":
    main()
