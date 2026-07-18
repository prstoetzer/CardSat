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

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "CardSat_RefCard_4x6.pdf")

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
 ("RADIOS (CAT) — CI-V",
  "<b>IC-820</b> 0x42/9600 &middot; <b>IC-821</b> 0x4C/9600 &middot; <b>IC-910</b> 0x60/19200 &middot; "
  "<b>IC-970</b> 0x2E/9600 &middot; <b>IC-9100</b> 0x7C/19200 &middot; <b>IC-9700</b> 0xA2/19200. "
  "Address &amp; baud per-rig configurable in Settings. Single-wire CI-V confirmed on IC-821."),
 ("RADIOS — Yaesu / Kenwood CAT",
  "<b>FT-847</b> 57600 &middot; <b>FT-736R</b> 4800 &middot; <b>TS-790</b> 4800 &middot; <b>TS-2000</b> 57600."),
 ("CAT TRANSPORTS (pick in Settings)",
  "<b>Wired</b> TTL UART CI-V (single or separate pin) &middot; <b>Icom LAN</b> RS-BA1 UDP 50001/50002 (IC-9700) &middot; "
  "<b>rigctld</b> Hamlib NET TCP &middot; <b>USB</b> adapter on USB-C (default-on since 0.9.59): "
  "FTDI 0403, CP210x 10c4, CH34x 1a86, PL2303 067b, any CDC-ACM. USB device strings lead with "
  "<b>#N</b> = device address = the id explicit binding stores (tells identical adapters apart)."),
 ("ROTATORS",
  "Serial protocols: <b>GS-232A/B</b>, <b>Easycomm I/II/III</b>, <b>SPID Rot2Prog</b> — each over the "
  "<b>I2C bridge</b> (SC16IS750), <b>Grove G1/G2</b>, or a <b>USB adapter</b> (Rot wire setting). "
  "Network: <b>rotctld</b> TCP, <b>PstRotator</b> UDP. Direct <b>Yaesu</b> via I2C ADC + expander "
  "(needs calibration). Grove rotator excludes wired CAT and Grove GPS (one UART)."),
 ("DATA SOURCES",
  "GP elements (JSON): <b>AMSAT</b> nasabare (default), any <b>CelesTrak</b> group, or a custom URL; "
  "plus whole-catalog CelesTrak <b>search</b> by NAME or CATNR. <b>SatNOGS</b> transponders &middot; "
  "<b>AMSAT status</b> API &middot; <b>hams.at</b> activations &middot; <b>LoTW</b> upload &middot; "
  "Cloudlog &middot; <b>Open-Meteo</b> weather (CC BY 4.0) &middot; <b>NOAA SWPC</b> space weather. "
  "Satellite names resolve source-independently (parenthetical designator bridge)."),
 ("COURTESY LIMITS (automatic)",
  "Primary GP catalog &amp; CelesTrak extras re-fetch no sooner than <b>2 h</b> per source URL "
  "(persisted across reboots; changing source fetches at once) &middot; catalog search <b>10 s</b> between "
  "queries &middot; extras capped at <b>25</b>."),
 ("CAPACITIES",
  "<b>150</b> satellites resident (favorites survive truncation) &middot; <b>64</b> transponders for the active sat &middot; "
  "<b>25</b> CelesTrak extras &middot; BASIC: <b>500,000</b> statements/run, <b>2,000</b> SATSEL calls, "
  "@() array &le; <b>256</b>, <b>6 KB</b> output — and <b>no INPUT</b>, by design."),
 ("ORBIT ENGINE",
  "SGP4/SDP4 (deep space included). Periods over ~225 min (Molniya, GTO, GEO) use the 0.9.59 "
  "scan finder: ~1 s AOS/LOS, horizon-long pass for a bird parked in view — Skyfield-verified "
  "(crossings &le; 0.04&deg;). Doppler: AMSAT One True Rule, per-satellite calibration."),
 ("KEY FILES  (/CardSat/ on SD)",
  "<b>config.json</b>(+.bak) settings &middot; <b>gp.json</b> primary catalog &middot; <b>mgp.json</b> hand-entered sats &middot; "
  "<b>ctq.json/ctx.json</b> search results / extras (+<b>ctx.ts, gp.ts</b> courtesy timestamps) &middot; "
  "<b>basic/</b> programs &amp; gated logs &middot; <b>plot.csv</b> grapher CSV mode &middot; <b>Reports/</b> printed reports &middot; "
  "<b>Logs/</b> QSO log &middot; <b>RovePlans/</b> &middot; <b>Screenshots/</b> &middot; <b>calib.txt</b> per-sat cal &middot; "
  "<b>lotw_sats.csv</b> LoTW name overrides &middot; <b>audio/</b> voice memos."),
]

BACK = [
 ("CALCULATOR — conventions",
  "Trig in <b>degrees</b> &middot; <b>Ans</b> = last result &middot; <b>x</b> = variable in the grapher &middot; "
  "suffixes on numbers: <b>f p n u m k M G T</b> (case matters: M mega, m milli) &middot; "
  "\\ toggles engineering-notation output."),
 ("General functions",
  "sin cos tan asin acos atan &middot; atan2(y,x) &middot; sinh cosh tanh &middot; ln log log2 exp &middot; "
  "sqrt cbrt &middot; abs floor ceil round sign &middot; fact(n) ncr(n,r) npr(n,r) &middot; "
  "min max mod hypot (2-arg) &middot; rnd() &middot; d2r r2d."),
 ("Ham / RF functions",
  "dbm(W) w(dBm) &middot; db(ratio) undb(dB) &middot; wl(MHz)&rarr;m fq(m)&rarr;MHz &middot; "
  "swr2rl rl2swr &middot; mml(swr) mismatch loss &middot; fspl(MHz,km) path loss &middot; "
  "nf2t(dB) t2nf(K) noise fig &harr; temp &middot; dbd dbi (&plusmn;2.15) &middot; "
  "dop(MHz, rrKmS)&rarr;Hz — feed it SATRR."),
 ("Orbital one-liners + constants",
  "porb(altKm)&rarr;min &middot; vorb(altKm)&rarr;km/s &middot; fpr(altKm)&rarr;footprint km. "
  "Constants: pi e c kB Re mu g0."),
 ("TINY BASIC — statements",
  "LET / bare <b>V=</b> &middot; PRINT (<b>?</b>) &middot; IF..THEN &middot; GOTO &middot; GOSUB/RETURN &middot; "
  "FOR..TO..[STEP]..NEXT &middot; ON e GOTO n1,n2,.. &middot; DIM @(n) &middot; DATA / READ / RESTORE &middot; REM &middot; END &middot; "
  "<b>SATSEL i</b> re-snapshot SAT* for catalog sat i &middot; <b>TXSEL i</b> &rarr; TX* &middot; "
  "<b>LPRINT</b> to report sinks &middot; gfx: <b>CLS PSET LINE CIRCLE TEXT SHOW</b> (colors 0-9; SHOWed frame "
  "holds after the run) &middot; <b>FOPEN\"n\" FPRINT FCLOSE</b> (Settings-gated, /CardSat/basic/) &middot; FILES."),
 ("Expressions & functions",
  "Ops: + - * / ^ <b>MOD</b>; IF compares = &lt;&gt; &lt; &gt; &lt;= &gt;= with <b>AND OR NOT</b>. "
  "Vars A-Z + one <b>@(i)</b> array. Fns: ABS INT SQR SGN SIN COS TAN ATN LOG EXP MIN MAX RND(n). "
  "Trig in degrees, matching the calculators."),
 ("System names (read-only snapshot)",
  "<b>Sat</b>: SATAZ EL RNG RR LAT LON ALT SUN INC ECC RAAN MM NOR &middot; "
  "<b>Pass</b>: AOSIN LOSIN PASSEL PASSVIS PASSN PASSAOS/LOS/MAX(k&le;8) &middot; "
  "<b>Sky</b>: SUNAZ SUNEL MOONAZ MOONEL &middot; <b>Site</b>: MYLAT MYLON MYALT LSTHR &middot; "
  "<b>Clock</b>: UTCYR MON DAY H M S &middot; <b>SpaceWx</b>: SFI KP AINDEX &middot; "
  "<b>Wx</b>: WXTEMP WIND DIR HUM &middot; <b>GPS</b>: GPSSATS GPSSPD GPSLAT LON ALT &middot; "
  "<b>Device</b>: BATT GPAGE NFAV HEAPFREE UPTIME NSAT NTX &middot; "
  "<b>TX</b> (after TXSEL): TXDL UL BW INV LIN &middot; "
  "<b>Flags</b>: SATOK TIMEOK POSOK WXOK SPWXOK PASSOK GPSOK."),
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
    canvas.drawString(62, PAGE_H - 13.0, 'v' + FW_VER + '  \u00b7  Reference Card (radios / data / calc / BASIC)')
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
