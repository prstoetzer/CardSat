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

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_SATLIST, SCR_SCHEDULE, SCR_PASSES, SCR_PASSDETAIL,
  SCR_TRACK, SCR_POLAR, SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT,
  SCR_PASSPOLAR, SCR_MUTUAL, SCR_WIFISCAN, SCR_ABOUT, SCR_LOG, SCR_LOGENTRY,
  SCR_LOGLIST, SCR_VIS, SCR_ILLUM, SCR_WORLDMAP, SCR_ROTMAN, SCR_GPS, SCR_HELP, SCR_ORBIT, SCR_SIM,
  SCR_SUNMOON, SCR_GRID, SCR_GPSRC, SCR_MANUAL, SCR_STATES, SCR_DXCC, SCR_SPACEWX, SCR_TXDB, SCR_QRZ, SCR_WEATHER, SCR_EQX, SCR_BIG, SCR_MANUALBIG, SCR_NETREBOOT
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
  int      view[MAX_SATS];        // db indices currently shown
  int      viewN = 0;
  int      viewSel = 0;           // cursor into view[]

  // manual-entry scratch (GP elements + transponder fields)
  SatEntry mtSat;                 // manual GP-entry accumulator
  uint32_t mtxDl = 0, mtxUl = 0, mtxDlHigh = 0, mtxUlHigh = 0;
  bool     mtxInv = false;
  PassPredict passes[PASS_LIST_LEN];
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
  Screen   visReturn = SCR_PASSES; // screen to return to from vis/illum (Satellites or Passes)
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
  TuneMode tuneMode = TM_HOLD;    // Doppler tune mode (cycle with 'd' on Track)
                                  // holds a constant frequency AT THE SATELLITE
  uint32_t lastRxSet = 0;         // downlink dial the rig is on (read-back): knob detect + send guard
  uint32_t lastUlHz  = 0;         // last uplink dial commanded (send guard)
  static constexpr uint32_t FREQ_GUARD_HZ = 2;   // skip a re-send within this many Hz of the last value
  static constexpr uint32_t KNOB_MOVE_HZ  = 5;   // read-vs-last delta that counts as an operator knob move
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
  static int  s_fetchDepth;                    // >0 while any outbound fetch is active
  static bool netFetchActive();                // service*() skip rebuild while true
  void webdHandleRequest(const String& reqLine);  // route one HTTP request
  void webdSendStatusJson();                   // GET /api/status
  void webdSendSatsJson();                     // GET /api/sats
  void webdSendPassesJson();                   // GET /api/passes
  void webdSendOrbitJson();                     // GET /api/orbit (orbital analysis)
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
  bool dlOnSub() const { return cfg.vfoType == VFO_MAIN_UP_SUB_DOWN; }
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
  void driveDownlink(uint32_t rx, bool readback, uint32_t threshHz = FREQ_GUARD_HZ) {
    uint32_t d = (rx > lastRxSet) ? rx - lastRxSet : lastRxSet - rx;
    if (lastRxSet && d < threshHz) return;               // within deadband: already there
    if (!rigSetDownlinkFreq(rx)) return;
    uint32_t back;
    lastRxSet = (readback && rig->canReadFreq() && rigReadDownlinkFreq(back)) ? back : rx;
  }
  // Drive the uplink dial only when it moved; then leave the active band on the
  // downlink so the operator's knob/read stays on RX. No read-back (the uplink
  // knob is never followed).
  void driveUplink(uint32_t tx, uint32_t threshHz = FREQ_GUARD_HZ) {
    uint32_t d = (tx > lastUlHz) ? tx - lastUlHz : lastUlHz - tx;
    if (lastUlHz && d < threshHz) return;
    if (rigSetUplinkFreq(tx)) { lastUlHz = tx; rigSelectDownlink(); }
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
  void loadCalForSat(uint32_t norad);      // per-satellite calibration -> calDl/calUl
  void saveCalForSat(uint32_t norad);      // persist current calDl/calUl for this sat
  void loadFavs();
  void saveFavs();
  bool isFav(uint32_t norad) const;
  void toggleFav(uint32_t norad);
  void buildSatView();                     // (re)build the filtered satellite list
  void buildSchedule();                    // next pass for every favorite, sorted
  void buildPassDetail(const PassPredict& p);  // sample one pass into pdEl/pdAz/pdSunlit
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

  // ---- per-screen input ----
  void keyHome(char c, bool enter, bool back);
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
