#!/usr/bin/env python3
"""
fetch_decay_calibration.py -- build a decay-model calibration set from Space-Track.

WHAT THIS COLLECTS
  For objects that have already re-entered (taken from your TIPS.json), it pulls
  the element-set HISTORY over the last N days before re-entry. Pairing "elements
  at T-30/-14/-7/-3 days" with "actual decay time" is what lets us score a
  predicted days-to-reentry, which the n-dot rate calibration cannot test.

WHY IT IS SHAPED THIS WAY
  Space-Track's published limits are <30 requests/minute and <300/hour, and their
  documentation explicitly asks users NOT to send one request per satellite but to
  combine objects into comma-delimited lists. So this script groups targets by the
  MONTH they decayed and issues ONE gp_history request per month (plus a couple of
  satcat requests) -- about two dozen requests total for a few hundred objects,
  instead of several hundred. It also restricts the returned fields with
  /predicates/ to keep the payload small, which their bandwidth guidance asks for.

USAGE
  export SPACETRACK_USER='you@example.com'
  export SPACETRACK_PASS='...'
  python3 fetch_decay_calibration.py --tips TIPS.json --out calib_elsets.json

  # Start here -- one month, few objects, to confirm credentials and syntax:
  python3 fetch_decay_calibration.py --tips TIPS.json --months 1 --max-objects 15 \
          --out smoke_test.json

OPTIONS
  --days-before N   how far back before decay to collect elsets (default 40)
  --months N        only use objects that decayed in the last N months (default 18)
  --max-objects N   cap the target list (default 250)
  --min-per-month N skip months with fewer than this many decayed objects (default 3)

NOTE ON THE DATA
  Space-Track's user agreement restricts redistribution of the raw data. The
  intent here is to derive calibration COEFFICIENTS (a handful of numbers) which
  can ship in the firmware; the downloaded elsets themselves should stay local and
  out of the public repository.
"""
import argparse, collections, datetime as dt, getpass, json, os, ssl, sys, time
import urllib.parse, urllib.request, urllib.error, http.cookiejar

BASE = "https://www.space-track.org"
LOGIN = BASE + "/ajaxauth/login"
QUERY = BASE + "/basicspacedata/query"

# Conservative pacing: their limit is 30/min; we stay well under it. The per-hour
# limit (300) is not a concern at ~25 total requests.
SLEEP_BETWEEN = 4.0

GP_FIELDS = ("NORAD_CAT_ID,OBJECT_NAME,EPOCH,MEAN_MOTION,ECCENTRICITY,INCLINATION,"
             "BSTAR,MEAN_MOTION_DOT,MEAN_MOTION_DDOT,RA_OF_ASC_NODE,ARG_OF_PERICENTER,"
             "MEAN_ANOMALY,APOAPSIS,PERIAPSIS,EPHEMERIS_TYPE")
SATCAT_FIELDS = "NORAD_CAT_ID,OBJECT_NAME,OBJECT_TYPE,RCS_SIZE,LAUNCH,DECAY,PERIOD,APOGEE,PERIGEE"


def ssl_context():
    """A verifying SSL context that works on a stock macOS python.org build.

    The python.org framework Python bundles its own OpenSSL and does NOT read the
    macOS keychain, so it has no trust roots until you run
    "/Applications/Python 3.x/Install Certificates.command". If that has not been
    run, fall back to certifi's bundle when it is importable.

    Verification is never disabled: this request carries your Space-Track
    password, so an unverified context would expose it to interception.
    """
    try:
        import certifi
        return ssl.create_default_context(cafile=certifi.where())
    except ImportError:
        return ssl.create_default_context()


def login(user, password):
    cj = http.cookiejar.CookieJar()
    op = urllib.request.build_opener(
        urllib.request.HTTPCookieProcessor(cj),
        urllib.request.HTTPSHandler(context=ssl_context()))
    data = urllib.parse.urlencode({"identity": user, "password": password}).encode()
    req = urllib.request.Request(LOGIN, data=data,
                                 headers={"User-Agent": "CardSat decay calibration/1.0"})
    try:
        with op.open(req, timeout=60) as r:
            body = r.read().decode("utf-8", "replace")
    except urllib.error.URLError as e:
        if "CERTIFICATE_VERIFY_FAILED" in str(e):
            sys.exit(
                "\nTLS certificate verification failed -- this Python has no CA bundle.\n"
                "This is a macOS python.org packaging quirk, not a Space-Track problem.\n"
                "Fix it once, either way:\n"
                '  open "/Applications/Python 3.13/Install Certificates.command"\n'
                "or:\n"
                "  python3 -m pip install certifi   (this script will then find it)\n"
                "Do NOT disable verification -- this request sends your password.\n")
        raise
    if "Failed" in body or "login" in body.lower() and len(body) < 200 and "success" not in body.lower():
        # Space-Track returns "" or a JSON blob on success; a failure page mentions Failed.
        if "Failed" in body:
            sys.exit("Login failed -- check SPACETRACK_USER / SPACETRACK_PASS.")
    print("  logged in")
    return op


def get(op, path, label):
    url = QUERY + path
    req = urllib.request.Request(url, headers={"User-Agent": "CardSat decay calibration/1.0"})
    for attempt in (1, 2, 3):
        try:
            with op.open(req, timeout=300) as r:
                raw = r.read().decode("utf-8", "replace")
            if raw.strip().startswith("<"):          # an HTML error page
                raise RuntimeError("HTML returned (session expired or bad query)")
            return json.loads(raw) if raw.strip() else []
        except Exception as e:
            msg = str(e)
            wait = 65 if ("500" in msg or "rate" in msg.lower()) else 10 * attempt
            print(f"    ! {label}: {e} -- retry {attempt}/3 in {wait}s")
            time.sleep(wait)
    print(f"    ! {label}: giving up")
    return []


def pick_targets(tips_path, months, max_objects, min_per_month):
    """Final TIP message per object -> decay time. Keep recent ones, spread over months."""
    tips = json.load(open(tips_path))
    last = {}
    for t in tips:
        nid, dec, msg = t.get("NORAD_CAT_ID"), t.get("DECAY_EPOCH"), t.get("MSG_EPOCH")
        if not (nid and dec):
            continue
        nid = int(nid)
        if nid not in last or (msg or "") > (last[nid].get("MSG_EPOCH") or ""):
            last[nid] = t
    today = dt.date.today()
    cutoff = (today - dt.timedelta(days=int(months * 30.4))).isoformat()
    rows = [(int(n), t["DECAY_EPOCH"], t) for n, t in last.items()
            if cutoff <= t["DECAY_EPOCH"][:10] < today.isoformat()]
    by_month = collections.defaultdict(list)
    for nid, dec, t in rows:
        by_month[dec[:7]].append((nid, dec, t))
    by_month = {k: v for k, v in by_month.items() if len(v) >= min_per_month}
    # Even sample across months so the set spans solar conditions, not one epoch.
    months_sorted = sorted(by_month)
    if not months_sorted:
        sys.exit("No decayed objects in that window -- widen --months.")
    per = max(1, max_objects // len(months_sorted))
    targets = collections.OrderedDict()
    for m in months_sorted:
        grp = sorted(by_month[m], key=lambda x: x[1])
        step = max(1, len(grp) // per)
        targets[m] = grp[::step][:per]
    n = sum(len(v) for v in targets.values())
    print(f"  {n} target objects across {len(targets)} months "
          f"({months_sorted[0]} .. {months_sorted[-1]})")
    return targets


def chunks(seq, n):
    for i in range(0, len(seq), n):
        yield seq[i:i + n]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tips", default="TIPS.json")
    ap.add_argument("--out", default="calib_elsets.json")
    ap.add_argument("--days-before", type=int, default=40)
    ap.add_argument("--months", type=int, default=18)
    ap.add_argument("--max-objects", type=int, default=250)
    ap.add_argument("--min-per-month", type=int, default=3)
    a = ap.parse_args()

    user = os.environ.get("SPACETRACK_USER") or input("Space-Track user: ").strip()
    pw = os.environ.get("SPACETRACK_PASS") or getpass.getpass("Space-Track password: ")

    print("selecting targets from TIPS...")
    targets = pick_targets(a.tips, a.months, a.max_objects, a.min_per_month)
    all_ids = sorted({nid for grp in targets.values() for nid, _, _ in grp})

    print("logging in...")
    op = login(user, pw)

    requests_made = 0

    # 1) Official decay times + object metadata (mass class matters for drag).
    print("fetching satcat...")
    satcat = []
    for batch in chunks(all_ids, 100):
        ids = ",".join(str(i) for i in batch)
        satcat += get(op, f"/class/satcat/NORAD_CAT_ID/{ids}"
                          f"/predicates/{SATCAT_FIELDS}/format/json", "satcat")
        requests_made += 1
        time.sleep(SLEEP_BETWEEN)
    print(f"  satcat rows: {len(satcat)}")

    # 2) Element-set history: ONE request per decay month, comma-delimited IDs.
    elsets = []
    for month, grp in targets.items():
        ids = ",".join(str(nid) for nid, _, _ in grp)
        decays = sorted(d for _, d, _ in grp)
        lo = (dt.datetime.fromisoformat(decays[0].replace(" ", "T"))
              - dt.timedelta(days=a.days_before)).date().isoformat()
        hi = (dt.datetime.fromisoformat(decays[-1].replace(" ", "T"))
              + dt.timedelta(days=1)).date().isoformat()
        path = (f"/class/gp_history/NORAD_CAT_ID/{ids}/EPOCH/{lo}--{hi}"
                f"/predicates/{GP_FIELDS}/orderby/NORAD_CAT_ID,EPOCH/format/json")
        rows = get(op, path, f"gp_history {month}")
        requests_made += 1
        print(f"  {month}: {len(grp):>3} objects -> {len(rows):>6} elsets")
        elsets += rows
        time.sleep(SLEEP_BETWEEN)

    out = {
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "params": vars(a),
        "decay_times": {str(nid): dec for grp in targets.values() for nid, dec, _ in grp},
        "satcat": satcat,
        "elsets": elsets,
    }
    json.dump(out, open(a.out, "w"))
    mb = os.path.getsize(a.out) / 1e6
    print(f"\nwrote {a.out}  ({len(elsets)} elsets, {len(satcat)} satcat rows, {mb:.1f} MB)")
    print(f"requests made: {requests_made} (limits: 30/min, 300/hr)")
    print("\nSend that file back and it becomes the calibration + validation set.")


if __name__ == "__main__":
    main()
