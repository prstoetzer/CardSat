#!/usr/bin/env python3
"""st_inspect.py -- inspect what Space-Track actually returns, through the exact
same pipeline CardSat's Space-Track history tool uses.

Reproduces, step by step, what the firmware does:
  1. POST /ajaxauth/login (form), reject {"Login":"Failed"}, capture the
     chocolatechip session cookie.
  2. GET  /basicspacedata/query/class/gp_history/... with the cookie
     (same predicates, same EPOCH filter, same orderby, format/csv).
  3. Split the body on '\n' (showing any trailing '\r' the firmware would see).
  4. Apply the firmware's per-cell rules: an EMPTY cell does not count; for
     SEMIMAJOR_AXIS and PERIOD a value <= 0 does not count.
  5. Decimate into 120 time bins exactly as the firmware does and report how
     many bins end up populated PER COLUMN -- the number that decides whether
     the graph/table shows anything.

Usage:
  python3 st_inspect.py --user you@example.com --norad 7530 --days 365
  python3 st_inspect.py --user you@example.com --norad 7530 --max
  (password is prompted, or pass --password / set SPACETRACK_PW)

Paste the output back and the failure names itself.
"""

import argparse, getpass, os, sys, time, urllib.request, urllib.parse, http.cookiejar
from datetime import datetime, timezone

COLS = ["SEMIMAJOR_AXIS", "ECCENTRICITY", "INCLINATION", "PERIOD",
        "APOAPSIS", "PERIAPSIS", "BSTAR"]
BINS = 120

def fw_parse_epoch(s):
    """Firmware rule: skip leading quotes/spaces; Y-M-D<any one char>H:M:S(.f)."""
    s = s.lstrip('" ')
    try:
        d, t = s[:10], s[11:]
        Y, M, D = int(d[0:4]), int(d[5:7]), int(d[8:10])
        hh, mm = int(t[0:2]), int(t[3:5])
        ss = float(t[6:]) if len(t) > 6 else 0.0
        return datetime(Y, M, D, hh, mm, int(ss), tzinfo=timezone.utc).timestamp()
    except Exception:
        return 0

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--user", required=True)
    ap.add_argument("--password", default=os.environ.get("SPACETRACK_PW"))
    ap.add_argument("--norad", type=int, default=7530)
    ap.add_argument("--days", type=int, default=365)
    ap.add_argument("--max", action="store_true", help="full archive (like the tool's max span)")
    ap.add_argument("--raw", type=int, default=3, help="raw lines to print verbatim")
    a = ap.parse_args()
    pw = a.password or getpass.getpass("Space-Track password: ")

    cj = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(cj))

    # ---- 1. login (firmware-identical semantics) ----------------------------
    body = urllib.parse.urlencode({"identity": a.user, "password": pw}).encode()
    r = op.open("https://www.space-track.org/ajaxauth/login", body, timeout=30)
    login_body = r.read().decode(errors="replace")
    print(f"[login] HTTP {r.status}; body: {login_body[:80]!r}")
    if '"Login":"Failed"' in login_body:
        sys.exit("LOGIN FAILED -- this is what the firmware reports as "
                 "'Space-Track login failed - check user/pass'.")
    ck = [f"{c.name}={c.value}" for c in cj]
    print(f"[login] cookies set: {ck}")

    # ---- 2. the query, byte-identical URL shape -----------------------------
    now = int(time.time())
    if a.max:
        u1 = (f"https://www.space-track.org/basicspacedata/query/class/gp_history/"
              f"NORAD_CAT_ID/{a.norad}/orderby/EPOCH%20asc/limit/1/format/csv/predicates/EPOCH")
        r = op.open(u1, timeout=60)
        pre = r.read().decode(errors="replace")
        print(f"[max-prequery] HTTP {r.status}; reply: {pre[:80]!r}")
        lines = pre.strip().splitlines()
        t0 = fw_parse_epoch(lines[1]) if len(lines) > 1 else 0
        if not t0:
            sys.exit("pre-query gave no epoch -- firmware reports 'no history rows'.")
    else:
        t0 = now - a.days * 86400
    since = datetime.fromtimestamp(t0, timezone.utc).strftime("%Y-%m-%d")
    url = (f"https://www.space-track.org/basicspacedata/query/class/gp_history/"
           f"NORAD_CAT_ID/{a.norad}/EPOCH/%3E{since}"
           f"/orderby/EPOCH%20asc/format/csv/predicates/EPOCH," + ",".join(COLS))
    print(f"[query] {url}")
    r = op.open(url, timeout=120)
    data = r.read().decode(errors="replace")
    print(f"[query] HTTP {r.status}; {len(data)} bytes")

    # ---- 3. raw look --------------------------------------------------------
    lines = data.split("\n")             # firmware splits on \n; \r stays visible
    print(f"[raw] {len(lines)} newline-split lines; first {a.raw} verbatim:")
    for ln in lines[:a.raw]:
        print("   ", repr(ln[:160]))
    if lines and not lines[0].startswith("EPOCH"):
        sys.exit("FIRST LINE IS NOT the EPOCH header -- firmware reports "
                 f"'Space-Track: {lines[0][:24]}'.")

    # ---- 4+5. firmware parse + bin simulation -------------------------------
    t1 = now
    rng = (t1 - t0) + 1.0
    stats = [{"empty": 0, "zero": 0, "bad": 0, "ok": 0,
              "min": None, "max": None} for _ in COLS]
    bins = [[0] * len(COLS) for _ in range(BINS)]
    rows = kept = drop_window = drop_fields = 0
    for ln in lines[1:]:
        ln = ln.rstrip("\r")
        if not ln:
            continue
        f = ln.split(",")
        rows += 1
        if len(f) < 8:
            drop_fields += 1
            continue
        te = fw_parse_epoch(f[0])
        if te < t0 or te > t1:
            drop_window += 1
            continue
        kept += 1
        bi = min(BINS - 1, max(0, int((te - t0) * BINS / rng)))
        for k in range(len(COLS)):
            cell = f[k + 1].strip("\r")
            st = stats[k]
            if cell == "":
                st["empty"] += 1
                continue
            try:
                v = float(cell)
            except ValueError:
                st["bad"] += 1
                continue
            if k in (0, 3) and v <= 0:     # firmware positivity rule (sma, period)
                st["zero"] += 1
                continue
            st["ok"] += 1
            st["min"] = v if st["min"] is None else min(st["min"], v)
            st["max"] = v if st["max"] is None else max(st["max"], v)
            bins[bi][k] += 1

    print(f"\n[rows] total {rows}  kept-in-window {kept}  "
          f"dropped: window {drop_window}, short-line {drop_fields}")
    print(f"[window] {since} .. {datetime.fromtimestamp(t1, timezone.utc):%Y-%m-%d}")
    print(f"\n{'column':<16}{'ok':>7}{'empty':>7}{'<=0':>6}{'bad':>5}"
          f"{'min':>14}{'max':>14}{'bins>0':>8}")
    for k, name in enumerate(COLS):
        st = stats[k]
        nb = sum(1 for b in bins if b[k])
        print(f"{name:<16}{st['ok']:>7}{st['empty']:>7}{st['zero']:>6}{st['bad']:>5}"
              f"{st['min'] if st['min'] is not None else '-':>14}"
              f"{st['max'] if st['max'] is not None else '-':>14}{nb:>8}")
    print("\nReading: 'bins>0' is what the device's graph/table draw from. If a "
          "column shows ok>0 here but the device shows no data for it, the bug "
          "is on the device side of this exact pipeline -- send this output back.")

if __name__ == "__main__":
    main()
