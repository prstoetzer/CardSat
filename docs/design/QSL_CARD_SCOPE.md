# QSL card printing — gap audit and design

*Audit + design. Nothing built yet.* Supersedes the findings half of
`PRINTING_GAPS.md`, which is now stale (see §1).

---

## 1. The old gap list is closed

`docs/design/PRINTING_GAPS.md` was written for 0.9.57 planning and listed seven
strong candidates plus six moderates. Checked against the current `PrintReport`
enum, **every one of them has since been built**:

| old gap | now |
|---|---|
| EME / moonbounce | `PR_EME` |
| EME 30-day plan | `PR_EMEPLAN` (+ `PR_EMEMUT`, which was not on the list) |
| Workable US states (list) | `PR_STATES` |
| Workable DXCC (list) | `PR_DXCCLIST` |
| Awards | `PR_AWARDS` |
| Station readiness | `PR_READY` |
| Visible-pass list | `PR_VISLIST` |
| HF/6m propagation | `PR_MUF` |
| Space weather | `PR_SPACEWX` |
| Sun / Moon | `PR_SUNMOON` |

The enum has grown from 24 entries to 49. That document should be marked closed
rather than left to look like an open backlog.

## 2. What is actually still missing

Re-running the same method — walk the `key*` handlers, ask what computes
paper-worthy content, subtract what already reaches `printReport()` — turns up
much less than last time. The one substantial hole is not a *screen* that cannot
print; it is a *granularity* that does not exist.

**Everything the log can print is aggregate.** `PR_LOG` renders the recent-QSO
table and `PR_AWARDS` renders totals. There is no output for **one QSO**. That is
the gap this document is about, because the single most conventional piece of
paper in amateur radio — a QSL card — is exactly a one-QSO artifact.

Three smaller findings, all of which the QSL work runs into:

* **No centering primitive.** `Printer::title()` centres on ESC/POS (`ESC a 1`)
  and on the raster path, but on TEXT / PCL / PostScript / ESC/P2 / Star / ZPL it
  falls through to an ordinary left-aligned line. Every report so far has been
  left-aligned tabular material, so this has never mattered. A QSL card is a
  centred layout on every format, so it does now.
* **No framing.** Nothing draws a border. `rule()` gives a horizontal line and
  that is the whole vocabulary.
* **No card-sized media.** `Sinks.paper` is 0 = Letter / 1 = A4 and applies only
  to the raster formats. A QSL is 5.5 × 3.5 in (140 × 89 mm).

## 3. Data-model gaps found while designing this

Three things surfaced that are worth fixing regardless of whether the card gets
built, because they are wrong or missing today.

### 3.1 `myGrid` can be empty and cannot be repaired

`qso.myGrid` is snapshotted in `beginQso()` from `loc.obs()` and persisted as CSV
column 9 (exported as ADIF `MY_GRIDSQUARE`). If there was no position fix when the
QSO was started, it is empty — and **the Edit QSO screen shows it read-only**
(`drawLogEntry()` paints it in the grey header line, not as one of the 13 editable
fields). So a QSO logged before the GPS settled has no own-grid, permanently, with
no way to correct it in the UI.

That is a pre-existing bug rather than a QSL one, but it is fatal for a QSL card:
own-grid is the field the *recipient* needs for VUCC and grid credit, and it is
the one field on the card that cannot honestly be defaulted.

**Recommended fix:** make MyGrid the 12th editable field on `SCR_LOGENTRY`
(`labels[]`/`vals[]` grow by one, `LF` becomes 12/14, one `SCR_EDIT` target). Small,
and it closes the hole for ADIF export too.

### 3.2 Nothing records that a card was sent

`qso.uploaded` carries bit0 = LoTW and bit1 = Cloudlog, and **both the writer and
the parser mask with `& 0x3`** (app.cpp ~11592 and ~11618). Bit 2 is free but
currently discarded on both paths. There is no "QSL sent" or "QSL received"
concept anywhere in the log.

Without it, printing cards is fire-and-forget: nothing distinguishes a QSO you
have already carded from one you have not, which is precisely the state a paper
QSL workflow needs to track. **Recommended:** widen both masks to `& 0x7` and use
bit 2 for QSL-card-sent, set after a successful print.

### 3.3 No postal address for direct QSLing

`cfg` has `myCall`, `opName`, `opEmail`, and the LoTW identity fields
(`lotwDxcc`, `lotwCqz`, `lotwItuz`, `lotwState`, `lotwCnty`) — a good set, and the
LoTW fields are exactly what a DX chaser wants copied off a card. There is no
mailing address. A card intended for direct return needs one.

This is an open question rather than a recommendation: an address is four more
`Settings` strings and a Settings screen that is already long, and for a
bureau/eQSL-oriented operator the email is enough. See §6.

## 4. Design — `PR_QSL`

### 4.1 What it is

One `PrintReport` that renders the QSO currently held in `App::qso` as a QSL card,
through the existing sink fan-out (network printer / serial / `/CardSat/Reports`).
No new transport, no new format — it is a report with a card-shaped layout.

### 4.2 Where it is reachable from

* **`p` on `SCR_LOGENTRY`** — the natural home. That screen *is* a single QSO, and
  `p` is free there (the handler binds `x`, `s`, arrows, ENTER and back).
* **`p` on `SCR_LOGLIST`** — prints the highlighted row, by copying
  `logRecs[logListSel]` into `qso` first, exactly as ENTER already does.

**Deliberately NOT in the About → Print submenu.** Every other entry there is
self-contained; this one depends on `qso`, which holds whatever was last opened.
A menu item that silently prints a stale QSO is worse than no menu item.

### 4.3 Content, and why each field is on the card

Ordered as a card is normally read: who is sending it, who it confirms, then the
contact details.

| block | source | note |
|---|---|---|
| Callsign | `qso.myCall` (falls back to `cfg.myCall`) | the sender |
| Name | `cfg.opName` | optional |
| **Grid** | **`qso.myGrid`** | **see §4.4 — the snapshot, never the current fix** |
| DXCC / CQ / ITU / State | `cfg.lotw*` | what the recipient transcribes for awards |
| Email | `cfg.opEmail` | optional |
| Worked callsign | `qso.call` | centred, emphasised |
| Date / UTC | `qso.utc` | `YYYY-MM-DD` and `HHMMZ` |
| Satellite | `qso.sat` | |
| Prop mode | literal `SAT` | ADIF `PROP_MODE`; LoTW needs it for satellite credit |
| Mode | `qso.mode` | |
| Uplink | `qso.ulHz` + `bandFor()` | ADIF `FREQ` / `BAND` |
| Downlink | `qso.dlHz` + `bandFor()` | ADIF `FREQ_RX` / `BAND_RX` |
| RST | `qso.rstS` | labelled **sent** — see §4.5 |
| Their grid | `qso.grid` | |
| Path | `distBearing()` (app.cpp ~21633) | only when both grids are present |
| LoTW note | `qso.uploaded & 0x1` | wording matters — see §4.5 |

Printing the satellite name and `PROP MODE: SAT` explicitly is the most useful
thing a *satellite* QSL can do. Satellite contacts only match in LoTW when both
logs agree on the satellite designator, and a card that states it unambiguously
removes a common source of transcription mismatch.

### 4.4 The own-grid rule

**The card prints `qso.myGrid` and nothing else.** For a QSO logged from a rove,
a hilltop or a previous QTH, the current fix is a different grid, and substituting
it would put a false grid on a card another operator may claim VUCC credit from.
Silently wrong is the one outcome that is not acceptable here.

So: if `qso.myGrid` is empty, **refuse to print** with `"QSO has no grid - set it
on the Edit QSO screen"`. That message is only actionable once §3.1 is fixed, so
§3.1 is a prerequisite of this feature, not an optional extra.

Presented in the traditional Maidenhead casing (`FM18lu`): fields uppercase,
subsquare lowercase, normalised on the way out regardless of how it was stored.
Given the grid's importance it sits in the sender block directly under the
callsign, on its own line, not folded into a run-on with the DXCC numbers.

### 4.5 Two wording decisions that are correctness, not style

* **`RST` is the report we SENT.** A QSL confirms what you gave the other station;
  `qso.rstR` is our own received copy and is not part of the confirmation. The card
  labels it `RST SENT` rather than bare `RST`, so it cannot be read backwards.
* **`uploaded & 0x1` means "we uploaded", not "it is confirmed."** The line reads
  `Uploaded to LoTW`, never `Confirmed on LoTW`. A card claiming a confirmation
  that has not happened is a small lie that propagates into someone else's log.

### 4.6 Layout

Two renderings selected by `Printer::narrow()`, the same convention the other
reports use.

**32 columns (58 mm thermal):**

```
--------------------------------
      RADIO QSL CONFIRMATION
--------------------------------
             N8HM
          FM18lu
        Paul Stoetzer
   DXCC 291  CQ 5  ITU 8  VA
--------------------------------
  CONFIRMING TWO-WAY CONTACT
             WITH
            W1ABC
--------------------------------
 DATE  2026-08-04
 UTC   1432Z
 SAT   AO-7
 PROP  SAT
 MODE  SSB
 UP    145.9250 MHz  2m
 DOWN  432.1750 MHz  70cm
 RST SENT  59
 UR GRID   FN31pr
 PATH  565 km  brg 041
--------------------------------
   PSE QSL  /  TNX FOR THE QSO
     Uploaded to LoTW
--------------------------------
          73 de N8HM
```

**44+ columns (80 mm receipt, A4/Letter, file sink):** same blocks, `kv()` pairs
on single lines, wider rules, plus a `Signed ______________  Date ________` line —
a full-size card is a document someone signs.

### 4.7 New printer primitives

Both belong in `print.cpp` next to `title()`/`rule()`, not in `app.cpp`, because
they are layout vocabulary rather than report content.

```cpp
void center(const String& s);   // centred at each sink's own width
void boxed(const String& s);    // "| ...centred... |" within a rule() frame
```

`center()` uses the native control code where one exists (`ESC a 1` on ESC/POS,
the raster path's own centring) and pads with spaces otherwise — which is the
correct fallback for every remaining format, since all of them are being fed
monospaced text.

`boxed()` is optional and only used for the card frame. It must not be used with
`wrap()`: a wrapped line cannot be boxed without re-measuring, and the card has no
free-text field long enough to need wrapping.

### 4.8 Card-sized media — explicitly deferred

A true QSL is 5.5 × 3.5 in. Three tiers, of which only the first is proposed now:

1. **Text-flow card (this design).** Works on all nine formats today. On a
   receipt printer the result is genuinely card-like; on a sheet printer it is a
   card-shaped block at the top of a page.
2. **Crop marks on a sheet.** PCL / PostScript / raster only: draw the 140 × 89 mm
   outline plus corner marks so the operator can cut. Moderate work, page-format
   only, natural follow-on.
3. **ZPL at card size.** `FMT_ZPL` already positions `^FO/^FD` fields inside
   `^XA...^XZ`, and a 4 × 6 in label stock is the closest thing in the existing
   format set to real card stock. This is the most promising route to something
   that looks like a printed QSL, and it is also the narrowest in reach.

Tier 1 first. Tiers 2 and 3 are separate scopes.

## 5. Work items, in order

1. **MyGrid editable on `SCR_LOGENTRY`** (§3.1) — prerequisite.
2. `Printer::center()` (+ optional `boxed()`) in `print.cpp` / `print.h` (§4.7).
3. `PR_QSL` enum entry, `prtStem()` case (`"qsl"`), `printQsl()` placed **after**
   `bandFor()` and `distBearing()` — the placement rule from the 0.9.56 compile
   errors.
4. `p` handlers on `SCR_LOGENTRY` and `SCR_LOGLIST` (§4.2).
5. QSL-sent bit (§3.2): widen both `& 0x3` masks to `& 0x7`, set bit 2 after a
   successful print, show it on the Edit QSO screen beside the LoTW/Cloudlog rows.
6. Width check at 32 and 44 columns; `audit_screen_geometry` does not cover
   printed output, so this is a read-the-output check against both sink widths.
7. Mark `PRINTING_GAPS.md` closed and point it here.

Both representations for every source change, and the nine gates.

## 6. What was actually built (0.9.73)

Scoped down on the owner's direction: this is a **hamfest hand-out**, not a
replacement for a desktop logger's card designer. It must be a valid confirmation
and nothing more.

Built:

* `Printer::center()` (§4.7). `boxed()` was **not** built -- rules top and bottom
  already read as a card and side borders buy nothing on a receipt.
* MyGrid editable on `SCR_LOGENTRY` (§3.1), and the own-grid refusal rule (§4.4).
* `PR_QSL` / `printQsl()`, `p` on `SCR_LOGENTRY` and `SCR_LOGLIST` (§4.2).
* `tools/host_qsl/qsl_layout_test.sh` -- renders the card at 32/42/48/64/80
  columns for a full, a bare and a long-callsign QSO, and fails on any overflow.

**Declined, by decision rather than oversight:**

* **Postal address (§3.3).** A card you hand to someone standing in front of you
  does not need a return address.
* **QSL-sent bit (§3.2).** Fire-and-forget is the right model for a hand-out;
  tracking what has been carded is a logging-program concern.
* **Batch mode**, crop marks, ZPL card stock (§4.8 tiers 2 and 3).

One layout change came out of the width check: `PROP  SAT (via amateur satellite)`
is 33 characters, which `kv()` split into a stranded `PROP` line on 58 mm paper.
The field now carries the bare ADIF token `SAT`, and the plain-English
`worked via amateur satellite` sits above the sign-off -- where it reads as a
sentence, and where the non-ham at the table will actually see it.

## 7. Open questions

* **Batch mode** and the card-stock tiers stay open, and both depend on the
  QSL-sent bit (§3.2) that was declined. Noted so that if either is ever wanted,
  the bit is designed first rather than bolted on.
* **`Fn`+`p` vs `p`.** `p` is free on both target screens, so plain `p` follows
  the established pattern. Flagging only because `SCR_LOGENTRY` is a
  field-editing screen and `p` is also a literal character — the handler runs
  before any text-entry path, but it is worth a bench check that typing a callsign
  containing `p` cannot reach it.
