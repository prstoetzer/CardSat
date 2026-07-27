# MINIMUF-3.5 reference — NOSC Technical Document 201

Rose, R.B. & Martin, J.N., *MINIMUF-3.5: Improved version of MINIMUF-3, a simplified
hf MUF prediction algorithm*, Naval Ocean Systems Center TD 201, 26 October 1978.
Public domain (US Navy work). DTIC AD-A066256; full scan on the Internet Archive.

## What this is

A simplified single-hop HF MUF model. Given path endpoints, date/UT and a sunspot
number it returns the maximum usable frequency in MHz. It is an F-region approximation
(ignores sporadic-E), most accurate on 800–8000 km one/two-hop paths, ~4 MHz RMS
against measured ionosphere. `src/app.cpp:minimufMHz()` is the implementation.

## Provenance of the implementation

The TD-201 BASIC listing (lines 1000–1740) was transcribed, but the only openly
available copy is a 1978 microfilm scan whose OCR corrupts several operators. Three
of those corruptions changed the result and were **not** recoverable from the scan
alone. They were resolved by cross-checking DXSpider's `perl/Minimuf.pm`
(github.com/latchdevel/DXspider, itself derived from the NOSC C source):

1. **Decay constant.** Line 1400 is `T9 = 9.7 * C0 ↑ 9.6`. The `↑` is Tektronix BASIC
   exponentiation, which OCR rendered as `+9.6`. Using addition made the daytime
   recovery limb collapse — the model was ~40 % low for half the diurnal cycle.
2. **Daylight-length numerator.** Line 1350 is `(-0.26 + sin(Y2)·sin(L0)) / …` — a
   **plus**, which the scan showed as `-0.26 · sin(…)` (a multiply). This sets the
   exact sunrise/sunset instant, so the error was invisible everywhere except the
   pre-dawn transition hours, where the reference goes 13.7 → 21.0 → 27.6 and the
   mis-transcribed version stayed flat at 13.3.
3. **Deep-night test.** Line 1270 branches to deep night on `cos(L0+Y2) <= -0.26`;
   DXSpider folds the arc selection into one combined boolean with an explicit `>0` /
   `<=0` split, which is correct at the boundary.

The `pow(C0,9.6)` exponentiation (item 1) was found independently before the DXSpider
check; items 2 and 3 were found only by the cross-check.

## Verification vector (TD-201 Figure 1)

TX 21°N, 156°W; RX 38°N, 122°W; 17 October; SSN 110. The published 24-hour table:

```
HR MUF   HR MUF   HR MUF   HR MUF
 0 32.0   6 20.9  12 14.6  18 32.0
 1 32.0   7 19.3  13 14.1  19 32.0
 2 32.0   8 18.0  14 13.7  20 32.0
 3 29.9   9 16.9  15 21.0  21 32.0
 4 25.0  10 16.0  16 27.6  22 32.0
 5 22.0  11 15.2  17 31.5  23 32.0
```

`minimufMHz()` reproduces this to **0.16 MHz RMS**, max 0.76 MHz (hour 5, an
arc-transition hour rounding against a table printed to 0.1 MHz). `muf_verify.sh`
extracts the function from the live `src/app.cpp` and asserts every hour within
0.8 MHz, so a transcription regression — which would move several hours by whole
MHz — fails immediately.

## Input convention

Latitudes and longitudes in **radians, west longitude positive**; month 1–12,
day 1–31, UT hour 0–23, SSN the sunspot number (convert from 10.7 cm flux via the
TD-201 Figure-2 relationship if only flux is on hand).
