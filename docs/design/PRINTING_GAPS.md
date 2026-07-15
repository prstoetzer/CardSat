# Printing gaps — audit

*Findings only, nothing built. For 0.9.57+ planning.*

## Method

Enumerated every `key*` handler, checked which reach `printReport()`, then filtered to screens
that actually compute report-worthy content (games, menus, editors, settings, live monitors and
type-to-search lookups are correctly not printable). Also confirmed the print-submenu table
(`PA_ITEMS`) so menu-only reports weren't miscounted as gaps.

**All 24 `PrintReport` enums are reachable.** Five (`PR_TICKET`, `PR_SATCARD`, `PR_KEPS`,
`PR_LOG`, `PR_HORIZON`) have no contextual key but sit in the About → Print submenu at indices
2–6. Not gaps.

## Confirmed gaps — strong candidates

Your EME hunch was right, and it's the biggest one.

| screen | content | `p` key |
|---|---|---|
| **EME / moonbounce** | self-echo Doppler per band (50/144/432/1296/10368), topocentric range + rate, degradation vs perigee, galactic sky-noise flag, Moon window vs a DX grid | **taken** — opens the 30-day plan |
| **EME 30-day plan** | declination + degradation per day for a month | free |
| **Workable US states** | the entity **list** — the *counts* reach paper via other reports, the list itself never does | free |
| **Workable DXCC** | same: the entity list | free |
| **Awards** | all-satellite totals + per-satellite tally, read from the QSO log | free |
| **Station readiness** | the pre-operating checklist — a natural thing to print once and pin up | free |
| **Visible-pass list** | 10 days of optically-visible passes with rise compass direction | free |

**EME is the standout.** It computes more genuinely paper-worthy content than several things that
*are* printable, and a 30-day planning table is exactly the sort of thing you'd want in the field
rather than on a 240×135 screen.

**The workable-states/DXCC gap is subtle** and worth calling out: those counts already reach paper
inside other reports, so it looks covered. It isn't — *which* entities are workable is the part a
rover actually needs, and that list has no print path at all.

## Moderate — defensible either way

| screen | note |
|---|---|
| HF/6m propagation | band-by-band guidance; `p` free |
| Space weather | SFI/Kp/A + outlook; `p` **taken** (opens propagation) |
| Transponder DB | the active satellite's transponder list |
| Grid dist/bearing | a single distance + heading — Tools-style result |
| Overhead now | changes minute to minute; ephemeral |
| Sun / Moon | az/el now; the transits screen is the more useful one |

## Deliberately not printable — leave alone

CQ zones, ITU zones, CTCSS, operating references, radio-math reference. Large static tables where
dumping hundreds of rows isn't useful. This matches the existing decision recorded for the DXCC
and character lookups, and it should stay consistent.

## Key-assignment note

Two of the strong candidates have `p` already bound:

- **EME** — `p` opens the 30-day plan
- **Space weather** — `p` opens the propagation guide

Those want **`Fn`+`p`**, which is the convention already established for exactly this case (the
scientific calculator and the BASIC editor, where a bare `p` must do something else). The rest
have `p` free and can follow the plain-`p` pattern used by the other report screens.

## Suggested order, if built

1. **EME** (`Fn`+`p`) and **EME 30-day plan** (`p`) — the clearest gap, real content, and the
   plan table is the kind of thing paper is *for*.
2. **Workable US states** / **Workable DXCC** (`p`) — the lists, not the counts.
3. **Station readiness** (`p`) — small, self-contained, obviously useful once.
4. **Awards** (`p`) — but note it reads the QSO log; keep the render bounded rather than walking
   an unbounded file into a `String` (the 0.9.56 heap lesson).
5. **Visible-pass list** (`p`) — a near-twin of the existing 10-day report; mostly a formatting job.

Each is a `PrintReport` enum + a name-map entry + a dispatch case + a `printX()` placed **after**
the helpers it calls (the placement rule from the 0.9.56 compile errors), plus a width check
against 32-col (58 mm) and 44-col (80 mm) paper.
