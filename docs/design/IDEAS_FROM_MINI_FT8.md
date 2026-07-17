# Ideas from Mini-FT8

*Assessment only. Nothing built. [Mini-FT8](https://github.com/wcheng95/Mini-FT8) — Wei (AG6AQ)
and Zhenxing (N6HAN) — the project that inspired CardSat, on the same Cardputer ADV.*

Read with an eye for what CardSat lacks. Ranked by what I'd actually do.

---

## 1. USB mass storage — get files off without pulling the SD card ★

Mini-FT8's **`C` key**: *"Stop radio audio and expose FATFS to the PC. Safely eject it on the PC,
then press `C` again to remount storage and return to RX."*

**CardSat has no equivalent, and the manual currently lies about it.** Line 2138 told operators to
open `/CardSat/calib.txt` *"with the microSD removed (or over USB mass storage)"* — there is no MSC
implementation. (Fixed as part of writing this.)

Today, getting reports, logs, memos, or `calib.txt` off a CardSat means **physically removing the
microSD**. That's a fiddly card in a pocket device, and it's the sort of thing that loses cards.
The files CardSat produces that people want off it:

- `/CardSat/Reports/*.txt` — the file print sink
- QSO log / ADIF for upload
- voice memos
- `calib.txt` for bulk editing
- `.bas` programs

**Why it's compelling:** it makes the *file sink* a first-class offload path, which matters
because the file sink is CardSat's answer for operators with no printer at all. And it costs
nothing at runtime — USB device mode is what the board already does.

**The catch:** it shares the S3's one USB PHY with the serial console, so it's the same runtime-switch dance
Mini-FT8 does. And CardSat's storage is SD-first (Mini-FT8 uses an internal FATFS partition),
which may make exposing it as a volume easier *or* harder — worth checking before promising.

**This is the strongest idea on the list**, and it's independent of the USB *printer* question.

## 2. A performance monitor ★

Mini-FT8 v2.0.4 added **`P` — "A Simple Performance Monitor."**

CardSat has `mem`/`memtrace` over serial, which found two real bugs. But they are **serial-only**
— invisible in the field, and about to become more awkward if USB is ever contended. An on-device
screen showing free heap, largest block, min-block-seen, and loop timing would:

- make the heap visible without a laptop (the 0.9.57 bug was found *because* Paul had a serial
  console attached; a field operator wouldn't have)
- give the RAM-refactor question (§2.1) a live readout instead of a log
- survive any future USB mode-switching

Small, self-contained, and it fits the About screen's diagnostic role. **Cheap and I'd do it.**

## 3. Number keys select the visible row ★

Mini-FT8: *"`1`..`6`: **always** select the currently visible row in the active mode."* Used
uniformly across RX, TX, Band, Menu, QSO, and Delete.

CardSat navigates by `;`/`.` + ENTER everywhere. On a 240×135 screen showing ~6 rows, a direct
"press 3 for the third row" is materially faster — one keypress instead of scroll-scroll-enter —
and Mini-FT8's *always* is the good part: it's the same everywhere, so it needs no per-screen
learning.

**The conflict to check:** CardSat's first-letter jump uses letters, not digits, so `1`–`6` are
mostly free — but the Satellites screen already binds `2` and `3` (sat-to-sat, 3D globe), and the
calculators obviously type digits. This would need the same audit discipline as the `h`/`b`
work: enumerate every handler that consumes digits before promising "always."

Worth doing **only if it can be genuinely uniform.** A "1–6 selects the row, except on four
screens" would be worse than not having it.

## 4. Menu pages as top-level keys

Mini-FT8 splits Settings into **MENU P1/P2/P3** reached by `M`/`N`/`O`, each a page of six
numbered items, *"press the current page key again to return to RX."*

CardSat's Settings is one long scrolling list in six categories. Mini-FT8's model is flatter —
two keypresses to any setting, and the toggle-back is a nice touch.

**But CardSat has ~90 settings across six categories to Mini-FT8's ~18.** Three pages of six
doesn't scale to that; the existing categorised list is the right structure. **What's worth
stealing is narrower:** the *"press the same key again to go back"* idiom, which is a small,
consistent affordance.

## 5. `/Radio` and `/Grid` macros in the comment field

Mini-FT8's ADIF comment field *"supports `/Radio` and `/Grid` macro expansion."*

CardSat logs QSOs and has a per-satellite note field. A macro that expands to the current
satellite, grid, or transponder in a logged comment is a small, natural fit — especially for rovers
whose grid changes between QSOs. **Low effort, genuinely useful, no new subsystem.**

## 6. An ignore/filter list

Mini-FT8 MENU P2 `4`: *"Edit ignore list. Prefixes are separated by spaces; maximum 64
characters."*

Applied to CardSat: a way to hide satellites you'll never work — dead birds, wrong hemisphere,
uplinks you can't transmit on. CardSat has favourites (opt-in) but no opt-out, so the full list is
always ~90–150 entries deep.

**Modest value** — favourites already solve most of this — but the *catalog* views (Overhead now,
sat-to-sat, 10-day) don't respect favourites, and that's where noise lives.

## 7. Copy-files-to-SD as an explicit action

Mini-FT8 MENU P3 `5`: *"Copy files to SD. Feedback is `Copied OK` or `Missed [n]`."*

Less relevant — CardSat is already SD-first when a card is present, so its files are *already*
there. Only interesting for the LittleFS fallback case (no SD card), where there's currently no
way to get files out at all short of the web UI. **Note that idea #1 (MSC) solves this case
better.**

---

## What CardSat does that Mini-FT8 doesn't

Worth stating, because the borrowing isn't one-directional and it shapes what's worth taking:

- **Printing** — nine formats, three sinks, 28 reports. Mini-FT8 has none.
- **Breadth** — CardSat is a tracker + CAT + rotator + logger + toolbox; Mini-FT8 is one mode
  done well. That's why its flat 3-page menu works and CardSat's can't.
- **The scale difference is the point:** Mini-FT8's UI ideas are good *because* its scope is
  narrow. Copying its structure wholesale would be a mistake; copying its **idioms** (uniform
  row-select, press-again-to-return, live diagnostics) is not.

## Recommendation

**Do #1 (USB mass storage) and #2 (performance monitor).** Both are self-contained, both fill real
gaps, and #1 fixes a documentation claim that is currently false.

**Consider #3 (number-row select) and #5 (log macros)** — cheap, but #3 only if it can be uniform.

**Skip #4, #6, #7** — CardSat's scale or existing design already answers them.

Everything here is judgement from reading Mini-FT8's README, not from running it. Paul uses it;
his read on which of these actually matter in the field beats mine.
