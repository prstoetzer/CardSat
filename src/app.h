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

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_SATLIST, SCR_SCHEDULE, SCR_PASSES, SCR_PASSDETAIL,
  SCR_TRACK, SCR_POLAR, SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT,
  SCR_PASSPOLAR, SCR_MUTUAL, SCR_WIFISCAN, SCR_ABOUT, SCR_LOG, SCR_LOGENTRY,
  SCR_LOGLIST, SCR_VIS, SCR_ILLUM, SCR_WORLDMAP, SCR_ROTMAN, SCR_GPS, SCR_HELP, SCR_ORBIT, SCR_SIM,
  SCR_SUNMOON, SCR_GRID, SCR_GPSRC, SCR_MANUAL, SCR_STATES, SCR_DXCC, SCR_SPACEWX, SCR_TXDB, SCR_QRZ, SCR_WEATHER, SCR_EQX, SCR_BIG, SCR_MANUALBIG, SCR_NETREBOOT, SCR_MEMOS, SCR_OSCAR, SCR_GLOBE, SCR_DXDOPP, SCR_SKYMAP, SCR_GPSPOS, SCR_SATSAT, SCR_MESSAGES, SCR_CATTEST, SCR_CHARGE, SCR_CATMON, SCR_TRANSIT, SCR_VISLIST, SCR_LOTW, SCR_HAMSAT, SCR_NOTES, SCR_NOTEEDIT, SCR_CLOUDLOG, SCR_LOTWSUB
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

class App {
public:
  void setup();
  void loop();

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
  void drawDxDopp();
  void keyDxDopp(char c, bool enter, bool back);
  void dxdCenterPassband();          // centre dxdPbOff on the selected linear transponder
  void dxdStepAnchorDial(int dir);   // step the anchored dial by 1 kHz (fixed modes)
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
  void drawMessages();
  void keyMessages(char c, bool enter, bool back);
  void buildOscarArc();              // sample the current/next pass ground track
  // Sun / Moon tracking screen (off main menu)
  bool     smOut = false;           // rotator engaged on the Sun/Moon screen
  int      smSel = 0;               // 0 = Sun, 1 = Moon
  bool     smGraphic = true;        // Sun/Moon screen: graphic sky-dome vs data list
  // Workable grid squares (4-char Maidenhead) under the footprint
  uint8_t  gridBits[4050];          // 1 bit per 4-char grid (32400 total, ~4 KB)
  int      gridN = 0;               // grids in footprint (set bits)
  int      gridScroll = 0;
  bool     gridLive = false;        // true = live now (from Track), false = pass union
  uint32_t gridBuiltMs = 0;         // last live rebuild (millis); 0 = build now

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
  time_t     nextAos = 0;              // soonest upcoming favorite AOS (alarm)
  char       nextAosName[26] = {0};
  time_t     alarmAos = 0;             // AOS we're currently counting down to
  uint8_t    alarmMarks = 0;           // bitmask of countdown beeps already fired
  time_t     alarmTca = 0;             // TCA of the pass in progress (0 = none)
  time_t     alarmLos = 0;             // LOS of the pass in progress (0 = none)
  uint8_t    alarmPassMarks = 0;       // TCA/LOS beeps already fired this pass
  uint32_t   aosFlashUntil = 0;        // screen-flash overlay end (millis)
  char       aosFlashName[26] = {0};
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
  Screen   trackReturn = SCR_PASSES; // where the Track screen's back key returns to
  Screen   passesReturn = SCR_SATLIST; // where the Passes screen's back key returns to
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
  void freeCanvasForTls();                     // free ~64 KB sprite for mbedTLS handshake
  void restoreCanvasAfterTls();
  void tickCanvasRestore();                    // loop-driven retry if restore alloc failed
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
  void computeMutual(const String& grid);      // co-visibility windows vs a DX grid
  void drawPolarGrid(int cx, int cy, int R);   // shared polar rings + cardinal cross
  void drawPolarArc(int cx, int cy, int R, const float* az, const float* el, int n);
  void refreshScheduleIfNeeded();          // rebuild in the background when due
  void serviceAosAlarm();                  // countdown beeps + flash before AOS
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
  void drawSpaceWx(); void keySpaceWx(char c, bool enter, bool back);
  void drawWeather(); void keyWeather(char c, bool enter, bool back);
  void drawTxDb();    void keyTxDb(char c, bool enter, bool back);
  int  txDbScroll = 0;             // transponder-browser scroll position
  int  txDbSel = 0;               // selected transponder entry (for delete)
  bool txDbDelArm = false;        // two-press delete confirmation (manual TX)
  void drawQrz();     void keyQrz(char c, bool enter, bool back);
  bool qrzLookup(const String& call, String& err);  // returns true on success
  String qrzSessionKey;            // cached QRZ XML session key (empty = need login)
  String qrzCall;                  // last looked-up callsign
  String qrzName, qrzAddr, qrzGrid, qrzCountry, qrzClass;  // parsed result fields
  bool   qrzHaveResult = false;    // a result is on screen
  int    qrzScroll = 0;            // result scroll (for long bios/addresses)
  // ---- LoTW upload screen (SCR_LOTW) ----
  void drawLotw();    void keyLotw(char c, bool enter, bool back);
  void doLotwUpload(const String& keyPass);  // build .tq8 + POST + mark uploaded
  void lotwEnter();                          // count pending QSOs + open screen
  int  lotwParseAccepted(const String& resp, int batchN);  // read accepted count
  int  markLogUploaded(int limit, uint8_t bit = 0x1);  // flag up to 'limit' QSOs sent (bit: 0x1=LoTW, 0x2=Cloudlog)
  int    lotwPending = 0;          // un-uploaded sat QSOs counted on entry
  int    lotwTotal = 0;            // total sat QSOs in the log (for re-send mode)
  int    lotwLastSent = 0;         // signed/accepted from the last attempt
  String lotwStatus;               // last result/error line shown on the screen
  bool   lotwBusy = false;         // an upload is in progress (suppress re-entry)
  bool   lotwResend = false;       // include ALREADY-uploaded QSOs too (opt-in re-upload)
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
  void cloudlogRebootUpload();              // write marker + reboot, then upload on fresh boot
  void resumeCloudlogIfPending();           // setup(): if marker set, upload in a clean boot
  int  clPending = 0;              // QSOs not yet sent to Cloudlog (bit 0x2 unset)
  int  clTotal = 0;                // total sat QSOs in the log (for re-send mode)
  String clStatus;                 // last result/error line shown on the screen
  bool clBusy = false;             // an upload is in progress (suppress re-entry)
  bool clResend = false;           // include QSOs already sent to Cloudlog (opt-in)
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
  int    hamsatN = 0;              // parsed activations
  int    hamsatSel = 0;           // list cursor
  int    hamsatScroll = 0;        // list scroll offset
  bool   hamsatDetail = false;    // detail view of the selected activation
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
  void addFootprintGrids(double subLat, double subLon, double altKm);
  void drawGrid();    void keyGrid(char c, bool enter, bool back);
  void buildStates(time_t a, time_t b);
  void addFootprintStates(double subLat, double subLon, double altKm);
  void drawStates();  void keyStates(char c, bool enter, bool back);
  void buildDxcc(time_t a, time_t b);
  void addFootprintDxcc(double subLat, double subLon, double altKm);
  void drawDxcc();    void keyDxcc(char c, bool enter, bool back);
  void drawGpSrc();   void keyGpSrc(char c, bool enter, bool back);
  void drawWorldMap(); void keyWorldMap(char c, bool enter, bool back);
  void drawRotMan(); void keyRotMan(char c, bool enter, bool back);
  void drawGps(); void keyGps(char c, bool enter, bool back);
  void drawHelp(); void keyHelp(char c, bool enter, bool back);

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
  void heapDefragViaReconnect();// drop WiFi/TLS to coalesce the heap on demand
  bool     chargeWoke = false;  // true briefly after a keypress wakes the screen
  uint32_t chargeWokeMs = 0;    // when the wake happened (auto-blank after a few s)
  // Append one result line: echo to Serial and store for the on-screen list.
  void catLog(const String& line);
  void catStep(const String& name, bool ok, const String& detail = String());
  void keyLocation(char c, bool enter, bool back);
  void keyUpdate(char c, bool enter, bool back);
  void keySettings(char c, bool enter, bool back);
  void startWifiScan();
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
  void loadLog();                 // load recent entries into logRecs[]
  bool rewriteLog(int fileIdx, const PendingQso* rec, bool del);  // edit/delete a row
  void keyEdit(char c, bool enter, bool back);

  // ---- small draw utilities ----
  void header(const String& t);
  void footer(const String& t);
};
