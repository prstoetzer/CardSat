#!/usr/bin/env python3
# CardSat 4x6 index-card key-reference generator (landscape, front + back).
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame,
                                Paragraph, PageBreak)
from reportlab.lib.styles import ParagraphStyle
from pypdf import PdfReader
import os, re

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CardSat_CheatCard_4x6.pdf")

# Pull the firmware version from src/config.h so the card never goes stale.
def _fw_version():
    here = os.path.dirname(os.path.abspath(__file__))
    cfg = os.path.join(here, "src", "config.h")
    try:
        m = re.search(r'FW_VERSION\s*=\s*"([0-9.]+)"', open(cfg).read())
        if m:
            return m.group(1)
    except Exception:
        pass
    return "0.0.0"
FW_VER = _fw_version()
PAGE_W, PAGE_H = 6 * inch, 4 * inch            # 432 x 288 pt (landscape 4x6)
ACCENT    = colors.HexColor('#0B7A3B')
ACCENT_DK = colors.HexColor('#064F26')
RULE      = colors.HexColor('#D7D7D7')
BAND_H, LM, RM, BM, GUT = 19, 12, 12, 10, 10
TOPGAP    = BAND_H + 3
frame_top = PAGE_H - TOPGAP
frame_h   = frame_top - BM
NCOL      = 2
col_w     = (PAGE_W - LM - RM - (NCOL - 1) * GUT) / float(NCOL)
XS        = [LM + i * (col_w + GUT) for i in range(NCOL)]

FRONT = [
 ("GLOBAL",
  "<b>;</b> up &middot; <b>.</b> down &middot; <b>,</b> <b>/</b> left/right &middot; "
  "<b>ENTER</b> select &middot; <b>`</b>/<b>DEL</b> back &middot; Home: 2-col grid, letter jumps, <b>,</b>/<b>/</b> hop columns &middot; "
  "<b>{</b> <b>}</b> page &middot; <b>b</b> screenshot &middot; <b>h</b> help &middot; <b>Fn+`</b>/<b>Fn+DEL</b> STOP all radio/rotator control "
  "(Help links: <b>g</b> glossary+math &middot; <b>m</b> user guide &middot; <b>s</b> sat history &middot; <b>t</b> tech help &middot; <b>l</b> learn theory &middot; <b>f</b> band plan)"),
 ("HOME",
  "<b>ENTER</b> opens item; menu scrolls: Satellites, Next Passes, Passes, Track, "
  "World Map, Sun/Moon, Space Wx, Weather, Activations, Overhead now, Grid dist/bearing, QRZ Lookup, Location, Update, Settings, Log, Messages, About, Charge/Sleep"),
 ("SATELLITES",
  "<b>f</b> favorite &middot; <b>v</b> favs-only &middot; <b>n</b> new GP sat &middot; <b>x</b> del manual sat &middot; "
  "<b>e</b> EQX table &middot; <b>k</b> OSCARLOCATOR &middot; <b>3</b> 3D globe &middot; <b>2</b> sat-to-sat &middot; <b>o</b> orbital &middot; <b>y</b> sim &middot; <b>t</b> transponders &middot; <b>d</b> 10-day &middot; <b>i</b> illum &middot; <b>L</b> share GP over LoRa &middot; <b>ENTER</b> passes &middot; "
  "right edge: dot = AMSAT heard, square = telemetry, ring = not heard"),
 ("SAT-TO-SAT (Sats &rarr; 2)",
  "Windows when selected sat + a 2nd fav are BOTH above your horizon (start, dur, peak el each). <b>n</b> next sat &middot; <b>r</b> recompute &middot; <b>`</b> back"),
 ("LIVE GPS POS (Location &rarr; v)",
  "DMS lat/lon (max precision) + decimal, alt, grid, speed, course. Rovers/portable. <b>`</b> back"),
 ("3D GLOBE (Sats &rarr; 3)",
  "Wireframe Earth, auto-follows selected sat. Graticule, coastline, "
  "day/night terminator, QTH, favs, selected sat + footprint + ground-track trail. <b>g</b> DX 2nd location (grid) + footprint &middot; <b>G</b> clear &middot; <b>arrows</b> turn &middot; <b>ENTER</b> re-follow &middot; <b>`</b> back"),
 ("EQX TABLE (Sats &rarr; e)",
  "Equator-crossing UTC + longitude (W-positive) for OSCARLOCATOR, next 3 days. "
  "<b>;</b>/<b>.</b> scroll &middot; <b>d</b> asc/desc node &middot; <b>r</b> recompute &middot; <b>`</b> back"),
 ("OSCARLOCATOR (Sats &rarr; k)",
  "Live azimuthal-equidistant plot: sub-point, footprint, QTH range ring + full ground track (AOS/LOS). "
  "<b>m</b> toggle polar (default, auto N/S, flips at equator) / QTH-centred &middot; <b>`</b> back"),
 ("DX DOPPLER TABLE (Mutual &rarr; ENTER &rarr; d)",
  "RX/TX dial freqs for BOTH stations every 30s across a mutual window. Two lines/step: me (green) + DX (cyan). From the Mutual list, <b>ENTER</b> opens a polar detail (me+DX arcs, AOS/LOS/el) then <b>d</b> the table (or <b>d</b> straight from the list). "
  "<b>t</b> cycle transponder &middot; <b>m</b> mode: true rule / fixed DL / fixed UL &middot; <b>a</b> anchor dial (me/DX RX/TX) &middot; <b>,</b>/<b>/</b> in fixed mode step anchored dial to round 1 kHz (else passband 1 kHz) &middot; header shows pb +/- from center &middot; <b>;</b>/<b>.</b> scroll &middot; <b>`</b> back"),
 ("NEXT PASSES (favs)",
  "<b>ENTER</b> track &middot; <b>m</b> world map &middot; <b>t</b> sky-at-a-glance timeline (bars by elev) &middot; <b>p</b> rove planner &middot; <b>w</b> workable horizon &middot; <b>s</b> target search &middot; <b>r</b> refresh &middot; <b>z</b> deep-sleep until AOS"),
 ("WORKABLE HORIZON (Next Passes &rarr; w)",
  "10-day union of everything workable across all favorites. <b>s</b> states list &middot; <b>d</b> DXCC list &middot; <b>g</b> re-run incl. grids (off by default) &middot; <b>w</b> save to /CardSat/workable/ &middot; <b>`</b> back"),
 ("TARGET SEARCH (Next Passes &rarr; s)",
  "Pick one state / DXCC / grid; lists every pass over 10 days where it's workable, time-ordered across all favs (sat / date / window / maxEl). <b>ENTER</b> polar of that pass &middot; <b>w</b> save to /CardSat/search/ &middot; <b>`</b> back"),
 ("ROVE PLANNER (Next Passes &rarr; p)",
  "Survey all favorites from a planned grid+time. Form: grid / date / time / +/- hrs / GO. Rows by AOS: sat / AOS / maxEl / states / DXCC. <b>ENTER</b> detail (arc + <b>s</b>/<b>d</b>/<b>g</b> state/DXCC/grid lists) &middot; <b>w</b> save txt &middot; <b>l</b> saved plans (<b>ENTER</b> view, <b>;</b>/<b>.</b> scroll, <b>d</b> del) &middot; <b>g</b> form"),
 ("PASSES (sel)",
  "<b>*</b> = optically visible. <b>;</b>/<b>.</b> select &middot; <b>d</b> detail &middot; <b>t</b>/<b>ENTER</b> track &middot; "
  "<b>n</b> add TX &middot; <b>r</b> recompute &middot; <b>x</b> mutual-DX &middot; "
  "<b>v</b> 10-day chart &middot; <b>V</b> visible-pass list &middot; <b>i</b> illum &middot; <b>g</b> grids &middot; <b>w</b> US states &middot; <b>e</b> DXCC &middot; <b>p</b> print"),
 ("TRACK (sel)",
  "<b>`</b> exits to previous screen &amp; KEEPS radio/rotator tracking (green RAD/ROT/R+R in header); <b>r</b>/<b>o</b> to stop. "
  "<b>m</b> TUNE/CAL &middot; <b>d</b> tune mode (FULL/DL/UL/hold) &middot; <b>t</b> next TX &middot; <b>n</b> jump to beacon &middot; "
  "<b>c</b> CTCSS &middot; <b>N</b> sat note &middot; <b>k</b> CW both legs (linear) &middot; <b>r</b> radio &middot; <b>o</b> rotator &middot; <b>p</b> polar &middot; <b>a</b> point-here arrow &middot; <b>z</b> big readout &middot; "
  "<b>y</b> tilt on/off (ADV) &middot; "
  "<b>f</b> Manual &middot; <b>l</b> log QSO &middot; <b>v</b> voice memo (SD) &middot; <b>g</b> grids &middot; <b>w</b> states &middot; <b>e</b> DXCC now &middot; <b>i</b>&times;2 report Heard (AMSAT) &middot; <b>ENTER</b> save cal"),
 ("BIG READOUT (z from Track)",
  "Big RX/TX + az/el + tune mode (follows Track). Radio+rotator keep tracking &middot; "
  "<b>,</b>/<b>/</b> tune &middot; <b>s</b>/<b>x</b> step/ctr &middot; <b>m</b>/<b>d</b> mode &middot; "
  "<b>t</b> TX &middot; <b>n</b> beacon &middot; <b>k</b> CW (linear) &middot; <b>r</b> radio &middot; <b>o</b> rot &middot; <b>y</b> tilt &middot; <b>l</b> log &middot; <b>z</b>/<b>`</b> back"),
 ("MANUAL (no radio)",
  "Fix one leg, read Doppler freq to tune the other by hand. <b>u</b> toggle "
  "HOLD/TUNE leg &middot; <b>,</b>/<b>/</b> passband (linear) &middot; <b>m</b> CAL &middot; "
  "<b>t</b> next TX &middot; <b>z</b> big view &middot; <b>l</b> log &middot; <b>p</b> polar &middot; <b>g</b> grids &middot; <b>w</b> states &middot; <b>e</b> DXCC &middot; <b>`</b>/<b>f</b> Track"),
 ("MANUAL BIG (z from Manual)",
  "HOLD/TUNE legs in big digits &middot; <b>u</b> swap leg &middot; <b>,</b>/<b>/</b> tune &middot; "
  "<b>s</b>/<b>x</b> &middot; <b>m</b> CAL &middot; <b>t</b> TX &middot; <b>z</b>/<b>`</b> back"),
 ("TRACK &middot; TUNE",
  "<b>,</b>/<b>/</b> tune -/+ &middot; <b>s</b> step 100/1k/5k &middot; <b>x</b> recenter &middot; tilt to tune if enabled (ADV)"),
 ("TRACK &middot; CAL",
  "<b>,</b>/<b>/</b> downlink &middot; <b>;</b>/<b>.</b> uplink &middot; "
  "<b>s</b> step 10/100/1k &middot; <b>x</b> zero"),
 ("WORKABLE GRIDS / STATES / DXCC",
  "Footprint coverage (per-pass union or live now), count on a cyan line: "
  "<b>g</b> 4-char grids &middot; <b>w</b> US states+DC &middot; <b>e</b> DXCC (all 340, "
  "hybrid polygons+points). On grids, <b>f</b> prefix filter (EM/EM2/EM21, upper-cased), <b>c</b> clear. <b>;</b>/<b>.</b> &amp; <b>{</b>/<b>}</b> scroll &middot; <b>`</b> back"),
 ("POLAR / PASS DETAIL",
  "Pass detail: <b>p</b> polar of this pass. Polar: <b>l</b> log QSO &middot; <b>v</b> voice memo (SD) &middot; "
  "<b>p</b>/<b>ENTER</b>/<b>`</b> back"),
 ("SUN / MOON",
  "Graphic sky-dome (Sun/Moon glyphs by az/el) &middot; <b>g</b> graphic/list &middot; "
  "<b>;</b>/<b>.</b> pick Sun/Moon &middot; <b>o</b> rotor track on/off &middot; "
  "<b>s</b> sky sources &middot; <b>t</b> transits &middot; <b>e</b> EME &middot; <b>x</b> stop &middot; <b>`</b> back"),

 ("SKY SOURCES (Sun/Moon &rarr; s)",
  "Planets (cyan dots) + strong radio sources (orange +): Cas A, Cyg A, galactic centre, Crab, Virgo A, on a sky dome. "
  "Antenna-pointing / RF reference. <b>;</b>/<b>.</b> select &middot; <b>`</b> back"),
 ("EME / MOONBOUNCE (Sun/Moon &rarr; e)",
  "Self-echo Doppler per band (50/144/432/1296/10368, topocentric) &middot; range + rate &middot; path degradation vs perigee &middot; "
  "galactic sky-noise flag &middot; SUN flag &lt;10&deg; &middot; <b>m</b> mutual window &middot; <b>p</b> 30-day plan &middot; <b>o</b> rotor track Moon &middot; <b>`</b> back"),
 ("GRID DIST/BEARING (menu)",
  "Enter Maidenhead grid &rarr; great-circle distance + beam heading (short/long path, km/mi). "
  "<b>g</b> grid &middot; <b>q</b> QRZ&rarr;grid lookup (seeds calc) &middot; <b>o</b> point rotor at bearing &middot; <b>`</b> back"),
]

BACK = [
 ("SPACE WX (menu)",
  "Solar 10.7cm flux + planetary Kp + A index + aurora likelihood (from Kp), labelled "
  "&amp; colour-coded, with HF/sat operating outlook &amp; data age &middot; <b>p</b> HF/6m propagation &middot; <b>r</b> refresh (WiFi) &middot; <b>`</b> back"),
 ("HF/6m PROPAGATION (Space Wx &rarr; p)",
  "Turns solar flux + Kp into band guidance: HF conditions (10/15/20m open/marg/shut), geomagnetic effect, "
  "auroral-VHF likelihood (6m/2m, beam N), D-layer absorption. Rule-of-thumb &middot; <b>r</b> refresh &middot; <b>`</b> back"),
 ("WEATHER (menu)",
  "Current conditions + multi-day forecast for your site (Open-Meteo). "
  "Refreshes on entry (WiFi) &amp; with Update. Units in Settings &middot; "
  "<b>r</b> refresh &middot; cached offline &middot; <b>`</b> back"),
 ("QRZ LOOKUP (menu)",
  "Callsign lookup via QRZ.com XML (needs QRZ XML subscription + user/pass in "
  "Settings &rarr; Network). <b>ENTER</b> type call &rarr; name/addr/grid/class. WiFi req'd &middot; <b>`</b> back"),
 ("BAND PLAN (Help &rarr; f)",
  "Worldwide amateur band reference LF&rarr;light: HF with ITU R1/R2/R3 splits, VHF/UHF/microwave EME+calling freqs, "
  "satellite subbands, IARU designators (H/A/V/U/L/S/C/X/K), sat modes incl QO-100. <b>;</b>/<b>.</b> scroll &middot; <b>`</b> back"),
 ("ACTIVATIONS (menu)",
  "Upcoming sat activations on hams.at (roves, grid/special ops). List: date, call, sat, grid (* = your own entry). <b>;</b>/<b>.</b> move &middot; <b>ENTER</b> detail &middot; <b>n</b> add your own sked (offline OK), <b>e</b> edit a * entry &middot; <b>r</b> refresh &middot; <b>`</b> back. Detail: UTC/mode/freq + <b>footprint note</b> (checks co-visibility with the activator +/-30 min of listed time), <b>;</b>/<b>.</b> scroll the full comment, <b>a</b> SKED reminder (T-60/30/10 beeps+flash), <b>w</b> mutual-window screen if a footprint exists. Mutual window: small polar plot (me + DX arcs), Date/AOS/LOS/dur + peak el each; <b>d</b> DX Doppler pre-set to the transponder &amp; fixed DL/UL parsed from the freq/comment (default table if none). Cached to card &mdash; last list shows offline; WiFi to refresh"),
 ("OVERHEAD NOW (menu)",
  "Snapshot of every catalog sat above the horizon right now, sorted by elevation, with az + rise compass dir (high=green, low=yellow) + count up/scanned. <b>r</b> rescan (this instant) &middot; <b>;</b>/<b>.</b> scroll &middot; <b>`</b> back"),
 ("TRANSPONDER DB (Sats &rarr; t)",
  "Sat's transponder/beacon entries, two lines each: <b>D</b>=downlink+mode line, "
  "<b>U</b>=uplink+tone/inv line. Ordered two-way &gt; amateur &gt; active; inactive dimmed &amp; <b>(off)</b>. <b>;</b>/<b>.</b> select (* = manual) &middot; <b>x</b> del manual (2x) &middot; <b>`</b> back"),
 ("ORBITAL ANALYSIS",
  "<b>,</b>/<b>/</b> 10 pages: Info / Live / Next pass / Ground track / Doppler / Nodal / Sun-Beta / "
  "Pass outlook / Orbit position / Phys (velocity + launch date/age) &middot; <b>r</b> recompute &middot; Doppler <b>f</b> sets beacon freq"),
 ("SIMULATION",
  "<b>,</b>/<b>/</b> step time &middot; <b>;</b>/<b>.</b> step size &middot; "
  "<b>m</b> world-map view (sub-point + footprint) &middot; "
  "<b>x</b> reset to now &middot; <b>`</b> back"),
 ("LOCATION",
  "<b>e</b>/<b>o</b>/<b>a</b> lat/lon/alt &middot; <b>g</b> grid &middot; <b>p</b> GPS on/off &middot; "
  "<b>s</b> GPS source &middot; <b>c</b> set clock &middot; <b>v</b> live position (DMS) &middot; <b>ENTER</b> GPS sky plot"),
 ("GPS SKY PLOT",
  "Live GNSS by az/el, coloured by signal (green=strong, grey=weak) &middot; <b>`</b> back"),
 ("WORLD MAP",
  "All footprints + night-side shading &middot; <b>f</b> highlight favorite &middot; <b>c</b> recenter on QTH/0&deg; &middot; "
  "yellow=sunlit cyan=eclipse &middot; <b>`</b> back"),
 ("SCHEDULES",
  "10-day: <b>;</b>/<b>.</b> scroll +/-1 day (fills full days), <b>r</b> recompute &middot; "
  "Illum: <b>,</b>/<b>/</b> scroll +/-60 days &middot; Mutual: <b>;</b>/<b>.</b> scroll, <b>ENTER</b> polar detail, <b>d</b> Doppler"),
 ("LOG",
  "Menu (scrolls): <b>ENTER</b> new QSO / browse / export ADIF / LoTW upload / Cloudlog upload / voice memos / notes / awards &middot; <b>Awards</b>: QSO/grid/state/DXCC totals (states+DXCC derived from grid), <b>g</b>/<b>s</b>/<b>d</b> scrollable worked lists, <b>ENTER</b> per-sat &middot; List: <b>;</b>/<b>.</b> scroll, "
  "<b>ENTER</b> edit &middot; Entry: <b>;</b>/<b>.</b> field, <b>ENTER</b> edit, <b>s</b> save, "
  "<b>x</b> x2 delete &middot; editing a QSO re-arms its upload; extra <b>LoTW</b>/<b>Cloudlog</b> rows (ENTER toggles) override that"),
 ("SIGN &amp; UPLOAD TO LoTW (Log &rarr; Sign &amp; upload)",
  "Uploads sat QSOs direct to ARRL LoTW over WiFi. Needs SD + your LoTW key (lotw_key.pem/lotw_cert.pem in /CardSat/ &mdash; make them from your TQSL .p12 with tools/lotw_cert_converter.html in a browser; in Settings pick DXCC &rarr; primary (state/province/...) &rarr; secondary (county/city) + zones + IOTA, all gated pickers w/ type-to-filter) &mdash; see manual §8. <b>u</b> upload &middot; <b>a</b> re-send all &middot; <b>`</b> back"),
 ("UPLOAD TO CLOUDLOG (Log &rarr; Upload to Cloudlog)",
  "Uploads sat QSOs to a self-hosted Cloudlog/Wavelog over WiFi (an alternative to LoTW &mdash; Cloudlog can forward to LoTW). Set <b>URL</b> + read-write <b>API key</b> + <b>station ID</b> in Settings. <b>u</b> upload &middot; <b>a</b> re-send all &middot; <b>`</b> back"),
 ("VOICE MEMOS (Log &rarr; Voice Memos)",
  "Browse newest-first (date/time/sat/len). <b>ENTER</b> play &middot; <b>n</b> new &middot; <b>d</b> del &middot; <b>r</b> refresh &middot; record via <b>v</b> on Track. SD req'd"),
 ("NOTES (Log &rarr; Notes)",
  "Free-form text notes (.txt in /CardSat/notes/, newest-first w/ date/time). Browser: <b>ENTER</b> open &middot; <b>n</b> new &middot; <b>d</b>+ENTER del. "
  "Editor (commands use <b>Fn</b> so <b>;</b><b>.</b><b>,</b><b>/</b> type): type freely, ENTER=newline, DEL=backspace &middot; <b>Fn+,</b>/<b>Fn+/</b> cursor L/R &middot; <b>Fn+;</b>/<b>Fn+.</b> up/down &middot; <b>Fn+s</b> save &middot; <b>`</b> exit (auto-saves)"),
 ("LORA MESSAGES (Home &rarr; Messages)",
  "CardSat-to-CardSat broadcast chat (Cap LoRa). Same freq/SF/BW = same group. Set <b>region</b> (US 33cm / EU 70cm / JP 430) in Settings for a legal default freq. <b>n</b> write/send &middot; <b>;</b>/<b>.</b> scroll+select (newest on bottom) &middot; <b>o</b> station roster &middot; <b>r</b> retry &middot; <b>m</b> LoRa RX/hex monitor &middot; <b>`</b> back. <b>Actionable msgs</b> (plain text, interop): a msg with <b>@lat,lon</b> / <b>#SAT</b> / <b>!SAT date time</b> &rarr; <b>ENTER</b> opens bearing compass (dist/brg/grid) / sat detail / pre-filled sked; <b>#SAT</b> carries NORAD (<b>#name/norad</b>) so it resolves across differing names. Send for the current sat &amp; your location: <b>p</b> position (also a presence ping) &middot; <b>s</b> satellite &middot; <b>k</b> sked (date&rarr;time). <b>Roster</b> (<b>o</b>): who is heard, with grid + dist/brg + signal; ENTER=compass, <b>p</b>=ping. Opt-in <b>Auto position reply</b> setting answers others&rsquo; positions (loop-guarded). <b>Rx always on</b>: badge + banner (opt-in beep, <b>Msg notify</b> in Settings). <b>Share GP elements</b>: a peer&rsquo;s <b>Sats &rarr; L</b> pops an <b>Import satellite?</b> prompt (sender/name/NORAD) &mdash; <b>y</b> add/update, <b>n</b> decline; checksum-verified, experimental. Needs RadioLib build."),
 ("LORA RX / HEX MONITOR (Messages &rarr; m)",
  "Receive/inspect <b>any</b> LoRa signal (not just sats). Config: set freq (type in <b>MHz</b>), SF, BW, CR, sync, preamble, CRC &mdash; saved across reboots. <b>ENTER</b> starts RX. Monitor: live hexdump+ASCII, RSSI/SNR, <b>p</b> pause (read a frame on a busy channel) &middot; <b>;</b>/<b>.</b> scroll &middot; <b>s</b>/<b>b</b>/<b>c</b>/<b>f</b> tune SF/BW/CR/step &middot; <b>,</b>/<b>/</b> freq &middot; <b>`</b> esc. Rx-only. Untested HW"),
 ("UPDATE",
  "<b>k</b>/<b>ENTER</b> GP+clock+AMSAT+space-wx+weather &middot; <b>f</b> fast (GP+AMSAT+favs' TX) &middot; "
  "<b>a</b> cache all TX (auto-reboots) &middot; <b>w</b> WiFi only"),
 ("SETTINGS",
  "<b>,</b>/<b>/</b> change &middot; <b>ENTER</b> edit/toggle &middot; <b>s</b> scan WiFi "
  "&middot; opt. 2nd WiFi (field fallback) &middot; reset = ERASE"),
 ("GP SOURCE",
  "<b>AMSAT</b> / <b>CelesTrak</b> JSON-PP category / <b>Custom URL</b> &middot; <b>;</b>/<b>.</b> move &middot; <b>ENTER</b> select"),
 ("ROTATOR (manual)",
  "<b>,</b>/<b>/</b> az &middot; <b>;</b>/<b>.</b> el &middot; <b>s</b> step &middot; "
  "<b>x</b> stop &middot; <b>`</b> back"),
 ("NETWORK SERVERS",
  "<b>rigctld</b> PC drives rig (VFOA=DL/B=UL) &middot; <b>rotctld</b> PC drives GS-232 &middot; "
  "<b>rigctl</b> drives remote rig &middot; <b>Web</b> opt-in mobile page (no auth, trusted LAN): live sky plot, "
  "Doppler readout, tap-to-copy freqs, visible-pass list + AOS alerts, radio/rotator control, <b>Files</b> = download-only /CardSat browser (no upload)"),
 ("EDIT",
  "type &middot; <b>DEL</b> backspace &middot; <b>ENTER</b> ok &middot; <b>`</b> cancel"),
 ("ABOUT",
  "Build/version, IP, free heap and diagnostics (read-only). <b>r</b> Station readiness checklist &middot; <b>t</b> <b>Tools</b> (35): scientific/graphing/programmer calculators, <b>Tiny BASIC</b>, <b>location converter</b> (grid/DMS/DDM/Plus/UTM/MGRS), DXCC/CQ/ITU lookups, RF/antenna workbench, link budget, phasing/stub, attenuator, RF exposure, orbit lifetime, <b>State vector &rarr; GP</b> (fit mean elements from a launch state vector, TEME/J2000, save as sat), Q-codes/phonetics/RST, CTCSS &middot; <b>p</b> <b>Print</b>: 19 reports to network printer / serial / 80-col /Reports file (any mix); contextual <b>p</b> on report screens, <b>P</b> all-passes &amp; polar map &middot; <b>l</b> License &amp; credits &middot; <b>z</b> <b>Games menu</b>: six mini-games (<b>;</b>/<b>.</b> pick, ENTER launch)."),
]


TOTAL_PAGES = 2  # updated at build time


def header(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor('#BBBBBB')); canvas.setLineWidth(0.6)
    canvas.rect(2, 2, PAGE_W - 4, PAGE_H - 4)
    canvas.setFillColor(ACCENT)
    canvas.rect(0, PAGE_H - BAND_H, PAGE_W, BAND_H, fill=1, stroke=0)
    canvas.setFillColor(colors.white)
    canvas.setFont('Helvetica-Bold', 10.5); canvas.drawString(9, PAGE_H - 13.4, 'CardSat')
    canvas.setFont('Helvetica', 7.6)
    canvas.drawString(62, PAGE_H - 13.0, 'v' + FW_VER + '  \u00b7  Key Reference')
    pg = canvas.getPageNumber()
    side = 'Front \u00b7 operating' if pg == 1 else 'Back \u00b7 setup & tools'
    canvas.drawRightString(PAGE_W - 9, PAGE_H - 13.0, '%s   %d/%d' % (side, pg, TOTAL_PAGES))
    canvas.setStrokeColor(RULE); canvas.setLineWidth(0.4)
    for i in range(1, NCOL):
        rx = XS[i] - GUT / 2.0
        canvas.line(rx, BM, rx, frame_top)
    canvas.restoreState()


def _styles(body_fs):
    tf = body_fs + 0.7
    t = ParagraphStyle('t', fontName='Helvetica-Bold', fontSize=tf,
                       leading=tf + 0.6, textColor=ACCENT_DK,
                       spaceBefore=2.0, spaceAfter=0.5)
    b = ParagraphStyle('b', fontName='Helvetica', fontSize=body_fs,
                       leading=body_fs + 0.9, spaceAfter=1.5, textColor=colors.black)
    return t, b


def _frames():
    return [Frame(XS[i], BM, col_w, frame_h, leftPadding=0, rightPadding=2,
                  topPadding=0, bottomPadding=0, showBoundary=0) for i in range(NCOL)]


def measure(sections, fs):
    import io
    t, b = _styles(fs)
    doc = BaseDocTemplate(io.BytesIO(), pagesize=(PAGE_W, PAGE_H),
                          leftMargin=LM, rightMargin=RM, topMargin=0, bottomMargin=BM)
    doc.addPageTemplates(PageTemplate(id='c', frames=_frames()))
    story = []
    for title, body in sections:
        story += [Paragraph(title, t), Paragraph(body, b)]
    doc.build(story)
    return doc.page


def best_fs(sections, hi=9.5, lo=3.5):
    fs = hi
    while fs >= lo:
        if measure(sections, fs) <= 1:
            return fs
        fs -= 0.25
    return lo


def build(front_fs, back_fs):
    doc = BaseDocTemplate(OUT, pagesize=(PAGE_W, PAGE_H),
                          leftMargin=LM, rightMargin=RM, topMargin=0, bottomMargin=BM)
    doc.addPageTemplates(PageTemplate(id='card', frames=_frames(), onPage=header))
    ft, fb = _styles(front_fs)
    bt, bb = _styles(back_fs)
    story = []
    for title, body in FRONT:
        story += [Paragraph(title, ft), Paragraph(body, fb)]
    story.append(PageBreak())
    for title, body in BACK:
        story += [Paragraph(title, bt), Paragraph(body, bb)]
    doc.build(story)


front_fs = best_fs(FRONT)
back_fs  = best_fs(BACK)
# Front is always 1 page; back may span more than one at the legibility floor.
TOTAL_PAGES = measure(FRONT, front_fs) + measure(BACK, back_fs)
print("front_fs=%.2f  back_fs=%.2f  pages=%d" % (front_fs, back_fs, TOTAL_PAGES))
build(front_fs, back_fs)
pages = len(PdfReader(OUT).pages)
print("final pages:", pages)
