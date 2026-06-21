#!/usr/bin/env python3
# CardSat 4x6 index-card key-reference generator (landscape, front + back).
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame,
                                Paragraph, PageBreak)
from reportlab.lib.styles import ParagraphStyle
from pypdf import PdfReader

OUT = "/home/claude/CardSat_CheatCard_4x6.pdf"
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
  "<b>ENTER</b> select &middot; <b>`</b>/<b>DEL</b> back &middot; "
  "<b>{</b> <b>}</b> page &middot; <b>b</b> screenshot &middot; <b>h</b> help"),
 ("HOME",
  "<b>ENTER</b> opens item; menu scrolls: Satellites, Next Passes, Passes, Track, "
  "World Map, Sun/Moon, Space Wx, Weather, QRZ Lookup, Location, Update, Settings, Log, Messages, About"),
 ("SATELLITES",
  "<b>f</b> favorite &middot; <b>v</b> favs-only &middot; <b>n</b> new GP sat &middot; <b>x</b> del manual sat &middot; "
  "<b>e</b> EQX table &middot; <b>k</b> OSCARLOCATOR &middot; <b>3</b> 3D globe &middot; <b>2</b> sat-to-sat &middot; <b>o</b> orbital &middot; <b>s</b> sim &middot; <b>t</b> transponders &middot; <b>d</b> 10-day &middot; <b>i</b> illum &middot; <b>ENTER</b> passes &middot; "
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
  "<b>m</b> toggle QTH-centred / polar (auto N/S, flips at equator) &middot; <b>`</b> back"),
 ("DX DOPPLER TABLE (Mutual &rarr; d)",
  "RX/TX dial freqs for BOTH stations every 30s across a mutual window, for the selected transponder (pick with <b>t</b>). "
  "<b>m</b> mode: true rule / fixed DL / fixed UL &middot; <b>a</b> anchor dial (me/DX RX/TX) &middot; <b>,</b>/<b>/</b> linear passband point &middot; <b>;</b>/<b>.</b> scroll &middot; <b>`</b> back"),
 ("NEXT PASSES (favs)",
  "<b>ENTER</b> track &middot; <b>m</b> world map &middot; <b>r</b> refresh &middot; <b>z</b> deep-sleep until AOS"),
 ("PASSES (sel)",
  "<b>;</b>/<b>.</b> select &middot; <b>d</b> detail &middot; <b>t</b>/<b>ENTER</b> track &middot; "
  "<b>n</b> add TX &middot; <b>r</b> recompute &middot; <b>x</b> mutual-DX &middot; "
  "<b>v</b> 10-day &middot; <b>i</b> illum &middot; <b>g</b> grids &middot; <b>w</b> US states &middot; <b>e</b> DXCC"),
 ("TRACK (sel)",
  "<b>m</b> TUNE/CAL &middot; <b>d</b> tune mode (FULL/DL/UL/hold) &middot; <b>t</b> next TX &middot; <b>n</b> jump to beacon &middot; "
  "<b>c</b> CTCSS &middot; <b>r</b> radio &middot; <b>o</b> rotator &middot; <b>p</b> polar &middot; <b>z</b> big readout &middot; "
  "<b>y</b> tilt on/off (ADV) &middot; "
  "<b>f</b> Manual &middot; <b>l</b> log QSO &middot; <b>v</b> voice memo (SD) &middot; <b>g</b> grids &middot; <b>w</b> states &middot; <b>e</b> DXCC now &middot; <b>ENTER</b> save cal"),
 ("BIG READOUT (z from Track)",
  "Big RX/TX + az/el + tune mode (follows Track). Radio+rotator keep tracking &middot; "
  "<b>,</b>/<b>/</b> tune &middot; <b>s</b>/<b>x</b> step/ctr &middot; <b>m</b>/<b>d</b> mode &middot; "
  "<b>t</b> TX &middot; <b>n</b> beacon &middot; <b>r</b> radio &middot; <b>o</b> rot &middot; <b>y</b> tilt &middot; <b>l</b> log &middot; <b>z</b>/<b>`</b> back"),
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
  "hybrid polygons+points). <b>;</b>/<b>.</b> &amp; <b>{</b>/<b>}</b> scroll &middot; <b>`</b> back"),
 ("POLAR / PASS DETAIL",
  "Pass detail: <b>p</b> polar of this pass. Polar: <b>l</b> log QSO &middot; "
  "<b>p</b>/<b>ENTER</b>/<b>`</b> back"),
]

BACK = [
 ("SUN / MOON",
  "Graphic sky-dome (Sun/Moon glyphs by az/el) &middot; <b>g</b> graphic/list &middot; "
  "<b>;</b>/<b>.</b> pick Sun/Moon &middot; <b>o</b> rotor track on/off &middot; "
  "<b>s</b> sky sources &middot; <b>x</b> stop &middot; <b>`</b> back"),

 ("SKY SOURCES (Sun/Moon &rarr; s)",
  "Planets (cyan dots) + strong radio sources (orange +): Cas A, Cyg A, galactic centre, Crab, Virgo A, on a sky dome. "
  "Antenna-pointing / RF reference. <b>;</b>/<b>.</b> select &middot; <b>`</b> back"),
 ("SPACE WX (menu)",
  "Solar 10.7cm flux + planetary Kp, labelled &amp; colour-coded, with HF/sat "
  "operating outlook &amp; data age &middot; <b>r</b> refresh (WiFi) &middot; <b>`</b> back"),
 ("WEATHER (menu)",
  "Current conditions + multi-day forecast for your site (Open-Meteo). "
  "Refreshes on entry (WiFi) &amp; with Update. Units in Settings &middot; "
  "<b>r</b> refresh &middot; cached offline &middot; <b>`</b> back"),
 ("QRZ LOOKUP (menu)",
  "Callsign lookup via QRZ.com XML (needs QRZ XML subscription + user/pass in "
  "Settings &rarr; Network). <b>ENTER</b> type call &rarr; name/addr/grid/class. WiFi req'd &middot; <b>`</b> back"),
 ("TRANSPONDER DB (Sats &rarr; t)",
  "Sat's transponder/beacon entries: <b>D</b> downlink+mode, "
  "<b>U</b> uplink+tone/inv &middot; <b>;</b>/<b>.</b> select (* = manual) &middot; <b>x</b> del manual (2x) &middot; <b>`</b> back"),
 ("ORBITAL ANALYSIS",
  "<b>,</b>/<b>/</b> 9 pages: Info / Live / Next pass / Ground track / Doppler / Nodal / Sun-Beta / "
  "Pass outlook / Orbit position &middot; <b>r</b> recompute &middot; Doppler <b>f</b> sets beacon freq"),
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
  "All footprints &middot; <b>f</b> highlight favorite &middot; <b>c</b> recenter on QTH/0&deg; &middot; "
  "yellow=sunlit cyan=eclipse &middot; <b>`</b> back"),
 ("SCHEDULES",
  "10-day: <b>;</b>/<b>.</b> scroll +/-1 day (fills full days), <b>r</b> recompute &middot; "
  "Illum: <b>,</b>/<b>/</b> scroll +/-60 days &middot; Mutual: <b>;</b>/<b>.</b> scroll"),
 ("LOG",
  "Menu: <b>ENTER</b> new QSO / browse / export ADIF / voice memos &middot; List: <b>;</b>/<b>.</b> scroll, "
  "<b>ENTER</b> edit &middot; Entry: <b>;</b>/<b>.</b> field, <b>ENTER</b> edit, <b>s</b> save, "
  "<b>x</b> x2 delete"),
 ("VOICE MEMOS (Log &rarr; Voice Memos)",
  "Browse newest-first (date/time/sat/len). <b>ENTER</b> play &middot; <b>n</b> new &middot; <b>d</b> del &middot; <b>r</b> refresh &middot; record via <b>v</b> on Track. SD req'd"),
 ("LORA MESSAGES (Home &rarr; Messages)",
  "CardSat-to-CardSat broadcast chat (Cap LoRa). Same freq/SF/BW = same group. <b>n</b> write/send &middot; <b>;</b>/<b>.</b> scroll &middot; <b>r</b> retry &middot; <b>`</b> back. Needs RadioLib build. Untested HW"),
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
  "<b>rigctl</b> drives remote rig &middot; <b>Web</b> opt-in mobile page (no auth, trusted LAN)"),
 ("EDIT",
  "type &middot; <b>DEL</b> backspace &middot; <b>ENTER</b> ok &middot; <b>`</b> cancel"),
 ("ABOUT",
  "Build/version, IP, free heap and diagnostics (read-only)."),
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
    canvas.drawString(62, PAGE_H - 13.0, 'v0.9.23  \u00b7  Key Reference')
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
                       leading=tf + 1.0, textColor=ACCENT_DK,
                       spaceBefore=2.8, spaceAfter=0.8)
    b = ParagraphStyle('b', fontName='Helvetica', fontSize=body_fs,
                       leading=body_fs + 1.4, spaceAfter=2.2, textColor=colors.black)
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


def best_fs(sections, hi=9.5, lo=5.0):
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
