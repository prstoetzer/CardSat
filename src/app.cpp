// ===========================================================================
//  app.cpp  -  UI state machine, rendering, and real-time Doppler control
// ===========================================================================
#include "app.h"
#include "config.h"
#include <M5Cardputer.h>
#include <LittleFS.h>
#include "storage.h"
#include <time.h>
#include <sys/time.h>
#include <math.h>
#include <esp_sleep.h>   // deep-sleep-until-next-pass

// 16-bit 565 colours
static const uint16_t CL_BLACK=0x0000, CL_WHITE=0xFFFF, CL_GREEN=0x07E0, CL_RED=0xF800,
                      CL_YELLOW=0xFFE0, CL_CYAN=0x07FF, CL_ORANGE=0xFD20, CL_GREY=0x7BEF,
                      CL_BLUE=0x041F, CL_DGREEN=0x0320;

static M5Canvas canvas(&M5Cardputer.Display);

// --- small time helpers ----------------------------------------------------
static bool timeIsSet() { time_t t = time(nullptr); return t > 1700000000; }

static String fmtHM(time_t t) {
  struct tm tmv; gmtime_r(&t, &tmv);
  char b[16]; snprintf(b, sizeof(b), "%02d:%02d", tmv.tm_hour, tmv.tm_min);
  return String(b);
}
static String fmtMDHM(time_t t) {
  struct tm tmv; gmtime_r(&t, &tmv);
  char b[20]; snprintf(b, sizeof(b), "%02d/%02d %02d:%02d",
                       tmv.tm_mon+1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
  return String(b);
}
static String fmtClock(time_t t) {
  struct tm tmv; gmtime_r(&t, &tmv);
  char b[16]; snprintf(b, sizeof(b), "%02d:%02d:%02d",
                       tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  return String(b);
}
static String fmtMHz(uint32_t hz) {
  char b[20]; snprintf(b, sizeof(b), "%.5f", hz / 1e6); return String(b);
}

// Compact countdown: "45s", "12m", "1h05".
static String fmtCountdown(long s) {
  if (s < 0) s = 0;
  char b[12];
  if (s < 60)        snprintf(b, sizeof(b), "%lds", s);
  else if (s < 3600) snprintf(b, sizeof(b), "%ldm", s / 60);
  else               snprintf(b, sizeof(b), "%ldh%02ld", s / 3600, (s % 3600) / 60);
  return String(b);
}

// --- GP element-set age (staleness) ---------------------------------------
// The epoch is stored directly as Unix seconds when the GP record is parsed.
static double gpAgeDays(const SatEntry& s) {
  if (!timeIsSet() || s.epochUnix <= 0) return -1;
  return ((double)time(nullptr) - s.epochUnix) / 86400.0;
}
static uint16_t ageColor(double d) {
  if (d < 0)  return CL_GREY;       // unknown / clock not set
  if (d < 14) return CL_GREEN;      // fresh
  if (d < 28) return CL_YELLOW;     // getting old
  return CL_RED;                    // stale -- predictions will drift
}

// --- speaker beep (AOS alarm) ---------------------------------------------
static void beep(uint16_t freq, uint16_t ms) {
  M5Cardputer.Speaker.tone(freq, ms);
}

// ===========================================================================
//  Setup
// ===========================================================================
void App::setup() {
  auto m5cfg = M5.config();
  M5Cardputer.begin(m5cfg, true);   // true => init keyboard
  Serial.begin(115200);             // diagnostics on the USB serial monitor
  M5Cardputer.Display.setRotation(1);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextWrap(false);

  setenv("TZ", "UTC0", 1); tzset();   // work entirely in UTC

  db.begin();
  if (!Store::ready())
    setStatus("No filesystem! Allocate SPIFFS or insert SD.", 8000);
  else if (Store::onSD())
    setStatus("Using SD card for storage", 4000);
  if (!cfg.load()) { cfg.save(); }     // first boot: write defaults
  calDl = cfg.calDlHz; calUl = cfg.calUlHz;

  applyRadioFromCfg();
  applyRotatorFromCfg();

  // Location
  if (cfg.useGps) startGps();
  if (cfg.lat != 0.0 || cfg.lon != 0.0) loc.setManual(cfg.lat, cfg.lon, cfg.altM);
  pred.setSite(loc.obs());

  // Try cached GP data so the unit is useful offline at boot.
  if (db.loadGpFromFs()) setStatus("Loaded cached GP: " + String(db.count()));
  else setStatus("No GP data yet. Use Update.");
  db.loadManualGpFile();    // merge any hand-entered satellites
  loadFavs();
  buildSatView();

  M5Cardputer.Speaker.setVolume(180);   // AOS alarm
  if (timeIsSet() && favN) buildSchedule();

  // If we woke from the deep-sleep-until-pass timer, jump to the schedule so
  // the imminent pass is front and centre (the AOS alarm will sound shortly).
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    if (favN && timeIsSet()) { buildSchedule(); schedSel = 0; screen = SCR_SCHEDULE; }
  }

  draw();
}

void App::applyRadioFromCfg() {
  RadioModel m = (RadioModel)cfg.radioModel;
  uint32_t baud = cfg.civBaud ? cfg.civBaud : RADIOS[m].defaultBaud;
  if (rig) { delete rig; rig = nullptr; }
  rig = makeRig(m);                                   // Icom / Yaesu / Kenwood
  if (!rig) return;
  rig->begin(baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN);
  if (RADIOS[m].proto == PROTO_CIV)
    rig->setAddress(cfg.civAddr ? cfg.civAddr : RADIOS[m].civAddr);
  // Icom: this software drives MAIN/SUB directly, so force the rig's own sat
  // mode OFF (it would invert the band roles). Yaesu/Kenwood: this is a no-op --
  // their full-duplex/sat mode is set up by the operator and left untouched.
  rig->enableSatMode(false);
}

void App::applyRotatorFromCfg() {
  if (rot) { delete rot; rot = nullptr; }
  rotOut = false; rotParked = false;
  lastAzCmd = lastElCmd = -999.0f;
  if (!cfg.rotEnable) return;          // rotator disabled in Settings
  rot = makeRotator(cfg.rotBaud);      // GS-232 over the I2C->UART bridge
  if (rot) rot->begin();
}

// Choose the SSB/FM mode for each leg.
//   Linear transponders default to USB downlink + LSB uplink: an inverting
//   linear transponder flips the spectrum, so an LSB uplink is heard as a
//   normal USB downlink. Exception: if either leg is HF (< 30 MHz) the
//   convention is USB up and USB down. FM / single-channel birds use the
//   transponder's own mode on both legs.
void App::applyTransponderModes(const Transponder& t) {
  if (!rig || !rig->ready()) return;
  if (t.isLinear) {
    bool hf = (t.uplink   && t.uplink   < 30000000UL) ||
              (t.downlink && t.downlink < 30000000UL);
    rig->setSubMode(RM_USB);                                  // downlink: USB
    if (t.uplink) rig->setMainMode(hf ? RM_USB : RM_LSB);     // uplink: LSB (USB if HF)
  } else {
    RigMode m = Rig::modeFromString(t.mode);
    rig->setSubMode(m);
    if (t.uplink) rig->setMainMode(m);
  }
}

// The PL/CTCSS tone wanted for the current transponder: only FM uplinks carry
// one (0 = none). Comes from the SatNOGS/manual transponder, tagged in
// ensureTransponders() from the built-in known-FM-bird table.
float App::desiredToneHz() const {
  if (activeTxCount <= 0) return 0.0f;
  const Transponder& t = activeTx[curTx];
  if (!t.uplink || Rig::modeFromString(t.mode) != RM_FM) return 0.0f;
  return t.toneHz;
}

// Push the CTCSS encoder state to the radio, but only when it actually changes
// (cheap to call every Doppler tick). Self-corrects across sat/transponder
// changes and radio on/off (toneApplied is reset to the -2 sentinel on OFF).
void App::applyCtcssForCurrentTx() {
  if (!rig || !rig->ready() || !rig->hasTone()) return;
  float want = desiredToneHz();
  if (want == toneApplied) return;
  rig->setCtcss(want > 0, want);
  toneApplied = want;
  if (want > 0) setStatus("PL " + String(want, 1) + " Hz on uplink", 2500);
}

// GPS source profiles: { display name, UART, RX pin, TX pin, baud }. All use
// UART2 so CI-V keeps UART1. Grove uses G1/G2 (shared with default CI-V pins);
// both caps put the GNSS UART on G15(RX)/G13(TX), differing only in baud.
struct GpsProfile { const char* name; int uart; int rx; int tx; uint32_t baud; };
static const GpsProfile GPS_PROFILES[GPS_SRC_COUNT] = {
  { "Grove 9600",   2,  1,  2,   9600 },
  { "Grove 115200", 2,  1,  2, 115200 },
  { "Cap LoRa868",  2, 15, 13, 115200 },
  { "Cap LoRa1262", 2, 15, 13, 115200 },
};

void App::startGps() {
  const GpsProfile& g = GPS_PROFILES[cfg.gpsSource % GPS_SRC_COUNT];
  loc.beginGps(g.uart, g.rx, g.tx, g.baud);
  Serial.printf("[gps] source=%s uart=%d rx=%d tx=%d baud=%lu\n",
                g.name, g.uart, g.rx, g.tx, (unsigned long)g.baud);
}

// Wipe ALL persistent state (settings, GP data, transponder caches, manual GP,
// favorites, calibration) and reboot to a clean first-run state. Formatting the
// whole LittleFS partition is the simplest way to be sure nothing survives.
void App::factoryReset() {
  setStatus("Erasing all data...");
  draw();
  Store::formatInternal();
  delay(300);
  ESP.restart();   // reboot -> setup() runs fresh with built-in defaults
}

void App::setStatus(const String& s, uint32_t ms) {
  status = s; statusUntil = millis() + ms;
}

time_t App::nowUtc() { return time(nullptr); }

SatEntry* App::activeSat() {
  if (db.count() == 0) return nullptr;
  if (satSel < 0) satSel = 0;
  if (satSel >= db.count()) satSel = db.count() - 1;
  return &db.at(satSel);
}

// ===========================================================================
//  Transponders
// ===========================================================================
bool App::ensureTransponders(SatEntry& s) {
  activeTxCount = 0; curTx = 0;
  // 1) try LittleFS cache
  activeTxCount = SatDb::loadTxCache(s.norad, activeTx, MAX_TX_PER_SAT);
  // 2) try network if nothing cached
  if (activeTxCount == 0 && net.connected()) {
    String j;
    if (net.fetchSatnogsTransmitters(s.norad, j)) {
      SatDb::saveTxCache(s.norad, j);
      activeTxCount = SatDb::parseTransmittersJson(j, activeTx, MAX_TX_PER_SAT);
    }
  }
  // 3) always append any manually-entered transponders for this sat
  if (activeTxCount < MAX_TX_PER_SAT)
    activeTxCount += loadManualTx(s.norad, activeTx + activeTxCount,
                                  MAX_TX_PER_SAT - activeTxCount);
  // 4) tag FM uplinks with the effective PL/CTCSS tone: the user's per-satellite
  //    override if one is set, otherwise the built-in table (SatNOGS has none).
  retagTones(s.norad);
  s.txLoaded = (activeTxCount > 0);
  return s.txLoaded;
}

// Recenter the passband tuning to mid-band and choose a sensible default
// track-screen mode for the currently selected transponder.
void App::onTransponderChanged() {
  radioTune = false; lastRxSet = 0;           // start each channel in manual tune
  if (activeTxCount <= 0) { pbOffset = 0; trackMode = 1; return; }
  Transponder& t = activeTx[curTx];
  if (t.isLinear && t.bandwidth() > 0) {
    pbOffset = (int32_t)(t.bandwidth() / 2);  // start in the middle of the passband
    trackMode = 0;                            // TUNE mode for linear birds
  } else {
    pbOffset = 0;
    trackMode = 1;                            // CAL mode for FM / single-channel
  }
}

// ===========================================================================
//  GP element update (download AMSAT GP/OMM JSON)
// ===========================================================================
void App::doUpdateGp() {
  setStatus("WiFi..."); draw();
  if (!net.connected() && !net.connect(cfg.ssid, cfg.pass)) {
    Serial.println("[gp] WiFi connect failed");
    setStatus("WiFi failed (check SSID/pass)"); return;
  }
  Serial.printf("[gp] WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());
  net.syncTimeNtp();
  setStatus("Downloading GP..."); draw();
  // Stream straight to the cache file (the download IS the offline cache) and
  // parse from flash -- avoids holding the whole ~75 KB body in RAM.
  if (!net.fetchGpToFile(cfg.gpUrl, FILE_GP)) {
    Serial.printf("[gp] download failed: %s\n", net.lastErr.c_str());
    setStatus("GP DL failed: " + net.lastErr); return;
  }
  int n = db.loadGpFromFile(FILE_GP);
  db.loadManualGpFile();               // re-merge hand-entered sats after replace
  Serial.printf("[gp] parsed %d satellites\n", n);
  if (n <= 0) { setStatus("Got data but parsed 0 sats"); return; }
  buildSatView();
  nextAos = 0; lastSchedMs = 0;        // force schedule/alarm to recompute
  setStatus("GP OK: " + String(n) + " sats");
}

// Fetch and cache every satellite's transponders to flash, so the unit works
// fully offline afterwards. One HTTPS GET per satellite (slow but one-time).
void App::doCacheAllTransponders() {
  if (!net.connected() && !net.connect(cfg.ssid, cfg.pass)) {
    setStatus("WiFi failed (check SSID/pass)"); return;
  }
  int n = db.count();
  if (n == 0) { setStatus("No sats. Update GP first."); return; }
  int ok = 0;
  for (int i = 0; i < n; ++i) {
    SatEntry& s = db.at(i);
    setStatus("TX " + String(i+1) + "/" + String(n) + ": " + s.name); draw();
    String j;
    if (net.fetchSatnogsTransmitters(s.norad, j) && SatDb::saveTxCache(s.norad, j))
      ok++;
    delay(40);   // be gentle on the API
  }
  Serial.printf("[tx] cached %d/%d satellites\n", ok, n);
  setStatus("Cached TX: " + String(ok) + "/" + String(n));
}

// ---- Per-satellite calibration (LittleFS text store: "norad dl ul" lines) ---
void App::loadCalForSat(uint32_t norad) {
  // Default to the global calibration from config; override if a per-sat entry
  // exists in the store.
  calDl = cfg.calDlHz; calUl = cfg.calUlHz;
  File f = Store::fs().open(FILE_CALIB, "r");
  if (!f) return;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    unsigned long nd; long dl, ul;
    if (sscanf(line.c_str(), "%lu %ld %ld", &nd, &dl, &ul) == 3
        && nd == norad) {
      calDl = (int32_t)dl; calUl = (int32_t)ul;
      break;
    }
  }
  f.close();
}

void App::saveCalForSat(uint32_t norad) {
  // Read existing entries (excluding this norad), then rewrite with the new one.
  String out;
  File f = Store::fs().open(FILE_CALIB, "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      String t = line; t.trim();
      if (t.length() == 0) continue;
      unsigned long nd;
      if (sscanf(t.c_str(), "%lu", &nd) == 1 && nd == norad) continue;
      out += t; out += '\n';
    }
    f.close();
  }
  char buf[48];
  snprintf(buf, sizeof(buf), "%lu %ld %ld\n",
           (unsigned long)norad, (long)calDl, (long)calUl);
  out += buf;
  File w = Store::fs().open(FILE_CALIB, "w");
  if (w) { w.print(out); w.close(); }
}

// ---- Per-satellite CTCSS override (text store: "norad tenths" lines) -------
// tenths >= 0 is a user override (0 = force tone off); a missing line means
// "use the built-in table". Mirrors the calibration store.
float App::toneOverrideHz(uint32_t norad) {
  File f = Store::fs().open(FILE_TONES, "r");
  if (!f) return -1.0f;
  float hz = -1.0f;
  while (f.available()) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    unsigned long nd; int tenths;
    if (sscanf(line.c_str(), "%lu %d", &nd, &tenths) == 2 && nd == norad) {
      hz = tenths / 10.0f; break;
    }
  }
  f.close();
  return hz;
}

void App::saveToneOverride(uint32_t norad, float hz) {
  // Rewrite the file without this norad, then append the new entry (unless
  // hz < 0, which clears the override so the built-in default applies again).
  String out;
  File f = Store::fs().open(FILE_TONES, "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n'); String t = line; t.trim();
      if (t.length() == 0) continue;
      unsigned long nd;
      if (sscanf(t.c_str(), "%lu", &nd) == 1 && nd == norad) continue;
      out += t; out += '\n';
    }
    f.close();
  }
  if (hz >= 0.0f) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%lu %d\n",
             (unsigned long)norad, (int)lroundf(hz * 10.0f));
    out += buf;
  }
  File w = Store::fs().open(FILE_TONES, "w");
  if (w) { w.print(out); w.close(); }
}

// Re-stamp the loaded transponders with the effective tone (user override wins
// over the built-in table) and force the encoder to be re-asserted next tick.
void App::retagTones(uint32_t norad) {
  float ov = toneOverrideHz(norad);                       // <0 = no override
  float pl = (ov >= 0.0f) ? ov : SatDb::knownCtcssHz(norad);
  for (int i = 0; i < activeTxCount; ++i)
    if (activeTx[i].uplink && Rig::modeFromString(activeTx[i].mode) == RM_FM)
      activeTx[i].toneHz = pl;
  toneApplied = -2.0f;
}

// ---- Favorites (LittleFS: one NORAD id per line) --------------------------
void App::loadFavs() {
  favN = 0;
  File f = Store::fs().open(FILE_FAVS, "r");
  if (!f) return;
  while (f.available() && favN < MAX_SATS) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    unsigned long nd = strtoul(line.c_str(), nullptr, 10);
    if (nd) favs[favN++] = (uint32_t)nd;
  }
  f.close();
}

void App::saveFavs() {
  File f = Store::fs().open(FILE_FAVS, "w");
  if (!f) return;
  for (int i = 0; i < favN; ++i) f.printf("%lu\n", (unsigned long)favs[i]);
  f.close();
}

bool App::isFav(uint32_t norad) const {
  for (int i = 0; i < favN; ++i) if (favs[i] == norad) return true;
  return false;
}

void App::toggleFav(uint32_t norad) {
  nextAos = 0; lastSchedMs = 0;             // schedule/alarm must recompute
  for (int i = 0; i < favN; ++i) {
    if (favs[i] == norad) {                 // remove
      for (int j = i; j < favN - 1; ++j) favs[j] = favs[j+1];
      favN--;
      saveFavs();
      return;
    }
  }
  if (favN < MAX_SATS) { favs[favN++] = norad; saveFavs(); }   // add
}

// Rebuild the (optionally favorites-filtered) list of db indices to display.
void App::buildSatView() {
  viewN = 0;
  int n = db.count();
  for (int i = 0; i < n && viewN < MAX_SATS; ++i) {
    if (!favOnly || isFav(db.at(i).norad)) view[viewN++] = i;
  }
  if (viewSel >= viewN) viewSel = (viewN > 0) ? viewN - 1 : 0;
  if (viewSel < 0) viewSel = 0;
  if (satScroll > viewSel) satScroll = viewSel;
  if (viewSel > satScroll + 9) satScroll = viewSel - 9;
  if (satScroll < 0) satScroll = 0;
  satSel = (viewN > 0) ? view[viewSel] : 0;
}

// ---- Feature 1: next pass for every favorite, merged + sorted by AOS -------
void App::buildSchedule() {
  schedN = 0; nextAos = 0; nextAosName[0] = 0;
  if (!timeIsSet()) return;
  time_t now = nowUtc();
  pred.setSite(loc.obs());

  for (int i = 0; i < favN && schedN < SCHED_MAX; ++i) {
    int idx = db.indexOfNorad(favs[i]);
    if (idx < 0) continue;
    SatEntry& s = db.at(idx);
    if (!pred.setSat(s)) continue;

    SchedEntry e;
    e.norad = s.norad;
    strncpy(e.name, s.name, sizeof(e.name) - 1); e.name[sizeof(e.name) - 1] = 0;

    LiveLook L = pred.look(now);
    if (L.el >= 0.0) {
      // Currently above the horizon: show it now and find LOS by coarse scan.
      e.inProgress = true; e.aos = now; e.maxEl = (float)L.el;
      time_t t = now, los = now;
      for (int k = 0; k < 120; ++k) {            // up to 60 min, 30 s steps
        t += 30; LiveLook l2 = pred.look(t);
        if (l2.el < 0.0) { los = t; break; }
        if (l2.el > e.maxEl) e.maxEl = (float)l2.el;
        los = t;
      }
      e.los = los;
    } else {
      PassPredict p;
      if (pred.predictPasses(now, cfg.minPassEl, &p, 1) < 1) continue;
      e.inProgress = false; e.aos = p.aos; e.los = p.los; e.maxEl = p.maxEl;
    }
    sched[schedN++] = e;
  }

  // Insertion sort by AOS (schedN is small).
  for (int i = 1; i < schedN; ++i) {
    SchedEntry key = sched[i]; int j = i - 1;
    while (j >= 0 && sched[j].aos > key.aos) { sched[j+1] = sched[j]; --j; }
    sched[j+1] = key;
  }
  if (schedSel >= schedN) schedSel = (schedN > 0) ? schedN - 1 : 0;

  // Soonest *future* AOS feeds the alarm.
  for (int i = 0; i < schedN; ++i) {
    if (!sched[i].inProgress && sched[i].aos > now) {
      nextAos = sched[i].aos;
      strncpy(nextAosName, sched[i].name, sizeof(nextAosName) - 1);
      nextAosName[sizeof(nextAosName) - 1] = 0;
      break;
    }
  }
  // Restore the propagator to the user's active satellite.
  SatEntry* a = activeSat(); if (a) pred.setSat(*a);
}

// ---- Feature 7: sample one pass into the elevation curve cache -------------
void App::buildPassDetail(const PassPredict& p) {
  pdValid = false; pdPass = p;
  SatEntry* s = activeSat(); if (!s) return;
  pred.setSite(loc.obs()); pred.setSat(*s);
  double span = (double)(p.los - p.aos); if (span < 1) span = 1;
  for (int i = 0; i < PD_SAMPLES; ++i) {
    time_t t = p.aos + (time_t)llround(span * i / (double)(PD_SAMPLES - 1));
    LiveLook L = pred.look(t);
    pdEl[i]     = (float)L.el;
    pdAz[i]     = (float)L.az;
    pdSunlit[i] = L.sunlit;
  }
  pdValid = true;
}

void App::refreshScheduleIfNeeded() {
  if (favN == 0 || !timeIsSet()) return;
  if (screen == SCR_TRACK || screen == SCR_POLAR) return;  // don't disturb Doppler
  if (screen == SCR_PASSES) return;                        // owns the propagator
  uint32_t ms = millis();
  bool due = (nextAos == 0) || (nowUtc() >= nextAos) ||
             (ms - lastSchedMs > 600000UL);                // at least every 10 min
  if (!due) return;
  buildSchedule();
  lastSchedMs = ms;
}

// ---- Feature 2: AOS alarm (countdown beeps + screen flash) -----------------
void App::serviceAosAlarm() {
  if (!cfg.aosAlarm || !timeIsSet() || nextAos == 0) return;
  if (alarmAos != nextAos) { alarmAos = nextAos; alarmMarks = 0; }  // new target
  long dt = (long)(nextAos - nowUtc());
  if (dt <= 60 && !(alarmMarks & 1)) { alarmMarks |= 1; beep(1500, 80); }
  if (dt <= 30 && !(alarmMarks & 2)) { alarmMarks |= 2; beep(1500, 80); }
  if (dt <= 10 && !(alarmMarks & 4)) { alarmMarks |= 4; beep(1800, 90); }
  if (dt <= 0  && !(alarmMarks & 8)) {
    alarmMarks |= 8;
    beep(2600, 250); delay(120); beep(2600, 250);          // AOS!
    aosFlashUntil = millis() + 8000;
    strncpy(aosFlashName, nextAosName, sizeof(aosFlashName) - 1);
    aosFlashName[sizeof(aosFlashName) - 1] = 0;
    nextAos = 0;            // force refreshScheduleIfNeeded() to find the next
  }
}

// ---- Feature 4: deep-sleep until ~60 s before the next favorite AOS --------
void App::sleepUntilNextPass() {
  if (!timeIsSet())  { setStatus("Set the clock first"); return; }
  if (nextAos == 0)  buildSchedule();
  if (nextAos == 0)  { setStatus("No upcoming pass"); return; }
  const long lead = 60;
  long secs = (long)(nextAos - nowUtc()) - lead;
  if (secs < 5) { setStatus("Pass too soon to sleep"); return; }
  if (secs > 12L * 3600) secs = 12L * 3600;                // safety cap

  canvas.fillScreen(CL_BLACK);
  canvas.setTextSize(1); canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(6, 38); canvas.printf("Deep sleep %ldm%02lds", secs/60, secs%60);
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 54); canvas.printf("until %.20s", nextAosName);
  canvas.setCursor(6, 68); canvas.print("Wakes ~60s before AOS");
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 86); canvas.print("(press reset to wake early)");
  canvas.pushSprite(0, 0);
  delay(1800);
  M5Cardputer.Display.sleep();
  esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);
  esp_deep_sleep_start();    // resets the SoC; setup() runs again on wake
}

// ---- Manual transponders (LittleFS text: "dl ul invert mode" per line) ----
void App::saveManualTx(uint32_t norad, const Transponder& t) {
  char path[32]; snprintf(path, sizeof(path), FILE_MTX, (unsigned long)norad);
  File f = Store::fs().open(path, "a");
  if (!f) return;
  // format: dlLow dlHigh ulLow ulHigh invert mode
  f.printf("%lu %lu %lu %lu %d %s\n",
           (unsigned long)t.downlink, (unsigned long)t.downlinkHigh,
           (unsigned long)t.uplink,   (unsigned long)t.uplinkHigh,
           t.invert ? 1 : 0, t.mode[0] ? t.mode : "FM");
  f.close();
}

int App::loadManualTx(uint32_t norad, Transponder* out, int maxN) {
  char path[32]; snprintf(path, sizeof(path), FILE_MTX, (unsigned long)norad);
  File f = Store::fs().open(path, "r");
  if (!f) return 0;
  int n = 0;
  while (f.available() && n < maxN) {
    String line = f.readStringUntil('\n'); line.trim();
    if (line.length() == 0) continue;
    unsigned long dl, dlh, ul, ulh; int inv; char mode[12] = {0};
    if (sscanf(line.c_str(), "%lu %lu %lu %lu %d %11s",
               &dl, &dlh, &ul, &ulh, &inv, mode) >= 5) {
      Transponder& t = out[n];
      t = Transponder();
      t.downlink = (uint32_t)dl; t.downlinkHigh = (uint32_t)dlh;
      t.uplink   = (uint32_t)ul; t.uplinkHigh   = (uint32_t)ulh;
      t.invert   = (inv != 0);
      t.isLinear = (t.downlinkHigh > t.downlink) && (t.uplink != 0);
      strncpy(t.mode, mode[0] ? mode : "FM", sizeof(t.mode)-1);
      snprintf(t.desc, sizeof(t.desc), t.isLinear ? "manual lin" : "manual");
      n++;
    }
  }
  f.close();
  return n;
}

// ===========================================================================
//  Main loop
// ===========================================================================
void App::loop() {
  M5Cardputer.update();
  if (cfg.useGps) {
    if (loc.pollGps()) { pred.setSite(loc.obs()); }
  }
  // Live-refresh the Location screen when the GPS picture changes (fix gained
  // or lost, or satellite count changes) without waiting for a keypress.
  if (screen == SCR_LOCATION) {
    if (loc.gpsHasFix() != lastGpsFix || loc.gpsSats() != lastGpsSats) {
      lastGpsFix = loc.gpsHasFix();
      lastGpsSats = loc.gpsSats();
      draw();
    }
  }

  refreshScheduleIfNeeded();   // keep the all-favorites schedule fresh
  serviceAosAlarm();           // countdown beeps + flash before AOS

  // Keyboard
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto ks = M5Cardputer.Keyboard.keysState();
    char c = ks.word.empty() ? 0 : ks.word.front();
    handleKey(c, ks.enter, ks.del);
    draw();
  }

  // Real-time Doppler service (on the tracking and polar screens)
  uint32_t ms = millis();
  if (screen == SCR_TRACK || screen == SCR_POLAR) {
    if (radioOut && rig && rig->ready() && ms - lastDoppMs > 500) {
      lastDoppMs = ms;
      SatEntry* s = activeSat();
      if (s && activeTxCount > 0 && timeIsSet()) {
        LiveLook L = pred.look(nowUtc());
        Transponder& t = activeTx[curTx];
        uint32_t dlOp, ulOp, rx, tx;
        if (radioTune && t.isLinear && rig->canReadFreq()) {
          // ---- One True Rule (KB5MU): hold a constant frequency AT THE
          // SATELLITE while the operator tunes the rig's knob. Read the
          // downlink the operator is on, back out Doppler to find their chosen
          // spot in the passband, then Doppler-correct BOTH legs around that
          // fixed satellite frequency. Let go of the knob and nothing drifts.
          uint32_t rxNow;
          if (rig->readSubFreq(rxNow)) {
            double beta = L.rangeRate * 1000.0 / 299792458.0;
            // The rig only moves when the operator turns it (we set it last
            // tick), so any change from lastRxSet is a deliberate knob move.
            bool moved = (lastRxSet == 0) ||
                (llabs((long long)rxNow - (long long)lastRxSet) > 20);
            if (moved) {
              double dlSat = ((double)rxNow - (double)calDl) / (1.0 - beta);
              int32_t off = (int32_t)llround(dlSat - (double)t.downlink);
              int32_t bw  = (int32_t)t.bandwidth();
              if (off < 0) off = 0; if (off > bw) off = bw;
              pbOffset = off;                       // new fixed satellite point
            }
          }
          Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
          Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
          if (t.downlink) rig->setSubFreq(rx);                // hold sat downlink
          if (t.uplink)   { delay(8); rig->setMainFreq(tx);   // hold sat uplink
                            rig->selectSubBand(); }           // dial back on RX
          lastRxSet = rx;
        } else {
          Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
          Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
          if (t.downlink) rig->setSubFreq(rx);                // downlink (RX) on SUB
          if (t.uplink)   { delay(8); rig->setMainFreq(tx); } // uplink (TX) on MAIN
        }
        applyCtcssForCurrentTx();   // FM uplink PL tone (only re-sends on change)
      }
    }
    // Rotator pointing (independent of radio output). Rotators are slow, so
    // update at ~1 Hz and only when az/el has moved past the deadband.
    if (rotOut && rot && rot->ready() && ms - lastRotMs > 1000) {
      lastRotMs = ms;
      SatEntry* s = activeSat();
      if (s && timeIsSet()) {
        LiveLook L = pred.look(nowUtc());
        if (L.el >= 0.0) {                              // above horizon: track
          float az = (float)L.az + cfg.rotAzOff;
          float el = (float)L.el + cfg.rotElOff;
          if (cfg.rotFlip && el > 90.0f) { az += 180.0f; el = 180.0f - el; }
          while (az >= 360.0f) az -= 360.0f;
          while (az < 0.0f)    az += 360.0f;
          float elMax = cfg.rotFlip ? 180.0f : 90.0f;
          if (el < 0) el = 0; if (el > elMax) el = elMax;
          if (lastAzCmd < -500.0f ||
              fabsf(az - lastAzCmd) >= (float)cfg.rotDeadband ||
              fabsf(el - lastElCmd) >= (float)cfg.rotDeadband) {
            rot->point(az, el);
            lastAzCmd = az; lastElCmd = el; rotParked = false;
          }
        } else if (!rotParked) {                        // below horizon: park once
          rot->point((float)cfg.rotParkAz, (float)cfg.rotParkEl);
          rotParked = true; lastAzCmd = lastElCmd = -999.0f;
        }
      }
    }
    if (ms - lastDrawMs > 500) { lastDrawMs = ms; draw(); }
  } else if (screen == SCR_PASSES || screen == SCR_HOME ||
             screen == SCR_SCHEDULE || screen == SCR_PASSDETAIL) {
    if (ms - lastDrawMs > 1000) { lastDrawMs = ms; draw(); }  // live clock / countdown
  }

  // While an AOS alarm is flashing or counting down, animate on any screen.
  long dt = (nextAos && timeIsSet()) ? (long)(nextAos - nowUtc()) : 999999;
  bool alarmActive = (millis() < aosFlashUntil) || (cfg.aosAlarm && dt <= 60 && dt > -2);
  if (alarmActive && ms - lastDrawMs > 500) { lastDrawMs = ms; draw(); }
}

// ===========================================================================
//  Input dispatch
// ===========================================================================
void App::handleKey(char c, bool enter, bool back) {
  switch (screen) {
    case SCR_HOME:     keyHome(c, enter, back); break;
    case SCR_SATLIST:  keySatList(c, enter, back); break;
    case SCR_SCHEDULE: keySchedule(c, enter, back); break;
    case SCR_PASSES:   keyPasses(c, enter, back); break;
    case SCR_PASSDETAIL: keyPassDetail(c, enter, back); break;
    case SCR_TRACK:    keyTrack(c, enter, back); break;
    case SCR_POLAR:    keyPolar(c, enter, back); break;
    case SCR_PASSPOLAR: keyPassPolar(c, enter, back); break;
    case SCR_MUTUAL:   keyMutual(c, enter, back); break;
    case SCR_LOCATION: keyLocation(c, enter, back); break;
    case SCR_UPDATE:   keyUpdate(c, enter, back); break;
    case SCR_SETTINGS: keySettings(c, enter, back); break;
    case SCR_EDIT:     keyEdit(c, enter, back); break;
    case SCR_WIFISCAN: keyWifiScan(c, enter, back); break;
  }
}

// Arrow legends on the Cardputer keys: ';' up  '.' down  ',' left  '/' right
static bool isUp(char c)    { return c == ';'; }
static bool isDown(char c)  { return c == '.'; }
static bool isLeft(char c)  { return c == ','; }
static bool isRight(char c) { return c == '/'; }
static bool isBack(char c, bool del) { return c == '`' || del; }

// ---------------------------------------------------------------------------
void App::keyHome(char c, bool enter, bool back) {
  const int N = 7;
  if (isUp(c))   homeSel = (homeSel + N - 1) % N;
  if (isDown(c)) homeSel = (homeSel + 1) % N;
  if (enter) {
    switch (homeSel) {
      case 0: buildSatView(); screen = SCR_SATLIST; break;
      case 1: schedSel = 0; buildSchedule(); screen = SCR_SCHEDULE; break;
      case 2: // passes for selected sat
      case 3: { // track selected sat
        SatEntry* s = activeSat();
        if (!s) { setStatus("No sats. Update first."); break; }
        pred.setSite(loc.obs());
        pred.setSat(*s);
        if (homeSel == 2) {
          passN = timeIsSet()
                ? pred.predictPasses(nowUtc(), cfg.minPassEl, passes, PASS_LIST_LEN)
                : 0;
          passSel = 0;
          screen = SCR_PASSES;
        } else {
          ensureTransponders(*s);
          onTransponderChanged();
          loadCalForSat(s->norad);
          radioOut = false;
          screen = SCR_TRACK;
        }
      } break;
      case 4: screen = SCR_LOCATION; break;
      case 5: screen = SCR_UPDATE; break;
      case 6: setSel = 0; screen = SCR_SETTINGS; break;
    }
  }
}

void App::keySchedule(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (isUp(c)   && schedN) schedSel = (schedSel + schedN - 1) % schedN;
  if (isDown(c) && schedN) schedSel = (schedSel + 1) % schedN;
  if (c == 'r') { setStatus("Computing passes..."); draw(); buildSchedule();
                  lastSchedMs = millis(); setStatus("Schedule updated"); }
  if (c == 'z') { sleepUntilNextPass(); return; }     // deep-sleep until AOS
  if (enter && schedN > 0) {
    int idx = db.indexOfNorad(sched[schedSel].norad);
    if (idx >= 0) {
      satSel = idx;
      SatEntry* s = &db.at(idx);
      pred.setSite(loc.obs()); pred.setSat(*s);
      ensureTransponders(*s);
      onTransponderChanged();
      loadCalForSat(s->norad);
      radioOut = false; lastDoppMs = 0;
      screen = SCR_TRACK;
    }
  }
}

void App::keySatList(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (c == 'n') {                              // new manual GP entry
    mtSat = SatEntry();
    editTarget = 310; editTitle = "Sat name"; editBuf = ""; screen = SCR_EDIT;
    return;
  }
  if (c == 'v') { favOnly = !favOnly; buildSatView();
                  setStatus(favOnly ? "Favorites only" : "All satellites"); return; }
  int n = viewN;
  if (n == 0) return;
  if (isUp(c)   && viewSel > 0)     viewSel--;
  if (isDown(c) && viewSel < n - 1) viewSel++;
  if (c == '{') viewSel = max(0, viewSel - 10);
  if (c == '}') viewSel = min(n - 1, viewSel + 10);
  if (c == 'f') {                              // toggle favorite for selected
    toggleFav(db.at(view[viewSel]).norad);
    if (favOnly) buildSatView();               // may shrink the list
  }
  if (viewSel < satScroll)      satScroll = viewSel;
  if (viewSel > satScroll + 9)  satScroll = viewSel - 9;
  if (viewN > 0) satSel = view[viewSel];
  if (enter && viewN > 0) {
    SatEntry* s = activeSat();
    pred.setSite(loc.obs());
    pred.setSat(*s);
    ensureTransponders(*s);
    onTransponderChanged();
    passN = timeIsSet()
          ? pred.predictPasses(nowUtc(), cfg.minPassEl, passes, PASS_LIST_LEN)
          : 0;
    passSel = 0;
    screen = SCR_PASSES;
  }
}

void App::keyPasses(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_SATLIST; return; }
  int visN = (passN < 9) ? passN : 9;            // only the visible rows
  if (isUp(c)   && visN) passSel = (passSel + visN - 1) % visN;
  if (isDown(c) && visN) passSel = (passSel + 1) % visN;
  if (passSel >= visN) passSel = 0;
  if (c == 'n') {                       // add a transponder manually for this sat
    mtxDl = mtxUl = mtxDlHigh = mtxUlHigh = 0; mtxInv = false;
    editTarget = 320; editTitle = "Downlink low Hz"; editBuf = ""; screen = SCR_EDIT;
    return;
  }
  if (c == 'r') {  // recompute
    if (timeIsSet()) { passN = pred.predictPasses(nowUtc(), cfg.minPassEl,
                                                  passes, PASS_LIST_LEN);
                       passSel = 0; }
  }
  if (c == 'd' && passN > 0 && passSel < passN) {  // open the pass-detail plot
    buildPassDetail(passes[passSel]);
    screen = SCR_PASSDETAIL; lastDrawMs = 0;
    return;
  }
  if (c == 'x') {                       // mutual-window vs a remote DX grid
    editTarget = 330; editTitle = "DX grid (4 or 6 char)"; editBuf = "";
    screen = SCR_EDIT; return;
  }
  if (enter || c == 't') {     // enter tracking
    SatEntry* s = activeSat();
    if (s) loadCalForSat(s->norad);
    radioOut = false;
    lastDoppMs = 0;
    screen = SCR_TRACK;
  }
}

void App::keyPassDetail(char c, bool enter, bool back) {
  if (c == 'p') { screen = SCR_PASSPOLAR; lastDrawMs = 0; return; }  // polar of this pass
  if (isBack(c, back) || enter) { screen = SCR_PASSES; lastDrawMs = 0; }
}

void App::keyTrack(char c, bool enter, bool back) {
  if (isBack(c, back)) {
    if (radioOut) { radioOut = false; }     // stop sending on first back
    else {
      if (rotOut && rot) { rot->point((float)cfg.rotParkAz, (float)cfg.rotParkEl);
                           rotOut = false; rotParked = true; }
      screen = SCR_PASSES;
    }
    return;
  }

  bool linear = (activeTxCount > 0) && activeTx[curTx].isLinear &&
                activeTx[curTx].bandwidth() > 0;

  if (c == 'm') {                                    // toggle TUNE / CAL
    if (linear) trackMode ^= 1;
    else { trackMode = 1; setStatus("Not linear: CAL only"); }
  }

  if (c == 'd') {                                    // toggle radio-knob tuning
    if (!linear) setStatus("Radio tune: linear birds only");
    else if (!rig->canReadFreq())
      setStatus(String(rig->name()) + " can't read freq");
    else {
      radioTune = !radioTune;
      lastRxSet = 0;                                  // re-sync to the knob
      setStatus(radioTune ? "Radio tune: knob = passband (OTR)"
                          : "Radio tune off");
    }
  }

  if (trackMode == 0 && linear && !radioTune) {
    // ---- TUNE mode: move within the transponder passband via device keys ----
    int32_t bw = (int32_t)activeTx[curTx].bandwidth();
    if (isLeft(c))  pbOffset -= tuneStep;            // tune down in passband
    if (isRight(c)) pbOffset += tuneStep;            // tune up in passband
    if (pbOffset < 0)  pbOffset = 0;
    if (pbOffset > bw) pbOffset = bw;
    if (c == 's')                                    // cycle tune step
      tuneStep = (tuneStep == 100) ? 1000 : (tuneStep == 1000 ? 5000 : 100);
    if (c == 'x') pbOffset = bw / 2;                 // recenter passband
  } else {
    // ---- CAL mode: trim radio/satellite oscillator offset ----
    if (isLeft(c))  calDl -= calStep;
    if (isRight(c)) calDl += calStep;
    if (isUp(c))    calUl += calStep;
    if (isDown(c))  calUl -= calStep;
    if (c == 's')                                    // cycle cal step
      calStep = (calStep == 10) ? 100 : (calStep == 100 ? 1000 : 10);
    if (c == 'x') { calDl = 0; calUl = 0; }          // zero calibration
  }

  if (c == 't' && activeTxCount > 0) {               // cycle transponder
    curTx = (curTx + 1) % activeTxCount;
    onTransponderChanged();
    if (radioOut) applyTransponderModes(activeTx[curTx]);   // refresh rig modes
  }
  if (c == 'c' && activeTxCount > 0) {               // set/clear CTCSS tone (per sat)
    float cur = activeTx[curTx].toneHz;
    editTarget = 340;
    editTitle  = "CTCSS Hz (0=off, blank=auto)";
    editBuf    = (cur > 0) ? String(cur, 1) : "";
    screen = SCR_EDIT; return;
  }
  if (c == 'r') {                                    // toggle radio output
    radioOut = !radioOut;
    if (radioOut) {
      // Satellite mode is intentionally never enabled; MAIN/SUB are driven
      // directly. Downlink rides SUB, uplink rides MAIN.
      if (activeTxCount > 0) applyTransponderModes(activeTx[curTx]);
      lastDoppMs = 0;
      setStatus("Radio ON");
    } else {
      if (rig && rig->ready() && rig->hasTone() && toneApplied > 0)
        rig->setCtcss(false, 0);   // don't leave an encode tone on the operator's rig
      toneApplied = -2.0f;         // force re-apply next time radio goes ON
      setStatus("Radio OFF");
    }
  }
  if (c == 'o') {                                    // toggle rotator pointing
    if (!cfg.rotEnable || !rot) setStatus("Rotator: enable in Settings");
    else if (!rot->ready())     setStatus("Rotator: bridge not found");
    else {
      rotOut = !rotOut;
      if (rotOut) {
        lastRotMs = 0; lastAzCmd = lastElCmd = -999.0f; rotParked = false;
        setStatus("Rotator ON");
      } else {
        rot->point((float)cfg.rotParkAz, (float)cfg.rotParkEl);
        rotParked = true; setStatus("Rotator OFF (parked)");
      }
    }
  }
  if (c == 'p') { polarPathValid = false; screen = SCR_POLAR;
                  lastDrawMs = 0; return; }                   // live polar
  if (enter) {  // persist calibration for THIS satellite (per-sat store)
    SatEntry* s = activeSat();
    if (s) { saveCalForSat(s->norad);
             setStatus("Cal saved: " + String(s->name)); }
  }
}

void App::keyPolar(char c, bool enter, bool back) {
  // Any of back / ENTER / 'p' returns to the tracking screen.
  if (isBack(c, back) || enter || c == 'p') { screen = SCR_TRACK; lastDrawMs = 0; }
}

// ---- Polar view of one selected pass (from the pass-detail screen) ---------
void App::drawPassPolar() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Pass polar"));
  canvas.setTextSize(1);
  if (!pdValid) { canvas.setTextColor(CL_YELLOW, CL_BLACK);
                  canvas.setCursor(6, 50); canvas.print("No pass data.");
                  footer("` back"); return; }

  const int cx = 66, cy = 78, R = 50;
  drawPolarGrid(cx, cy, R);
  drawPolarArc(cx, cy, R, pdAz, pdEl, PD_SAMPLES);

  // Live position marker if this pass is currently in progress.
  if (timeIsSet() && pdPass.los > pdPass.aos) {
    time_t now = nowUtc();
    if (now >= pdPass.aos && now <= pdPass.los) {
      double f = (double)(now - pdPass.aos) / (double)(pdPass.los - pdPass.aos);
      int i = (int)lround(f * (PD_SAMPLES - 1));
      if (i < 0) i = 0; if (i >= PD_SAMPLES) i = PD_SAMPLES - 1;
      if (pdEl[i] >= 0) {
        double e = pdEl[i] > 90 ? 90 : pdEl[i];
        double rr = R * (90.0 - e) / 90.0, a = pdAz[i] * (M_PI / 180.0);
        canvas.fillCircle(cx + (int)lround(rr*sin(a)), cy - (int)lround(rr*cos(a)),
                          3, CL_GREEN);
      }
    }
  }

  int rx = 128;
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 24); canvas.printf("AOS %s", fmtHM(pdPass.aos).c_str());
  canvas.setCursor(rx, 36); canvas.printf("LOS %s", fmtHM(pdPass.los).c_str());
  canvas.setCursor(rx, 48); canvas.printf("Dur %ldm", (long)((pdPass.los-pdPass.aos)/60));
  canvas.setCursor(rx, 60); canvas.printf("Max el %.0f", pdPass.maxEl);
  canvas.setTextColor(CL_GREEN, CL_BLACK);
  canvas.setCursor(rx, 78); canvas.printf("A az %03.0f", pdPass.azAos);
  canvas.setTextColor(CL_ORANGE, CL_BLACK);
  canvas.setCursor(rx, 90); canvas.printf("L az %03.0f", pdPass.azLos);
  footer("p plot   ` back");
}

void App::keyPassPolar(char c, bool enter, bool back) {
  if (c == 'p') { screen = SCR_PASSDETAIL; lastDrawMs = 0; return; }  // toggle to elev plot
  if (isBack(c, back) || enter) { screen = SCR_PASSES; lastDrawMs = 0; }
}

// ---- Mutual (co-visibility) windows vs a remote DX grid --------------------
void App::computeMutual(const String& grid) {
  double dlat, dlon;
  if (!Location::gridToLatLon(grid, dlat, dlon)) {
    setStatus("Bad grid (e.g. FM18lw)"); screen = SCR_PASSES; return;
  }
  if (!timeIsSet()) { setStatus("Set the clock first"); screen = SCR_PASSES; return; }
  SatEntry* s = activeSat();
  if (!s) { setStatus("No satellite"); screen = SCR_PASSES; return; }

  String g = grid; g.trim(); g.toUpperCase();
  strncpy(dxGrid, g.c_str(), sizeof(dxGrid) - 1); dxGrid[sizeof(dxGrid)-1] = 0;
  dxLat = dlat; dxLon = dlon;

  setStatus("Computing mutual windows..."); draw();
  Observer dx; dx.lat = dlat; dx.lon = dlon; dx.altM = 0; dx.valid = true;
  pred.setSite(loc.obs()); pred.setSat(*s);
  mutualN = pred.mutualWindows(nowUtc(), dx, 0.0f, mutual, MUTUAL_MAX);
  mutualSel = 0; screen = SCR_MUTUAL; lastDrawMs = 0;
  setStatus(mutualN ? (String(mutualN) + " mutual window(s)") : "No mutual windows");
}

void App::drawMutual() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + " mutual") : String("Mutual"));
  canvas.setTextSize(1);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 18);
  canvas.printf("me %s  DX %s",
                Location::toGrid(loc.obs().lat, loc.obs().lon).c_str(), dxGrid);

  if (mutualN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("No co-visibility windows.");
    footer("` back"); return;
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 28); canvas.print("Start UTC    Dur   me  dx");

  const int VIS = 8;
  int scroll = (mutualSel >= VIS) ? (mutualSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < mutualN; ++v) {
    int i = scroll + v;
    MutualWindow& m = mutual[i];
    long secs = (long)(m.end - m.start);
    int y = 38 + v*10;
    if (i == mutualSel) { canvas.fillRect(0, y-1, 240, 10, CL_GREEN);
                          canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                  canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s %ld:%02ld %3.0f %3.0f", fmtMDHM(m.start).c_str(),
                  secs/60, secs%60, m.myMaxEl, m.dxMaxEl);
  }
  footer(";/. select   ` back");
}

void App::keyMutual(char c, bool enter, bool back) {
  if (isBack(c, back) || enter) { screen = SCR_PASSES; return; }
  if (mutualN) {
    if (isUp(c))   mutualSel = (mutualSel + mutualN - 1) % mutualN;
    if (isDown(c)) mutualSel = (mutualSel + 1) % mutualN;
  }
}

void App::keyLocation(char c, bool enter, bool back) {
  if (isBack(c, back)) { pred.setSite(loc.obs()); screen = SCR_HOME; return; }
  if (c == 'p') {                       // toggle GPS use
    cfg.useGps = !cfg.useGps; cfg.save();
    if (cfg.useGps) startGps();
    setStatus(cfg.useGps ? "GPS enabled" : "GPS off");
  }
  if (c == 's') {                       // cycle GPS source
    cfg.gpsSource = (cfg.gpsSource + 1) % GPS_SRC_COUNT;
    cfg.save();
    if (cfg.useGps) startGps();         // re-open on the new port/baud
    setStatus(String("GPS: ") + GPS_PROFILES[cfg.gpsSource].name);
  }
  if (c == 'e') { editTarget = 100; editTitle = "Latitude (deg)";
                  editBuf = String(cfg.lat, 5); screen = SCR_EDIT; }
  if (c == 'o') { editTarget = 101; editTitle = "Longitude (deg)";
                  editBuf = String(cfg.lon, 5); screen = SCR_EDIT; }
  if (c == 'a') { editTarget = 102; editTitle = "Altitude (m)";
                  editBuf = String(cfg.altM, 1); screen = SCR_EDIT; }
  if (c == 'g') { editTarget = 103; editTitle = "Grid (Maidenhead)";
                  editBuf = ""; screen = SCR_EDIT; }
  if (c == 'c') { editTarget = 300; editTitle = "UTC YYYY-MM-DD HH:MM:SS";
                  editBuf = ""; screen = SCR_EDIT; }
}

void App::keyUpdate(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (c == 'k' || enter) { doUpdateGp(); }
  if (c == 'a') { doCacheAllTransponders(); }   // cache all TX for offline use
  if (c == 'w') {
    setStatus(net.connect(cfg.ssid, cfg.pass) ? "WiFi connected" : "WiFi failed");
  }
}

void App::keySettings(char c, bool enter, bool back) {
  const int N = 16;
  if (isBack(c, back)) { applyRadioFromCfg(); applyRotatorFromCfg();
                         screen = SCR_HOME; return; }
  if (isUp(c))   setSel = (setSel + N - 1) % N;
  if (isDown(c)) setSel = (setSel + 1) % N;
  if (c == 's' && setSel == 4) { startWifiScan(); return; }   // scan from the SSID row

  auto adj = [&](int dir){
    switch (setSel) {
      case 0: { int m = (cfg.radioModel + dir + RIG_COUNT) % RIG_COUNT;
                cfg.radioModel = m; cfg.civAddr = RADIOS[m].civAddr;
                cfg.civBaud = RADIOS[m].defaultBaud; cfg.save();
                applyRadioFromCfg(); } break;
      case 2: { uint32_t bs[] = {1200,4800,9600,19200,38400,57600,115200};
                int idx=2; for (int i=0;i<7;i++) if (bs[i]==cfg.civBaud) idx=i;
                idx = (idx + dir + 7) % 7; cfg.civBaud = bs[idx];
                cfg.save(); applyRadioFromCfg(); } break;
      case 3: cfg.minPassEl = constrain(cfg.minPassEl + dir, 0, 30); cfg.save(); break;
      case 7: cfg.aosAlarm = !cfg.aosAlarm; cfg.save(); break;
      case 8: cfg.rotEnable = !cfg.rotEnable; cfg.save(); applyRotatorFromCfg(); break;
      case 9: { uint32_t bs[] = {1200,4800,9600};
                int idx=2; for (int i=0;i<3;i++) if (bs[i]==cfg.rotBaud) idx=i;
                idx = (idx + dir + 3) % 3; cfg.rotBaud = bs[idx];
                cfg.save(); applyRotatorFromCfg(); } break;
      case 10: cfg.rotDeadband = constrain((int)cfg.rotDeadband + dir, 1, 15);
               cfg.save(); break;
      case 11: { int a = (int)cfg.rotParkAz + dir*5; while (a<0) a+=360; a%=360;
                 cfg.rotParkAz = (uint16_t)a; cfg.save(); } break;
      case 12: cfg.rotAzOff = constrain((int)cfg.rotAzOff + dir, -180, 180);
               cfg.save(); break;
      case 13: cfg.rotElOff = constrain((int)cfg.rotElOff + dir, -30, 30);
               cfg.save(); break;
    }
  };
  if (isLeft(c))  adj(-1);
  if (isRight(c)) adj(+1);
  if (enter) {
    switch (setSel) {
      case 1: editTarget = 200; editTitle = "CI-V addr (hex)";
              editBuf = String(cfg.civAddr, HEX); screen = SCR_EDIT; break;
      case 4: editTarget = 201; editTitle = "WiFi SSID";
              editBuf = cfg.ssid; screen = SCR_EDIT; break;
      case 5: editTarget = 202; editTitle = "WiFi password";
              editBuf = cfg.pass; screen = SCR_EDIT; break;
      case 6: setStatus(net.connect(cfg.ssid, cfg.pass) ? "WiFi OK" : "WiFi FAIL");
              break;
      case 7: cfg.aosAlarm = !cfg.aosAlarm; cfg.save(); break;
      case 8: cfg.rotEnable = !cfg.rotEnable; cfg.save(); applyRotatorFromCfg(); break;
      case 14: editTarget = 203; editTitle = "GP source URL";
               editBuf = cfg.gpUrl; screen = SCR_EDIT; break;
      case 15: editTarget = 400; editTitle = "Type ERASE to wipe all";
               editBuf = ""; screen = SCR_EDIT; break;
      default: adj(+1); break;
    }
  }
}

static Screen editHome(int t) {
  if (t == 340) return SCR_TRACK;       // CTCSS tone override
  if (t >= 400) return SCR_SETTINGS;    // reset confirmation
  if (t >= 320) return SCR_PASSES;      // manual transponder
  if (t >= 310) return SCR_SATLIST;     // manual GP entry
  if (t >= 300) return SCR_LOCATION;    // manual time
  if (t >= 200) return SCR_SETTINGS;    // radio / WiFi
  if (t >= 100) return SCR_LOCATION;    // location fields
  return SCR_HOME;
}

void App::keyEdit(char c, bool enter, bool back) {
  if (c == '`') { screen = editHome(editTarget); return; }      // cancel
  // Backspace: handle the DEL key whether the keyboard reports it via ks.del
  // (back) or delivers it as a raw backspace/delete character code.
  if (back || c == 8 || c == 127) {
    if (editBuf.length()) editBuf.remove(editBuf.length() - 1);
    return;
  }
  if (enter) {
    switch (editTarget) {
      case 100: cfg.lat = editBuf.toFloat(); break;
      case 101: cfg.lon = editBuf.toFloat(); break;
      case 102: cfg.altM = editBuf.toFloat(); break;
      case 103: loc.setFromGrid(editBuf);
                cfg.lat = loc.obs().lat; cfg.lon = loc.obs().lon; break;
      case 200: cfg.civAddr = (uint8_t)strtol(editBuf.c_str(), nullptr, 16); break;
      case 201: strncpy(cfg.ssid, editBuf.c_str(), sizeof(cfg.ssid)-1);
                cfg.ssid[sizeof(cfg.ssid)-1] = 0; break;
      case 202: strncpy(cfg.pass, editBuf.c_str(), sizeof(cfg.pass)-1);
                cfg.pass[sizeof(cfg.pass)-1] = 0; break;
      case 203: strncpy(cfg.gpUrl, editBuf.c_str(), sizeof(cfg.gpUrl)-1);
                cfg.gpUrl[sizeof(cfg.gpUrl)-1] = 0; break;

      // ---- mutual co-visibility vs a DX grid ----
      case 330: computeMutual(editBuf); return;

      // ---- per-satellite CTCSS tone override ----
      case 340: {
        SatEntry* s = activeSat();
        if (!s) { screen = SCR_TRACK; return; }
        String b = editBuf; b.trim();
        if (b.length() == 0) {                        // blank -> revert to auto
          saveToneOverride(s->norad, -1.0f);
          setStatus("CTCSS: auto (built-in)");
        } else {
          float hz = b.toFloat();
          if (hz <= 0) {                              // 0 -> force tone off
            saveToneOverride(s->norad, 0.0f);
            setStatus("CTCSS off for this sat");
          } else {
            int idx = ctcssToneIndex(hz);             // snap to nearest standard
            if (idx < 0) { setStatus("Not a standard CTCSS tone");
                           screen = SCR_TRACK; return; }
            hz = ctcssToneHz(idx);
            saveToneOverride(s->norad, hz);
            setStatus("CTCSS " + String(hz, 1) + " Hz set");
          }
        }
        retagTones(s->norad);                         // apply now + re-assert
        screen = SCR_TRACK; return;
      }

      // ---- manual UTC time entry ----
      case 300: {
        struct tm tmv; memset(&tmv, 0, sizeof(tmv));
        if (sscanf(editBuf.c_str(), "%d-%d-%d %d:%d:%d",
                   &tmv.tm_year, &tmv.tm_mon, &tmv.tm_mday,
                   &tmv.tm_hour, &tmv.tm_min, &tmv.tm_sec) >= 5) {
          tmv.tm_year -= 1900; tmv.tm_mon -= 1;
          time_t epoch = mktime(&tmv);            // TZ=UTC0, so this is UTC
          struct timeval tv; tv.tv_sec = epoch; tv.tv_usec = 0;
          settimeofday(&tv, nullptr);
          setStatus("Clock set (UTC)");
        } else setStatus("Use YYYY-MM-DD HH:MM:SS");
        screen = SCR_LOCATION; return;
      }

      // ---- manual GP entry: name then each orbital element ----
      case 310: strncpy(mtSat.name, editBuf.c_str(), sizeof(mtSat.name)-1);
                mtSat.name[sizeof(mtSat.name)-1] = 0;
                editTarget = 311; editTitle = "NORAD ID"; editBuf = ""; return;
      case 311: mtSat.norad = strtoul(editBuf.c_str(), nullptr, 10);
                editTarget = 312; editTitle = "EPOCH YYYY-MM-DD HH:MM:SS";
                editBuf = ""; return;
      case 312: mtSat.epochUnix = SatDb::gpEpochToUnix(editBuf.c_str());
                editTarget = 313; editTitle = "Inclination (deg)";
                editBuf = ""; return;
      case 313: mtSat.incl = atof(editBuf.c_str());
                editTarget = 314; editTitle = "RAAN (deg)"; editBuf = ""; return;
      case 314: mtSat.raan = atof(editBuf.c_str());
                editTarget = 315; editTitle = "Eccentricity (0.xxxxxxx)";
                editBuf = ""; return;
      case 315: mtSat.ecc = atof(editBuf.c_str());
                editTarget = 316; editTitle = "Arg of perigee (deg)";
                editBuf = ""; return;
      case 316: mtSat.argp = atof(editBuf.c_str());
                editTarget = 317; editTitle = "Mean anomaly (deg)";
                editBuf = ""; return;
      case 317: mtSat.ma = atof(editBuf.c_str());
                editTarget = 318; editTitle = "Mean motion (rev/day)";
                editBuf = ""; return;
      case 318: mtSat.meanMotion = atof(editBuf.c_str());
                editTarget = 319; editTitle = "BSTAR (0 if unknown)";
                editBuf = ""; return;
      case 319: {
        mtSat.bstar = atof(editBuf.c_str());
        bool ok = db.addGp(mtSat);
        buildSatView();
        setStatus(ok ? (String("Added ") + mtSat.name)
                     : "Invalid (check NORAD / mean motion)");
        screen = SCR_SATLIST; return;
      }

      // ---- manual transponder entry ----
      //   dl low -> ul low -> dl high (0=single) -> [ul high -> invert] -> mode
      case 320: mtxDl = strtoul(editBuf.c_str(), nullptr, 10);
                editTarget = 321; editTitle = "Uplink low Hz (0=none)";
                editBuf = ""; return;
      case 321: mtxUl = strtoul(editBuf.c_str(), nullptr, 10);
                editTarget = 323; editTitle = "Downlink high Hz (0=single)";
                editBuf = ""; return;
      case 323: mtxDlHigh = strtoul(editBuf.c_str(), nullptr, 10);
                if (mtxDlHigh > mtxDl && mtxUl) {     // linear -> ask the rest
                  editTarget = 324; editTitle = "Uplink high Hz (0=same BW)";
                } else {                              // single channel -> mode
                  mtxDlHigh = 0; editTarget = 322; editTitle = "Mode (FM/USB/CW)";
                }
                editBuf = ""; return;
      case 324: mtxUlHigh = strtoul(editBuf.c_str(), nullptr, 10);
                editTarget = 325; editTitle = "Inverting? y/n";
                editBuf = ""; return;
      case 325: mtxInv = (editBuf.length() &&
                          (editBuf[0]=='y' || editBuf[0]=='Y' || editBuf[0]=='1'));
                editTarget = 322; editTitle = "Mode (FM/USB/CW)";
                editBuf = ""; return;
      case 322: {
        Transponder t;
        t.downlink = mtxDl; t.uplink = mtxUl;
        t.downlinkHigh = mtxDlHigh; t.uplinkHigh = mtxUlHigh;
        t.invert = mtxInv;
        t.isLinear = (mtxDlHigh > mtxDl) && (mtxUl != 0);
        strncpy(t.mode, editBuf.length() ? editBuf.c_str() : "FM", sizeof(t.mode)-1);
        t.mode[sizeof(t.mode)-1] = 0;
        snprintf(t.desc, sizeof(t.desc), t.isLinear ? "manual lin" : "manual");
        SatEntry* s = activeSat();
        if (s) {
          saveManualTx(s->norad, t);
          if (activeTxCount < MAX_TX_PER_SAT) activeTx[activeTxCount++] = t;
          setStatus(t.isLinear ? "Linear TX added" : "Transponder added");
        }
        screen = SCR_PASSES; return;
      }
      // ---- factory reset confirmation ----
      case 400: {
        if (editBuf == "ERASE") { factoryReset(); return; }  // formats + reboots
        setStatus("Reset cancelled");
        screen = SCR_SETTINGS; return;
      }
    }
    if (editTarget <= 200) {
      loc.setManual(cfg.lat, cfg.lon, cfg.altM);
      pred.setSite(loc.obs());
    }
    cfg.save();
    applyRadioFromCfg();
    screen = editHome(editTarget);
    setStatus("Saved");
    return;
  }
  if (c >= 32 && c < 127) editBuf += c;
}

// ===========================================================================
//  Rendering
// ===========================================================================
void App::header(const String& t) {
  canvas.fillRect(0, 0, 240, 16, CL_BLUE);

  // Battery indicator (top-right). getBatteryLevel() is <0 if unknown.
  int lvl = M5.Power.getBatteryLevel();
  const int bx = 216, by = 3, bw = 18, bh = 9;
  canvas.drawRect(bx, by, bw, bh, CL_WHITE);
  canvas.fillRect(bx + bw, by + 2, 2, bh - 4, CL_WHITE);    // terminal nub
  if (lvl >= 0) {
    if (lvl > 100) lvl = 100;
    int fw = (lvl * (bw - 2)) / 100;
    uint16_t col = (lvl > 50) ? CL_GREEN : (lvl > 20 ? CL_YELLOW : CL_RED);
    if (fw > 0) canvas.fillRect(bx + 1, by + 1, fw, bh - 2, col);
  }

  // Clock (left of the battery). Build it first so the title can be fit to the
  // space that's left over.
  String clk;
  int rightLimit = bx;                          // title must stop before the battery
  if (timeIsSet()) {
    clk = fmtClock(nowUtc()) + "Z";
    rightLimit = bx - (int)clk.length() * 6 - 5;  // …and before the clock when it's shown
  }

  // Title (satellite name) at text size 2 = 12 px/char. Truncate to whatever
  // fits before the clock/battery so a long name can't overwrite them.
  const int titleX = 3, charW = 12, gap = 4;
  int maxChars = (rightLimit - gap - titleX) / charW;
  if (maxChars < 1) maxChars = 1;
  String title = ((int)t.length() > maxChars) ? t.substring(0, maxChars) : t;
  canvas.setTextColor(CL_WHITE, CL_BLUE);
  canvas.setTextSize(2);
  canvas.setCursor(titleX, 1);
  canvas.print(title);
  canvas.setTextSize(1);

  if (clk.length()) {
    canvas.setTextColor(CL_WHITE, CL_BLUE);
    canvas.setCursor(bx - (int)clk.length() * 6 - 5, 4);    // left of the battery
    canvas.print(clk);
  }
}
void App::footer(const String& t) {
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setTextSize(1);
  canvas.setCursor(2, 127);
  canvas.print(t);
}

void App::draw() {
  canvas.fillScreen(CL_BLACK);
  switch (screen) {
    case SCR_HOME:     drawHome(); break;
    case SCR_SATLIST:  drawSatList(); break;
    case SCR_SCHEDULE: drawSchedule(); break;
    case SCR_PASSES:   drawPasses(); break;
    case SCR_PASSDETAIL: drawPassDetail(); break;
    case SCR_TRACK:    drawTrack(); break;
    case SCR_POLAR:    drawPolar(); break;
    case SCR_PASSPOLAR: drawPassPolar(); break;
    case SCR_MUTUAL:   drawMutual(); break;
    case SCR_LOCATION: drawLocation(); break;
    case SCR_UPDATE:   drawUpdate(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_EDIT:     drawEdit(); break;
    case SCR_WIFISCAN: drawWifiScan(); break;
  }
  // transient status
  if (status.length() && millis() < statusUntil) {
    canvas.fillRect(0, 114, 240, 11, CL_DGREEN);
    canvas.setTextColor(CL_WHITE, CL_DGREEN);
    canvas.setTextSize(1);
    canvas.setCursor(2, 115);
    canvas.print(status);
  }
  // AOS alarm overlay (drawn on top of any screen)
  long dt = (nextAos && timeIsSet()) ? (long)(nextAos - nowUtc()) : 999999;
  if (millis() < aosFlashUntil) {
    bool on = ((millis() / 400) & 1);              // blink
    canvas.fillRect(20, 46, 200, 44, on ? CL_RED : CL_BLACK);
    canvas.drawRect(20, 46, 200, 44, CL_WHITE);
    canvas.setTextColor(CL_WHITE, on ? CL_RED : CL_BLACK);
    canvas.setTextSize(2); canvas.setCursor(34, 52); canvas.print("AOS!");
    canvas.setTextSize(1); canvas.setCursor(34, 74); canvas.printf("%.22s", aosFlashName);
  } else if (cfg.aosAlarm && dt >= 0 && dt <= 60) {
    canvas.fillRect(0, 16, 240, 11, CL_ORANGE);
    canvas.setTextColor(CL_BLACK, CL_ORANGE);
    canvas.setTextSize(1); canvas.setCursor(2, 17);
    canvas.printf("AOS %.14s  T-%s", nextAosName, fmtCountdown(dt).c_str());
  }
  canvas.pushSprite(0, 0);
}

void App::drawHome() {
  header("CardSat");
  const char* items[] = { "Satellites", "Next Passes (all favs)", "Passes (sel)",
                          "Track (sel)", "Location", "Update GP/Freq", "Settings" };
  canvas.setTextSize(1);
  for (int i = 0; i < 7; ++i) {
    int y = 20 + i*12;
    if (i == homeSel) { canvas.fillRect(0, y-2, 240, 12, CL_GREEN);
                        canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y);
    canvas.print(items[i]);
  }
  SatEntry* s = activeSat();
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(6, 106);
  canvas.print(s ? String("Sel: ") + s->name : String("Sel: none"));
  footer("; / . move   ENT select");
}

void App::drawSchedule() {
  header("Next Passes");
  canvas.setTextSize(1);
  if (favN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 42); canvas.print("No favorites yet.");
    canvas.setCursor(6, 54); canvas.print("Star sats with 'f' in Satellites.");
    footer("` back");
    return;
  }
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 42); canvas.print("Clock not set (NTP or GPS).");
    footer("r refresh  ` back");
    return;
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 18); canvas.print("When    Satellite     El Len");
  if (schedN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("No passes >= min elev.");
  }
  time_t now = nowUtc();
  const int VIS = 9;
  int scroll = (schedSel >= VIS) ? (schedSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < schedN; ++v) {
    int i = scroll + v;
    SchedEntry& e = sched[i];
    int y = 28 + v*10;
    if (i == schedSel) { canvas.fillRect(0, y-1, 240, 10, CL_GREEN);
                         canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else canvas.setTextColor(e.inProgress ? CL_GREEN : CL_WHITE, CL_BLACK);
    String when = e.inProgress ? String("NOW") : fmtCountdown((long)(e.aos - now));
    long lenMin = (e.los - e.aos) / 60;
    canvas.setCursor(4, y);
    canvas.printf("%-6s %-13.13s %3.0f %2ldm",
                  when.c_str(), e.name, e.maxEl, lenMin);
    // staleness flag for this satellite's elements
    int idx = db.indexOfNorad(e.norad);
    if (idx >= 0 && gpAgeDays(db.at(idx)) >= 14) {
      canvas.setTextColor(CL_RED, (i == schedSel) ? CL_GREEN : CL_BLACK);
      canvas.setCursor(232, y); canvas.print("!");
    }
  }
  footer("ENT trk  r refresh  z sleep  ` bk");
}

void App::drawSatList() {
  header(favOnly ? "Satellites *" : "Satellites");
  canvas.setTextSize(1);
  if (db.count() == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 40); canvas.print("No GP data. Run Update.");
    canvas.setCursor(6, 52); canvas.print("or 'n' to add one manually.");
    footer("n new  ` back");
    return;
  }
  if (viewN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 40); canvas.print("No favorites yet.");
    canvas.setCursor(6, 52); canvas.print("'v' all  'f' add favorite");
    footer("v all  ` back");
    return;
  }
  for (int row = 0; row < 10 && (satScroll+row) < viewN; ++row) {
    int vi = satScroll + row;
    SatEntry& s = db.at(view[vi]);
    int y = 20 + row*10;
    if (vi == viewSel) { canvas.fillRect(0, y-1, 240, 10, CL_GREEN);
                         canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                 canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%c%-21s%5lu", isFav(s.norad) ? '*' : ' ',
                  s.name, (unsigned long)s.norad);
  }
  footer("ENT pass  f fav  v favs  n new  ` bk");
}

void App::drawPasses() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Passes"));
  canvas.setTextSize(1);
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 40); canvas.print("Clock not set.");
    canvas.setCursor(6, 52); canvas.print("Run Update (NTP) or GPS.");
    footer("` back  r recompute");
    return;
  }
  if (!loc.obs().valid) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 40); canvas.print("Set your location first.");
    footer("` back");
    return;
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 18); canvas.print("AOS (UTC)    Dur El  LOS");
  if (s) {                                   // element-set age (staleness)
    double age = gpAgeDays(*s);
    if (age >= 0) {
      canvas.setTextColor(ageColor(age), CL_BLACK);
      canvas.setCursor(186, 18); canvas.printf("GP%4.1fd", age);
    }
  }
  if (passN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 40); canvas.print("No passes >= min elev.");
  }
  for (int i = 0; i < passN && i < 9; ++i) {
    int y = 30 + i*10;
    PassPredict& p = passes[i];
    long mins = (p.los - p.aos) / 60;
    if (i == passSel) { canvas.fillRect(0, y-1, 240, 10, CL_GREEN);
                        canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s  %2ldm %3.0f %s",
                  fmtMDHM(p.aos).c_str(), mins, p.maxEl, fmtHM(p.los).c_str());
  }
  footer("ENT trk r rcmp d dtl n+TX x mut `bk");
}

void App::drawPassDetail() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Pass"));
  canvas.setTextSize(1);
  if (!pdValid) { canvas.setTextColor(CL_YELLOW, CL_BLACK);
                  canvas.setCursor(6, 50); canvas.print("No pass data.");
                  footer("` back"); return; }

  const int x0 = 26, x1 = 232, y0 = 20, y1 = 94;   // plot box

  // Elevation gridlines + labels (0 / 30 / 60 / 90 deg).
  for (int e = 0; e <= 90; e += 30) {
    int y = y1 - (int)lround((double)(y1 - y0) * e / 90.0);
    canvas.drawLine(x0, y, x1, y, CL_GREY);
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, y - 3); canvas.printf("%2d", e);
  }
  canvas.drawLine(x0, y0, x0, y1, CL_GREY);

  // Elevation curve, colored yellow when sunlit, blue when in eclipse.
  int prevx = -1, prevy = -1;
  for (int i = 0; i < PD_SAMPLES; ++i) {
    int x = x0 + (int)lround((double)(x1 - x0) * i / (double)(PD_SAMPLES - 1));
    float el = pdEl[i]; if (el < 0) el = 0;
    int y = y1 - (int)lround((double)(y1 - y0) * el / 90.0);
    if (prevx >= 0) canvas.drawLine(prevx, prevy, x, y, pdSunlit[i] ? CL_YELLOW : CL_BLUE);
    prevx = x; prevy = y;
  }

  // "Now" marker if the pass is in progress.
  if (timeIsSet()) {
    time_t now = nowUtc();
    if (now >= pdPass.aos && now <= pdPass.los && pdPass.los > pdPass.aos) {
      double f = (double)(now - pdPass.aos) / (double)(pdPass.los - pdPass.aos);
      int x = x0 + (int)lround((double)(x1 - x0) * f);
      canvas.drawLine(x, y0, x, y1, CL_CYAN);
    }
  }

  // Stats: AOS/LOS times + azimuths, duration, max elevation, sunlit fraction.
  int sun = 0; for (int i = 0; i < PD_SAMPLES; ++i) if (pdSunlit[i]) sun++;
  int sunPct = sun * 100 / PD_SAMPLES;
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(2, 99);
  canvas.printf("AOS %s az%03.0f  max el%3.0f",
                fmtHM(pdPass.aos).c_str(), pdPass.azAos, pdPass.maxEl);
  canvas.setCursor(2, 110);
  canvas.printf("LOS %s az%03.0f  %ldm sun%d%%",
                fmtHM(pdPass.los).c_str(), pdPass.azLos,
                (long)((pdPass.los - pdPass.aos) / 60), sunPct);
  footer("p polar   ` back");
}

void App::drawTrack() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Track"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  // Az / El / range / range-rate
  canvas.setTextColor(L.visible ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(4, 20);
  canvas.printf("Az %5.1f  El %5.1f%s", L.az, L.el, L.visible ? " *" : "");
  { double age = gpAgeDays(*s);              // element-set age (staleness)
    if (age >= 0) { canvas.setTextColor(ageColor(age), CL_BLACK);
                    canvas.setCursor(186, 20); canvas.printf("GP%4.1fd", age); } }
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(4, 31);
  canvas.printf("Rng %5.0fkm  Rate %+5.2f km/s", L.rangeKm, L.rangeRate);
  if (timeIsSet() && !L.sunlit) {              // satellite in Earth's shadow
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(214, 31); canvas.print("ECL");
  }

  // Transponder + Doppler
  if (activeTxCount == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(4, 48); canvas.print("No transponder data.");
    canvas.setCursor(4, 59); canvas.print("Connect WiFi + reopen sat.");
  } else {
    Transponder& t = activeTx[curTx];
    bool linear = t.isLinear && t.bandwidth() > 0;
    uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
    Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
    Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);

    canvas.setTextColor(CL_CYAN, CL_BLACK);
    canvas.setCursor(4, 44);
    canvas.printf("TX%d/%d %s%-.16s", curTx+1, activeTxCount,
                  linear ? "[LIN] " : "", t.desc);

    // DN/UP show the operating (passband) frequency; RX/TX are Doppler-tuned.
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, 56);
    canvas.printf("DN %s", fmtMHz(dlOp).c_str());
    canvas.setTextColor(CL_GREEN, CL_BLACK);
    canvas.setCursor(120, 56);
    canvas.printf("RX %s", fmtMHz(rx).c_str());

    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, 67);
    if (ulOp) canvas.printf("UP %s", fmtMHz(ulOp).c_str());
    else      canvas.print("UP  (rx only)");
    if (ulOp) {
      canvas.setTextColor(CL_ORANGE, CL_BLACK);
      canvas.setCursor(120, 67);
      canvas.printf("TX %s", fmtMHz(tx).c_str());
    }

    // Passband position (linear only).
    if (linear) {
      float halfk = t.bandwidth() / 2000.0f;
      float posk  = (pbOffset - (int32_t)(t.bandwidth()/2)) / 1000.0f;
      uint16_t col = radioTune ? CL_ORANGE : (trackMode == 0 ? CL_CYAN : CL_GREY);
      const char* tag = radioTune ? "<RADIO>" : (trackMode == 0 ? "<TUNE>" : "");
      canvas.setTextColor(col, CL_BLACK);
      canvas.setCursor(4, 79);
      canvas.printf("PB %+.1fk bw%.1fk %s%s", posk, halfk,
                    t.invert ? "INV " : "", tag);
    }

    // FM uplink PL/CTCSS tone (FM birds aren't linear, so this y row is free).
    if (!linear && t.uplink && t.toneHz > 0) {
      bool can = rig && rig->hasTone();
      canvas.setTextColor((radioOut && can) ? CL_ORANGE : CL_GREY, CL_BLACK);
      canvas.setCursor(4, 79);
      canvas.printf("PL %.1f Hz%s", t.toneHz, can ? "" : " (rig n/a)");
    }

    // Calibration line (active in CAL mode).
    canvas.setTextColor(trackMode == 1 ? CL_YELLOW : CL_GREY, CL_BLACK);
    canvas.setCursor(4, 90);
    canvas.printf("cal DN%+ld UP%+ld st%ld%s",
                  (long)calDl, (long)calUl,
                  (long)(trackMode == 0 ? tuneStep : calStep),
                  trackMode == 1 ? " <CAL>" : "");
  }

  // Radio status line
  canvas.setCursor(4, 102);
  if (!rig || !rig->ready()) { canvas.setTextColor(CL_GREY, CL_BLACK); canvas.print("Radio: n/a"); }
  else {
    canvas.setTextColor(radioOut ? CL_GREEN : CL_GREY, CL_BLACK);
    canvas.printf("Radio %s [%s %02X]", radioOut ? "ON " : "off",
                  rig->name(), rig->address());
  }
  if (cfg.rotEnable) {                       // compact rotator indicator
    bool rok = rot && rot->ready();
    canvas.setTextColor(rotOut ? CL_GREEN : (rok ? CL_GREY : CL_ORANGE), CL_BLACK);
    canvas.setCursor(174, 102);
    canvas.printf("Rot %s", rotOut ? "ON" : (rok ? "off" : "n/c"));
  }
  if (rig && !rig->selVerified()) {
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(4, 113);
    canvas.print("! verify MAIN/SUB for this rig");
  }
  if (trackMode == 0)
    footer(",/tune s=stp x=ctr m=cal t r o p=plr");
  else
    footer(",/DN ;.UP s=stp x=0 m=tn t=tp r o p=plr");
}

// Shared polar grid: elevation rings (0/30/60/90) + cardinal cross + labels.
void App::drawPolarGrid(int cx, int cy, int R) {
  canvas.drawCircle(cx, cy, R, CL_GREY);
  canvas.drawCircle(cx, cy, (R*2)/3, CL_GREY);   // 30 deg
  canvas.drawCircle(cx, cy, R/3, CL_GREY);       // 60 deg
  canvas.drawPixel(cx, cy, CL_WHITE);
  canvas.drawLine(cx, cy - R, cx, cy + R, CL_GREY);
  canvas.drawLine(cx - R, cy, cx + R, cy, CL_GREY);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(cx - 2,     cy - R - 9); canvas.print("N");
  canvas.setCursor(cx - 2,     cy + R + 2); canvas.print("S");
  canvas.setCursor(cx + R + 2, cy - 3);     canvas.print("E");
  canvas.setCursor(cx - R - 8, cy - 3);     canvas.print("W");
}

// Draw a satellite ground-track arc (az/el samples) on the polar plot, with AOS
// (green) and LOS (orange) markers and a white arrowhead showing travel direction.
void App::drawPolarArc(int cx, int cy, int R, const float* az, const float* el, int n) {
  auto XY = [&](int i, int& x, int& y) {
    double e = el[i]; if (e > 90) e = 90;
    double rr = R * (90.0 - e) / 90.0;
    double a  = az[i] * (M_PI / 180.0);
    x = cx + (int)lround(rr * sin(a));
    y = cy - (int)lround(rr * cos(a));
  };
  int prevx = -1, prevy = -1, firstI = -1, lastI = -1;
  for (int i = 0; i < n; ++i) {
    if (el[i] < 0) { prevx = -1; continue; }
    int px, py; XY(i, px, py);
    if (prevx >= 0) canvas.drawLine(prevx, prevy, px, py, CL_CYAN);
    prevx = px; prevy = py;
    if (firstI < 0) firstI = i;
    lastI = i;
  }
  if (firstI < 0) return;
  int ax, ay, lx, ly; XY(firstI, ax, ay); XY(lastI, lx, ly);
  canvas.drawCircle(ax, ay, 3, CL_GREEN);
  canvas.setTextColor(CL_GREEN, CL_BLACK);  canvas.setCursor(ax + 4, ay - 3); canvas.print("A");
  canvas.fillCircle(lx, ly, 2, CL_ORANGE);
  canvas.setTextColor(CL_ORANGE, CL_BLACK); canvas.setCursor(lx + 4, ly - 3); canvas.print("L");
  // Arrowhead at the middle of the visible arc, pointing along travel.
  int m = (firstI + lastI) / 2, m2 = m + 1; if (m2 > lastI) m2 = lastI;
  if (m2 > m) {
    int mx, my, nx, ny; XY(m, mx, my); XY(m2, nx, ny);
    double dx = nx - mx, dy = ny - my, dl = sqrt(dx*dx + dy*dy);
    if (dl > 0.5) {
      dx /= dl; dy /= dl;
      double ex = -dy, ey = dx;                       // perpendicular
      int tx = mx + (int)lround(6*dx),  ty = my + (int)lround(6*dy);
      int b1x = mx + (int)lround(-2*dx + 3*ex), b1y = my + (int)lround(-2*dy + 3*ey);
      int b2x = mx + (int)lround(-2*dx - 3*ex), b2y = my + (int)lround(-2*dy - 3*ey);
      canvas.fillTriangle(tx, ty, b1x, b1y, b2x, b2y, CL_WHITE);
    }
  }
}

// Sample the current pass (or the next one, if not currently up) for the live
// polar arc. Rebuilt on entry and whenever the cached pass has ended.
void App::buildPolarPath() {
  polarPathValid = false;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet()) return;
  pred.setSite(loc.obs());
  if (!pred.setSat(*s)) return;
  time_t now = nowUtc();
  PassPredict pp[3];
  int np = pred.predictPasses(now - 1800, 0.5f, pp, 3);   // include an in-progress pass
  PassPredict* use = nullptr;
  for (int i = 0; i < np; ++i) if (pp[i].los > now) { use = &pp[i]; break; }
  if (!use) return;
  polarPass = *use;
  double span = (double)(use->los - use->aos); if (span < 1) span = 1;
  for (int i = 0; i < POLAR_PTS; ++i) {
    time_t t = use->aos + (time_t)llround(span * i / (double)(POLAR_PTS - 1));
    double az, el; pred.azelAt(t, az, el);
    polarAz[i] = (float)az; polarEl[i] = (float)el;
  }
  polarPathValid = true;
}

void App::drawPolar() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Polar"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }

  const int cx = 66, cy = 78, R = 50;   // plot centre + outer (horizon) radius

  // (Re)build the ground-track arc on entry or when the cached pass has ended.
  if (timeIsSet() && (!polarPathValid || nowUtc() > polarPass.los)) buildPolarPath();

  drawPolarGrid(cx, cy, R);
  if (polarPathValid) drawPolarArc(cx, cy, R, polarAz, polarEl, POLAR_PTS);

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  if (timeIsSet() && L.el > 0) {
    double rr = R * (90.0 - L.el) / 90.0;       // radius shrinks toward zenith
    double a  = L.az * (M_PI / 180.0);
    int px = cx + (int)lround(rr * sin(a));
    int py = cy - (int)lround(rr * cos(a));
    canvas.drawLine(cx, cy, px, py, CL_DGREEN);
    canvas.fillCircle(px, py, 3, CL_GREEN);
  }

  // Sun glyph (only when the Sun is above the horizon).
  if (timeIsSet() && L.sunEl > 0) {
    double rr = R * (90.0 - L.sunEl) / 90.0;
    double a  = L.sunAz * (M_PI / 180.0);
    int px = cx + (int)lround(rr * sin(a));
    int py = cy - (int)lround(rr * cos(a));
    canvas.fillCircle(px, py, 2, CL_YELLOW);
    canvas.drawCircle(px, py, 4, CL_YELLOW);
  }

  // Right-hand readout.
  int rx = 128;
  canvas.setTextColor(L.visible ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(rx, 22);
  if (L.visible)             canvas.print("VISIBLE");
  else if (polarPathValid)   canvas.printf("AOS %s", fmtHM(polarPass.aos).c_str());
  else                       canvas.print("below horizon");
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 40); canvas.printf("Az  %5.1f", L.az);
  canvas.setCursor(rx, 52); canvas.printf("El  %5.1f", L.el);
  canvas.setCursor(rx, 64); canvas.printf("Rng %.0f km", L.rangeKm);
  canvas.setCursor(rx, 76); canvas.printf("%s %.3f km/s",
                  L.rangeRate >= 0 ? "away" : "appr", fabs(L.rangeRate));
  if (timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(rx, 88); canvas.printf("Sun %03.0f/%+.0f", L.sunAz, L.sunEl);
    canvas.setTextColor(L.sunlit ? CL_GREEN : CL_ORANGE, CL_BLACK);
    canvas.setCursor(rx, 100); canvas.print(L.sunlit ? "sat SUNLIT" : "sat ECLIPSE");
  } else {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(rx, 96); canvas.print("clock not set");
  }
  footer("p / ENT / ` back to track");
}

void App::drawLocation() {
  header("Location");
  canvas.setTextSize(1);
  const Observer& o = loc.obs();
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 22); canvas.printf("Lat: %.5f", o.lat);
  canvas.setCursor(6, 34); canvas.printf("Lon: %.5f", o.lon);
  canvas.setCursor(6, 46); canvas.printf("Alt: %.0f m", o.altM);
  canvas.setCursor(6, 58); canvas.printf("Grid: %s",
       (o.valid ? Location::toGrid(o.lat, o.lon).c_str() : "----"));
  canvas.setTextColor(cfg.useGps ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(6, 74);
  canvas.printf("GPS: %s  fix:%s sats:%d", cfg.useGps ? "on" : "off",
                loc.gpsHasFix() ? "Y" : "N", loc.gpsSats());
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(6, 86);
  canvas.printf("Src: %s", GPS_PROFILES[cfg.gpsSource % GPS_SRC_COUNT].name);
  footer("e/o/a g grid p gps s src c clk `bk");
}

void App::drawUpdate() {
  header("Update");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 24); canvas.print("k / ENT : download GP (AMSAT)");
  canvas.setCursor(6, 38); canvas.print("a       : cache ALL transponders");
  canvas.setCursor(6, 52); canvas.print("w       : connect WiFi only");
  canvas.setCursor(6, 70);
  canvas.printf("Sats in memory: %d", db.count());
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 84); canvas.print("'a' downloads every sat's TX so");
  canvas.setCursor(6, 94); canvas.print("the unit works fully offline.");
  footer("` back");
}

void App::drawSettings() {
  header("Settings");
  canvas.setTextSize(1);
  const int N = 16;
  String rows[N];
  rows[0]  = String("Radio: ") + RADIOS[cfg.radioModel].name;
  rows[1]  = String("CI-V addr: ") + String(cfg.civAddr, HEX);
  rows[2]  = String("CAT baud: ") + String(cfg.civBaud);
  rows[3]  = String("Min pass el: ") + String((int)cfg.minPassEl) + " deg";
  rows[4]  = String("WiFi SSID: ") + cfg.ssid;
  rows[5]  = String("WiFi pass: ") + String(strlen(cfg.pass) ? "******" : "(none)");
  rows[6]  = String("Save & test WiFi");
  rows[7]  = String("AOS alarm: ") + (cfg.aosAlarm ? "on" : "off");
  rows[8]  = String("Rotator: ") + (cfg.rotEnable ? "on" : "off");
  rows[9]  = String("Rot baud: ") + String(cfg.rotBaud);
  rows[10] = String("Rot deadband: ") + String(cfg.rotDeadband) + " deg";
  rows[11] = String("Rot park az: ") + String(cfg.rotParkAz) + " deg";
  rows[12] = String("Rot Az offset: ") + String(cfg.rotAzOff) + " deg";
  rows[13] = String("Rot El offset: ") + String(cfg.rotElOff) + " deg";
  {
    String u = cfg.gpUrl;                    // show a trimmed tail so it fits
    if (u.length() > 28) u = "..." + u.substring(u.length() - 25);
    rows[14] = String("GP URL: ") + u;
  }
  rows[15] = String("Reset all data (erase)");
  const int VIS = 9;
  int scroll = (setSel >= VIS) ? (setSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < N; ++v) {
    int i = scroll + v;
    int y = 19 + v*11;
    bool danger = (i == N - 1);
    if (i == setSel) { canvas.fillRect(0, y-1, 240, 11, danger ? CL_RED : CL_GREEN);
                       canvas.setTextColor(CL_BLACK, danger ? CL_RED : CL_GREEN); }
    else               canvas.setTextColor(danger ? CL_RED : CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y); canvas.print(rows[i]);
  }
  if (setSel == 4) footer(",/ change  ENT edit  s scan  ` back");
  else             footer(",/ change  ENT edit  ` back");
}

void App::startWifiScan() {
  setStatus("Scanning WiFi...");
  draw();                                   // show the notice before the blocking scan
  wifiApCount = net.scanWifi(wifiAp, MAX_WIFI_AP);
  wifiSel = 0;
  screen = SCR_WIFISCAN;
  if (wifiApCount > 0)       setStatus(String(wifiApCount) + " network(s)");
  else if (wifiApCount == 0) setStatus("No networks found");
  else { wifiApCount = 0;    setStatus("Scan failed"); }
}

void App::keyWifiScan(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_SETTINGS; return; }
  if (c == 'r') { startWifiScan(); return; }            // rescan
  if (wifiApCount <= 0) return;
  if (isUp(c))   wifiSel = (wifiSel + wifiApCount - 1) % wifiApCount;
  if (isDown(c)) wifiSel = (wifiSel + 1) % wifiApCount;
  if (enter) {
    strncpy(cfg.ssid, wifiAp[wifiSel].ssid, sizeof(cfg.ssid) - 1);
    cfg.ssid[sizeof(cfg.ssid) - 1] = 0;
    cfg.save();
    if (wifiAp[wifiSel].enc) {                  // secured -> ask for the password
      editTarget = 202;
      editTitle  = String("Password: ") + cfg.ssid;
      editBuf    = "";
      screen     = SCR_EDIT;
    } else {                                    // open network -> no password
      cfg.pass[0] = 0; cfg.save();
      setStatus(String("Selected ") + cfg.ssid);
      screen = SCR_SETTINGS;
    }
  }
}

void App::drawWifiScan() {
  header("WiFi scan");
  canvas.setTextSize(1);
  if (wifiApCount <= 0) {
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 40);
    canvas.print("No networks found.");
    footer("r rescan  ` back");
    return;
  }
  const int VIS = 9;
  int scroll = (wifiSel >= VIS) ? (wifiSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < wifiApCount; ++v) {
    int i = scroll + v;
    int y = 19 + v * 11;
    if (i == wifiSel) { canvas.fillRect(0, y - 1, 240, 11, CL_GREEN);
                        canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%-22.22s %4ddBm%s", wifiAp[i].ssid, (int)wifiAp[i].rssi,
                  wifiAp[i].enc ? " *" : "");
  }
  footer("ENT use  r rescan  ` back  *=secured");
}

void App::drawEdit() {
  header("Edit");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(6, 30); canvas.print(editTitle);
  canvas.drawRect(6, 46, 228, 18, CL_WHITE);
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(10, 51); canvas.print(editBuf + "_");
  footer("type  DEL bksp  ENT ok  ` cancel");
}
