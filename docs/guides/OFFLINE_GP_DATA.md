# Curating Your Own GP Data (Offline / SD-Card GP)

CardSat normally downloads orbital elements over WiFi (the **Update** screen, key
`k`). But you don't have to. CardSat reads its orbital data from a plain JSON file
on the storage card, and you can **write that file yourself** — hand-pick exactly
the satellites you want, drop the file on the microSD card, and run CardSat with no
network at all. This is useful for:

- **Field / portable use** with no WiFi available.
- **A fixed, curated set** of satellites you care about (no 150-object catalog).
- **Air-gapped or privacy-conscious** operation (CardSat never has to phone home).
- **Pinning known-good elements** for a special operation, so an automatic refresh
  can't replace them mid-event.

This guide explains the exact file format CardSat expects, where to put it, and how
to build it by hand from public sources.

> **You still need the clock set.** Orbital prediction needs accurate time. With no
> WiFi/NTP, set the clock from **GPS** (if your unit has it) or by hand on the
> **Location → set time** entry. Stale *or* wrong time gives wrong predictions even
> with perfect elements.

---

## 1. Where the file goes

CardSat reads its cached GP from one fixed path on whichever storage it is using:

```
/CardSat/gp.json
```

- With a **microSD card inserted**, that's `/CardSat/gp.json` on the card. Create a
  top-level folder named `CardSat` and put `gp.json` inside it.
- With **no card**, CardSat falls back to internal flash and looks for the same
  path there — but the practical way to hand-load a file is the **SD card**, since
  you can write it from a computer.

At boot CardSat loads this file automatically; if it parses at least one satellite
you'll see a brief "Loaded cached GP: N" status. From then on everything (passes,
tracking, Doppler, the analysis screens) runs off your file.

> **The online Update overwrites this file.** If you press `k` on the Update screen
> with WiFi connected, CardSat fetches fresh elements and **rewrites
> `/CardSat/gp.json`**, replacing your hand-curated file. To stay fully offline,
> simply don't run the online update — or keep a backup copy of your curated file
> and restore it after any accidental refresh. (Hand-entered *manual* satellites are
> stored separately and are not wiped, but the main GP file is.)

---

## 2. The file format

`gp.json` is a **JSON array of OMM objects**. OMM (Orbit Mean-Elements Message) is
the modern JSON form of a TLE — it's exactly what CelesTrak and Space-Track hand out
when you ask for "JSON" format, so the easiest path (below) is to let them generate
it and then trim it down.

The outer structure is a single array:

```json
[
  { ...satellite 1... },
  { ...satellite 2... }
]
```

Each object describes one satellite. CardSat reads these fields (the names are
case-sensitive and must match exactly):

| Field | Meaning | Required |
|---|---|---|
| `OBJECT_NAME` | Display name (e.g. `"AO-91"`). See note on `AMSAT_NAME` below. | name needed |
| `AMSAT_NAME` | Preferred display name if present; falls back to `OBJECT_NAME`. | optional |
| `NORAD_CAT_ID` | NORAD catalog number — the satellite's identity in CardSat. | **yes** |
| `EPOCH` | Element epoch, ISO 8601 (`"2026-06-15T12:34:56.000"`). | **yes** |
| `MEAN_MOTION` | Revolutions per day. | **yes** |
| `ECCENTRICITY` | 0–1 (a circle is 0). | strongly |
| `INCLINATION` | Degrees. | strongly |
| `RA_OF_ASC_NODE` | RAAN, degrees. | strongly |
| `ARG_OF_PERICENTER` | Argument of perigee, degrees. | strongly |
| `MEAN_ANOMALY` | Degrees. | strongly |
| `BSTAR` | Drag term (1/earth radii). | recommended |
| `MEAN_MOTION_DOT` | First derivative of mean motion. | optional |
| `MEAN_MOTION_DDOT` | Second derivative of mean motion. | optional |
| `OBJECT_ID` | International designator (e.g. `"2017-073E"`). | optional |
| `REV_AT_EPOCH` | Revolution number at epoch. | optional |
| `ELEMENT_SET_NO` | Element set number. | optional |

**Minimum to load at all:** `NORAD_CAT_ID`, `EPOCH`, and `MEAN_MOTION` must be
present and non-zero, or CardSat silently skips that object. **For correct
predictions** you want the full Keplerian set (eccentricity, inclination, RAAN,
argument of perigee, mean anomaly) plus `BSTAR` — i.e. all the fields a normal TLE
carries. Leave out the optional ones and prediction still works; leave out the
Keplerian ones and the orbit will be wrong.

> **Epoch format matters.** Use ISO 8601 with the `T` (or a space) between date and
> time, and include the time of day: `"2026-06-15T12:34:56"`. Omitting the time
> silently zeroes the hours/minutes/seconds and can shift pass times by up to ~12
> hours. The fractional seconds are optional.

### A complete one-satellite example

```json
[
  {
    "OBJECT_NAME": "AO-91",
    "OBJECT_ID": "2017-073E",
    "EPOCH": "2026-06-15T08:12:34.000000",
    "MEAN_MOTION": 14.79123456,
    "ECCENTRICITY": 0.0023456,
    "INCLINATION": 97.7123,
    "RA_OF_ASC_NODE": 215.4321,
    "ARG_OF_PERICENTER": 123.4567,
    "MEAN_ANOMALY": 236.7890,
    "BSTAR": 0.00012345,
    "MEAN_MOTION_DOT": 0.00000123,
    "MEAN_MOTION_DDOT": 0,
    "NORAD_CAT_ID": 43017,
    "REV_AT_EPOCH": 41234,
    "ELEMENT_SET_NO": 999
  }
]
```

(The numbers above are illustrative — don't fly them. Get real values from a current
source as described next.)

---

## 3. The easy way: let CelesTrak/Space-Track generate it, then trim

You almost never want to type these numbers by hand — they go stale within days and
typos produce garbage orbits. The reliable workflow is to **download real OMM JSON
and keep only the objects you want**:

1. **Get OMM JSON for a satellite or group.** From CelesTrak's GP query, request a
   satellite by catalog number or name and choose **JSON** as the format. (Space-Track
   can export OMM JSON as well, with an account.) You'll get exactly the array-of-
   objects structure above.
2. **Keep the satellites you want.** If you downloaded a group (e.g. the amateur
   set), open the file in any text editor and delete the objects you don't care
   about, leaving a clean array of just your picks. Watch the commas: every object is
   separated by a comma, and there's no trailing comma after the last one.
3. **Save as `gp.json`** and copy it to `/CardSat/gp.json` on the microSD card.
4. **Boot CardSat** (no WiFi needed) — it loads your file and you're tracking.

This gives you authoritative, correctly-formatted elements while still letting you
curate the exact list. It's the recommended approach.

### Converting from a TLE you already have

If all you have is a classic two-line element set, every field above maps directly
from the TLE — `MEAN_MOTION`, `ECCENTRICITY`, `INCLINATION`, etc. are the same
numbers in the same units. The bundled helper **`tools/tle2gp.py`** does this
conversion for you, turning a TLE into the OMM JSON object CardSat expects; see the
manual's screen-map section for where it's referenced. Converting by hand is possible
but error-prone (the TLE packs the epoch as a year+fractional-day and drops the
leading decimal on several fields), so the helper is strongly preferred.

---

## 4. Keeping it fresh

Hand-curated elements **age just like downloaded ones**. CardSat shows the element
age on the **About** screen and color-grades the GP age on the satellite screens, so
you can see when your file is getting stale. Low-orbit satellites drift noticeably
within a few days; a week-old element set will have visibly wrong pass times.

When you're back in range of a computer, regenerate the file from a current source
and copy it over again. There's no harm in re-curating often — it's just a file.

---

## 5. Quick checklist

- [ ] File is valid JSON: a single `[ ... ]` array of `{ ... }` objects, commas
      between objects, no trailing comma.
- [ ] Each object has at least `NORAD_CAT_ID`, `EPOCH`, and `MEAN_MOTION`.
- [ ] Each object has the full Keplerian set for correct predictions.
- [ ] `EPOCH` is ISO 8601 with the time of day included.
- [ ] File is saved at `/CardSat/gp.json` on the microSD card.
- [ ] The clock is set (GPS or manual) since you're offline.
- [ ] You did **not** run the online Update (which would overwrite the file), or you
      kept a backup to restore.

That's it — CardSat will track whatever you put in the file, entirely offline.
