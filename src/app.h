#pragma once
// ===========================================================================
//  app.h  -  top-level application: UI state machine + Doppler control loop
// ===========================================================================
#include <Arduino.h>
#include "settings.h"
#include "satdb.h"
#include "net.h"
#include "location.h"
#include "predict.h"
#include "rig.h"
#include "rotator.h"
#include "voicememo.h"
#include "irbeacon.h"
#include "lora.h"
#include "lorarx.h"   // general LoRa RX / hex monitor (feature-guarded)

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_SATLIST, SCR_SCHEDULE, SCR_PASSES, SCR_PASSDETAIL,
  SCR_TRACK, SCR_POLAR, SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT,
  SCR_PASSPOLAR, SCR_MUTUAL, SCR_WIFISCAN, SCR_ABOUT, SCR_LOG, SCR_LOGENTRY,
  SCR_LOGLIST, SCR_VIS, SCR_ILLUM, SCR_WORLDMAP, SCR_ROTMAN, SCR_GPS, SCR_HELP, SCR_ORBIT, SCR_SIM,
  SCR_SUNMOON, SCR_GRID, SCR_GPSRC, SCR_MANUAL, SCR_STATES, SCR_DXCC, SCR_SPACEWX, SCR_TXDB, SCR_QRZ, SCR_WEATHER, SCR_EQX, SCR_BIG, SCR_MANUALBIG, SCR_NETREBOOT, SCR_MEMOS, SCR_OSCAR, SCR_GLOBE, SCR_DXDOPP, SCR_SKYMAP, SCR_GPSPOS, SCR_SATSAT, SCR_MESSAGES, SCR_CATTEST, SCR_CHARGE, SCR_CATMON, SCR_TRANSIT, SCR_VISLIST, SCR_LOTW, SCR_HAMSAT, SCR_NOTES, SCR_NOTEEDIT, SCR_CLOUDLOG, SCR_LOTWSUB, SCR_GLOSSARY, SCR_USERGUIDE, SCR_LICENSE, SCR_SATHIST, SCR_TECHHELP, SCR_LEARN, SCR_ARROW, SCR_OVERHEAD, SCR_SKEDENTRY, SCR_GAME, SCR_SKYGLANCE, SCR_AWARDS, SCR_AWARDSAT, SCR_AWARDLIST,
  SCR_GAMES, SCR_GDOPPLER, SCR_GPASS, SCR_GROTOR, SCR_GMORSE, SCR_GGRID, SCR_LORARX,
  SCR_ACTMUTUAL, SCR_ACTDOPP, SCR_MUTUALDETAIL,
  SCR_LORACOMPASS, SCR_LORASAT, SCR_LORAROSTER, SCR_AMSATSTAT, SCR_EME, SCR_GRIDCALC, SCR_QRZGRID, SCR_BANDPLAN, SCR_PROP, SCR_READY, SCR_EMEPLAN, SCR_AMSRPT, SCR_AMSRPICK, SCR_TOOLS, SCR_CALC, SCR_PCALC, SCR_CHARLK, SCR_TOOLFORM, SCR_DXLK, SCR_DXLKD, SCR_CQZ, SCR_CQZD, SCR_ITUZ, SCR_ITUZD, SCR_LINKB, SCR_OPREF, SCR_CTCSS, SCR_ORBITZOO, SCR_MATHREF, SCR_PLANNER, SCR_PLANDETAIL, SCR_GPFIT, SCR_ROVELIST, SCR_ROVEVIEW, SCR_GPIMPORT, SCR_WORKHZN, SCR_TGTSEARCH, SCR_TGTHITS, SCR_CUBESIM, SCR_FOXANAT, SCR_FOXTEXT, SCR_CSIMINFO, SCR_PRINTABOUT
};

// Doppler tune mode (cycled with 'd' on the Track screen, linear birds).
enum TuneMode : uint8_t {
  TM_HOLD = 0,   // hold the passband; Doppler-correct BOTH legs (device-key tuning)
  TM_FULL,       // One True Rule: rig knob = passband, correct BOTH legs
  TM_DL,         // One True Rule on the downlink only (uplink left untouched)
  TM_UL,         // Doppler-correct the uplink only (downlink left untouched)
};

// One upcoming (or in-progress) pass for a favorite, used by the schedule view.
struct SchedEntry {
  uint32_t norad = 0;
  char     name[26] = {0};
  time_t   aos = 0, los = 0;
  float    maxEl = 0;
  bool     inProgress = false;
  bool     visible = false;     // visually observable (sunlit + observer dark + high enough)
  uint8_t  visWhy = 0;          // 0 not-eval, 1 visible, 2 daylight, 3 sat-in-shadow, 4 too-low
};

// One QSO being entered on the log screen (snapshotted auto fields + typed fields).
struct PendingQso {
  uint32_t utc;
  char     sat[18];
  char     mode[8];
  uint32_t dlHz, ulHz;
  char     myGrid[10];
  char     myCall[14];
  char     call[14];
  char     rstS[6];
  char     rstR[6];
  char     grid[10];
  char     notes[40];
  uint8_t  uploaded;              // bit0: sent to LoTW (CSV col 13, absent => 0)
};

// Result of decoding the sigils in a received LoRa message's text (see decodeMsg in
// app.cpp): an @position, a #satellite, and/or a !sked proposal. Declared here (not in
// app.cpp) so that in the single-file Arduino build the type is defined before the
// auto-generated prototype for decodeMsg() -- a free function returning this struct,
// which otherwise triggers "'MsgDecode' does not name a type".
struct MsgDecode {
  bool   hasPos = false;  double lat = 0, lon = 0;
  bool   hasSat = false;  char   sat[12] = {0};   uint32_t norad = 0;      // #NAME/NORAD (norad 0 = none)
  bool   hasSked = false; char   skedSat[12] = {0}, skedDate[11] = {0}, skedTime[6] = {0};
                          uint32_t skedNorad = 0;  // !NAME/NORAD date time (norad 0 = none)
};

class App {
public:
  void setup();
  void loop();

#if CARDSAT_HAS_LORARX
  friend class LoraRxMon;   // the RX monitor reads cfg/lora and calls loraStart via this
#endif

private:
  // subsystems
  Settings  cfg;
  SatDb     db;
  Net       net;
  Location  loc;
  Predictor pred;
  Rig*      rig = nullptr;   // active CAT backend (Icom/Yaesu/Kenwood)
  Rotator*  rot = nullptr;   // active rotator backend (GS-232), or null
  VoiceMemo memo;            // SD-card voice memo recorder ('v' on Track family)
  IrBeacon  irBeacon;        // IR pass-alert beacon (distinct flash count per event)
  LoraRadio lora;            // SX1262 LoRa radio for CardSat-to-CardSat messaging
#if CARDSAT_HAS_LORARX
  LoraRxMon    lorarx;          // general LoRa RX / hex monitor (own state; see lorarx.h)
#endif
  void toggleMemo();         // start/stop a memo; shared by the Track-family keys
  void drawMemoIndicator();  // red REC badge overlay while a memo is recording

  // UI state
  Screen   screen = SCR_HOME;
  int      homeSel = 0;
  int      satSel = 0, satScroll = 0;
  int      passN = 0;

  // favorites + filtered satellite view
  uint32_t favs[MAX_SATS];
  int      favN = 0;
  bool     favOnly = false;
  int      mapHi = -1;            // world map: highlighted favorite (-1 = none); 'f' cycles
  Screen   mapReturn = SCR_SCHEDULE; // where the World Map's back key returns to
                                  // (set on entry: Home vs the 'm' shortcut)
  Screen   netRebootReturn = SCR_HOME; // screen to restore if the user declines reboot
  float    manAz = 0, manEl = 0;  // manual rotator control target (deg)
  int      manStep = 5;           // manual rotator jog step (deg)
  Screen   helpReturn = SCR_HOME; // screen to return to when leaving Help
  int      helpScroll = 0;        // Help screen scroll offset
  int      glossScroll = 0;       // Glossary screen scroll offset
  int      guideScroll = 0;       // User-guide screen scroll offset
  int      licScroll = 0;         // License/credits screen scroll offset
  int      satHistScroll = 0;     // Satellite-history screen scroll offset
  int      techScroll = 0;        // Tech-assistance screen scroll offset
  int      learnScroll = 0;       // Learn (theory) screen scroll offset
  // "What's overhead now" snapshot: sats currently above the horizon, by elevation.
  static const int OVH_MAX = 40;
  uint32_t ovhNorad[OVH_MAX] = {0};
  char     ovhName[OVH_MAX][26] = {{0}};
  float    ovhAz[OVH_MAX] = {0};
  float    ovhEl[OVH_MAX] = {0};
  int      ovhN = 0;              // entries collected
  int      ovhScanned = 0;        // sats scanned (for the footer)
  int      ovhScroll = 0;
  bool     ovhValid = false;      // a scan has been done this visit

  // CAT self-test (SCR_CATTEST): results of exercising every CAT function, shown
  // on the device and echoed to the serial monitor. Fixed buffer (no heap churn
  // on the no-PSRAM ESP32); lines past the cap are dropped.
  static const int CATTEST_MAX = 48;
  String   catLines[CATTEST_MAX];
  int      catCount  = 0;        // number of result lines filled
  int      catScroll = 0;        // scroll offset on the results screen

  // CAT serial terminal/monitor (SCR_CATMON): a small ring buffer of the most
  // recent raw CAT frames (TX/RX hex), filled by the catTraceSink hook. Read-only
  // diagnostic; the operator can also type a raw hex frame to transmit.
  static const int CATMON_MAX = 64;     // ring-buffer depth (lines)
  String   catMonLines[CATMON_MAX];
  bool     catMonIsTx[CATMON_MAX] = {}; // true = TX line (colour), false = RX
  int      catMonHead = 0;              // next write index (ring)
  int      catMonCount = 0;             // lines filled (<= CATMON_MAX)
  int      catMonScroll = 0;            // 0 = follow tail (live); >0 = scrolled back
  bool     catMonActive = false;        // true while the screen owns the trace sink
  bool     catMonPoll   = true;         // actively poll the rig so there's live traffic
  uint32_t catMonLastPollMs = 0;        // millis() of the last monitor poll
  static constexpr uint32_t CATMON_POLL_MS = 700;  // monitor heartbeat read interval
  int      catPass   = 0;        // tally for the summary line
  int      catFail   = 0;
  int      view[MAX_SATS];        // db indices currently shown
  int      viewN = 0;
  int      viewSel = 0;           // cursor into view[]

  // manual-entry scratch (GP elements + transponder fields)
  SatEntry mtSat;                 // manual GP-entry accumulator
  uint32_t mtxDl = 0, mtxUl = 0, mtxDlHigh = 0, mtxUlHigh = 0;
  bool     mtxInv = false;
  PassPredict passes[PASS_LIST_LEN];
  bool     passVis[PASS_LIST_LEN] = {false};  // cached optical visibility per pass,
                                              // filled by computePasses() so the
                                              // passes screen never re-runs the
                                              // (expensive) visibility scan per frame
  int      passSel = 0;          // cursor into the passes list

  // pass-detail plot: cached elevation + azimuth curve over one pass
  float    pdEl[PD_SAMPLES];
  float    pdAz[PD_SAMPLES];
  bool     pdSunlit[PD_SAMPLES];
  PassPredict pdPass;
  bool     pdValid = false;

  // live polar ground-track arc (current pass, or next pass if not up now)
  PassPredict polarPass;
  float    polarAz[POLAR_PTS];
  float    polarEl[POLAR_PTS];
  bool     polarPathValid = false;

  // mutual-window (co-visibility) results for a remote DX grid
  MutualWindow mutual[MUTUAL_MAX];
  int      mutualN = 0;
  int      mutualSel = 0;

  // ---- Activation footprint + shared mutual-window detail (SCR_ACTMUTUAL,
  //      SCR_MUTUALDETAIL) and tailored DX Doppler (SCR_ACTDOPP). New in 0.9.43.
  //      The activation flow (from SCR_HAMSAT detail) checks whether the listed
  //      satellite is actually co-visible with the activator near the listed time
  //      (+/-30 min, since hams.at keps can lag), shows the window on a polar plot,
  //      and opens a DX Doppler pre-seeded from the activation's freq/comment.
  static const int ACTMU_PTS = 40;      // az/el samples across a mutual window (polar arc)
  float    actMuAzMe[ACTMU_PTS];        // sat az/el as seen from MY site
  float    actMuElMe[ACTMU_PTS];
  float    actMuAzDx[ACTMU_PTS];        // sat az/el as seen from the DX site
  float    actMuElDx[ACTMU_PTS];
  // Detail-screen (SCR_HAMSAT) footprint state, computed on ENTER into detail.
  int8_t   actFpState = 0;              // 0 not computed, 1 have window, 2 none, 3 can't (grid/sat/clock)
  MutualWindow actFpWin;               // the mutual window found near the listed time
  double   actDxLat = 0, actDxLon = 0;  // activator's grid -> lat/lon
  int      actCommentScroll = 0;        // comment scroll offset (lines) on the detail screen
  // Parse result for the tailored DX Doppler (from freq field, then comment).
  int      actTxIdx = -1;               // matched transponder index (into activeTx[]) or -1
  int      actFixMode = 0;              // 0 none/default, 1 fixed DX downlink, 2 fixed DX uplink
  uint32_t actFixHz = 0;                // the parsed frequency in Hz (for display)
  // Which mutual-detail screen we're on decides the onward DX-Doppler target.
  bool     mdFromActivation = false;    // true: SCR_ACTMUTUAL (-> SCR_ACTDOPP); false: SCR_MUTUALDETAIL (-> SCR_DXDOPP)

  int  activationFootprint();           // run the +/-30-min search; fills actFpWin; returns actFpState
  void buildMutualArcs(const MutualWindow& w);   // sample both stations' az/el into actMu*[]
  void drawMutualDetailBody(const MutualWindow& w);  // shared polar-plot + AOS/LOS/el layout
  void drawActMutual();  void keyActMutual(char c, bool enter, bool back);
  void drawMutualDetail(); void keyMutualDetail(char c, bool enter, bool back);
  void openActMutual();  // from SCR_HAMSAT detail: seed + enter SCR_ACTMUTUAL
  void drawActDopp();    void keyActDopp(char c, bool enter, bool back);
  void dxdStepAnchorToHz(uint32_t targetHz);  // park anchored dial on a specific freq (activation)

  // 10-day pass overview (InstantTrack-style), cached on entry
  PassPredict visPasses[VIS_PASS_MAX];
  int      visN = 0;
  int      visDayOff = 0;         // 10-day overview start offset from today, in DAYS (>=0)
  // Visible-passes LIST screen (SCR_VISLIST): optically-visible passes for the
  // active sat over the next VIS_DAYS days, as a scrollable list (distinct from
  // the 10-day chart above).
  PassPredict vlPasses[VIS_PASS_MAX];
  int      vlN = 0;
  int      vlSel = 0;
  int      vlScroll = 0;
  Screen   visReturn = SCR_PASSES; // screen to return to from vis/illum (Satellites or Passes)
  Screen   passDetailReturn = SCR_PASSES; // where pass-detail returns to (Passes or VisList)
  bool     building = false;      // a build is in progress (suppress empty-state placeholders)

  // EQX table (equatorial crossings, ascending node) for OSCARLOCATOR use.
  // 3-day window; an AO-7-class orbit gives ~37 crossings, higher orbits fewer.
  static const int EQX_MAX = 64;
  time_t   eqxT[EQX_MAX];         // unix UTC of each ascending-node crossing
  float    eqxLonW[EQX_MAX];      // sub-longitude in West-positive degrees (0..360)
  int      eqxN = 0;
  int      eqxScroll = 0;
  bool     eqxDescending = false;  // false = ascending node (EQX), true = descending node

  // 60-day illumination (DK3WN illum-style). Raster: cols = days, rows = orbit
  // phase; a set bit means the satellite is in eclipse at that (day, phase).
  uint8_t  illumBits[ILLUM_DAYS][(ILLUM_ROWS + 7) / 8];
  bool     illumValid     = false;
  int      illumDayOff    = 0;      // illumination raster start offset from today, in DAYS (>=0)
  // Orbital analysis screen (off Satellites), multi-page
  int      orbitPage = 0;
  // Orbit explorer (Orbit page 10): a teaching/planning sandbox. Editable apogee,
  // perigee (km altitude) and inclination, pre-filled from the active satellite, that
  // recompute derived characteristics without touching the real element set.
  bool     oxInit = false;      // have we seeded from the active sat this visit?
  double   oxApo = 800.0;       // apogee altitude, km
  double   oxPeri = 600.0;      // perigee altitude, km
  double   oxIncl = 51.6;       // inclination, deg
  int      oxSel = 0;           // 0=apogee 1=perigee 2=incl
  bool     oxEditing = false; String oxEditBuf;
  void oxSeedFromSat();
  double   orbAscLon = 0;           // sub-longitude of next ascending node (deg)
  time_t   orbAscT   = 0;           // time of next ascending node (UTC)
  bool     orbHasPass = false;
  PassPredict orbPass;
  bool     orbEcl = false;          // next pass includes eclipse?
  time_t   orbEclT0 = 0, orbEclT1 = 0;  // first eclipse entry/exit within the pass
  float    orbSunPct = 0;           // % of next pass in sunlight
  bool     orbVisible = false;      // next pass optically visible?
  double   orbDecayDays = -1;       // rough days-to-reentry (-1 n/a, 1e9 stable)
  double   orbDecayLo = -1;         // low-density (solar-min) bound: longer life
  double   orbDecayHi = -1;         // high-density (solar-max) bound: shorter life
  // Apogee/perigee shown on the Info page, sampled from the SAME perturbed predictor
  // that produces the live altitude (geocentric, over one orbit), so the displayed
  // Altitude is always within [perigee, apogee]. A mean-element apogee (a(1+e)-RE) can
  // read a few km BELOW the osculating altitude near apogee (SGP4 adds short-period J2
  // oscillation), which made the live altitude appear to exceed apogee. Cached in
  // buildOrbit(); fall back to the mean-element values until populated.
  double   orbApoKm = 0;            // max geocentric altitude over one orbit (km)
  double   orbPeriKm = 0;           // min geocentric altitude over one orbit (km)

  // Pass outlook (page 7): aggregate stats over the next ORB_OUTLOOK_DAYS days,
  // computed once in buildOrbit().
  int      orbOutlookN = 0;         // passes in the window
  int      orbOutlookHi = 0;        // of those, count above 30 deg
  float    orbBestEl = 0;           // best (max) elevation in the window
  time_t   orbBestT = 0;            // AOS time of that best pass
  int      orbBestDur = 0;          // duration (s) of that best pass
  float    orbLongestMin = 0;       // longest pass duration in the window (min)
  float    orbAvgGapH = 0;          // mean spacing between passes (hours)
  // Simulation screen (off Satellites): scrub a frozen UTC time
  time_t   simTime = 0;
  bool     simMap = false;          // Sim screen: world-map view vs data list
  int      simStepIdx = 2;          // index into SIM_STEP[] (1m/10m/1h/6h/1d)
  int      homeScroll = 0;          // scroll offset for the (now scrollable) home menu
  // Voice-memo browser (SCR_MEMOS): list of memos on the SD card, newest first.
  VoiceMemo::MemoEntry memos[MEMO_LIST_MAX];
  int      memoN = 0;               // number of memos currently listed
  int      memoSel = 0;             // selected row
  int      memoScroll = 0;          // scroll offset
  bool      memoConfirmDel = false; // true while a delete confirmation is pending
  // OSCARLOCATOR polar view (SCR_OSCAR): live azimuthal-equidistant plot.
  // oscarMode: 0 = QTH-centred (your station at disc centre), 1 = polar (N or S
  // pole at centre, chosen automatically from the satellite's hemisphere).
  int       oscarMode = 0;
  // Cached OSCARLOCATOR ground-track arc: one full orbital period of sub-points
  // (lat/lon), sampled once and held static, so the arc covers the whole disc
  // like a real OSCARLOCATOR. Re-projected each draw (survives QTH<->polar
  // toggles); the parts outside the plotted radius are naturally clipped.
  static const int OSCAR_ARC_PTS = 96;
  float     oscarArcLat[OSCAR_ARC_PTS];
  float     oscarArcLon[OSCAR_ARC_PTS];
  int       oscarArcN = 0;           // valid sample count (0 = no arc)
  time_t    oscarArcEnd = 0;         // rebuild when now passes this (one period out)
  time_t    oscarArcAos = 0, oscarArcLos = 0;  // AOS/LOS within the track (for markers)
  uint32_t  oscarArcTriedMs = 0;     // millis() of last build attempt (throttle retries)
  // 3D globe view (SCR_GLOBE): orthographic wireframe Earth that auto-follows the
  // selected satellite (the globe rotates to keep its sub-point centred), with a
  // day/night terminator and all favourites plotted. Arrow keys nudge the view
  // away from the follow point; a key re-snaps to auto-follow.
  double    globeViewLat = 0, globeViewLon = 0;  // current centre of the visible disc (deg)
  bool      globeFollow = true;       // true = re-centre on the selected sat each frame
  void drawGlobe();
  void keyGlobe(char c, bool enter, bool back);
  // DX Doppler table (SCR_DXDOPP): predicted RX/TX for BOTH my station and the DX
  // station, every 30 s across the selected mutual window, for the selected
  // transponder. Three tracking modes, and (for a linear transponder) a passband
  // operating point anchored to one station's RX or TX dial.
  int       dxdMode   = 0;            // 0 = true rule, 1 = fixed downlink, 2 = fixed uplink
  int       dxdAnchor = 0;            // who/what is the fixed/selected dial: 0 me-RX 1 me-TX 2 dx-RX 3 dx-TX
  int32_t   dxdPbOff  = 0;            // passband operating offset (Hz up from downlink low)
  int       dxdRow    = 0;            // scroll position (top visible 30 s step)
  int       dxdWin    = 0;            // which mutual window index this table is for
  uint32_t  dxdAnchorHz = 0;         // fixed-mode target dial freq (Hz); re-applied on transponder change (0 = none)
  void drawDxDopp();
  void keyDxDopp(char c, bool enter, bool back);
  void dxdCenterPassband();          // centre dxdPbOff on the selected linear transponder
  void dxdStepAnchorDial(int dir);   // step the anchored dial by 1 kHz (fixed modes)
  void dxdReanchorToStored();        // re-apply dxdAnchorHz to the current transponder after a change
  void dxDoppFreqs(time_t t, uint32_t& myRx, uint32_t& myTx,
                   uint32_t& dxRx, uint32_t& dxTx);  // core per-step calculator
  // Celestial sky plot (SCR_SKYMAP): planets and strong radio sources on a sky
  // dome, off the Sun/Moon screen. For antenna pointing and RF-source reference.
  int       skySel = 0;               // highlighted object index
  void drawSkyMap();
  void keySkyMap(char c, bool enter, bool back);
  // Live GPS position (SCR_GPSPOS): DMS lat/lon to full precision, speed, course,
  // grid. Off the Location screen.
  void drawGpsPos();
  void keyGpsPos(char c, bool enter, bool back);
  // Sat-to-sat visibility finder (SCR_SATSAT): windows where the selected
  // satellite and a second satellite are BOTH above the horizon at a chosen
  // location (default: my QTH) over the next days.
  int       satsatOther = 0;          // index (into favorites/db view) of the 2nd sat
  bool      satsatPicking = true;     // true = choosing the 2nd sat (no calc yet);
                                      // n/p change the pick, ENTER/r runs the search
  int       satsatSel   = 0;          // highlighted result row
  struct SatSatWin { time_t start, end; float maxElA, maxElB; };
  static const int SATSAT_MAX = 16;
  SatSatWin satsatWin[SATSAT_MAX];
  int       satsatN = 0;
  bool      satsatComputed = false;
  // Incremental sat-to-sat job. The full search (sampling both satellites'
  // elevation across a multi-day window) is too long to run in one blocking call
  // without starving the idle task / tripping the watchdog, so it runs a few
  // hundred samples per loop() tick and shows live progress. Phases:
  //   0 = idle, 1 = sampling sat A, 2 = sampling sat B, 3 = scanning for overlaps.
  static const int SATSAT_NS = 3 * 24 * 60;   // 4320 samples (3 days @ 60 s)
  int8_t*   satsatElA = nullptr;     // heap elevation tracks (allocated while running)
  int8_t*   satsatElB = nullptr;
  int       satsatJobPhase = 0;      // 0 idle / 1 sampling A / 2 sampling B / 3 scan
  int       satsatJobI = 0;          // progress index within the current phase
  time_t    satsatT0 = 0;            // job window start
  int       satsatJobPct = 0;        // 0..100 for the progress display
  void      satsatStartJob();        // begin/refresh the incremental search
  void      satsatJobTick();         // advance the job a little; called from loop()
  void      satsatAbortJob();        // free buffers + stop (on leaving the screen)

  // ---- Sun/Moon transit finder (SCR_TRANSIT) ------------------------------
  // Scans the next ~48 h for times the active satellite passes close to the Sun
  // or Moon as seen from the observer. Incremental (a chunk per loop tick) so it
  // never blocks: coarse 2 s steps, refined locally around each close approach.
  struct TransitHit {
    time_t  t;          // time of minimum separation (UTC)
    float   sepDeg;     // minimum angular separation (deg)
    float   bodyEl;     // body elevation at that time (deg)
    uint8_t body;       // 0 = Sun, 1 = Moon
    bool    central;    // true if separation < body radius (true transit), else conjunction
  };
  static const int TRANSIT_MAX = 16;       // results cap
  static const long TRANSIT_WIN_S = 48*3600;   // search window (s)
  static const int  TRANSIT_STEP_S = 2;        // coarse scan step (s)
  TransitHit transitHits[TRANSIT_MAX];
  int        transitN = 0;
  int        transitSel = 0;
  int        transitJobPhase = 0;          // 0 idle / 1 scanning
  long       transitJobOff = 0;            // seconds offset into the window (progress)
  time_t     transitT0 = 0;                // window start
  int        transitJobPct = 0;
  float      transitSep1[2] = {0, 0};      // separation one step ago, per body (0=Sun,1=Moon)
  float      transitSep2[2] = {0, 0};      // separation two steps ago, per body
  uint8_t    transitHist[2] = {0, 0};      // how many valid prior samples per body (0..2)
  float      transitMaxThreshDeg = 1.0f;   // report transits + conjunctions within this
  void       transitStartJob();
  void       transitJobTick();
  void       drawTransit();
  void       keyTransit(char c, bool enter, bool back);
  // angular separation (deg) between two az/el points
  static float angSepDeg(double az1, double el1, double az2, double el2);
  void drawSatSat();
  void keySatSat(char c, bool enter, bool back);
  // LoRa text messaging (SCR_MESSAGES): broadcast group chat between CardSats on
  // a shared frequency. Fixed-size history ring (no String, no heap growth).
  struct LoraMsg {
    char    from[14];     // sender callsign ("" = me / outgoing)
    char    text[49];     // message text (fixed)
    bool    mine;         // true = sent by this unit
    int16_t rssi;         // dBm (received only)
    int8_t  snr;          // dB  (received only)
    uint32_t tMs;         // millis() when added (for relative-time display)
  };
  static const int MSG_MAX = 24;
  LoraMsg   msgRing[MSG_MAX];
  int       msgHead = 0;          // index of next write (ring)
  int       msgCount = 0;         // number held (<= MSG_MAX)
  int       msgScroll = 0;        // view scroll (0 = newest at bottom)
  uint16_t  msgUnread = 0;        // inbound messages not yet viewed (header badge)
  char      msgLastFrom[16] = {0};// sender of the most recent unread (for the banner)
  bool      loraStarted = false;  // begin() attempted/succeeded this session
  void msgPush(const char* from, const char* text, bool mine, int rssi, int snr);
  void loraStart();               // apply cfg, bring the radio up
  void loraPoll();                // drain RX into the ring (call from loop)
  void loraSendCurrent(const char* text);   // frame + transmit a typed message
  // ---- LoRa object transfer (Feature 3, GP elements only for now) ----
  // A second frame type (magic 0xC6, distinct from the 0xC5 text frames) carries a larger
  // object split across small chunks: [0]=0xC6 [1]=VER [2]=OBJTYPE [3]=XFERID [4]=SEQ
  // [5]=COUNT [6..]=payload. OBJTYPE 1 = a GP element set, serialized as a pipe-delimited
  // text body with a trailing CRC16. Send is jobbed one frame per loop tick; receive
  // reassembles ONE object at a time (no PSRAM to hold several) and, on a complete CRC-valid
  // transfer, prompts before importing into GP data via db.addGp(). No ARQ: a missing chunk
  // fails the transfer and the sender must re-broadcast. UNTESTED on hardware.
  static const uint8_t LORA_OBJ_MAGIC = 0xC6;
  static const uint8_t LORA_OBJ_VER   = 0x01;
  static const uint8_t LORA_OBJ_GP    = 1;    // OBJTYPE: GP element set
  static const int LORA_OBJ_HDR       = 6;    // magic,ver,type,xferid,seq,count
  static const int LORA_OBJ_CHUNK     = 48;   // payload bytes per chunk (frame <= 54B, < RX buf)
  static const int LORA_OBJ_MAXCHUNKS = 8;    // a GP object is ~3 chunks; cap the reassembly
  static const int LORA_OBJ_MAXLEN    = LORA_OBJ_CHUNK * LORA_OBJ_MAXCHUNKS;  // 384 bytes
  // outbound send job (one frame per loop tick)
  bool     loraObjTxActive = false;
  uint8_t  loraObjTxBuf[LORA_OBJ_MAXLEN];     // serialized object being sent
  int      loraObjTxLen = 0;                  // total bytes
  int      loraObjTxCount = 0;                // total chunks
  int      loraObjTxSeq = 0;                  // next chunk to send
  uint8_t  loraObjTxId = 0;                   // this transfer's XFERID
  uint8_t  loraObjTxType = 0;                 // OBJTYPE of the outbound object
  uint32_t loraObjTxLastMs = 0;               // last-frame time (inter-frame gap)
  // inbound reassembly (one object at a time)
  bool     loraObjRxActive = false;
  uint8_t  loraObjRxBuf[LORA_OBJ_MAXLEN];     // reassembly buffer
  uint8_t  loraObjRxId = 0;                   // XFERID being reassembled
  uint8_t  loraObjRxType = 0;                 // OBJTYPE being reassembled
  int      loraObjRxCount = 0;                // total chunks expected
  uint16_t loraObjRxGot = 0;                  // bitmap of received chunks (<=8 -> uint16 ok)
  int      loraObjRxLen = 0;                  // highest byte offset written + 1
  uint32_t loraObjRxLastMs = 0;               // last-chunk time (for timeout)
  char     loraObjRxFrom[16] = {0};           // sender callsign (from the object body)
  // pending GP import awaiting the user's confirm (SCR_GPIMPORT)
  SatEntry loraImportSat;                     // the reassembled, parsed element set
  char     loraImportFrom[16] = {0};          // who sent it
  bool     loraImportPending = false;
  void loraObjSendGp(const SatEntry& s);      // start a jobbed GP-element broadcast
  void loraObjTxTick();                        // send the next chunk (called from loop)
  void loraObjRxFrame(const uint8_t* buf, int n, int rssi, int snr);  // handle one object frame
  bool loraParseGpBody(const char* body, SatEntry& out, char* fromOut, int fromCap);
  void drawGpImport(); void keyGpImport(char c, bool enter, bool back);
  void drawMessages();
  void keyMessages(char c, bool enter, bool back);
  // LoRa decoded-action screens (reached by ENTER on a message row whose text
  // carries an @position, #satellite, or !sked sigil -- see decodeMsg()).
  int      msgSel = 0;            // selected row in the Messages list (0 = newest)
  double   lcLat = 0, lcLon = 0;  // decoded peer position for the compass screen
  char     lcFrom[16] = {0};      // peer callsign for the compass screen
  uint32_t lcMsgMs = 0;          // millis() of the source message (age display)
  char     lsSat[12] = {0};       // decoded satellite name for the sat-detail screen
  uint32_t lsNorad = 0;           // decoded satellite NORAD for the sat-detail screen (0 = none)
  void drawLoraCompass();         // north-up bearing plot to a decoded peer position
  void keyLoraCompass(char c, bool enter, bool back);
  void drawLoraSat();             // detail for a satellite named in a received message
  void keyLoraSat(char c, bool enter, bool back);

  // Station roster (SCR_LORAROSTER): every station heard sending an @position is tracked
  // here so the operator can see who's in range and where. Positions are decoded from the
  // ordinary @lat,lon messages (no new frame type); the grid is computed locally. A fixed
  // array keyed by callsign, newest-heard first when drawn.
  struct RosterEntry {
    char     call[14];            // sender callsign
    double   lat, lon;            // last reported position
    int16_t  rssi;                // last RSSI (dBm)
    int8_t   snr;                 // last SNR (dB)
    uint32_t heardMs;             // millis() of last position heard
    uint32_t autoRepliedMs;       // millis() we last auto-replied to THIS station (0 = never)
  };
  static const int ROSTER_MAX = 16;
  RosterEntry rosterList[ROSTER_MAX];
  int      rosterCount = 0;
  int      rosterSel = 0;         // selected row (for the compass shortcut)
  uint32_t lastAutoReplyMs = 0;   // millis() of our last auto-reply (global rate limit)
  void rosterUpsert(const char* call, double lat, double lon, int rssi, int snr);  // record a heard position
  int  rosterIndexByCall(const char* call);   // -1 if not present
  void maybeAutoReplyPosition(const char* toCall);  // send our @pos if enabled + loop-guards pass
  void drawLoraRoster();
  void keyLoraRoster(char c, bool enter, bool back);
  void sendMyPosition();          // broadcast our @lat,lon (the 'p' key / presence ping)

  // AMSAT status screen (SCR_AMSATSTAT): a live view of every satellite with an AMSAT
  // activity report in the current window, sorted status-then-recency. Built into a small
  // index array (into the catalog) on entry; ENTER selects a sat as active.
  static const int AMSTAT_MAX = 64;
  int      amStatIdx[AMSTAT_MAX];   // catalog indices with a report, sorted for display
  int      amStatN = 0;
  int      amStatSel = 0, amStatScroll = 0;
  void buildAmsatStatusView();      // populate + sort amStatIdx from db
  void drawAmsatStatus();
  void keyAmsatStatus(char c, bool enter, bool back);
  void openDecodedAction(int orderIdx);   // ENTER on a message row: act on its sigils
  void beginSkedSend();                    // start the date->time prompt for a !sked send
  char ssSat[12] = {0};                    // sked-send: sat name captured at start
  uint32_t ssNorad = 0;                    // sked-send: sat NORAD captured at start (0 = none)
  char ssDate[11] = {0};                   // sked-send: date captured from the first prompt
  void buildOscarArc();              // sample the current/next pass ground track
  // Sun / Moon tracking screen (off main menu)
  bool     smOut = false;           // rotator engaged on the Sun/Moon screen
  int      smSel = 0;               // 0 = Sun, 1 = Moon
  bool     smGraphic = true;        // Sun/Moon screen: graphic sky-dome vs data list
  // Workable grid squares (4-char Maidenhead) under the footprint.
  // Allocated lazily (see ensureGridBits) rather than living in .bss: this ~4 KB
  // is only needed by the awards/grid-map screens, and keeping it out of the static
  // image raises the largest contiguous heap block that the TLS handshake needs.
  static const size_t GRID_BITS_LEN = 4050;   // 1 bit per 4-char grid (32400 total)
  uint8_t* gridBits = nullptr;      // lazily malloc'd; nullptr = not yet allocated
  bool     ensureGridBits();        // allocate+zero on first use; false if OOM
  int      gridN = 0;               // grids in footprint (set bits)
  int      gridScroll = 0;
  bool     gridLive = false;        // true = live now (from Track), false = pass union
  uint32_t gridBuiltMs = 0;         // last live rebuild (millis); 0 = build now
  char     gridFilter[5] = {0};     // optional prefix filter (e.g. "EM", "EM2", "EM21"); "" = all
  int      gridShown = 0;           // grids matching the filter (== gridN when no filter)
  // "Zap the Sats" — a tiny Space-Invaders easter egg (satellites are the invaders,
  // a ham op with an arrow antenna is the gun). All fixed-size state, no heap.
  static const int GAME_COLS = 6, GAME_ROWS = 3, GAME_INV = GAME_COLS * GAME_ROWS;
  static const int GAME_SHOTS = 4;
  uint8_t  gInvAlive[GAME_INV];     // 1 = satellite still up
  int      gInvLeft = 0, gInvTop = 0;   // top-left of the formation block (px)
  int      gInvDir = 1;             // +1 right, -1 left
  uint8_t  gInvType[GAME_INV];      // which sprite (0..2) per invader
  int      gInvAliveN = 0;          // remaining invaders
  int      gShotX[GAME_SHOTS], gShotY[GAME_SHOTS];   // player shots; y<0 = inactive
  int      gGunX = 0;               // gun centre x
  int      gScore = 0, gLives = 0, gLevel = 0;
  uint8_t  gState = 0;              // 0 attract, 1 playing, 2 win-wave, 3 game over
  uint32_t gStepMs = 0;             // last formation step (millis)
  uint32_t gFrameMs = 0;            // last frame advance

  // --- Games menu + additional mini-games. All fixed-size .bss, no heap. The
  // games share gScore/gState/gLevel/gFrameMs above where useful; each adds only
  // a few scalars. Sound + tilt are gated on cfg.gameSound / cfg.gameTilt.
  int      gamesSel = 0;            // Games-menu cursor
  // Doppler Lock: hold a marker on a drifting target (sim of transponder tuning).
  float    gdlTarget = 0, gdlCursor = 0, gdlVel = 0;   // 0..1 positions
  float    gdlPhase = 0;           // drift oscillator phase
  uint32_t gdlInMs = 0, gdlLastMs = 0;                 // time-in-band accum, last frame
  // Catch the Pass: key up when the sat crosses the elevation window.
  float    gpAz = 0, gpEl = 0, gpT = 0;                // arc parameter
  uint8_t  gpPhase = 0;            // 0 rising to window, 1 in window, 2 leaving
  int      gpMisses = 0;
  uint32_t gpArcMs = 0;
  // Rotor Runner: slew a crosshair (tilt/keys) to keep a moving sat centred.
  float    grSatX = 0, grSatY = 0, grSatVX = 0, grSatVY = 0;   // sat pos/vel (px)
  float    grCurX = 0, grCurY = 0;                     // crosshair pos (px)
  uint32_t grOnMs = 0, grLastMs = 0;
  // Morse Meteors: clear falling letters by keying their Morse.
  static const int GMOR_MAX = 5;
  int8_t   gmLetter[GMOR_MAX];     // -1 = empty slot, else 0..25 (A..Z)
  float    gmY[GMOR_MAX];          // fall position (px)
  int      gmX[GMOR_MAX];
  char     gmType[10];             // typed Morse buffer for the targeted letter
  uint8_t  gmTypeN = 0;
  uint32_t gmSpawnMs = 0, gmDotMs = 0;   // last spawn, last key-down (for dot/dash timing)
  // Grid Chase: pick the correct Maidenhead grid from options against a timer.
  char     ggGrid[8];              // the shown grid
  char     ggOpt[4][8];            // 4 option strings
  uint8_t  ggCorrect = 0;          // which option is right
  uint8_t  ggSel = 0;              // cursor
  int      ggStreak = 0;
  uint32_t ggDeadline = 0;         // answer-by (millis)

  // Workable US states/DC (parallel to grids: same footprint walk, point-in-polygon
  // lookup against bundled simplified boundaries). 51 entities -> 7-byte bitset.
  uint8_t  stateBits[7];            // 1 bit per entity (STATE_N <= 56)
  int      stateN = 0;             // entities in footprint (set bits)
  int      stateScroll = 0;
  bool     stateLive = false;      // true = live now (Track/Manual), false = pass union
  uint32_t stateBuiltMs = 0;       // last live rebuild (millis); 0 = build now

  // Workable DXCC (hybrid: country polygons + island/micro-entity points = 340).
  uint8_t  dxccBits[43];           // 1 bit per entity (DXCC_N = 340, hybrid)
  int      dxccN = 0;
  int      dxccScroll = 0;
  bool     dxccLive = false;
  uint32_t dxccBuiltMs = 0;
  // GP / orbital-elements source picker (AMSAT + CelesTrak categories + custom)
  int      gpSrcSel = 0;
  int      gpSrcScroll = 0;
  float    illumPeriodMin = 0;
  bool     illumSunNow    = true;
  bool     illumNextEclipse = false;
  long     illumNextSec   = -1;     // seconds to next sun<->shadow transition
  float    illumEclMin    = 0;      // eclipse minutes in the current orbit
  float    illumEclPct    = 0;
  // Live space weather (fetched with GP). f107 <= 0 means "none cached yet".
  float    spaceF107      = -1;     // last-known 10.7 cm solar radio flux (sfu)
  time_t   spaceWxEpoch   = 0;      // unix time the F10.7 value was observed/fetched
  float    spaceKp        = -1;     // latest planetary Kp index (0-9, -1 = none)
  float    spaceA         = -1;     // latest running A index (a_running, -1 = none)

  // Terrestrial weather (Open-Meteo), cached for offline use. -999 = no data.
  float    wxTempNow      = -999;   // current temperature (in cfg.wxUnits)
  float    wxWindNow      = -999;   // current wind speed (in cfg.wxUnits)
  int      wxWindDirNow   = -1;     // current wind direction (deg, -1 = none)
  int      wxHumidNow     = -1;     // current relative humidity (%, -1 = none)
  int      wxCodeNow      = -1;     // current WMO weather code (-1 = none)
  int      wxDayCount     = 0;      // number of forecast days parsed (0..WX_FORECAST_DAYS)
  // Hourly cloud cover (next 48 h) from the same Open-Meteo fetch, for judging
  // visible passes and transits. Base is the unix hour of sample 0; -1/-0 = none.
  uint8_t  wxCloud[48]; time_t wxCloudBase = 0; uint8_t wxCloudN = 0;
  int cloudAtUnix(time_t t);   // percent 0-100, or -1 outside the window
  int      wxDayCode[WX_FORECAST_DAYS] = {0};   // per-day WMO weather code
  float    wxDayHi[WX_FORECAST_DAYS]   = {0};   // per-day high temp
  float    wxDayLo[WX_FORECAST_DAYS]   = {0};   // per-day low temp
  int      wxDayPop[WX_FORECAST_DAYS]  = {0};   // per-day precip probability max (%)
  long     wxDayEpoch[WX_FORECAST_DAYS] = {0};  // per-day date (unix, local midnight)
  time_t   wxEpoch        = 0;      // when this weather was fetched (0 = none)
  uint8_t  wxCachedUnits  = 0;      // units the cached values are stored in
  int      spaceScroll    = 0;      // Space Wx screen scroll position
  char     dxGrid[8] = {0};
  double   dxLat = 0, dxLon = 0;

  // all-favorites schedule + AOS alarm
  SchedEntry sched[SCHED_MAX];
  int        schedN = 0, schedSel = 0;
  uint32_t   lastSchedMs = 0;          // throttle background rebuilds
  // Rove planner (SCR_PLANNER): a from-a-hypothetical-place/time pass survey. Enter a
  // grid, date, time and +/- window; for every favorite it lists each pass with AOS/LOS/
  // max-el and the number of workable US states and DXCC entities during that pass.
  // Fixed-size .bss result array -- no heap. The workable counts are footprint-based
  // (property of the satellite, independent of the entered site); the entered site only
  // affects AOS/LOS/max-el pass geometry. Compute is jobbed to stay responsive.
  struct PlanRow {
    uint32_t norad = 0;
    time_t   aos = 0, los = 0;
    float    maxEl = 0;
    uint8_t  states = 0;      // workable US states during the pass
    uint16_t dxcc = 0;        // workable DXCC entities during the pass
    uint16_t grids = 0;       // workable 4-char grids during the pass
  };
  static const int PLAN_MAX = 28;      // passes listed across all favorites in the window
  PlanRow  planRow[PLAN_MAX];
  int      planN = 0, planSel = 0, planScroll = 0;
  bool     planComputed = false;
  bool     planJobRunning = false;     // survey in progress (pumped from loop())
  int      planJobFav = 0;             // favorite index the jobbed build is up to
  double   planLat = 0, planLon = 0;   // entered site (from grid)
  char     planGrid[8] = "";
  time_t   planCenter = 0;             // entered date/time (unix UTC)
  int      planWindowH = 3;            // +/- hours
  Observer planObs;                    // entered-site observer for pass prediction
  void buildPlanner();                 // jobbed survey step
  void planSeedDefaults();             // fill grid/time from current site & clock
  void drawPlanner(); void keyPlanner(char c, bool enter, bool back);
  void drawPlanDetail(); void keyPlanDetail(char c, bool enter, bool back);
  String exportRovePlan();             // write the survey to a formatted .txt; returns path
  // planner input form: which field is being edited
  int      planField = 0;              // 0=grid 1=date 2=time 3=window 4=[compute]
  int      planDetailIdx = 0;          // which PlanRow the detail screen is showing
  void planDetailFrom(int idx);

  // ---- Workable horizon (SCR_WORKHZN): 10-day "ever workable" union across favorites ----
  // Sweeps every pass of the selected favorite(s) over HORIZON_DAYS and OR-accumulates which
  // DXCC / US states / grids are workable at least once. Union bitsets are cheap: OR-ing a
  // thousand passes costs the same RAM as one. Grids reuse the shared ~4 KB gridBits block,
  // freed on done/cancel (heap discipline). Jobbed one PASS per loop() with a progress bar.
  enum WhPhase { WH_IDLE, WH_COUNTING, WH_RUNNING, WH_DONE, WH_CANCEL };
  static const int HORIZON_DAYS = 10;
  // The union accumulator IS the shared stateBits / dxccBits / gridBits: zeroed once at start,
  // then addFootprint* OR into them per sample with no clearing between passes. No extra bitset.
  WhPhase  whPhase = WH_IDLE;
  bool     whAllFavs = true;           // scope: all favorites vs the single selected sat
  bool     whWantGrids = true;         // false = fast mode (states+DXCC only, no gridBits alloc)
  uint32_t whSingleNorad = 0;          // when !whAllFavs
  int      whFavCursor = 0;            // index into favs currently being swept
  time_t   whLastAos = 0;              // AOS of the previous pass processed (non-progress guard)
  time_t   whWinFrom = 0, whWinTo = 0; // per-favorite predictPasses paging cursor
  static const int WH_SEG_MAX = 20;    // day-segment cache (one pass processed per tick)
  PassPredict whSeg[WH_SEG_MAX];
  int      whSegN = 0, whSegI = 0;
  time_t   whHorizonEnd = 0;           // now + HORIZON_DAYS (fixed at start)
  int      whPassesDone = 0, whPassesTotal = 0;   // progress-bar numerator / denominator
  int      whStateN = 0, whDxccN = 0, whGridN = 0;// live popcounts
  uint32_t whLastDrawMs = 0;
  void whStart(bool allFavs, bool wantGrids, uint32_t singleNorad);  // seed + estimate passes
  void whJobTick();                    // advance one pass of the sweep (pumped from loop())
  void whNextFavorite();               // advance the pager to the next favorite (resets guard)
  void whFinish();                     // popcounts + free gridBits
  String whExport();                   // write the union to a .txt under /CardSat; returns path
  void drawWorkHzn(); void keyWorkHzn(char c, bool enter, bool back);

  // ---- Target search (SCR_TGTSEARCH pick / SCR_TGTHITS run+results) ----
  // Inverse of the workable horizon: pick ONE target (US state / DXCC / grid) and find every
  // pass on any favorite over HORIZON_DAYS during which it is workable. Keeps per-pass TIMING
  // (a small .bss hit list), not a union. The membership test is one shared pointInFootprint()
  // per sample against the target's representative point (bbox centre for state/DXCC polygons,
  // the point coord for DXCC point-entities, the locator centre for grids) -- far cheaper than
  // filling a bitset, so this search is snappier than the union sweep. Zero heap allocation.
  enum TsPhase { TS_PICK, TS_RUNNING, TS_DONE, TS_CANCEL };
  static const int TS_HIT_MAX = 40;
  struct HitRow {
    uint32_t norad;                    // favorite satellite
    time_t   aos, los;                 // full pass window
    time_t   inStart, inEnd;           // workable sub-window (target inside footprint)
    uint8_t  maxElWhole;               // pass max elevation (context)
  };
  HitRow   tsHits[TS_HIT_MAX];         // .bss result list
  int      tsHitN = 0;
  TsPhase  tsPhase = TS_PICK;
  uint8_t  tsKind = 0;                 // 0=state 1=dxcc 2=grid
  int      tsPickSel = 0, tsPickScroll = 0;   // selection cursor in the pick list
  char     tsFilter[8] = {0};          // type-to-filter prefix for the state/DXCC pick list
  int      tsGeoIdx = -1;              // state index, or DXCC geometry index (0..DXCC_N-1)
  double   tsLat = 0, tsLon = 0;       // resolved target representative point
  char     tsGrid[8] = {0};            // grid text entry (kind=grid)
  char     tsLabel[24] = {0};          // display label for the chosen target
  // Chronological merge across ALL favorites: keep each favorite's NEXT upcoming pass in a slot,
  // and each tick process the globally-earliest slot, then refill just that one. This yields hits
  // in time order across the fleet, so the TS_HIT_MAX cap keeps the SOONEST passes (not the first
  // favorite's). tsNextAos[i] == 0 means favorite i is exhausted.
  static const int TS_FAV_MAX = 32;    // favorites merged (bounds .bss; far above realistic use)
  PassPredict tsNextPass[TS_FAV_MAX];  // each favorite's next upcoming pass (valid if tsNextAos>0)
  time_t   tsNextAos[TS_FAV_MAX];      // AOS of that pass, or 0 when the favorite is exhausted
  time_t   tsCursor[TS_FAV_MAX];       // per-favorite paging cursor (search-from time)
  time_t   tsLastAos[TS_FAV_MAX];      // AOS of last CONSUMED pass (skips the re-returned stale one)
  int      tsFavCount = 0;             // number of valid favorites participating
  time_t   tsHorizonEnd = 0;
  int      tsPassesDone = 0, tsPassesTotal = 0;
  int      tsHitSel = 0, tsHitScroll = 0;
  Screen   tsReturn = SCR_SCHEDULE;    // where both target-search screens' back key returns to
  uint32_t tsLastDrawMs = 0;
  bool     tsResolveTarget();          // fill tsLat/tsLon/tsLabel from kind+selection; false=bad
  bool     tsMatchFilter(const char* code); // true if code starts with tsFilter (case-insensitive)
  int      tsFilteredGeoIdx(int matchPos);  // map the matchPos-th filtered entry -> geometry index
  void     tsStart();                  // seed per-favorite cursors + estimate pass total
  void     tsJobTick();                // process the globally-earliest next pass across favorites
  void     tsFinish();                 // mark done, request a redraw (hits already time-ordered)
  void     tsRefillFav(int fi);        // load favorite fi's next pass at/after its cursor
  void     drawTgtSearch(); void keyTgtSearch(char c, bool enter, bool back);
  void     drawTgtHits();   void keyTgtHits(char c, bool enter, bool back);
  String   tsExport();                 // write the hit list to a .txt under /CardSat

  // ---- Saved rove-plan browser (SCR_ROVELIST) + read-only viewer (SCR_ROVEVIEW) ----
  // Lists the .txt files exportRovePlan() writes under /CardSat/RovePlans and shows one,
  // read-only, in a scrolling viewer. The viewer holds a BOUNDED slice of the file in RAM
  // (ROVEVIEW_MAX) so a large plan can't exhaust the no-PSRAM heap; over the cap it shows a
  // truncation note and points the user at the web download for the whole file.
  static const int ROVE_LIST_MAX  = 40;    // saved plans listed in the browser
  static const int ROVE_NAME_MAX  = 40;    // base filename length (no dir; keeps ".txt")
  static const int ROVEVIEW_MAX   = 3000;  // max bytes of a plan held in the viewer (heap-bounded)
  char     roveList[ROVE_LIST_MAX][ROVE_NAME_MAX]; // base names (with ".txt")
  time_t   roveTime[ROVE_LIST_MAX];        // each plan's last-write time (0 if unknown)
  uint32_t roveSize[ROVE_LIST_MAX];        // each plan's size in bytes
  int      roveListN = 0;                  // plans found
  int      roveSel = 0;                    // browser cursor
  int      roveScroll = 0;                 // browser scroll offset
  bool     roveConfirmDel = false;         // two-step delete confirmation
  String   roveViewBuf;                    // bounded contents of the viewed plan
  String   roveViewName;                   // base name of the viewed plan
  bool     roveViewTrunc = false;          // true if the file exceeded ROVEVIEW_MAX
  int      roveViewTop = 0;                // top wrapped-row shown in the viewer
  void buildRoveList();                    // enumerate /CardSat/RovePlans newest-first
  void roveViewLoad(int idx);              // load roveList[idx] (bounded) into roveViewBuf
  void drawRoveList();  void keyRoveList(char c, bool enter, bool back);
  void drawRoveView();  void keyRoveView(char c, bool enter, bool back);
  // "Sky at a glance": a horizontal timeline of upcoming passes for all favorites
  // over the next SKY_HOURS. One row per favorite that has a pass in the window;
  // each bar is one pass, coloured by peak elevation. Fixed-size (.bss), no heap.
  static const int SKY_ROWS = 12;      // favorites shown (rows)
  static const int SKY_BARS = 60;      // total passes drawn across all rows
  static const int SKY_HOURS = 6;      // timeline span (hours)
  struct SkyBar { uint8_t row; time_t aos, los; float maxEl; bool vis; };
  SkyBar     skyBars[SKY_BARS];
  int        skyBarN = 0;
  char       skyName[SKY_ROWS][12];    // short sat name per row
  uint32_t   skyRowNorad[SKY_ROWS];
  int        skyRowN = 0;
  time_t     skyStart = 0;             // window start (== build time)
  uint32_t   skyBuiltMs = 0;
  // Awards tracking: tallies derived from the QSO log. The all-sats view uses the
  // shared grid/state/DXCC bitsets (computed once on entry); per-satellite drill-down
  // recomputes those bitsets filtered to one sat. Distinct sats and per-band counts
  // are accumulated while streaming. All fixed-size, no heap.
  static const int AW_SATS = 24;       // distinct satellites tracked in the log
  static const int AW_BANDS = 11;      // matches bandFor's enumeration length
  int        awQsoTotal = 0;           // total QSOs in the log
  int        awGridN = 0, awStateN = 0, awDxccN = 0;   // all-sats unique counts
  int        awBandQso[AW_BANDS];      // QSOs per band (all sats)
  char       awSatName[AW_SATS][18];   // distinct sat names
  int        awSatQso[AW_SATS];        // QSOs per distinct sat
  int        awSatN = 0;               // number of distinct sats
  int        awSel = 0, awScroll = 0;  // list cursor on the awards screen
  int        awSatSel = -1;            // which distinct sat is being drilled into
  int        awSatGridN = 0, awSatStateN = 0, awSatDxccN = 0;  // per-sat unique counts
  int        awSatBandQso[AW_BANDS];   // per-sat QSOs per band
  uint32_t   awBuiltMs = 0;
  // Awards list sub-view: a scrollable list of the actual worked grids / states /
  // DXCC entities (decoded from the bitsets), for either the all-sats totals or one
  // satellite. awListKind 0=grids 1=states 2=DXCC; awListSat -1=all sats else sat idx.
  int        awListKind = 0;
  int        awListScroll = 0;
  int        awListSat = -1;
  // Which data the shared grid/state/DXCC bitsets currently hold, so list views can
  // skip a redundant re-stream of the log: -1 = all-sats totals, >=0 = that sat index,
  // -2 = unknown/none. (Heap note 0.9.41: avoids re-scanning the whole log just to open
  // a list when the bitsets already hold the right data.)
  int        awBitsSat = -2;
  time_t     nextAos = 0;              // soonest upcoming favorite AOS (alarm)
  char       nextAosName[26] = {0};
  time_t     alarmAos = 0;             // AOS we're currently counting down to
  uint8_t    alarmMarks = 0;           // bitmask of countdown beeps already fired
  time_t     alarmTca = 0;             // TCA of the pass in progress (0 = none)
  time_t     alarmLos = 0;             // LOS of the pass in progress (0 = none)
  uint8_t    alarmPassMarks = 0;       // TCA/LOS beeps already fired this pass
  // user-set sked reminder (from an hams.at activation), independent of favorites
  time_t     skedAt = 0;               // unix UTC of the sked (0 = none set)
  uint8_t    skedMarks = 0;            // countdown beeps already fired (T-60/30/10/now)
  char       skedName[24] = {0};       // label shown in the sked flash/status
  uint32_t   aosFlashUntil = 0;        // screen-flash overlay end (millis)
  char       aosFlashName[26] = {0};
  uint32_t   skedFlashUntil = 0;       // sked-reminder flash overlay end (millis)
  int      curTx = 0;             // selected transponder index for active sat
  Transponder activeTx[MAX_TX_PER_SAT];
  int      activeTxCount = 0;     // transponders loaded for the active sat
  int      setSel = 0;            // settings menu cursor
  int      setCat = -1;           // settings category (-1 = top-level list, else 0..3)

  // WiFi scan (Settings -> WiFi SSID -> 's')
  static const int MAX_WIFI_AP = 16;
  WifiAp   wifiAp[MAX_WIFI_AP];
  int      wifiApCount = 0;
  int      wifiSel = 0;

  // tracking / doppler
  bool     radioOut = false;      // are we sending freqs to the rig?
  int32_t  calDl = 0, calUl = 0;  // working calibration (Hz), seeded from cfg
  int32_t  calStep = 10;          // Hz per calibration nudge
  int32_t  pbOffset = 0;          // passband tune offset (Hz up from dl bottom)
  int32_t  tuneStep = 1000;       // Hz per passband-tune nudge
  uint8_t  trackMode = 0;         // 0 = TUNE (passband), 1 = CAL (calibration)
  bool     imuReady = false;      // BMI270 present (Cardputer ADV) for tilt tuning
  float    tiltAccum = 0;         // sub-Hz tilt-rate integrator (carries fractional Hz)
  uint32_t lastTiltMs = 0;        // last tilt-tune service time (rate integration)
  bool     manFixUp = false;      // Manual mode: false = fix downlink, true = fix uplink
  Screen   liveReturn = SCR_TRACK; // polar/grid/log return here (Track or Manual)
  Screen   wListReturn = SCR_PASSES; // non-live states/DXCC list back target (default: Passes)
  Screen   trackReturn = SCR_PASSES; // where the Track screen's back key returns to
  Screen   passesReturn = SCR_SATLIST; // where the Passes screen's back key returns to
  Screen   lotwReturn = SCR_LOG; // where the LoTW upload screen's back key returns to
  uint32_t trackedNorad = 0;     // NORAD that radio/rotator tracking is engaged for;
                                 // if the active sat changes away from this while
                                 // tracking runs off-screen, the loop stops tracking
                                 // so the rig never silently retargets a new bird
  TuneMode tuneMode = TM_HOLD;    // Doppler tune mode (cycle with 'd' on Track)
  bool     cwMode   = false;      // linear bird: force CW on both legs (toggle 'm');
                                  // per-session, reset on every transponder change
                                  // holds a constant frequency AT THE SATELLITE
  uint32_t lastRxSet = 0;         // downlink dial the rig is on (read-back): knob detect + send guard
  uint32_t lastUlHz  = 0;         // last uplink dial commanded (send guard)
  uint8_t  uplinkDeferTicks = 0;  // suppress the uplink write for N ticks after a downlink write/knob
                                  // move (OscarWatch "defer uplink after dial move"); lets the SUB
                                  // read + downlink settle before the bus is used for the MAIN uplink
  static constexpr uint32_t FREQ_GUARD_HZ = 2;   // skip a re-send within this many Hz of the last value
  // Operator-knob-move detection. A read-back that differs from the last value we
  // wrote, by more than the threshold, is treated as a deliberate dial move (vs.
  // rig tuning-step rounding or read-back jitter). The threshold is mode-aware --
  // SSB/CW need fine resolution, FM channels are coarse -- following SatPC32, which
  // uses ~100 Hz for SSB/CW and 500 Hz+ for FM. RIG_STEP_HZ is the rounding margin:
  // most CAT rigs (Icom/Yaesu/Kenwood) resolve frequency to <=10 Hz, so a delta
  // below this is rig rounding, never a knob move.
  static constexpr uint32_t RIG_STEP_HZ      = 10;   // assumed worst-case rig freq resolution
  static constexpr uint32_t KNOB_MOVE_SSB_HZ = 30;   // SSB/CW: deliberate dial move threshold
  static constexpr uint32_t KNOB_MOVE_FM_HZ  = 250;  // FM: coarse, avoid chasing channelized jitter
  // While the operator is actively turning the dial, stop pushing Doppler writes to
  // the downlink for a short grace window so we never tug against the knob; we still
  // read and adopt their new passband point each tick. Resumes correcting once they
  // let go (no further moves for the window).
  static constexpr uint32_t TUNE_GRACE_MS    = 400;  // quiet-down window after a detected knob move
  uint32_t lastKnobMoveMs = 0;                       // millis() of the last detected dial move

  // Out-of-passband warning: when an operator knob move (FULL/DL One-True-Rule)
  // lands past a passband edge, pbOffset clamps to the edge and the downlink is
  // pulled back -- but we flash a transient banner so the operator knows they
  // tuned off the band. pbOobUntilMs is the millis() deadline the banner shows
  // to; pbOobDir is +1 (above the high edge) or -1 (below the low edge) for the
  // banner text. The banner outlives the brief grace window so it's actually
  // readable during the pull-back.
  static constexpr uint32_t PB_OOB_BANNER_MS = 1500; // how long the OOB banner flashes
  uint32_t pbOobUntilMs = 0;                         // millis() the OOB banner shows until
  int8_t   pbOobDir     = 0;                         // -1 below low edge, +1 above high edge

  // Mode-aware knob-move threshold for the active transponder (FM vs SSB/CW),
  // floored at the rig's rounding step so quantization never reads as a move.
  uint32_t knobMoveThreshHz(const Transponder& t) const {
    bool fm = (Rig::modeFromString(t.mode) == RM_FM);
    uint32_t base = fm ? KNOB_MOVE_FM_HZ : KNOB_MOVE_SSB_HZ;
    return base > RIG_STEP_HZ ? base : RIG_STEP_HZ;
  }
  static constexpr uint8_t  UPLINK_DEFER_TICKS = 1; // ticks to hold off the uplink write after a downlink
                                                    // write/knob move, so the SUB read + RX settle first
  // ---- CAT write deadband + adaptive threshold + predictive lead (OscarWatch-
  // inspired) -------------------------------------------------------------------
  // (1) Mode-aware write deadband: only push a leg when it moved more than the
  // threshold for its mode. FM tolerates kHz of Doppler in its passband, so a
  // loose value avoids needless CI-V chatter; linear SSB/CW needs tight tracking.
  static constexpr uint32_t DOPP_THRESH_FM_HZ     = 300;  // FM downlink/uplink write deadband
  static constexpr uint32_t DOPP_THRESH_LINEAR_HZ = 50;   // linear SSB/CW write deadband
  // (2) Adaptive threshold near TCA: when the Doppler slew (Hz/s) is high (fast
  // geometry around closest approach), tighten the threshold so the rig keeps up;
  // when slew is low, keep it loose. Linear interpolate between the breakpoints.
  static constexpr float    DOPP_SLEW_START_HZS = 15.0f;  // below this slew: base threshold
  static constexpr float    DOPP_SLEW_FULL_HZS  = 35.0f;  // at/above this: threshold halved
  static constexpr uint32_t DOPP_THRESH_MIN_HZ  = 25;     // floor when slew is high
  // (3) Predictive CAT lead: compute Doppler for now+lead to mask CAT latency,
  // but taper the lead to zero near TCA (where range rate is small and a forward
  // lead overshoots). Capped small; only meaningful on fast overhead passes.
  static constexpr float    DOPP_LEAD_MAX_MS        = 50.0f;  // hard cap on lead
  static constexpr float    DOPP_LEAD_TAPER_KMS     = 0.35f;  // |range rate| (km/s) for full lead
  static constexpr float    DOPP_LEAD_SLOPE_START   = 0.010f; // km/s^2: lead blend begins
  static constexpr float    DOPP_LEAD_SLOPE_FULL    = 0.016f; // km/s^2: lead blend full
  uint32_t lastDoppMs = 0;
  float    toneApplied = -2.0f;   // CTCSS tone currently set on the rig (Hz);
                                  // 0 = off, -2 = unknown/never (force re-apply)
  bool     rotOut = false;       // are we pointing the rotator?
  float    lastAzCmd = -999.0f;  // last az/el we commanded (deadband)
  float    lastElCmd = -999.0f;
  float    lastUnwrappedAz = -999.0f;  // actual az sent (tracks 0..450 overlap)
  // 450-deg overlap lookahead (OscarWatch-inspired): when an upcoming north wrap
  // is predicted, pre-commit to the 361-450 band BEFORE the bearing crosses, so
  // the rotator climbs into the overlap instead of unwinding ~360 the long way.
  // Computed in the live-track tick (only when NOT flipped); consumed by rotPoint
  // (only in ROT_AZ_450). These two guards keep it off the 180-flip and 360 paths.
  bool     rotAz450PreCommit = false;  // force +360 representation this command
  static constexpr float ROT_AZ_LOOKAHEAD_SEC = 3.0f;  // predict az this far ahead
  bool     rotParked = false;
  uint32_t lastRotMs = 0;
  PassPredict rotPass;             // cached upcoming pass, for AOS pre-positioning
  bool     rotPassValid = false;
  uint32_t rotPassNorad = 0;       // which satellite rotPass belongs to
  uint32_t lastRotPassMs = 0;      // throttle for rotPass recompute
  bool     rotFlipPass = false;    // flip decision for the active pass (held to LOS)
  time_t   rotFlipUntil = 0;       // LOS the current flip decision is valid until
  // rigctld TCP server (item 2): created when enabled and WiFi is up.
  WiFiServer* rigd = nullptr;
  WiFiClient  rigdCli;             // single connected client
  String      rigdBuf;             // line-assembly buffer
  uint8_t     rigdVfo = 0;         // 0 = downlink (VFOA/RX), 1 = uplink (VFOB/TX)
  uint32_t    rigdLastSub = 0, rigdLastMain = 0;  // last freq set (get_freq fallback)
  RigMode     rigdSubMode = RM_USB, rigdMainMode = RM_LSB;
  // rotctld TCP server (item 4): a networked PC drives the wired rotator.
  WiFiServer* rotd = nullptr;
  WiFiClient  rotdCli;             // single connected client
  String      rotdBuf;             // line-assembly buffer
  // Mobile web control page: a phone on the LAN selects sats, sees passes, and
  // drives the radio/rotator. One client at a time, serviced cooperatively.
  WiFiServer* webd = nullptr;
  WiFiClient  webdCli;            // single connected client
  String      webdBuf;            // request-assembly buffer (request line + headers)
  String      webdReqLine;        // the captured HTTP request line (method + path)
  uint32_t lastDrawMs = 0;
  uint32_t lastNetCycleMs = 0;    // millis() when the last update cycle STARTED (0 = never);
                                  // gates back-to-back cycles so TIME_WAIT PCBs can drain
  bool netCooldownOk();           // true if a new update cycle may start; else sets a "wait Ns" status
  uint32_t lastInputMs = 0;       // last keypress -- drives the screen-sleep timer
  bool     keyFn = false;         // Fn modifier state for the current keypress
                                  // (read by the notes editor for cursor moves)
  bool     screenAsleep = false;  // backlight blanked for power saving
  bool     lastGpsFix  = false;   // for Location-screen auto-refresh
  int      lastGpsSats = 0;

  // text editor
  String   editBuf;
  String   editTitle;
  int      editTarget = 0;        // which field is being edited
  PendingQso qso;                 // QSO currently being entered
  int      logSel = 0;            // selected field on the log-entry screen
  int      logMenuSel = 0;        // selected row on the Log menu
  Screen   logReturn = SCR_HOME;  // screen to return to after entry
  PendingQso logRecs[LOG_VIEW_MAX]; // recent log entries loaded for view/edit
  int      logFileRows[LOG_VIEW_MAX]; // file data-row index for each logRecs[] entry
                                      // (parallel array; survives the date sort)
  int      logRecN = 0;           // number loaded
  int      logFirstIdx = 0;       // file data-row index of logRecs[0]
  int      logListSel = 0;        // selected row in the log list
  int      logEditIdx = -1;       // editing an existing entry (array idx) or -1=new
  String   exportSats[16];        // distinct sats needing a LoTW SAT_NAME prompt
  int      exportPendN = 0;       // number queued
  int      exportPendIdx = 0;     // current prompt index
  bool     logDelArm = false;     // two-press delete confirmation
  bool     satDelArm = false;     // two-press delete confirmation (manual GP sat)
  bool     logPickSat = false;    // sat list opened to pick a satellite for a log entry

  // status line
  String   status;
  uint32_t statusUntil = 0;

  // ---- helpers ----
  void applyRadioFromCfg();
  void serviceRigctld();                       // pump the rigctld TCP server
  void rigdHandleLine(const String& line);     // parse + act on one rigctld command
  void serviceRotctld();                       // pump the rotctld TCP server
  void rotdHandleLine(const String& line);     // parse + act on one rotctld command
  void serviceWebd();                          // pump the mobile web-control server
  void suspendNetServers();                    // tear down rigd/rotd/webd listeners
  void freeCanvasForTls();                     // no-op since the BearSSL migration (see body); sprite stays resident
  void restoreCanvasAfterTls();
  void tickCanvasRestore();                    // loop-driven retry if restore alloc failed
  bool tryRecreateCanvas();                    // one sprite re-create attempt (shared by the two above)
  uint32_t lastCanvasRetryMs = 0;              // throttle for the loop-driven re-create retry
  bool canvasFreed = false;                    // true while the sprite is freed for a fetch
                                               // (free their sockets) before a
                                               // blocking download; they auto-rebuild
  static App* s_self;                          // for the static Net TLS hook to reach us
  static void tlsBusyTrampoline(bool busy);    // Net::onTlsBusy target
  static void catMonTrampoline(const char* dir, const uint8_t* b, size_t n);  // CatTraceFn target
  static int  s_fetchDepth;                    // >0 while any outbound fetch is active
  static bool netFetchActive();                // service*() skip rebuild while true
  void webdHandleRequest(const String& reqLine);  // route one HTTP request
  void webdSendStatusJson();                   // GET /api/status
  void webdSendSatsJson();                     // GET /api/sats
  void webdSendPassesJson();                   // GET /api/passes
  void webdSendOrbitJson();                     // GET /api/orbit (orbital analysis)
  void webdSendTxJson();                        // GET /api/tx (transponder list + current)
  bool webdSelectSat(uint32_t norad);          // POST /api/select
  void webdSendPage();                         // GET / (the mobile HTML page)
  // ---- Web file transfer (download + listing only; no upload) ----
  void webdSendFilesPage();                    // GET /files (the file-browser HTML page)
  void webdSendFileList(const String& path);   // GET /api/files?dir=... (JSON dir listing)
  void webdSendFile(const String& path);       // GET /api/file?path=... (stream a download)
  void applyRotatorFromCfg();
  bool passNeedsFlip(time_t aos, time_t los);  // per-pass flip decision (0-180 el rotators)
  void rotPoint(float az, float el);   // send az/el applying the az-range convention
  void applyTransponderModes(const Transponder& t);  // per-leg SSB/FM mode policy
  // Compute the per-tick CAT write deadband (mode-aware, adaptively tightened
  // near TCA) and the TCA-tapered predictive lead range rate. `rrNow` is the
  // instantaneous range rate (km/s); `centerHz` the higher of the two leg freqs
  // (for slew estimation); `linear` selects the linear vs FM base threshold.
  // Returns the effective threshold (Hz); writes the lead-adjusted range rate
  // (km/s) to *leadRrOut.
  uint32_t dopplerThreshAndLead(double rrNow, uint32_t centerHz, bool linear,
                                double nowSec, double* leadRrOut);
  // Route logical downlink/uplink to the physical MAIN/SUB VFOs per cfg.vfoType.
  // Receive-only transponders (no uplink: beacons, telemetry, SSTV, CW) are tuned
  // on the MAIN band with satellite mode OFF -- this matches how OscarWatch and the
  // SDR-Control apps drive Icom rigs for receive-only, and on the IC-821 the MAIN
  // band also reads far more reliably than SUB. txReceiveOnly() reflects the active
  // transponder; when true, the downlink ignores the VFO-type swap and uses MAIN.
  bool txReceiveOnly() const {
    return activeTxCount > 0 && activeTx[curTx].uplink == 0 && activeTx[curTx].downlink != 0;
  }
  bool dlOnSub() const {
    if (txReceiveOnly()) {                            // receive-only (beacon/telemetry)
      switch (cfg.rxOnlyVfo) {
        case RXO_MAIN: return false;                 // force downlink to MAIN (legacy)
        case RXO_SUB:  return true;                  // force downlink to SUB
        default: break;                              // RXO_FOLLOW: fall through to vfoType
      }
    }
    return cfg.vfoType == VFO_MAIN_UP_SUB_DOWN;
  }
  bool rigSetDownlinkFreq(uint32_t hz) { return dlOnSub() ? rig->setSubFreq(hz)  : rig->setMainFreq(hz); }
  bool rigSetUplinkFreq  (uint32_t hz) { return dlOnSub() ? rig->setMainFreq(hz) : rig->setSubFreq(hz); }
  void rigSetDownlinkMode(RigMode m)   { if (dlOnSub()) rig->setSubMode(m);  else rig->setMainMode(m); }
  void rigSetUplinkMode  (RigMode m)   { if (dlOnSub()) rig->setMainMode(m); else rig->setSubMode(m); }
  bool rigReadDownlinkFreq(uint32_t& h){ return dlOnSub() ? rig->readSubFreq(h) : rig->readMainFreq(h); }
  void rigSelectDownlink()             { if (dlOnSub()) rig->selectSubBand();  else rig->selectMainBand(); }
  void rigSelectUplink()               { if (dlOnSub()) rig->selectMainBand(); else rig->selectSubBand(); }
  // Drive the downlink dial, but only when it actually moved (don't re-send the
  // same freq every cycle -- avoids CI-V chatter and receiver-mute clicks). After
  // a real set, read the accepted freq back and remember THAT, so the rig's
  // tuning-step rounding can't later masquerade as an operator knob move. Pass
  // readback=false in modes where we don't follow the knob (saves a round-trip).
  // Returns true if it actually wrote the dial (the caller uses this to briefly
  // defer the uplink write so the SUB read + downlink settle first -- the
  // OscarWatch "defer uplink after a dial move" rule, important on slow single-
  // wire Main/Sub rigs like the IC-821).
  bool driveDownlink(uint32_t rx, bool readback, uint32_t threshHz = FREQ_GUARD_HZ) {
    uint32_t d = (rx > lastRxSet) ? rx - lastRxSet : lastRxSet - rx;
    if (lastRxSet && d < threshHz) return false;         // within deadband: already there
    if (!rigSetDownlinkFreq(rx)) return false;
    uint32_t back;
    lastRxSet = (readback && rig->canReadFreq() && rigReadDownlinkFreq(back)) ? back : rx;
    return true;
  }
  // Drive the uplink dial only when it moved; then leave the active band on the
  // downlink so the operator's knob/read stays on RX. No read-back (the uplink
  // knob is never followed).
  void driveUplink(uint32_t tx, uint32_t threshHz = FREQ_GUARD_HZ) {
    uint32_t d = (tx > lastUlHz) ? tx - lastUlHz : lastUlHz - tx;
    if (lastUlHz && d < threshHz) return;
    if (rigSetUplinkFreq(tx)) { lastUlHz = tx; rigSelectDownlink(); }
  }
  // Uplink write with a one-tick defer after a downlink write or operator knob
  // move (OscarWatch "defer uplink after a dial move"). Sequence: consume any
  // pending defer, drive the uplink only if not currently deferred, then re-arm
  // the defer if the downlink moved this tick AND we just drove. The re-arm guard
  // guarantees that during a fast Doppler slew (downlink writing every tick) the
  // uplink still services every other tick instead of starving. `ulEnabled` is
  // the caller's drvUL && t.uplink condition.
  void driveUplinkDeferred(uint32_t tx, uint32_t threshHz, bool dlMoved, bool ulEnabled) {
    bool deferred = (uplinkDeferTicks > 0);
    if (uplinkDeferTicks > 0) uplinkDeferTicks--;
    bool drove = false;
    if (!deferred && ulEnabled) { driveUplink(tx, threshHz); drove = true; }
    if (dlMoved && drove) uplinkDeferTicks = UPLINK_DEFER_TICKS;   // defer the NEXT uplink
  }
  // Soft floor: never send CAT faster than the configured baud can comfortably
  // service one update. Returns max(configured rate, baud-derived minimum), ms.
  uint32_t effectiveCatRateMs() const {
    uint32_t baud = cfg.civBaud ? cfg.civBaud : 9600;
    uint32_t floorMs = (CAT_BYTES_PER_UPDATE * 10000UL) / baud;
    return cfg.catRateMs > floorMs ? cfg.catRateMs : floorMs;
  }
  float desiredToneHz() const;                       // PL tone wanted for current TX
  void  applyCtcssForCurrentTx();                    // push CTCSS to rig if changed
  float toneOverrideHz(uint32_t norad);              // user CTCSS override, <0 = none
  void  saveToneOverride(uint32_t norad, float hz);  // hz<0 clears (reverts to auto)
  void  retagTones(uint32_t norad);                  // re-apply override/table to activeTx
  void startGps();                         // (re)open GPS per cfg.gpsSource
  void factoryReset();                     // wipe LittleFS + reboot to defaults
  void setStatus(const String& s, uint32_t ms = 2500);
  time_t nowUtc();
  SatEntry* activeSat();
  bool ensureTransponders(SatEntry& s);   // load (cache or net)
  void onTransponderChanged();             // recenter passband + pick default mode
  bool connectWifiCfg(uint32_t timeoutMs = 12000);  // try primary then 2nd WiFi
  void serviceTiltTune();                  // accelerometer (tilt) passband tuning (ADV)
  void doUpdateGp();
  void doFastUpdate();                      // GP + transponders for favorites only
  String gpSourceLabel();                  // human label for the configured GP source
  void doCacheAllTransponders();           // fetch+cache every sat's TX (offline prep)
  int  cacheTxBatch(int start);            // cache one TX_CACHE_BATCH-sized batch
  void resumeCacheIfPending();             // continue a batched cache run after reboot
  void fetchAmsatStatus();                 // fetch AMSAT OSCAR status, mark active/not-heard
  void fetchSpaceWeather();                // fetch F10.7 solar flux (best-effort, with GP)
  void fetchWeather();                     // fetch terrestrial weather (Open-Meteo, best-effort)
  void spaceWxEnter();                     // open Space Wx screen, show cache, fetch if WiFi up
  void weatherEnter();                     // open Weather screen, show cache, fetch if WiFi up
  bool loadWeatherCache();                 // load cached weather from flash on boot
  static const char* wxCodeText(int code); // WMO weather code -> short label
  static const char* windDirName(int deg);  // degrees -> 16-point compass ("" if <0)
  const char* wxTempUnit();                // "F" or "C" per cfg.wxUnits
  const char* wxWindUnit();                // "mph" / "km/h" / "m/s" per cfg.wxUnits
  void loadSpaceWeather();                 // load cached F10.7 from flash at boot
  double decayDensityScale() const;        // density scale for the decay point estimate
  uint8_t decayLevelFor(const SatEntry& s); // 0 none / 1 watch / 2 soon / 3 imminent (list flag)
  void loadCalForSat(uint32_t norad);      // per-satellite calibration -> calDl/calUl
  void saveCalForSat(uint32_t norad);      // persist current calDl/calUl for this sat
  // Per-satellite operating notes (keyed by NORAD, persisted in FILE_NOTES).
  static constexpr int NOTE_MAX = 120;     // max note length (chars), no-PSRAM cap
  static constexpr int NOTE_FILE_MAX = 64; // max stored notes (file-size cap)
  // Max LoRa message text (chars). Must match the on-air LORA_TEXT_MAX in app.cpp;
  // used to cap the compose field so long messages can't overflow the screen.
  static constexpr int MSG_TEXT_MAX = 48;
  char     satNote[NOTE_MAX + 1] = {0};    // active satellite's note (RAM)
  void     loadNoteForSat(uint32_t norad); // fills satNote from FILE_NOTES (or "")
  void     saveNoteForSat(uint32_t norad, const char* text);  // persist/clear a note
  bool     satHasNote(uint32_t norad);     // quick test for the list glyph
  void loadFavs();
  void saveFavs();
  bool isFav(uint32_t norad) const;
  void toggleFav(uint32_t norad);
  void buildSatView();                     // (re)build the filtered satellite list
  void buildSchedule();                    // next pass for every favorite, sorted
  // Evaluate whether a pass is visually observable (satellite sunlit, observer in
  // darkness, peak elevation over the gate). Samples the above-horizon interval.
  // Returns the visWhy code and sets vis=true when observable.
  uint8_t visEvalPass(time_t aos, time_t los, float maxEl, bool& vis);
  void buildPassDetail(const PassPredict& p);  // sample one pass into pdEl/pdAz/pdSunlit
  void computePasses();                        // predict passes[] for the active sat
                                               // and fill passVis[] once (not per frame)
  void buildPolarPath();                       // sample current/next pass for the live polar
  void buildPolarForPass(uint32_t norad, time_t aos, time_t los);  // polar arc for a specific pass
  void computeMutual(const String& grid);      // co-visibility windows vs a DX grid
  void drawPolarGrid(int cx, int cy, int R);   // shared polar rings + cardinal cross
  void drawPolarArc(int cx, int cy, int R, const float* az, const float* el, int n);
  void refreshScheduleIfNeeded();          // rebuild in the background when due
  void serviceAosAlarm();                  // countdown beeps + flash before AOS
  void serviceSkedAlarm();                 // countdown beeps + flash before a user sked
  bool setSkedFromActivation(int idx);     // arm a sked from hamsatList[idx] (false if no time)
  void drawSkedEntry();  void keySkedEntry(char c, bool enter, bool back);  // manual activation entry
  bool saveUserSked();   int loadUserSked();   // persist/restore user-entered activations
  void mergeUserSked();  // append userSked[] into hamsatList[] (after a feed parse / cache load)
  void beginSkedEntry(int editIdx);  // open the entry screen for a new (-1) or existing entry
  void sleepUntilNextPass();               // deep-sleep until ~60 s before AOS
  void saveManualTx(uint32_t norad, const Transponder& t);
  int  loadManualTx(uint32_t norad, Transponder* out, int maxN);
  bool deleteManualTx(uint32_t norad, int manualIdx);  // remove the Nth manual TX

  // ---- input ----
  void handleKey(char c, bool enter, bool back);
  void takeScreenshot();                   // 'b' key -> BMP to /CardSat/Screenshots/

  // ---- per-screen render ----
  void draw();
  void drawHome();
  void drawSatList();
  void drawSchedule();
  void drawPasses();
  void drawPassDetail();
  void drawTrack();
  void drawBig();    // large-font operating readout (toggled from Track with 'z')
  void drawManual();
  void drawManualBig();   // large-font version of the Manual calculator ('z')
  void drawPolar();
  void drawPassPolar();
  void drawMutual();
  void drawLocation();
  void drawUpdate();
  void drawSettings();
  void drawWifiScan();
  void drawAbout();
  void drawLog();
  void drawLogEntry();
  void drawLogList();
  void drawEdit();
  void drawMemos();                 // voice-memo browser (list / play / delete)
  void buildMemoList();             // (re)enumerate memos from SD, newest first
  void drawOscar();                 // OSCARLOCATOR live azimuthal-equidistant view

  // ---- per-screen input ----
  void keyHome(char c, bool enter, bool back);
  void keyMemos(char c, bool enter, bool back);
  void keyOscar(char c, bool enter, bool back);
  void keySatList(char c, bool enter, bool back);
  void keySchedule(char c, bool enter, bool back);
  void buildSkyGlance(); void drawSkyGlance(); void keySkyGlance(char c, bool enter, bool back);
  void buildAwards(); void drawAwards(); void keyAwards(char c, bool enter, bool back);
  void drawAwardSat(); void keyAwardSat(char c, bool enter, bool back);
  void awardsForSat(const char* satName);   // per-sat tally (drill-down), reuses shared bitsets
  void openAwardList(int kind, int satIdx);  // load bitset + show grids/states/DXCC list
  void drawAwardList(); void keyAwardList(char c, bool enter, bool back);
  void keyPasses(char c, bool enter, bool back);
  void keyPassDetail(char c, bool enter, bool back);
  void keyTrack(char c, bool enter, bool back);
  void keyBig(char c, bool enter, bool back);
  void keyManual(char c, bool enter, bool back);
  void keyManualBig(char c, bool enter, bool back);
  void keyPolar(char c, bool enter, bool back);
  void keyPassPolar(char c, bool enter, bool back);
  void keyMutual(char c, bool enter, bool back);
  void buildVis();   void drawVis();   void keyVis(char c, bool enter, bool back);
  void buildVisList(); void drawVisList(); void keyVisList(char c, bool enter, bool back);
  void buildIllum(); void drawIllum(); void keyIllum(char c, bool enter, bool back);
  void buildOrbit(bool quiet = false); void drawOrbit(); void keyOrbit(char c, bool enter, bool back);
  void buildEqx();   void drawEqx();   void keyEqx(char c, bool enter, bool back);
  void drawSim();    void keySim(char c, bool enter, bool back);
  void drawSunMoon(); void keySunMoon(char c, bool enter, bool back);

  // EME (moonbounce) screen (SCR_EME), reached from the Sun/Moon screen. Live lunar
  // Doppler per band, topocentric range + range-rate, path degradation, and a coarse
  // sky-noise flag; a sub-view finds the mutual-Moon window against a DX grid.
  bool     emeRotOut = false;       // rotator engaged pointing at the Moon from EME screen
  static const int EME_MUT_MAX = 32;
  struct EmeWin { time_t aos; time_t los; };
  EmeWin   emeMut[EME_MUT_MAX];     // mutual-Moon (common-window) list
  char     emeMutGrid[10] = {0};    // DX grid the mutual list was computed for
  int      emeMutN = 0, emeMutSel = 0, emeMutScroll = 0;  // mutual-Moon window list
  bool     emeMutShown = false;     // showing the mutual-window sub-view
  void drawEme(); void keyEme(char c, bool enter, bool back);
  void emeComputeMutual(const String& grid);   // fill the mutual-Moon window list

  // Grid distance/bearing calculator (SCR_GRIDCALC), a main-menu tool. Enter a
  // Maidenhead grid -> great-circle distance + beam heading from your QTH, with an
  // option to point the rotator at that bearing. Seedable from SCR_QRZGRID (a separate
  // QRZ-lookup screen that leaves the existing QRZ screen untouched).
  char     gcGrid[10] = {0};        // the entered/seeded target grid
  bool     gcHaveResult = false;
  double   gcDistKm = 0, gcBearing = 0;
  bool     gcRotOut = false;        // rotator pointed at the grid bearing
  void drawGridCalc(); void keyGridCalc(char c, bool enter, bool back);
  void gcCompute();                 // recompute distance/bearing from gcGrid + QTH

  // Separate QRZ-lookup-to-grid screen (SCR_QRZGRID): looks up a callsign's grid and
  // seeds the grid calculator. Independent of the existing SCR_QRZ screen.
  char     qgCall[12] = {0};
  String   qgGrid, qgName, qgStatus;
  bool     qgHave = false;
  void drawQrzGrid(); void keyQrzGrid(char c, bool enter, bool back);
  void qrzGridLookup();             // resolve qgCall -> grid via qrzLookup(), fill qg* fields

  // Frequency/allocation reference (SCR_BANDPLAN): a scrollable worldwide amateur
  // band reference, LF to light, with ITU-region differences, VHF/UHF/microwave and
  // EME calling frequencies, and satellite subbands + band designators.
  int      bpScroll = 0;
  void drawBandPlan(); void keyBandPlan(char c, bool enter, bool back);

  // HF/6m propagation guidance (SCR_PROP), off the Space Wx screen. Translates the
  // F10.7 solar flux and Kp index (already fetched for Space Wx) into rule-of-thumb
  // band-opening, geomagnetic, aurora, and absorption guidance. No new data source.
  void drawProp(); void keyProp(char c, bool enter, bool back);

  // Previous space-weather sample (for trend deltas on the propagation screen).
  // Shifted from the current values when a fresh fetch lands >= 6 h newer; persisted
  // in the space-wx cache so trends survive a reboot. -1/0 = no prior sample.
  float    spacePrevF107 = -1;
  float    spacePrevKp   = -1;
  time_t   spacePrevEpoch = 0;

  // Station-readiness checklist (SCR_READY), off About: green/red rows for clock,
  // grid, GP data/age, WiFi, radio, rotator, SD, callsign, favorites, battery.
  void drawReady(); void keyReady(char c, bool enter, bool back);

  // Post-LOS handoff on Track: when the tracked bird drops below the horizon a
  // banner offers the next favorite pass (from serviceAosAlarm's nextAos) and 'q'
  // arms deep-sleep-until-pass. trkPrevEl carries the previous tick's elevation.
  Screen dxdReturn = SCR_MUTUAL;   // where DX Doppler backs to (Mutual or Mutual detail)
  uint32_t losPromptUntil = 0;
  float    trkPrevEl = -999;

  // Low-battery note on the AOS lead alert (-1 = none this pass).
  int      aosFlashBatt = -1;

  // EME 30-day planning view (SCR_EMEPLAN): per-day Moon declination and path
  // degradation, computed once on entry at 12:00 UTC each day.
  void drawEmePlan(); void keyEmePlan(char c, bool enter, bool back);

  // Per-satellite AMSAT report detail (SCR_AMSRPT), 'g' from the AMSAT status
  // screen: recent individual reports (callsign, grid, age, status) for the
  // selected bird, fetched on demand from reports.php, plus a distinct-grid
  // count -- a footprint-activity read the summary count can't give.
  struct AmsRpt { char call[12]; char grid[8]; uint8_t st; time_t t; };
  AmsRpt   amsRpt[24];
  int      amsRptN = 0; int amsRptScroll = 0; int amsRptGrids = 0;
  char     amsRptFor[14] = "";
  void fetchAmsatReports(const char* apiName);
  void drawAmsRpt(); void keyAmsRpt(char c, bool enter, bool back);

  // AMSAT status reporting. postAmsatReport() POSTs {name, report, callsign,
  // grid_square, reported_at} to reports.php (server-side dedup: same sat +
  // call + hour + 15-min period replaces). Track's one-key path arms on the
  // first press and sends "Heard" on the second within the window; ambiguous
  // multi-mode birds (AO-7_[U/v] vs _[V/a]) open the picker instead, which is
  // also the deliberate full-status path from the AMSAT status screen.
  void fetchAmsatCatalog();                       // GET catalog.php -> map (cached)
  bool postAmsatReport(const char* apiName, const char* status);
  const char* amsPickNameFor(int satIdx, bool& ambiguous);  // mode-aware choice
  uint32_t rptArmUntil = 0;                       // Track confirm-tap window
  char     rptArmName[28] = "";
  const char* pickName[6]; int pickN = 0, pickSel = 0, pickStat = 0, pickNameSel = 0;
  int      pickSatIdx = -1;
  Screen   pickReturn = SCR_AMSATSTAT;
  void drawAmsRpick(); void keyAmsRpick(char c, bool enter, bool back);

  // ---- Tools hub (off About, key 't') --------------------------------------
  // A menu of ham-radio bench tools: an infix scientific calculator plus a set
  // of live-recalc forms (coax loss, antenna dimensions, RF unit conversions,
  // station math). All computation is local -- these work offline.
  int toolsSel = 0;
  void drawTools(); void keyTools(char c, bool enter, bool back);

  // Infix scientific calculator (SCR_CALC): type an expression, ENTER evaluates.
  // Supports + - * / ^ (), unary -, and sin/cos/tan/asin/acos/atan/log/ln/sqrt/
  // exp/abs plus pi and e; trig in degrees. Result carries into the next entry
  // as "Ans".
  String calcBuf, calcResult; bool calcErr = false; double calcAns = 0;
  void drawCalc(); void keyCalc(char c, bool enter, bool back);
  double calcEval(const char* s, bool& ok);

  // Programmer's calculator (SCR_PCALC): a 64-bit value shown simultaneously in
  // hex / dec / bin / oct, with bitwise ops (AND OR XOR NOT << >>) and +-*/ via a
  // pending-operation model. Handy for CI-V byte math and bitmask work. Entry base
  // is selectable; digits are validated against it.
  uint64_t pcalcVal = 0;       // current (accumulator) value
  uint64_t pcalcAcc = 0;       // stashed operand while an operation is pending
  int   pcalcBase = 16;        // entry/edit base: 16, 10, 8, or 2
  int   pcalcWidth = 32;       // display width in bits: 8, 16, 32, 64
  char  pcalcPend = 0;         // pending op: & | ^ + - * / < > (0 = none)
  bool  pcalcFresh = true;     // next digit starts a new value (post-op / post-equals)
  void  drawPCalc(); void keyPCalc(char c, bool enter, bool back);

  // Character / raw-value lookup (SCR_CHARLK): one value shown at once in all four
  // bases plus what it *is* -- ASCII char + control name, Morse, Baudot/ITA2 (US-TTY,
  // both shifts), and the BCD reading (CI-V encodes frequencies as BCD). ;/. browse
  // +-1, ,// cycle the entry base, digits enter in that base, g-z looks a character
  // up directly.
  uint8_t clkVal = 65;         // current value ('A')
  int     clkBase = 16;        // entry base
  bool    clkFresh = true;
  bool    clkTableMode = false; // false = single-char detail, true = browsable full table
  int     clkTableScroll = 0;   // first row shown in table mode
  void drawCharLk(); void keyCharLk(char c, bool enter, bool back);
  void drawCharLkTable();   // browsable full ASCII/Morse/Baudot table mode

  // DXCC entity lookup (SCR_DXLK search, SCR_DXLKD detail). Type a prefix, partial
  // name, or entity code; matches are listed live. ENTER opens a detail card with the
  // code, prefix, continent, ITU/CQ zones, name, flags, and any ARRL footnotes. Data
  // is an embedded table generated from the ARRL DXCC list (dxcc_lookup.h).
  String dxQuery;
  int dxSel = 0, dxScroll = 0;      // selection + scroll within the filtered list
  int dxMatch[32]; int dxMatchN = 0; // indices into DXCC_LK of current matches (capped)
  int dxDetail = 0;                  // DXCC_LK index shown on the detail card
  void dxRunFilter();
  void drawDxLk(); void keyDxLk(char c, bool enter, bool back);
  void drawDxLkDetail(); void keyDxLkDetail(char c, bool enter, bool back);

  // CQ (WAZ) zone reference (SCR_CQZ list, SCR_CQZD detail). All 40 zones with their
  // names; the detail view shows the full prefix/region text. Reachable from the Tools
  // hub and jumped-to from a DXCC entity's CQ zone. Embedded from the CQ WAZ list.
  int cqzSel = 0, cqzScroll = 0;
  int cqzDetail = 0;
  int cqzDScroll = 0;
  int cqzReturn = 0;
  void openCqZone(int zoneNum, int ret);
  void drawCqz(); void keyCqz(char c, bool enter, bool back);
  void drawCqzDetail(); void keyCqzDetail(char c, bool enter, bool back);

  // ITU zone reference (SCR_ITUZ list, SCR_ITUZD detail). ITU zones have no names --
  // just number + prefix/region text. Same shape as the CQ-zone tool; jumped-to from a
  // DXCC entity's ITU zone. Embedded from the RSGB ITU zones list.
  int ituSel = 0, ituScroll = 0;
  int ituDetail = 0;
  int ituDScroll = 0;
  int ituReturn = 0;
  void openItuZone(int zoneNum, int ret);
  void drawItuz(); void keyItuz(char c, bool enter, bool back);
  void drawItuzDetail(); void keyItuzDetail(char c, bool enter, bool back);

  // Link budget calculator (SCR_LINKB). The full chain: TX power -> feedline ->
  // antenna -> free-space path (+ extra losses) -> RX antenna -> feedline -> receiver
  // noise floor -> SNR -> margin against the selected mode's requirement. Twelve
  // inputs in a scrolling list with the key outputs pinned below, all live-recalc.
  // A mode preset row sets bandwidth + required SNR together.
  static const int LB_NF = 12;         // number of input rows (row 0 = mode preset)
  double lbVal[LB_NF];
  int  lbSel = 1, lbScroll = 0;        // start on Freq (row 1)
  int  lbMode = 2;                     // preset index (default SSB)
  bool lbEditing = false; String lbEditBuf;
  bool lbSynced = false;               // distance/freq pre-filled from the tracked sat
  void lbInit();
  void lbSyncFromSat();
  void drawLinkB(); void keyLinkB(char c, bool enter, bool back);

  // Operating references (SCR_OPREF): Q-codes, ITU phonetics, RST -- tabbed static text.
  int oprefTab = 0;      // 0=Q-codes 1=phonetics 2=RST
  int oprefScroll = 0;
  void drawOpref(); void keyOpref(char c, bool enter, bool back);
  // CubeSatSim command & control reference (SCR_CUBESIM): offline crib for commanding
  // AMSAT's CubeSat Simulator (DTMF / APRS / carrier, plus config-script options).
  void drawCubeSim();
  void keyCubeSim(char c, bool enter, bool back);
  int  cubesimScroll = 0;
  // AMSAT Fox anatomy (SCR_FOXANAT): animated Learn explainer -- a rotating 1U
  // wireframe with one doc-verified callout at a time (see drawFoxAnat).
  void drawFoxAnat();
  void keyFoxAnat(char c, bool enter, bool back);
  float    foxTheta = 0.0f;      // spin angle (rad)
  uint8_t  foxLabel = 0;         // current callout index
  bool     foxSpin  = true;      // auto-rotate (space toggles)
  uint32_t foxLblMs = 0;         // last label-advance time
  // Companion Learn text screens: Fox/CubeSat primer (SCR_FOXTEXT, via `i` on the
  // anatomy) and CubeSat Simulator intro (SCR_CSIMINFO, via `c` on Help).
  void drawFoxText();
  void keyFoxText(char c, bool enter, bool back);
  int  foxTextScroll = 0;
  void drawCsimInfo();
  void keyCsimInfo(char c, bool enter, bool back);
  int  csimInfoScroll = 0;
  // printPath, when set, overrides PR_ROVE's source file: the rove-plan viewer sets it
  // so its `p` prints the plan being READ rather than a fresh survey export.
  String printPath;
  // Print submenu (SCR_PRINTABOUT, opened with `p` from About): a scrollable list of
  // EVERY printable report, so reports without a natural home screen (ticket, card,
  // keps, log, horizon) are reachable on-device, alongside the contextual `p` keys.
  void drawPrintAbout();
  void keyPrintAbout(char c, bool enter, bool back);
  int  paSel = 0;
  // CTCSS tone reference (SCR_CTCSS): standard EIA tone list + known satellite tones.
  int ctcssScroll = 0;
  void drawCtcss(); void keyCtcss(char c, bool enter, bool back);
  // Radio math reference (SCR_MATHREF): a scrolling cheat sheet distilled from the ARRL
  // Radio Mathematics supplement -- dB table, AC RMS/peak factors, constants, formulas.
  int mathRefScroll = 0;
  void drawMathRef(); void keyMathRef(char c, bool enter, bool back);
  // State-vector -> GP-element fitter (SCR_GPFIT). Enter an epoch and a TEME state vector
  // (r km, v km/s); a differential-correction fit against CardSat's own SGP4 recovers the
  // GP mean elements, which can be saved as a manual satellite. Heap-flat: a few 6-vectors
  // and a 6x6 Jacobian on the stack. Input MUST be TEME (no on-device frame transform);
  // B*=0 (a single state carries no drag info). See STATEVEC_TO_GP_SCOPING.md.
  double gpfEpoch = 0;                 // entered epoch (unix UTC)
  double gpfR[3] = {0,0,0};            // TEME position, km
  double gpfV[3] = {0,0,0};            // TEME velocity, km/s
  int    gpfFrame = 0;                 // 0 = TEME input, 1 = J2000 (transformed to TEME)
  int    gpfField = 0;                 // 0=epoch 1..3=r 4..6=v 7=[solve]
  bool   gpfSolved = false;
  bool   gpfConverged = false;
  double gpfResidM = 0;                // final position residual, metres
  SatEntry gpfResult;                  // fitted GP elements
  void gpfInit();
  bool gpfSolve();                     // returns true on convergence; fills gpfResult
  void drawGpFit(); void keyGpFit(char c, bool enter, bool back);
  // Orbit-type animation (SCR_ORBITZOO): an animated Learn explainer cycling orbit
  // archetypes (LEO/MEO/GEO/Molniya/sun-sync/polar). Renders into the existing sprite;
  // the satellite dot advances by true anomaly each frame with a short fixed-size fading
  // trail. NO per-frame allocation -- the trail is a static ring buffer.
  int   ozType = 0;             // current orbit archetype index
  float ozPhase = 0.0f;         // mean-anomaly phase (rad), advanced each frame
  static const int OZ_TRAIL = 24;
  int16_t ozTrailX[OZ_TRAIL] = {0};
  int16_t ozTrailY[OZ_TRAIL] = {0};
  int   ozTrailHead = 0;        // ring buffer write index
  int   ozTrailCount = 0;
  void drawOrbitZoo(); void keyOrbitZoo(char c, bool enter, bool back);
  // Per-form-tool value persistence (FILE_TOOLDEF): remember a tool's field values
  // across sessions so station-specific inputs (coax, power, gains) aren't re-typed.
  void toolDefSave(int id);
  bool toolDefLoad(int id);
  bool tfDefOnly = false;    // when true, toolFormInit skips loading saved values (x-reset)

  // Tools UX state: menu scroll, calculator tape (scrolling history), and the
  // output-area scroll for multi-element antenna forms.
  int toolsScroll = 0;
  static const int CALC_TAPE = 12;
  String calcTape[CALC_TAPE]; int calcTapeN = 0;  // ring of past "expr" / "= result" lines
  int calcScroll = 0;                             // 0 = pinned to newest
  bool calcHintPage2 = false;                     // ' toggles the function-hint page
  bool calcEngNota = false;                        // = toggles engineering-notation output
  int tfOutScroll = 0;                            // form output scroll (yagi/quad element lists)

  // Live-recalc form engine (SCR_TOOLFORM). Each tool is a set of labeled numeric
  // fields plus computed output lines; editing a field recomputes instantly.
  // toolId selects which tool; the form arrays are filled by toolFormInit().
  static const int TF_MAXF = 6;
  int   toolId = 0;                       // which tool (see the TOOL_* ids)
  int   tfSel = 0, tfN = 0;               // selected field, field count
  char  tfLabel[TF_MAXF][18];
  char  tfUnit[TF_MAXF][8];
  double tfVal[TF_MAXF];
  int   tfChoice[TF_MAXF];                // for pick-list fields (-1 = numeric)
  int   tfChoiceN[TF_MAXF];               // number of choices if a pick field
  bool  tfEditing; String tfEditBuf;      // in-place numeric entry
  void  toolFormInit(int id);
  void  drawToolForm(); void keyToolForm(char c, bool enter, bool back);
  const char* tfChoiceLabel(int field, int idx);
  float    emePlanDec[30]; float emePlanDeg[30];
  time_t   emePlanT0 = 0;
  int      emePlanN = 0; int emePlanScroll = 0;

  // One-key QSO capture: a voice memo started from an operating screen also
  // appends a log stub (time/sat/mode/freqs, callsign empty) to fill in later.
  void logCreateStub(const char* memoPath);
  void drawSpaceWx(); void keySpaceWx(char c, bool enter, bool back);
  void drawWeather(); void keyWeather(char c, bool enter, bool back);
  void drawTxDb();    void keyTxDb(char c, bool enter, bool back);
  int  txDbScroll = 0;             // transponder-browser scroll position
  int  txDbSel = 0;               // selected transponder entry (for delete)
  bool txDbDelArm = false;        // two-press delete confirmation (manual TX)
  void drawQrz();     void keyQrz(char c, bool enter, bool back);
  bool qrzLookup(const String& call, String& err);  // returns true on success
  void fillGridsQrz();     // backfill missing QSO grids via QRZ (capped, streamed)
  String qrzSessionKey;            // cached QRZ XML session key (empty = need login)
  String qrzCall;                  // last looked-up callsign
  String qrzName, qrzAddr, qrzGrid, qrzCountry, qrzClass;  // parsed result fields
  bool   qrzHaveResult = false;    // a result is on screen
  int    qrzScroll = 0;            // result scroll (for long bios/addresses)
  // ---- LoTW upload screen (SCR_LOTW) ----
  void drawLotw();    void keyLotw(char c, bool enter, bool back);
  void doLotwUpload(const String& keyPass);  // build .tq8 + POST + mark uploaded
  int  countUnuploadedLotw();                // QSOs in the log still needing LoTW upload
  int  countUnuploadedCloudlog();            // QSOs still needing Cloudlog upload (bit 0x2)
  void scrubLotwBatchState();                // clear the RTC-held batch state + passphrase
  void lotwEnter();                          // count pending QSOs + open screen
  void lotwRebootUpload();                   // write marker + reboot, re-prompt + upload on fresh boot
  void resumeLotwIfPending();                // setup(): if marker set, re-prompt passphrase + upload
  int  lotwParseAccepted(const String& resp, int batchN);  // read accepted count
  int  markLogUploaded(int limit, uint8_t bit = 0x1);  // flag up to 'limit' QSOs sent (bit: 0x1=LoTW, 0x2=Cloudlog)
  int    lotwPending = 0;          // un-uploaded sat QSOs counted on entry
  int    lotwTotal = 0;            // total sat QSOs in the log (for re-send mode)
  int    lotwLastSent = 0;         // signed/accepted from the last attempt
  String lotwStatus;               // last result/error line shown on the screen
  bool   lotwBusy = false;         // an upload is in progress (suppress re-entry)
  bool   lotwResend = false;       // include ALREADY-uploaded QSOs too (opt-in re-upload)
  bool   lotwMoreBatches = false;  // set by a batch that has QSOs left; the top-level caller
                                   // loops on it so batches run iteratively, NOT recursively
                                   // (recursion stacked a full TLS+signing frame per batch
                                   //  and overflowed the loopTask stack on the 3rd batch)
  bool   lotwRebootPrompt = false; // upload hit a -1 connect; asking to reboot-and-retry
  // ---- Unified LoTW location picker (SCR_LOTWSUB) ----
  // One context-aware list picker drives the whole DXCC -> primary -> secondary chain.
  // lotwPickKind selects what's being chosen; the list/codes come from the flash tables
  // in lotw_subdiv.h. A typeahead filter narrows the long lists (340 DXCC, JA cities).
  enum LotwPick { LP_DXCC = 0, LP_PRIMARY = 1, LP_SECONDARY = 2 };
  // Resolve, for the configured DXCC, the primary field name + its choice list, and
  // whether that primary maps to US_STATE (so secondary = county) or JA_PREFECTURE
  // (secondary = city). Returns "" if the entity has no primary subdivision.
  const char* lotwPrimaryField(const char* dxcc, const SubdivEntry** list, int* n);
  // Resolve the secondary field + list, gated by the stored PRIMARY code. Returns "" if
  // the entity/primary has no secondary level.
  const char* lotwSecondaryField(const char* dxcc, const char* primaryCode,
                                 const SubdivEntry** list, int* n);
  void lotwPickEnter(LotwPick kind);   // open the picker in a given context
  void drawLotwSub();   void keyLotwSub(char c, bool enter, bool back);
  LotwPick lotwPickKind = LP_DXCC; // what the picker is currently choosing
  const SubdivEntry* lotwSubList = nullptr;  // active choice table (flash), for PRIMARY/SECONDARY
  int    lotwSubN = 0;             // entries in the active table
  int    lotwSubSel = 0;           // picker cursor (index into the FILTERED view)
  int    lotwSubScroll = 0;        // picker scroll offset
  String lotwSubFieldName;         // header label for the active context
  String lotwPickFilter;           // typeahead filter string (matches name or code)
  // Filtered-view helpers: map a filtered row position to the underlying table index.
  int    lotwFilteredCount();                 // # rows passing the current filter
  int    lotwFilteredIndex(int filteredPos);  // underlying index of the Nth filtered row
  bool   lotwRowMatches(const char* code, const char* name);  // filter predicate
  // ---- Cloudlog/Wavelog upload screen (SCR_CLOUDLOG) ----
  void drawCloudlog();   void keyCloudlog(char c, bool enter, bool back);
  void cloudlogEnter();                     // count pending QSOs + open screen
  void doCloudlogUpload();                  // build ADIF + JSON POST + mark uploaded
  void cloudlogRebootUpload(bool resend = false, int cursor = 0);  // write marker + reboot, then upload on fresh boot
  void resumeCloudlogIfPending();           // setup(): if marker set, upload in a clean boot
  int  clPending = 0;              // QSOs not yet sent to Cloudlog (bit 0x2 unset)
  int  clTotal = 0;                // total sat QSOs in the log (for re-send mode)
  String clStatus;                 // last result/error line shown on the screen
  bool clBusy = false;             // an upload is in progress (suppress re-entry)
  bool clResend = false;           // include QSOs already sent to Cloudlog (opt-in)
  bool clMoreBatches = false;      // set by a batch with QSOs left; the top-level caller loops
                                   // on it so batches run iteratively, not recursively (same
                                   // stack-overflow risk as the LoTW path)
  int  clResendSkip = 0;           // resend batching: QSOs already sent this run (cursor)
  bool clRebootPrompt = false;     // upload hit a -1 connect; asking to reboot-and-retry
  // ---- Upcoming activations feed (SCR_HAMSAT, from hams.at) ----
  struct Activation {
    char date[11];     // YYYY-MM-DD
    char call[14];     // activator callsign (may include /portable)
    char sat[12];      // satellite name
    char grid[10];     // Maidenhead grid
    char start[9];     // HH:MM:SS UTC
    char end[9];       // HH:MM:SS UTC
    char mode[10];     // SSB / FM / CW ...
    char freq[20];     // frequency text (or "" if none)
    char comment[48];  // activator comment (truncated; screen shows ~38 chars)
  };
  static const int HAMSAT_MAX = 20;   // cap kept modest: the array lives in .bss
  Activation hamsatList[HAMSAT_MAX];
  // Parse an activation's freq/comment into a fixed DX downlink/uplink on a
  // matching two-way transponder (declared here, after Activation is defined).
  bool parseActivationFreq(const Activation& a);
  int    hamsatN = 0;              // parsed activations
  int    hamsatSel = 0;           // list cursor
  int    hamsatScroll = 0;        // list scroll offset
  bool   hamsatDetail = false;    // detail view of the selected activation
  // Manual activation/sked entry (user-entered, kept separate from the feed so it
  // survives a refresh; merged into hamsatList[] after each parse/cache load).
  Activation skedDraft;           // the entry being built on SCR_SKEDENTRY
  int    skedField = 0;           // selected field on the entry screen
  int    skedEditIdx = -1;        // editing an existing user sked (index into store) or -1=new
  static const int USERSKED_MAX = 12;
  Activation userSked[USERSKED_MAX];   // persisted manual entries
  int    userSkedN = 0;
  String hamsatStatus;            // status/error line ("" when a list is shown)
  void drawHamsat();   void keyHamsat(char c, bool enter, bool back);
  void fetchHamsat();              // download + parse the feed (WiFi)
  int  parseHamsat(const String& xml);  // fill hamsatList[]; returns count
  void hamsatEnter();              // open screen, fetch if WiFi up
  bool saveHamsatCache();          // persist hamsatList[] to flash (survives reboot)
  int  loadHamsatCache();          // restore hamsatList[] from flash; returns count
  // ---- Notes: text-file browser (SCR_NOTES) + editor (SCR_NOTEEDIT) ----
  static const int NOTES_LIST_MAX = 64;   // files listed in the browser
  static const int NOTE_NAME_MAX  = 32;   // filename length (without path/.txt)
  static const int NOTE_BUF_MAX   = 4096; // max note size held/edited in RAM
  char     noteList[NOTES_LIST_MAX][NOTE_NAME_MAX]; // base names (no dir, no .txt)
  time_t   noteTime[NOTES_LIST_MAX];                 // each note's last-write time
  int      noteListN = 0;          // files found
  int      noteSel = 0;            // browser cursor
  int      noteScroll = 0;         // browser scroll offset
  bool     noteConfirmDel = false; // two-step delete confirmation in the browser
  String   noteBuf;                // editor text buffer (current file contents)
  String   noteName;               // base name of the file being edited
  int      noteCur = 0;            // cursor index into noteBuf (0..length)
  int      noteTopLine = 0;        // first visible wrapped line (editor scroll)
  bool     noteDirty = false;      // unsaved changes
  bool     noteIsNew = false;      // editing a not-yet-saved new file
  void drawNotes();    void keyNotes(char c, bool enter, bool back);
  void drawNoteEdit(); void keyNoteEdit(char c, bool enter, bool back);
  void notesEnter();               // build the list + open the browser
  void buildNoteList();            // (re)enumerate /CardSat/notes/*.txt
  bool noteLoad(const char* base); // read a file into noteBuf
  bool noteSave();                 // write noteBuf to its file
  void noteEditNew();              // start a blank new note
  void noteEditOpen(const char* base); // open an existing note in the editor
  void buildGrids(time_t a, time_t b);
  // Shared footprint-membership test (used by the workable-horizon sweep and target search):
  // true if the point (lat,lon) lies within the ground footprint of a satellite whose sub-point
  // is (subLat,subLon) at altitude altKm. Same spherical-cap geometry the addFootprint* fills use.
  static bool pointInFootprint(double lat, double lon, double subLat, double subLon, double altKm);
  // Per-sample bbox candidate scratch for the footprint fills (speeds up the union sweep + rove
  // planner; results are identical -- only entities whose bbox can't overlap the footprint are
  // skipped). Sized to the polygon-entity counts (161 DXCC polygons, 51 states).
  static const int DXCC_CAND_MAX = 161;
  static const int STATE_CAND_MAX = 51;
  int16_t  dxccCand[DXCC_CAND_MAX];
  int16_t  stateCand[STATE_CAND_MAX];
  int  countWorkableGrids(SatEntry& s, time_t a, time_t b);    // rove planner (explicit sat)
  void addFootprintGrids(double subLat, double subLon, double altKm);
  void drawGrid();    void keyGrid(char c, bool enter, bool back);
  void buildStates(time_t a, time_t b);
  int  countWorkableStates(SatEntry& s, time_t a, time_t b);   // rove planner (explicit sat)
  void addFootprintStates(double subLat, double subLon, double altKm);
  void drawStates();  void keyStates(char c, bool enter, bool back);
  void buildDxcc(time_t a, time_t b);
  int  countWorkableDxcc(SatEntry& s, time_t a, time_t b);     // rove planner (explicit sat)
  void addFootprintDxcc(double subLat, double subLon, double altKm);
  // Combined states+DXCC footprint fill: one shared mesh walk instead of two (the workable-horizon
  // sweep's hot path). Byte-identical to calling addFootprintStates then addFootprintDxcc.
  void addFootprintStatesDxcc(double subLat, double subLon, double altKm);
  void drawDxcc();    void keyDxcc(char c, bool enter, bool back);
  void drawGpSrc();   void keyGpSrc(char c, bool enter, bool back);
  void drawWorldMap(); void keyWorldMap(char c, bool enter, bool back);
  void drawRotMan(); void keyRotMan(char c, bool enter, bool back);
  void drawGps(); void keyGps(char c, bool enter, bool back);
  void drawHelp();
  void drawGlossary();  void keyGlossary(char c, bool enter, bool back);
  void drawUserGuide(); void keyUserGuide(char c, bool enter, bool back);
  void drawLicense();   void keyLicense(char c, bool enter, bool back);
  void drawSatHistory();void keySatHistory(char c, bool enter, bool back);
  void drawTechHelp();  void keyTechHelp(char c, bool enter, bool back);
  void drawLearn();     void keyLearn(char c, bool enter, bool back);
  void drawArrow();     void keyArrow(char c, bool enter, bool back);
  void drawOverhead();  void keyOverhead(char c, bool enter, bool back);
  void scanOverhead();  // synchronous all-DB above-horizon snapshot
  void drawGame();      void keyGame(char c, bool enter, bool back);  // "Zap the Sats"
  void gameReset(bool full);   // (re)start: full=new game, else next wave
  void gameStep();             // advance one formation step + shots

  // Games menu + the five mini-games. Each is draw + key + a reset; all fixed-size.
  void drawGamesMenu(); void keyGamesMenu(char c, bool enter, bool back);
  void beep(uint16_t freq, uint16_t ms);       // on-demand speaker beep (acquires + schedules release)
  void sfx(uint16_t freq, uint16_t ms);        // gated game sound (cfg.gameSound)
  // On-demand speaker power. The M5 Speaker holds ~8 KB of I2S/DMA buffers while enabled; keeping
  // it OFF except when audio is actually needed frees that block for TLS handshakes (LoTW uploads
  // fail when the largest contiguous block gets too small). acquire() brings it up (idempotent);
  // releaseAfter() schedules an end() once the deadline passes (so an alarm's tail still plays and
  // we don't thrash begin/end -- each cycle pops). serviceAudioRelease() runs the deferred end().
  void audioAcquire();                         // ensure speaker is up (begin + volume), cancel pending release
  void audioReleaseAfter(uint32_t ms);         // schedule speaker end() after ms of no audio
  void serviceAudioRelease();                  // loop hook: perform a due deferred end()
  // Serial command console (USB). Polled once per loop; accepts a short line and prints
  // status (heap, version, satellite count, next pass, ...). Zero heap cost: the input line
  // lives in a fixed buffer and nothing is dynamically allocated. It never changes DEVICE
  // state -- the `print` commands only transmit a report to the configured TCP:9100 receipt
  // printer -- so it's safe to leave always-on.
  void serviceSerialCli();
  void runSerialCommand(const char* cmd);      // dispatch one completed command line
  char     cliBuf[64] = {0};                   // serial input line accumulator
  uint8_t  cliLen = 0;
  // ---- Receipt printing (TCP:9100 ESC/POS; see print.h + docs/design/PRINTING_SCOPE.md) ----
  // Each report opens a Printer job, streams 32-col text, and closes -- no big buffer, transient
  // socket only. printReport() is the shared entry (opens/closes + error status); the per-report
  // builders assume an open job. Returns false (with setStatus) if the printer can't be reached.
  enum PrintReport { PR_PASSES, PR_ROVE, PR_TICKET, PR_HORIZON, PR_SATCARD, PR_LOG, PR_KEPS,
                     PR_AMSAT, PR_OPCARD, PR_MUTUAL, PR_DXDOPP, PR_EQX, PR_ALLPASS, PR_TARGET, PR_NOTE, PR_PASSPOLAR };
  static const char* prtStem(PrintReport w);   // /CardSat/Reports filename stem per report
  bool printReport(PrintReport which);
  void printPasses();        // today's favorites day-sheet
  void printTicket();        // outreach pass ticket for the active satellite
  void printSatCard();       // active satellite: transponders + next passes
  void printKeps();          // active satellite: Keplerian elements (nostalgia)
  void printLog();           // recent QSOs (paper backup)
  void printAmsatPitch();    // "support AMSAT" outreach page (About)
  void printOpCard();        // operator contact card + ham/satellite explainer (About)
  void printMutual();        // mutual-window (co-visibility) table + sky map
  void printDxDopp();        // DX Doppler RX/TX table for the selected window
  void printEqx();           // equator-crossing / descending-node table
  void printAllPasses();     // every favorite's upcoming passes (the schedule)
  void printTargetHits();    // target-search results
  void printNote();          // the note currently open in the editor/viewer
  void printPassPolar();     // ASCII polar sky sheet for the pass being viewed (pdAz/pdEl)
  void printPolarAscii(const float* az, const float* el, int n, const char* mark);
  bool printActiveHint();    // shared helper: "no active satellite" line, returns false if none
  int  buildFavPasses(time_t from, time_t to, uint32_t* norads, time_t* aoss,
                      uint8_t* els, uint16_t* azs, uint16_t* durs, int maxN);  // shared pass gather
  bool     audioUp = false;                    // is the speaker currently begun?
  uint32_t audioReleaseAt = 0;                 // millis() deadline for a pending end() (0 = none)
  bool     audioGameOwned = false;             // audio was acquired for a game session (release on exit)
  bool gameTiltAxis(float& outLR);             // tilt left/right in [-1,1]; false if unavailable
  void drawGDoppler();  void keyGDoppler(char c, bool enter, bool back);  void gDopplerReset();
  void drawGPass();     void keyGPass(char c, bool enter, bool back);     void gPassReset();
  void drawGRotor();    void keyGRotor(char c, bool enter, bool back);    void gRotorReset();
  void drawGMorse();    void keyGMorse(char c, bool enter, bool back);    void gMorseReset();
  void drawGGrid();     void keyGGrid(char c, bool enter, bool back);     void gGridReset();
  float gdlLevelRate();        // Doppler Lock: level-scaled drift rate
  void  ggNewRound();          // Grid Chase: build a fresh question
  void keyHelp(char c, bool enter, bool back);

  // CAT self-test: run the full sequence, render results, handle scrolling.
  void runCatTest();
  void drawCatTest();
  void keyCatTest(char c, bool enter, bool back);
  void enterCatMon();                       // open the monitor, claim the trace sink
  void drawCatMon();
  void keyCatMon(char c, bool enter, bool back);
  void catMonPush(const char* dir, const uint8_t* b, size_t n);  // append a trace line
  void catMonSendHex(const String& hex);    // parse "FE FE 4C..." and transmit

  // Charge / Sleep screen: a minimal low-power mode (Launcher-style). The screen
  // backlight blanks and only a tiny loop runs; any key wakes it to show battery
  // status; back/ESC returns to the home menu. chargeWoke gates the wake redraw.
  void enterChargeMode();
  void drawCharge();
  void keyCharge(char c, bool enter, bool back);
  int  batteryPercent();        // voltage-curve % (more accurate than raw level)
  bool     chargeWoke = false;  // true briefly after a keypress wakes the screen
  uint32_t chargeWokeMs = 0;    // when the wake happened (auto-blank after a few s)
  // Append one result line: echo to Serial and store for the on-screen list.
  void catLog(const String& line);
  void catStep(const String& name, bool ok, const String& detail = String());
  void keyLocation(char c, bool enter, bool back);
  void keyUpdate(char c, bool enter, bool back);
  void keySettings(char c, bool enter, bool back);
  void startWifiScan(bool forSecond = false);   // scan APs; forSecond -> store into WiFi 2
  bool     wifiScan2 = false;      // true while the scan targets the WiFi-2 slot
  void keyWifiScan(char c, bool enter, bool back);
  void keyAbout(char c, bool enter, bool back);
  void drawNetReboot(); void keyNetReboot(char c, bool enter, bool back);
  void keyLog(char c, bool enter, bool back);
  void keyLogEntry(char c, bool enter, bool back);
  void beginQso();                // snapshot auto fields, open the entry screen
  void seedQsoSatDefaults();      // fill qso sat/mode + non-Doppler centre/nominal freqs
  bool saveQso();                 // append the pending QSO to the CSV log
  int  qsoCount();                // number of logged QSOs
  bool exportAdif();              // write ADIF from the CSV log
  void beginAdifExport();         // resolve LoTW names (prompt for misses), then export
  void promptNextLotw();          // prompt for the next unmapped sat's LoTW SAT_NAME
  void keyLogList(char c, bool enter, bool back);
  void loadLog();                 // load recent entries into logRecs[] (newest-first)
  int  logFileRow(int recIdx) const;  // map a logRecs[] index back to its on-disk row
  bool rewriteLog(int fileIdx, const PendingQso* rec, bool del);  // edit/delete a row
  void keyEdit(char c, bool enter, bool back);

  // ---- small draw utilities ----
  void header(const String& t);
  void footer(const String& t);
  bool drawOobBanner();           // flashing out-of-passband warning (returns true while showing)
};
