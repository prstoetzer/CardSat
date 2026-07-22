#!/usr/bin/env python3
"""Skyfield reference comparison for the CardSat orbital harness output."""
import re, sys
from skyfield.api import load, wgs84, EarthSatellite
from skyfield import almanac

ts = load.timescale()
name, l1, l2 = [x.strip() for x in open('iss.tle').read().strip().split('\n')]
sat = EarthSatellite(l1, l2, name, ts)
site = wgs84.latlon(38.90, -77.04, elevation_m=20)

run = open('run1.txt').read()
t0 = int(re.search(r't0=(\d+)', run).group(1))

# ---- Track samples ----
rows = [tuple(map(float, m.groups())) for m in
        re.finditer(r'^T (\d+) ([-\d.]+) ([-\d.]+) ([-\d.]+) ([-\d.]+) (\d)', run, re.M)]
diff = sat - site
worst = dict(az=0.0, el=0.0, rng=0.0, rr=0.0)
for (tu, az_c, el_c, rng_c, rr_c, _su) in rows:
    t = ts.from_datetime(__import__('datetime').datetime.fromtimestamp(tu, __import__('datetime').timezone.utc))
    top = diff.at(t)
    el, az, rng = top.altaz()
    p = top.position.km; v = top.velocity.km_per_s
    import numpy as np
    rr = float(np.dot(p, v) / np.linalg.norm(p))
    def ang(a, b):
        d = abs(a - b) % 360.0
        return min(d, 360.0 - d)
    worst['az']  = max(worst['az'],  ang(az.degrees, az_c))
    worst['el']  = max(worst['el'],  abs(el.degrees - el_c))
    worst['rng'] = max(worst['rng'], abs(rng.km - rng_c))
    worst['rr']  = max(worst['rr'],  abs(rr - rr_c))
print(f"TRACK n={len(rows)}  worst: az={worst['az']:.4f}deg el={worst['el']:.4f}deg "
      f"range={worst['rng']:.3f}km  rr={worst['rr']*1000:.3f}m/s "
      f"(={worst['rr']/299792.458*435.5e6:.2f}Hz @435.5MHz)")

# ---- Pass events ----
import datetime as dt
tA = ts.from_datetime(dt.datetime.fromtimestamp(t0, dt.timezone.utc))
tB = ts.from_datetime(dt.datetime.fromtimestamp(t0 + 86400, dt.timezone.utc))
times, events = sat.find_events(site, tA, tB, altitude_degrees=0.0)
ref = []
cur = {}
for t, e in zip(times, events):
    u = int(t.utc_datetime().timestamp())
    if e == 0: cur = {'aos': u}
    elif e == 1:
        cur['tca'] = u
        el, az, _ = diff.at(t).altaz(); cur['maxel'] = el.degrees
    elif e == 2 and 'aos' in cur:
        cur['los'] = u; ref.append(cur); cur = {}
mine = [tuple(map(float, m.groups())) for m in
        re.finditer(r'^P (\d+) (\d+) (\d+) ([\d.]+) ([\d.]+) ([\d.]+)', run, re.M)]
print(f"PASSES cardsat={len(mine)} skyfield={len(ref)}")
for i, (m, r) in enumerate(zip(mine, ref)):
    print(f"  #{i}: dAOS={m[0]-r['aos']:+.0f}s dLOS={m[1]-r['los']:+.0f}s "
          f"dTCA={m[2]-r['tca']:+.0f}s dMaxEl={m[3]-r['maxel']:+.2f}deg")

# ---- Sunlit transitions ----
eph = load('de421.bsp')
trans = [(int(m.group(1)), int(m.group(2))) for m in re.finditer(r'^S (\d+) (\d)', run, re.M)]
bad = 0
for (tu, su) in trans[1:]:
    for probe, expect in ((tu - 90, 1 - su), (tu + 90, su)):
        t = ts.from_datetime(dt.datetime.fromtimestamp(probe, dt.timezone.utc))
        if int(sat.at(t).is_sunlit(eph)) != expect: bad += 1
print(f"SUNLIT transitions={len(trans)-1} disagreements(+/-90s)={bad}")

# ---- Beta angle ----
import numpy as np
t = ts.from_datetime(dt.datetime.fromtimestamp(t0, dt.timezone.utc))
# Frame-consistent reference: the TLE RAAN (hence the orbit normal) lives in the
# TEME/of-date frame family, so the Sun must be taken in of-date RA/dec too --
# mixing an ICRF Sun with a TEME normal injects ~0.34 deg of accumulated
# precession (J2000 -> 2026) and wrongly indicts the firmware.
ra, dec, _ = (eph['earth'].at(t).observe(eph['sun'])).apparent().radec(epoch='date')
r_, d_ = ra.radians, dec.radians
sun = np.array([np.cos(d_)*np.cos(r_), np.cos(d_)*np.sin(r_), np.sin(d_)])
inc = float(l2[8:16]) * np.pi/180; raan = float(l2[17:25]) * np.pi/180
n = np.array([np.sin(inc)*np.sin(raan), -np.sin(inc)*np.cos(raan), np.cos(inc)])
beta_ref = np.degrees(np.arcsin(np.clip(np.dot(n, sun), -1, 1)))
beta_c = float(re.search(r'beta=([-\d.]+)', run).group(1))
print(f"BETA cardsat={beta_c:.3f} ref={beta_ref:.3f} d={beta_c-beta_ref:+.3f}deg")


# ---- Higher-orbit pass finding: Molniya + GEO vs skyfield (0.9.59) ----
ho = run.split("== HIORBIT ==")
if len(ho) > 1:
    block = ho[1].split("\n==")[0] + "\n"
    for m in re.finditer(r"SAT (\w+)\nL1\|(.*)\|\nL2\|(.*)\|\n((?:P .*\n)*)", block):
        nm, L1, L2, plines = m.groups()
        es = EarthSatellite(L1, L2, nm, ts)
        dfe = es - site
        pp = [tuple(map(float, l.split()[1:5]))
              for l in plines.strip().splitlines() if l.startswith("P ")]
        if nm == "MOLNIYA":
            # Validate each firmware pass directly against skyfield elevations: up at
            # mid-pass, down just outside the endpoints (unless the pass is clipped by
            # the window edge), and the crossings themselves within 0.5 deg of zero --
            # robust for in-progress and horizon-clipped passes, where event pairing
            # off a fixed window misleads.
            def elat(u):
                tk = ts.from_datetime(dt.datetime.fromtimestamp(u, dt.timezone.utc))
                el, _, _ = dfe.at(tk).altaz(); return el.degrees
            bad = 0; worstx = 0.0
            for (aos, los, tca, mel) in pp:
                if elat((aos + los) / 2) <= 0: bad += 1
                if aos > t0 + 1:
                    if elat(aos - 180) >= 0: bad += 1
                    worstx = max(worstx, abs(elat(aos)))
                if los < t0 + 86400 - 1:
                    if elat(los + 180) >= 0: bad += 1
                    worstx = max(worstx, abs(elat(los)))
            # coverage: skyfield up-fraction vs the fraction the passes cover
            up = sum(1 for k in range(289) if elat(t0 + 300 * k) > 0)
            covered = sum(1 for k in range(289)
                          if any(a <= t0 + 300 * k <= l for (a, l, _, _) in pp))
            print(f"HIORBIT MOLNIYA: passes={len(pp)} bad_checks={bad} "
                  f"worst_crossing_el={worstx:.3f}deg "
                  f"up_samples sky={up} covered={covered}")
        else:
            up_all = True
            for k in range(49):
                tk = ts.from_datetime(dt.datetime.fromtimestamp(t0 + 86400 * k // 48,
                                                                dt.timezone.utc))
                el, az, _ = dfe.at(tk).altaz()
                if el.degrees < 0: up_all = False; break
            span = 0 if not pp else pp[0][1] - pp[0][0]
            ok = up_all and len(pp) == 1 and span >= 86000
            print(f"HIORBIT GEO: skyfield_up_all={up_all} cardsat_passes={len(pp)} "
                  f"span={span:.0f}s -> {'OK' if ok else 'MISMATCH'}")
