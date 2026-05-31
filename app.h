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
#include "civ.h"

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_SATLIST, SCR_SCHEDULE, SCR_PASSES, SCR_PASSDETAIL,
  SCR_TRACK, SCR_POLAR, SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT
};

// One upcoming (or in-progress) pass for a favorite, used by the schedule view.
struct SchedEntry {
  uint32_t norad = 0;
  char     name[26] = {0};
  time_t   aos = 0, los = 0;
  float    maxEl = 0;
  bool     inProgress = false;
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
  CivRadio  rig;

  // UI state
  Screen   screen = SCR_HOME;
  int      homeSel = 0;
  int      satSel = 0, satScroll = 0;
  int      passN = 0;

  // favorites + filtered satellite view
  uint32_t favs[MAX_SATS];
  int      favN = 0;
  bool     favOnly = false;
  int      view[MAX_SATS];        // db indices currently shown
  int      viewN = 0;
  int      viewSel = 0;           // cursor into view[]

  // manual-entry scratch (TLE name/line1, transponder fields)
  String   mtName, mtL1;
  uint32_t mtxDl = 0, mtxUl = 0, mtxDlHigh = 0, mtxUlHigh = 0;
  bool     mtxInv = false;
  PassPredict passes[PASS_LIST_LEN];
  int      passSel = 0;          // cursor into the passes list

  // pass-detail plot (Feature 7): cached elevation curve over one pass
  float    pdEl[PD_SAMPLES];
  bool     pdSunlit[PD_SAMPLES];
  PassPredict pdPass;
  bool     pdValid = false;

  // all-favorites schedule + AOS alarm
  SchedEntry sched[SCHED_MAX];
  int        schedN = 0, schedSel = 0;
  uint32_t   lastSchedMs = 0;          // throttle background rebuilds
  time_t     nextAos = 0;              // soonest upcoming favorite AOS (alarm)
  char       nextAosName[26] = {0};
  time_t     alarmAos = 0;             // AOS we're currently counting down to
  uint8_t    alarmMarks = 0;           // bitmask of countdown beeps already fired
  uint32_t   aosFlashUntil = 0;        // screen-flash overlay end (millis)
  char       aosFlashName[26] = {0};
  int      curTx = 0;             // selected transponder index for active sat
  Transponder activeTx[MAX_TX_PER_SAT];
  int      activeTxCount = 0;     // transponders loaded for the active sat
  int      setSel = 0;            // settings menu cursor

  // tracking / doppler
  bool     radioOut = false;      // are we sending freqs to the rig?
  int32_t  calDl = 0, calUl = 0;  // working calibration (Hz), seeded from cfg
  int32_t  calStep = 10;          // Hz per calibration nudge
  int32_t  pbOffset = 0;          // passband tune offset (Hz up from dl bottom)
  int32_t  tuneStep = 1000;       // Hz per passband-tune nudge
  uint8_t  trackMode = 0;         // 0 = TUNE (passband), 1 = CAL (calibration)
  bool     radioTune = false;     // One True Rule: tune the rig knob, software
                                  // holds a constant frequency AT THE SATELLITE
  uint32_t lastRxSet = 0;         // last downlink Hz we wrote (detect knob moves)
  uint32_t lastDoppMs = 0;
  uint32_t lastDrawMs = 0;

  // text editor
  String   editBuf;
  String   editTitle;
  int      editTarget = 0;        // which field is being edited

  // status line
  String   status;
  uint32_t statusUntil = 0;

  // ---- helpers ----
  void applyRadioFromCfg();
  void applyTransponderModes(const Transponder& t);  // per-leg SSB/FM mode policy
  void startGps();                         // (re)open GPS per cfg.gpsSource
  void factoryReset();                     // wipe LittleFS + reboot to defaults
  void setStatus(const String& s, uint32_t ms = 2500);
  time_t nowUtc();
  SatEntry* activeSat();
  bool ensureTransponders(SatEntry& s);   // load (cache or net)
  void onTransponderChanged();             // recenter passband + pick default mode
  void doUpdateKeps();
  void doCacheAllTransponders();           // fetch+cache every sat's TX (offline prep)
  void loadCalForSat(uint32_t norad);      // per-satellite calibration -> calDl/calUl
  void saveCalForSat(uint32_t norad);      // persist current calDl/calUl for this sat
  void loadFavs();
  void saveFavs();
  bool isFav(uint32_t norad) const;
  void toggleFav(uint32_t norad);
  void buildSatView();                     // (re)build the filtered satellite list
  void buildSchedule();                    // next pass for every favorite, sorted
  void buildPassDetail(const PassPredict& p);  // sample one pass into pdEl/pdSunlit
  void refreshScheduleIfNeeded();          // rebuild in the background when due
  void serviceAosAlarm();                  // countdown beeps + flash before AOS
  void sleepUntilNextPass();               // deep-sleep until ~60 s before AOS
  void saveManualTx(uint32_t norad, const Transponder& t);
  int  loadManualTx(uint32_t norad, Transponder* out, int maxN);

  // ---- input ----
  void handleKey(char c, bool enter, bool back);

  // ---- per-screen render ----
  void draw();
  void drawHome();
  void drawSatList();
  void drawSchedule();
  void drawPasses();
  void drawPassDetail();
  void drawTrack();
  void drawPolar();
  void drawLocation();
  void drawUpdate();
  void drawSettings();
  void drawEdit();

  // ---- per-screen input ----
  void keyHome(char c, bool enter, bool back);
  void keySatList(char c, bool enter, bool back);
  void keySchedule(char c, bool enter, bool back);
  void keyPasses(char c, bool enter, bool back);
  void keyPassDetail(char c, bool enter, bool back);
  void keyTrack(char c, bool enter, bool back);
  void keyPolar(char c, bool enter, bool back);
  void keyLocation(char c, bool enter, bool back);
  void keyUpdate(char c, bool enter, bool back);
  void keySettings(char c, bool enter, bool back);
  void keyEdit(char c, bool enter, bool back);

  // ---- small draw utilities ----
  void header(const String& t);
  void footer(const String& t);
};
