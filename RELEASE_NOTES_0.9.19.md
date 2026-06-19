# CardSat v0.9.19 — Release Notes

A world-map view you can recenter on your own location, plus a fast update option
that refreshes elements and transponders for just your favorite satellites.

> **Hardware status.** Host-verified (tokenizer-balanced, the projection math and
> seam handling checked off-device and rendered at several center longitudes) but
> not yet run on a device this release. The exact on-screen look and the footer fit
> still want a quick confirmation on a real Cardputer ADV.

---

## Recenter the world map on your location

The World Map (from the Schedule screen, key `m`) can now be **centered on your
QTH** instead of always showing the classic 0°-longitude-centered view. Press `c`
on the map to toggle:

- **Centered on QTH** — your station sits in the middle of the map, with the world
  wrapping around it. Handy when your area would otherwise sit off near the edge.
- **Classic (0°)** — the familiar view centered on the prime meridian.

The choice is **remembered** across reboots. Only the longitude (left–right) is
recentered; latitude stays fixed so north is always up and the equator stays in the
middle — the standard convention for this kind of map. Satellite sub-points,
footprint circles, the graticule, and the prime-meridian line all follow the
recentered view, and the international date-line seam is handled so coastlines and
footprints don't streak across the map wherever the seam lands.

This affects only the main World Map screen; the ground-track map on the
orbital-analysis page and the simulation map keep their standard 0°-centered view.

The World Map now also has its own entry on the **Home menu** (right after
Track (sel)), in addition to the existing `m` shortcut from the Next Passes screen.
Back returns to wherever you opened it from.

---

## Fast update (favorites only)

The Update screen has a new **`f` — fast update** key. It refreshes the orbital
elements (GP), the AMSAT activity marks (a single bulk fetch, so it's included),
and the transponder data for your **favorites only** — skipping the space-weather
and terrestrial-weather fetches that the full `k` update also pulls. It's the quick way to bring the birds you actually work
current without waiting for the longer full refresh — useful in the field on a
slower link. With no favorites marked, it refreshes the currently active satellite
instead. The existing `k` (full update) and `a` (cache all transponders) are
unchanged.

---

## Notes

- Host-verified only: the recenter and seam logic were checked off-device and the
  map rendered at several center longitudes (Americas, Europe, Asia/Pacific) to
  confirm the seam stays clean even when it falls over land. On-device appearance
  still wants a look on real hardware.
