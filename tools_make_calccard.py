# CardSat calculator reference card (4x6, two sides).
#
# Generated, not hand-maintained: the LAYOUT is shared verbatim with
# tools_make_refcard.py so the two cards cannot drift apart in style, and only the
# CONTENT differs. The function names below are the ones the evaluator actually
# recognises -- checked against src/app.cpp by tools/audit_calc_card.py, because a
# reference card that lists a function the firmware does not have is worse than no card
# at all: the operator assumes they typed it wrong.
#
# No version numbers anywhere on the card. It describes the firmware it ships with;
# "added in 0.9.x" tells a reader nothing they can act on and goes stale the moment the
# feature is no longer new.
#!/usr/bin/env python3
# CardSat 4x6 reference-card generator (landscape, front + back).
# The companion to the KEY reference card: no keypresses here -- radio and rotator
# support, data sources and courtesy limits, the file map, the full calculator
# function set, and the complete Tiny BASIC language + system-name reference.
# Every fact on this card is grounded in the source (radio_profiles.h, settings.h,
# the calculator's word() chain, the BASIC kw()/BASIC_SYS tables, config.h).
from reportlab.lib.units import inch
from reportlab.lib import colors
from reportlab.platypus import (BaseDocTemplate, PageTemplate, Frame,
                                Paragraph, PageBreak)
from reportlab.lib.styles import ParagraphStyle
from pypdf import PdfReader
import os, re

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CardSat_CalcCard_4x6.pdf")

# Pull the firmware version from src/config.h so the card never goes stale.
def _fw_version():
    here = os.path.dirname(os.path.abspath(__file__))
    cfg = os.path.join(here, "src", "config.h")
    try:
        m = re.search(r'FW_VERSION\s*=\s*"([^"]+)"', open(cfg).read())
        if m:
            return m.group(1)
    except Exception:
        pass
    return "0.0.0"
FW_VER = _fw_version()
PAGE_W, PAGE_H = 6 * inch, 4 * inch            # 432 x 288 pt (landscape 4x6)
ACCENT    = colors.HexColor('#0B3E7A')
ACCENT_DK = colors.HexColor('#062B4F')
RULE      = colors.HexColor('#D7D7D7')
BAND_H, LM, RM, BM, GUT = 19, 12, 12, 10, 10
TOPGAP    = BAND_H + 3
frame_top = PAGE_H - TOPGAP
frame_h   = frame_top - BM
NCOL      = 2
col_w     = (PAGE_W - LM - RM - (NCOL - 1) * GUT) / float(NCOL)
XS        = [LM + i * (col_w + GUT) for i in range(NCOL)]

FRONT = [
 ("ENTERING EXPRESSIONS",
  "Type and press <b>ENTER</b>. <b>Ans</b> is the previous result. <b>DEL</b> backspaces; "
  "<b>`</b> leaves. <b>[</b> <b>]</b> scroll the tape &middot; <b>'</b> toggles the on-screen hints &middot; "
  "<b>\\</b> engineering notation &middot; <b>Fn+p</b> prints the tape &middot; <b>Fn+f</b> full function list."),
 ("SI SUFFIXES",
  "Any number takes <b>p n u m k M G T</b> &mdash; <b>145M</b> is 145 000 000. "
  "Mixing them is fine: <b>fspl(145M/1M, 800)</b>."),
 ("ARITHMETIC",
  "<b>+ - * / ^</b> ( ) <b>mod</b> &middot; <b>sqrt cbrt abs sign round floor ceil</b> &middot; "
  "<b>min(a,b) max(a,b) hypot(a,b)</b> &middot; <b>fact(n) ncr(n,r) npr(n,r)</b>."),
 ("LOGS AND POWERS",
  "<b>ln</b> natural &middot; <b>log</b> base 10 &middot; <b>log2</b> &middot; <b>exp</b>."),
 ("TRIGONOMETRY &mdash; DEGREES",
  "<b>sin cos tan asin acos atan</b> take and return <b>degrees</b>, not radians. "
  "<b>atan2(y,x)</b> keeps the quadrant &mdash; use it for bearings. "
  "<b>sinh cosh tanh</b> &middot; <b>d2r r2d</b> convert."),
 ("CONSTANTS",
  "<b>pi e</b> &middot; <b>c</b> speed of light m/s &middot; <b>kB</b> Boltzmann J/K &middot; "
  "<b>Re</b> Earth radius 6378.137 km &middot; <b>mu</b> Earth GM 398600.4418 &middot; <b>g0</b> 9.80665."),
 ("GRAPHER",
  "Same language, plus <b>x</b> as the plotted variable. <b>ENTER</b> edits y=f(x), "
  "<b>2</b> a second trace, <b>t</b> trace, <b>m</b> mark, <b>z</b> zero, <b>b</b> table, "
  "<b>csv</b> export. <b>Fn+f</b> opens this list."),
]

BACK = [
 ("DECIBELS AND POWER",
  "<b>db(x)</b> ratio&rarr;dB &middot; <b>undb(x)</b> dB&rarr;ratio &middot; "
  "<b>dbm(w)</b> W&rarr;dBm &middot; <b>w(dbm)</b> dBm&rarr;W &middot; "
  "<b>dbm2w</b> / <b>w2dbm</b> the same pair by the other name &middot; "
  "<b>dbd(dbi)</b> and <b>dbi(dbd)</b> shift antenna gain by 2.15 dB."),
 ("SWR, FEEDLINE, NOISE",
  "<b>swr2rl(swr)</b> and <b>rl2swr(db)</b> &middot; <b>mml(swr)</b> mismatch loss dB &middot; "
  "<b>nf2t(nf)</b> noise figure&rarr;kelvin &middot; <b>t2nf(k)</b> back again."),
 ("FREQUENCY AND ANTENNAS",
  "<b>wl(mhz)</b> / <b>fq(m)</b> and <b>lam(mhz)</b> wavelength &middot; "
  "<b>dipole(mhz)</b> half-wave length in metres (0.95 velocity factor, so it is the "
  "length to cut) &middot; <b>dgain(d,mhz)</b> parabolic dish dBi at 55% efficiency &middot; "
  "<b>fspl(mhz,km)</b> free-space path loss dB."),
 ("SATELLITE",
  "<b>porb(alt)</b> orbital period in minutes &middot; <b>aorb(min)</b> the altitude that "
  "gives a period &middot; <b>vorb(alt)</b> orbital speed km/s &middot; "
  "<b>fpr(alt)</b> footprint radius km &middot; <b>slant(el,alt)</b> actual range at that "
  "elevation &middot; <b>dop(mhz,rr)</b> Doppler Hz from range rate km/s. Altitudes in km."),
 ("WORKED EXAMPLES",
  "<b>lam(435)</b> &rarr; 0.689 m &middot; <b>dipole(14.1)</b> &rarr; 10.10 m &middot; "
  "<b>porb(420)</b> &rarr; 93.0 min &middot; <b>slant(0,420)</b> &rarr; 2352 km at the horizon, "
  "against 420 km overhead &mdash; the reason to use it &middot; "
  "<b>fspl(435,2352)</b> &rarr; 152.6 dB &middot; <b>dgain(3,10368)</b> &rarr; 47.7 dBi."),
 ("WHY SOME OF THESE EXIST",
  "<b>atan2</b> because a bearing from <b>atan(y/x)</b> loses the quadrant and divides by "
  "zero due east. <b>slant</b> because treating altitude as range understates a horizon "
  "path by 5.6x, exactly where the link budget is tightest. <b>dipole</b> because a "
  "free-space half wave cuts elements measurably long."),
]


def header(canvas, doc):
    canvas.saveState()
    canvas.setStrokeColor(colors.HexColor('#BBBBBB')); canvas.setLineWidth(0.6)
    canvas.rect(2, 2, PAGE_W - 4, PAGE_H - 4)
    canvas.setFillColor(ACCENT)
    canvas.rect(0, PAGE_H - BAND_H, PAGE_W, BAND_H, fill=1, stroke=0)
    canvas.setFillColor(colors.white)
    canvas.setFont('Helvetica-Bold', 10.5); canvas.drawString(9, PAGE_H - 13.4, 'CardSat')
    canvas.setFont('Helvetica', 7.6)
    canvas.drawString(62, PAGE_H - 13.0, 'v' + FW_VER + '  \u00b7  Calculator Card (scientific + grapher)')
    pg = canvas.getPageNumber()
    side = 'Front \u00b7 hardware & data' if pg == 1 else 'Back \u00b7 calculator & BASIC'
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
