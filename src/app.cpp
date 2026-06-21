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
#include <esp_heap_caps.h>   // heap_caps_get_largest_free_block() for the TLS sprite-free

// 8bpp palette indices. The display canvas is an 8bpp (palette) sprite -- on a
// palette sprite M5GFX/LGFX uses the value passed to fill/draw as a PALETTE
// INDEX, not as a 16-bit colour (a 16-bit colour would be truncated to a bogus
// index). So the CL_* names below are indices into CL_PALETTE[], and every
// existing canvas.fill/draw(..., CL_x) call passes the right index unchanged.
// CL_PALETTE[] holds the real RGB565 colours, installed via createPalette() at
// startup. Indices 11/12 are the dim greys used for grid/axis lines that were
// previously raw 565 literals (0x2104/0x18E3 -> CL_DGREY, 0x4208 -> CL_MGREY).
enum : uint8_t {
  CL_BLACK = 0, CL_WHITE = 1, CL_GREEN = 2, CL_RED = 3,
  CL_YELLOW = 4, CL_CYAN = 5, CL_ORANGE = 6, CL_GREY = 7,
  CL_BLUE = 8, CL_DGREEN = 9, CL_SELBG = 10, CL_DGREY = 11, CL_MGREY = 12
};
// Parallel table of the real 16-bit 565 colours, indexed by the CL_* values.
// CL_SELBG: a calmer medium forest green for the selection bar (pure green
// glares and blooms thin glyphs with black text). Order MUST match the enum.
static const uint16_t CL_PALETTE[] = {
  /*0 BLACK */ 0x0000, /*1 WHITE */ 0xFFFF, /*2 GREEN */ 0x07E0, /*3 RED   */ 0xF800,
  /*4 YELLOW*/ 0xFFE0, /*5 CYAN  */ 0x07FF, /*6 ORANGE*/ 0xFD20, /*7 GREY  */ 0x7BEF,
  /*8 BLUE  */ 0x041F, /*9 DGREEN*/ 0x0320, /*10 SELBG*/ 0x05C0, /*11 DGREY*/ 0x2104,
  /*12 MGREY*/ 0x4208,
};
static const uint32_t CL_PALETTE_N = sizeof(CL_PALETTE) / sizeof(CL_PALETTE[0]);

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
// Format a frequency in MHz, dropping decimal places as the integer part grows
// so the string stays bounded for any band up to 99999 MHz. maxDigits caps the
// fractional places for low bands; higher bands shed decimals to keep width sane:
//   < 1000 MHz : maxDigits decimals (e.g. 145.99700)
//   < 10000    : 3 decimals        (e.g. 1296.500)
//   < 100000   : 1 decimal         (e.g. 10489.5 ... 99999.5)
static String fmtMHzN(uint32_t hz, int maxDigits) {
  double m = hz / 1e6;
  int dp = (m < 1000.0) ? maxDigits : (m < 10000.0) ? 3 : 1;
  char b[20]; snprintf(b, sizeof(b), "%.*f", dp, m); return String(b);
}
static String fmtMHz(uint32_t hz) { return fmtMHzN(hz, 5); }

// Escape a string for safe inclusion inside a JSON double-quoted value. Sat and
// transponder names come straight from catalog/SatNOGS data and can contain
// quotes, backslashes or stray control bytes; without escaping, one bad name
// produces malformed JSON and breaks the whole web page.
static String jsonEsc(const char* s) {
  String o;
  if (!s) return o;
  for (const char* p = s; *p; ++p) {
    unsigned char c = (unsigned char)*p;
    switch (c) {
      case '"':  o += "\\\""; break;
      case '\\': o += "\\\\"; break;
      case '\n': o += "\\n";  break;
      case '\r': o += "\\r";  break;
      case '\t': o += "\\t";  break;
      default:
        if (c < 0x20) { char b[8]; snprintf(b, sizeof(b), "\\u%04x", c); o += b; }
        else o += (char)c;
    }
  }
  return o;
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
// Copy a file within the active filesystem (config/favorites backup-restore).
static bool copyFile(const char* from, const char* to) {
  File in = Store::fs().open(from, "r");
  if (!in) return false;
  File out = Store::fs().open(to, "w");
  if (!out) { in.close(); return false; }
  uint8_t buf[256];
  while (in.available()) { size_t n = in.read(buf, sizeof(buf)); out.write(buf, n); }
  in.close(); out.close();
  return true;
}

void App::setup() {
  s_self = this;
  Net::onTlsBusy = &App::tlsBusyTrampoline;   // free LAN sockets around every fetch
  auto m5cfg = M5.config();
  m5cfg.internal_mic = true;        // wire the ADV ES8311 mic-enable path (voice memo)
  M5Cardputer.begin(m5cfg, true);   // true => init keyboard
  // Cardputer ADV has a BMI270 IMU; the original Cardputer has none. begin()
  // is a no-op when absent, and isEnabled() then reports false, so tilt tuning
  // silently does nothing on a non-ADV board.
  M5.Imu.begin();
  imuReady = M5.Imu.isEnabled();
  Serial.begin(115200);             // diagnostics on the USB serial monitor
  M5Cardputer.Display.setRotation(1);
  // 8bpp palette canvas: 32 KB sprite (vs 64 KB at 16bpp). The smaller footprint
  // leaves ~85 KB free for the mbedTLS handshake with the sprite KEPT allocated,
  // so we never delete/recreate it (the 16bpp 64 KB block never returns on IDF
  // 5.4.x, which froze the screen). Colours come from CL_PALETTE via the index
  // scheme (see the CL_* enum); createPalette installs them.
  canvas.setColorDepth(8);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.createPalette(CL_PALETTE, CL_PALETTE_N);
  canvas.setTextWrap(false);
  M5Cardputer.Display.setBrightness(SCREEN_BRIGHT);

  setenv("TZ", "UTC0", 1); tzset();   // work entirely in UTC

  db.begin();
  if (!Store::ready())
    setStatus("No filesystem! Allocate SPIFFS or insert SD.", 8000);
  else if (Store::onSD())
    setStatus("Using SD card for storage", 4000);
  if (!cfg.load()) { cfg.save(); }     // first boot: write defaults
  M5Cardputer.Display.setBrightness(cfg.bright);   // apply the saved brightness
  calDl = cfg.calDlHz; calUl = cfg.calUlHz;

  applyRadioFromCfg();
  applyRotatorFromCfg();

  // Location
  if (cfg.useGps) startGps();
  if (cfg.lat != 0.0 || cfg.lon != 0.0) loc.setManual(cfg.lat, cfg.lon, cfg.altM);
  pred.setSite(loc.obs());

  // If a WiFi network is configured, connect at boot and set the clock over NTP.
  // Best-effort: a failure is non-fatal (GPS or a cached/manual clock still work).
  if (cfg.ssid[0]) {
    setStatus("WiFi..."); draw();
    if (connectWifiCfg()) {
      Serial.printf("[boot] WiFi OK, IP %s\n", WiFi.localIP().toString().c_str());
      net.syncTimeNtp();
      setStatus(timeIsSet() ? "WiFi connected; clock set via NTP"
                            : "WiFi connected (NTP pending)", 3000);
    } else {
      Serial.println("[boot] WiFi connect failed");
      setStatus("WiFi connect failed at boot", 3000);
    }
  }

  // Try cached GP data so the unit is useful offline at boot.
  if (db.loadGpFromFs()) setStatus("Loaded cached GP: " + String(db.count()));
  else setStatus("No GP data yet. Use Update.");
  db.loadManualGpFile();    // merge any hand-entered satellites
  loadSpaceWeather();       // restore cached F10.7 for the decay estimate
  loadWeatherCache();       // restore cached terrestrial weather for offline view
  // Remove any leftover streaming scratch files (from older builds, or a fetch
  // interrupted by reset) so they don't waste space on the small filesystem.
  if (Store::fs().exists(FILE_SPACEWX_TMP)) Store::fs().remove(FILE_SPACEWX_TMP);
  if (Store::fs().exists(FILE_WEATHER_TMP)) Store::fs().remove(FILE_WEATHER_TMP);
  if (Store::fs().exists(FILE_DL_TMP))      Store::fs().remove(FILE_DL_TMP);
  loadFavs();
  buildSatView();
  db.applyAmsatStatusFile(FILE_AMSTAT);   // restore cached AMSAT activity marks

  // Auto-refresh: if we're online with a valid clock and even the freshest cached
  // element set is over a week old, pull fresh GP now so passes/Doppler stay sharp.
  if (net.connected() && timeIsSet() && db.count() > 0) {
    double minAge = 1e9;
    for (int i = 0; i < db.count(); ++i) {
      double a = gpAgeDays(db.at(i));
      if (a < minAge) minAge = a;
    }
    if (minAge > GP_STALE_DAYS) {
      setStatus("Elements stale - refreshing GP...", 2500); draw();
      doUpdateGp();
    }
  }

  M5Cardputer.Speaker.setVolume(180);   // AOS alarm
  irBeacon.begin();                     // IR pass-alert beacon (idle until an alert)
  if (cfg.loraEnable) loraStart();      // LoRa messaging radio (no-op without RadioLib)
  if (timeIsSet() && favN) buildSchedule();

  // If we woke from the deep-sleep-until-pass timer, jump to the schedule so
  // the imminent pass is front and centre (the AOS alarm will sound shortly).
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    if (favN && timeIsSet()) { buildSchedule(); schedSel = 0; screen = SCR_SCHEDULE; }
  }

  lastInputMs = millis();
  draw();

  // If a batched "cache all transponders" run is in progress, continue it now
  // (caches the next batch and reboots, until every sat is done).
  resumeCacheIfPending();
}

void App::applyRadioFromCfg() {
  RadioModel m = (RadioModel)cfg.radioModel;
  uint32_t baud = cfg.civBaud ? cfg.civBaud : RADIOS[m].defaultBaud;
  if (rig) { delete rig; rig = nullptr; }
  rig = makeRig(m, cfg.catType, cfg.catHost, cfg.catPort, cfg.catUser, cfg.catPass);
  if (!rig) return;
  rig->begin(baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN);   // net backend ignores UART args
  if (RADIOS[m].proto == PROTO_CIV)
    rig->setAddress(cfg.civAddr ? cfg.civAddr : RADIOS[m].civAddr);
  rig->setCmdDelay(cfg.catDelayMs);                  // CAT Delay: inter-command pause
  // The rig's satellite mode is no longer forced here -- it is commanded per the
  // Sat Mode setting when radio control is engaged (see keyTrack, 'r' key).
}

void App::applyRotatorFromCfg() {
  if (rot) { delete rot; rot = nullptr; }
  rotOut = false; rotParked = false;
  lastAzCmd = lastElCmd = lastUnwrappedAz = -999.0f;
  rotPassValid = false; rotPassNorad = 0;
  rotFlipPass = false; rotFlipUntil = 0;
  if (!cfg.rotEnable) return;          // rotator disabled in Settings
  if (cfg.rotType == ROT_YAESU) {      // direct Yaesu: built here (needs calibration)
    int azFull = (cfg.rotAzRange == ROT_AZ_450) ? 450 : 360;
    rot = new YaesuRotator(azFull, cfg.rotAzCnt0, cfg.rotAzCntF,
                           cfg.rotElCnt0, cfg.rotElCntF, cfg.rotDeadband);
  } else {
    rot = makeRotator(cfg.rotType, cfg.rotBaud, cfg.rotHost, cfg.rotPort);
  }
  if (rot) rot->begin();
}

// Decide whether a pass should be tracked "flipped" (elevation 0-180, azimuth
// +180) so the rapid azimuth swing of a high/overhead pass is moved onto the
// faster elevation axis instead of forcing a ~360 deg slew across the rotator's
// azimuth stop. Mirrors Gpredict's is_flipped_pass: sample the az track across
// the pass, normalise into the configured az window, and flip if it jumps more
// than 180 deg between samples. Only meaningful for 0-180 deg elevation rotators
// (rotFlip); the 450 deg overlap range avoids the same discontinuity differently.
bool App::passNeedsFlip(time_t aos, time_t los) {
  if (!cfg.rotFlip || cfg.rotAzRange == ROT_AZ_450) return false;
  if (los <= aos) return false;
  double minAz = (cfg.rotAzRange == ROT_AZ_180) ? -180.0 : 0.0;
  double maxAz = minAz + 360.0;
  double prev = 0; bool have = false;
  for (int i = 0; i <= 24; ++i) {
    time_t t = aos + (time_t)((double)(los - aos) * i / 24.0);
    double az, el;
    if (!pred.azelAt(t, az, el)) continue;
    double a = az + (double)cfg.rotAzOff;
    while (a >= maxAz) a -= 360.0; while (a < minAz) a += 360.0;
    if (have && fabs(a - prev) > 180.0) return true;
    prev = a; have = true;
  }
  return false;
}

// Send a commanded bearing to the active rotator, applying the configured
// azimuth-range convention. CardSat computes az as 0-360 (0=North); for a
// rotator whose azimuth axis is centred on North and runs -180..+180, re-express
// the bearing in that range (e.g. 270 -> -90). GS-232 controllers are natively
// 0-360 and re-wrap negatives, so in practice this only changes rotctld output.
void App::rotPoint(float az, float el) {
  if (!rot) return;
  while (az >= 360.0f) az -= 360.0f;     // target bearing in [0,360)
  while (az < 0.0f)    az += 360.0f;
  if (cfg.rotAzRange == ROT_AZ_180) {
    if (az > 180.0f) az -= 360.0f;        // centre on North: [-180,+180]
  } else if (cfg.rotAzRange == ROT_AZ_450) {
    // 90 deg overlap: a bearing <=90 is also reachable as +360 (360..450 region).
    // Pick whichever representation is nearer the last commanded position, so a
    // pass crossing North continues up into the overlap instead of unwinding 360.
    // The lookahead (rotAz450PreCommit, set in the track tick when a north wrap is
    // imminent) forces the +360 branch early, before the reactive test would.
    if (az + 360.0f <= 450.0f &&
        (rotAz450PreCommit ||
         (az <= 90.0f && lastUnwrappedAz > -500.0f &&
          fabsf((az + 360.0f) - lastUnwrappedAz) < fabsf(az - lastUnwrappedAz))))
      az += 360.0f;
  }
  lastUnwrappedAz = az;
  rotAz450PreCommit = false;   // single-shot: never carries to a later call (park, pre-pos, Sun/Moon, manual)
  rot->point(az, el);
}

// Choose the SSB/FM mode for each leg.
//   Linear transponders default to USB downlink + LSB uplink: an inverting
//   linear transponder flips the spectrum, so an LSB uplink is heard as a
//   normal USB downlink. Exception: if either leg is HF (< 30 MHz) the
//   convention is USB up and USB down. FM / single-channel birds use the
//   transponder's own mode on both legs.
void App::applyTransponderModes(const Transponder& t) {
  if (!rig || !rig->ready()) return;
  // Set the UPLINK (MAIN) leg first, then the DOWNLINK (SUB) leg, so the radio is
  // left selected on the downlink band -- that is where the operator's RX knob
  // sits and where the One-True-Rule read-back happens. (On the IC-821 the SUB
  // band must be the last one selected before a read, so ending here helps.)
  if (t.isLinear) {
    bool hf = (t.uplink   && t.uplink   < 30000000UL) ||
              (t.downlink && t.downlink < 30000000UL);
    if (t.uplink) rigSetUplinkMode(hf ? RM_USB : RM_LSB);     // uplink: LSB (USB if HF)
    rigSetDownlinkMode(RM_USB);                               // downlink: USB (selected last)
  } else {
    RigMode m = Rig::modeFromString(t.mode);
    if (t.uplink) rigSetUplinkMode(m);
    rigSetDownlinkMode(m);
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
  rigSelectUplink();                  // the PL tone lives on the uplink VFO
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

// Toggle a voice memo on/off (shared by the Track-family screens' 'v' key).
// SD-card required; the recorder streams a WAV to /CardSat/audio and keeps the
// radio/rotator/web services running by recording one small block per loop tick.
void App::toggleMemo() {
  if (memo.isRecording()) {
    memo.stop();
    setStatus("Memo saved");
  } else {
    SatEntry* s = activeSat();
    if (memo.start(s ? s->name : nullptr))
      setStatus("Recording memo...");
    else
      setStatus(String("Memo: ") + memo.lastError());   // e.g. "SD card required"
  }
  lastDrawMs = 0;
}

// Small red "REC ●" badge with the remaining seconds, drawn top-right while a memo
// records. Called at the end of each Track-family draw so it overlays the content.
void App::drawMemoIndicator() {
  if (!memo.isRecording()) return;
  canvas.setTextSize(1);
  // Blink the dot ~1 Hz so it reads as live.
  bool on = ((millis() / 500) & 1);
  int x = 188, y = 2;
  canvas.fillRect(x - 2, y - 1, 52, 11, CL_BLACK);
  if (on) canvas.fillCircle(x + 3, y + 4, 3, CL_RED);
  canvas.setTextColor(CL_RED, CL_BLACK);
  canvas.setCursor(x + 10, y + 1);
  canvas.printf("REC%lus", (unsigned long)memo.secondsLeft());
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

// Move two-way transponders (both an uplink and a downlink) to the front of the
// list, preserving the relative order within each group. SatNOGS lists beacons
// and telemetry downlinks too (the ISS has ~49 entries), so this puts the
// workable repeaters/transponders first on the Track screen. Stable, in place.
static void prioritizeTwoWay(Transponder* tx, int n) {
  int dst = 0;
  for (int i = 0; i < n; ++i) {
    if (tx[i].uplink && tx[i].downlink) {
      if (i != dst) {                            // rotate tx[dst..i] right by one
        Transponder t = tx[i];
        for (int j = i; j > dst; --j) tx[j] = tx[j - 1];
        tx[dst] = t;
      }
      dst++;
    }
  }
}

bool App::ensureTransponders(SatEntry& s) {
  activeTxCount = 0; curTx = 0;
  // 1) try LittleFS cache
  activeTxCount = SatDb::loadTxCache(s.norad, activeTx, MAX_TX_PER_SAT);
  // 2) try network if nothing cached
  if (activeTxCount == 0 && net.connected()) {
    // Stream the response straight to the cache file, then parse it from there.
    // The old in-RAM String path silently dropped chunks when the heap couldn't
    // grow the String under TLS pressure, corrupting large lists (e.g. the ISS).
    if (net.fetchSatnogsTransmittersToFile(s.norad, SatDb::txCachePath(s.norad).c_str()))
      activeTxCount = SatDb::loadTxCache(s.norad, activeTx, MAX_TX_PER_SAT);
  }
  // 3) always append any manually-entered transponders for this sat
  if (activeTxCount < MAX_TX_PER_SAT)
    activeTxCount += loadManualTx(s.norad, activeTx + activeTxCount,
                                  MAX_TX_PER_SAT - activeTxCount);
  // 3b) bring two-way transponders to the front so they're easy to reach
  prioritizeTwoWay(activeTx, activeTxCount);
  // 4) tag FM uplinks with the effective PL/CTCSS tone: the user's per-satellite
  //    override if one is set, otherwise the built-in table (SatNOGS has none).
  retagTones(s.norad);
  s.txLoaded = (activeTxCount > 0);
  return s.txLoaded;
}

// Recenter the passband tuning to mid-band and choose a sensible default
// track-screen mode for the currently selected transponder.
void App::onTransponderChanged() {
  tuneMode = TM_HOLD; lastRxSet = 0; lastUlHz = 0;   // start each channel holding both legs
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

// Accelerometer (tilt) passband tuning -- opt-in (cfg.tiltTune), ADV-only.
// Concept: roll the Cardputer left/right to nudge the transponder passband
// position, with tilt *angle* setting the tune RATE (gentle = fine, more = fast)
// rather than an absolute mapping, which is far steadier to hold by hand. Only
// active on Track/Big in TUNE mode on a linear bird; everything else no-ops.
// Called once per UI cycle (~tens of ms); it integrates a Hz/s rate so the step
// is independent of the exact call cadence.
// Connect to WiFi, trying the primary network first and then the optional
// secondary one (for field use on a second router or a phone hotspot). Returns
// true on the first network that associates. A blank SSID is skipped.
bool App::connectWifiCfg(uint32_t timeoutMs) {
  if (cfg.ssid[0] && net.connect(cfg.ssid, cfg.pass, timeoutMs)) return true;
  if (cfg.ssid2[0] && net.connect(cfg.ssid2, cfg.pass2, timeoutMs)) return true;
  return false;
}

void App::serviceTiltTune() {
  if (!cfg.tiltTune || !imuReady) return;
  bool onTrack  = (screen == SCR_TRACK || screen == SCR_BIG);
  bool onManual = (screen == SCR_MANUAL || screen == SCR_MANUALBIG);
  if (!onTrack && !onManual) return;
  if (trackMode != 0) return;                            // TUNE mode only
  // On Track/Big the FULL/DL modes are driven by the rig knob, so tilt must not
  // fight them. The Manual screen has no radio, so this guard doesn't apply there.
  if (onTrack && (tuneMode == TM_FULL || tuneMode == TM_DL)) return;
  if (activeTxCount <= 0) return;
  Transponder& t = activeTx[curTx];
  if (!t.isLinear || t.bandwidth() == 0) return;

  float ax = 0, ay = 0, az = 0;
  if (!M5.Imu.getAccel(&ax, &ay, &az)) return;           // g units

  // The Cardputer is used in landscape; roll about its long axis moves one
  // horizontal accel component through ~±1 g. Use that as the tilt signal, with
  // a dead-zone around level so a hand-held device doesn't creep, and a cap so
  // past ~35 deg the rate saturates. A light low-pass tames sensor noise.
  static float filt = 0;
  filt += 0.30f * (ax - filt);                           // 1-pole LPF
  float mag = fabsf(filt);
  const float DEAD = 0.12f;                               // ~7 deg dead-zone
  const float FULL = 0.58f;                               // ~35 deg -> max rate
  if (mag <= DEAD) { tiltAccum = 0; return; }
  float norm = (mag - DEAD) / (FULL - DEAD);
  if (norm > 1.0f) norm = 1.0f;

  // Rate curve: square the normalized tilt for fine control near level, scaled to
  // a sensible max slew. 8 kHz/s at full tilt crosses a 50 kHz passband in ~6 s.
  uint32_t dtMs = millis() - lastTiltMs; lastTiltMs = millis();
  if (dtMs > 250) dtMs = 250;                             // ignore long gaps (just switched in)
  float rateHz = 8000.0f * norm * norm;                  // Hz per second
  float deltaF = rateHz * (dtMs / 1000.0f);
  tiltAccum += (filt < 0 ? -deltaF : deltaF);            // sign = tilt direction

  int32_t whole = (int32_t)tiltAccum;
  if (whole == 0) return;
  tiltAccum -= whole;
  int32_t bw = (int32_t)t.bandwidth();
  pbOffset += whole;
  if (pbOffset < 0)  pbOffset = 0;
  if (pbOffset > bw) pbOffset = bw;
}

// ===========================================================================
//  GP element update (download AMSAT GP/OMM JSON)
// ===========================================================================

// Human-readable label for whatever GP source is currently configured in
// cfg.gpUrl: "AMSAT", a CelesTrak category ("CT:amateur"), or "Custom".
String App::gpSourceLabel() {
  String u = cfg.gpUrl;
  if (u == AMSAT_GP_URL) return "AMSAT";
  if (u.indexOf("celestrak") >= 0) {
    int g = u.indexOf("GROUP="), sp = u.indexOf("SPECIAL=");
    if (g >= 0)       { int e = u.indexOf('&', g);  return "CT:" + u.substring(g + 6, e < 0 ? u.length() : e); }
    if (sp >= 0)      { int e = u.indexOf('&', sp); return "CT:" + u.substring(sp + 8, e < 0 ? u.length() : e); }
    return "CelesTrak";
  }
  return "Custom";
}

void App::doUpdateGp() {
  setStatus("WiFi..."); draw();
  if (!net.connected() && !connectWifiCfg()) {
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
  fetchAmsatStatus();                  // tag active/not-heard from AMSAT status
                                       // (needs the loaded catalog)
  fetchSpaceWeather();                 // F10.7 flux + Kp/A from NOAA SWPC
  fetchWeather();                      // local surface weather (open-meteo)
  nextAos = 0; lastSchedMs = 0;        // force schedule/alarm to recompute
  setStatus("GP OK: " + String(n) + " sats");
  draw();                              // paint the final status (canvas is restored)
}

// Fast update: refresh orbital elements (GP), the AMSAT activity marks (a single
// bulk summary fetch, so it's cheap to include), and then transponder data for the
// FAVORITES only -- skipping the space-weather and local-weather fetches a full
// update does. The GP source is a bulk file, so the elements download is the same;
// the speed win is fetching transponders for just the handful of favorites
// (proactively, here) instead of every sat on demand, and skipping the two slower
// extra fetches. With no favorites it falls back to refreshing the active sat.
void App::doFastUpdate() {
  setStatus("WiFi..."); draw();
  if (!net.connected() && !connectWifiCfg()) {
    Serial.println("[fast] WiFi connect failed");
    setStatus("WiFi failed (check SSID/pass)"); return;
  }
  net.syncTimeNtp();
  setStatus("Fast: GP..."); draw();
  if (!net.fetchGpToFile(cfg.gpUrl, FILE_GP)) {
    Serial.printf("[fast] GP download failed: %s\n", net.lastErr.c_str());
    setStatus("GP DL failed: " + net.lastErr); return;
  }
  int n = db.loadGpFromFile(FILE_GP);
  db.loadManualGpFile();
  Serial.printf("[fast] parsed %d satellites\n", n);
  if (n <= 0) { setStatus("Got data but parsed 0 sats"); return; }
  buildSatView();
  fetchAmsatStatus();                  // AMSAT activity marks are a single bulk
                                       // summary fetch, so refresh them too (tags
                                       // the whole catalog in one call); space
                                       // weather + local weather are still skipped.

  // Refresh transponders for the favorites (fetch overwrites the per-sat cache).
  // If there are none, refresh the currently active sat so the action still does
  // something useful.
  int done = 0, fail = 0, total = favN;
  if (total > 0) {
    for (int i = 0; i < favN; ++i) {
      int idx = db.indexOfNorad(favs[i]);
      if (idx < 0) continue;                       // favorite not in this catalog
      SatEntry& s = db.at(idx);
      setStatus("Fast TX " + String(i+1) + "/" + String(total) + ": " + s.name); draw();
      if (net.fetchSatnogsTransmittersToFile(s.norad, SatDb::txCachePath(s.norad).c_str()))
        done++;
      else fail++;
      delay(40);                                   // be gentle on the API
    }
  } else if (activeSat()) {
    SatEntry* s = activeSat();
    setStatus("Fast TX: " + String(s->name)); draw();
    if (net.fetchSatnogsTransmittersToFile(s->norad, SatDb::txCachePath(s->norad).c_str()))
      done++; else fail++;
  }

  // Reload the active sat's transponders from the refreshed cache so the change
  // is visible immediately.
  if (activeSat()) { activeTxCount = 0; ensureTransponders(*activeSat()); }

  nextAos = 0; lastSchedMs = 0;        // force schedule/alarm to recompute
  String msg = "Fast OK: " + String(n) + " sats, " + String(done) + " fav TX";
  if (fail) msg += " (" + String(fail) + " failed)";
  setStatus(msg);
  draw();                              // paint the final status (canvas is restored)
}

// Pull the AMSAT OSCAR status summary and tag each catalog entry as heard
// (active) or not-heard recently. Cached to flash so the marks survive a
// reboot, and refreshed whenever elements are updated. Best-effort: a failure
// leaves the previous marks intact.
// ---------------------------------------------------------------------------
//  Streamed-download value scanners. The space-weather and weather feeds are
//  parsed by finding a few "key":value pairs, so we never need the whole body
//  in RAM (a large contiguous String is exactly what fails on the fragmented
//  no-PSRAM heap). These read the cached file in a small rolling buffer and
//  pull the value(s) out -- the same stream-and-discard discipline GP uses.
// ---------------------------------------------------------------------------

// Read an entire small cache file into a String sized exactly to the file (one
// allocation, no concat-doubling). Returns "" on failure. Use only for bodies
// known to be modest (< ~12 KB) such as the weather forecast; larger feeds use
// scanFileNum() instead so nothing big is ever held at once.
static String readSmallFile(const char* path, size_t cap) {
  String s;
  File f = Store::fs().open(path, "r");
  if (!f) return s;
  size_t n = f.size();
  if (n == 0 || n > cap) { f.close(); return s; }
  s.reserve(n + 1);
  uint8_t buf[256];
  while (f.available()) {
    int r = f.read(buf, sizeof(buf));
    if (r <= 0) break;
    s.concat((const char*)buf, r);
  }
  f.close();
  return s;
}

// Scan a (possibly large) cached file for the numeric value following a key,
// e.g. key="\"flux\":" -> the number after it. `wantLast` returns the value
// after the LAST occurrence (newest record in NOAA feeds that append); else the
// first in-range value. Range [lo,hi] rejects garbage. Returns NAN if none.
// Reads in a small sliding window so the file is never held whole in RAM.
static float scanFileNum(const char* path, const char* key,
                         bool wantLast, float lo, float hi) {
  File f = Store::fs().open(path, "r");
  if (!f) return NAN;
  const size_t KL = strlen(key);
  // Sliding window: keep the last (KL + 32) bytes so a key split across reads
  // is still found, plus room for the number that follows.
  const size_t TAIL = KL + 40;
  String win;                 // small, bounded: <= CHUNK + TAIL
  const size_t CHUNK = 512;
  uint8_t buf[CHUNK];
  float found = NAN;
  while (f.available()) {
    int r = f.read(buf, CHUNK);
    if (r <= 0) break;
    win.concat((const char*)buf, r);
    // scan all key occurrences currently in the window
    int from = 0, idx;
    while ((idx = win.indexOf(key, from)) >= 0) {
      // need some digits after the key+colon to parse safely; if the match is
      // too close to the end, defer it to the next chunk.
      int after = idx + (int)KL;
      if (after + 24 > (int)win.length() && f.available()) break;
      // skip the ":", optional quotes and whitespace between key and number
      const char* p = win.c_str() + after;
      while (*p==':' || *p=='"' || *p==' ' || *p=='\t') p++;
      float v = (float)atof(p);
      if (v >= lo && v <= hi) {
        found = v;
        if (!wantLast) { f.close(); return found; }   // first hit is enough
      }
      from = idx + (int)KL;
    }
    // retain only the tail (so a key spanning the chunk boundary still matches)
    if (win.length() > TAIL) win.remove(0, win.length() - TAIL);
  }
  f.close();
  return found;
}

void App::fetchAmsatStatus() {
  if (!net.connected()) return;
  String url = String(AMSAT_STATUS_URL) + AMSAT_STATUS_HOURS;
  setStatus("AMSAT status..."); draw();
  if (net.httpsGetToFileRetry(url, FILE_AMSTAT, 200000, nullptr, 3))
    db.applyAmsatStatusFile(FILE_AMSTAT);
  else
    Serial.printf("[amsat] status fetch failed: %s\n", net.lastErr.c_str());
}

// Fetch the latest NOAA SWPC 10.7 cm solar radio flux and cache it. Best-effort
// and non-fatal: a failure leaves the previous value (and the manual Decay-solar
// setting) untouched. The feed is a JSON array of records ordered NEWEST FIRST,
// each with a "flux" field (e.g. 1.06e+002 = 106 sfu) and, on the daily Noon
// record, a "ninety_day_mean". The 81-day-ish average tracks thermospheric
// density better than a single day's flux, so we prefer the first ninety_day_mean
// and fall back to the first (newest) flux. Values are validated to a plausible
// range; we scan a bounded slice rather than fully parsing the array.
void App::fetchSpaceWeather() {
  if (!net.connected()) return;
  setStatus("Space weather..."); draw();

  // --- Solar 10.7 cm flux ---
  // f107_cm_flux.json is an array of objects, newest first, e.g.
  //   {"time_tag":..,"flux":1.06e+002,"ninety_day_mean":null,"rec_count":null}
  // We want the *current* flux (the FIRST valid "flux"), NOT the 90-day mean.
  // Stream to a shared temp file and scan it from disk: nothing big is ever held
  // as one contiguous String (the cause of the intermittent parse failures on
  // the fragmented no-PSRAM heap). The temp file is deleted right after parsing
  // so it never competes with the GP cache for the small LittleFS partition.
  if (net.httpsGetToFileRetry(SPACEWX_F107_URL, FILE_DL_TMP, 40000, nullptr, 3, 25000)) {
    float flux = scanFileNum(FILE_DL_TMP, "\"flux\"", /*wantLast=*/false, 50.0f, 400.0f);
    Store::fs().remove(FILE_DL_TMP);
    if (!isnan(flux) && flux > 0) {
      spaceF107 = flux;
      spaceWxEpoch = nowUtc();
      File w = Store::fs().open(FILE_SPACEWX, "w");
      if (w) { w.printf("%.1f %ld\n", spaceF107, (long)spaceWxEpoch); w.close(); }
    } else {
      Serial.println("[wx] flux fetched but no in-range value parsed");
    }
  } else {
    Store::fs().remove(FILE_DL_TMP);
    Serial.printf("[wx] flux fetch failed: code=%d %s\n", net.lastCode, net.lastErr.c_str());
  }

  // --- Planetary Kp + running A index (geomagnetic activity) ---
  // noaa-planetary-k-index.json is an array of OBJECTS, newest LAST, e.g.
  //   {"time_tag":..,"Kp":3.67,"a_running":22,"station_count":8}
  // Bare numbers; newest reading is the LAST object, so take the value after the
  // LAST "Kp" and LAST "a_running". Streamed + scanned from disk like the flux.
  if (net.httpsGetToFileRetry(SPACEWX_KP_URL, FILE_DL_TMP, 80000, nullptr, 3, 25000)) {
    float kp = scanFileNum(FILE_DL_TMP, "\"Kp\"",        /*wantLast=*/true, 0.0f, 9.0f);
    float a  = scanFileNum(FILE_DL_TMP, "\"a_running\"", /*wantLast=*/true, 0.0f, 400.0f);
    Store::fs().remove(FILE_DL_TMP);
    if (!isnan(kp)) spaceKp = kp;
    if (!isnan(a))  spaceA  = a;
    if (isnan(kp) || isnan(a))
      Serial.printf("[wx] Kp parse incomplete: kp=%.2f a=%.0f\n", kp, a);
  } else {
    Store::fs().remove(FILE_DL_TMP);
    Serial.printf("[wx] Kp fetch failed: code=%d %s\n", net.lastCode, net.lastErr.c_str());
  }
  if (spaceWxEpoch == 0 && (spaceKp >= 0 || spaceF107 > 0)) spaceWxEpoch = nowUtc();
}

// Restore the cached F10.7 at boot so the decay estimate is informed offline.
void App::loadSpaceWeather() {
  File f = LittleFS.open(FILE_SPACEWX, "r");
  if (!f) return;
  String line = f.readStringUntil('\n'); f.close();
  float v = (float)atof(line.c_str());
  if (v > 50.0f && v < 400.0f) {
    spaceF107 = v;
    int sp = line.indexOf(' ');
    if (sp >= 0) spaceWxEpoch = (time_t)atol(line.c_str() + sp + 1);
  }
}

// ===========================================================================
//  Terrestrial weather (Open-Meteo) -- current conditions + a short forecast for
//  the operating site, cached for offline use. Free, no API key, non-commercial
//  (https://open-meteo.com). Populated from the same site lat/lon the predictor
//  uses; refreshed with the Update screen and on entry to the Weather screen.
// ===========================================================================

// Degrees -> 16-point compass abbreviation. Empty string for unknown (<0).
const char* App::windDirName(int deg) {
  if (deg < 0) return "";
  static const char* P[] = { "N","NNE","NE","ENE","E","ESE","SE","SSE",
                             "S","SSW","SW","WSW","W","WNW","NW","NNW" };
  int i = (int)((deg % 360) / 22.5f + 0.5f) & 15;
  return P[i];
}

// Convert a cached temperature/wind value (stored in wxCachedUnits) into the
// currently-selected display units, so changing units takes effect immediately
// without needing a re-fetch.
static float wxConvTemp(float v, uint8_t from, uint8_t to) {
  if (v < -900) return v;
  if (from == to) return v;
  bool fromF = (from == WX_IMPERIAL), toF = (to == WX_IMPERIAL);
  if (fromF == toF) return v;                 // both metric variants share deg C
  return fromF ? (v - 32.0f) * 5.0f / 9.0f    // F -> C
               : v * 9.0f / 5.0f + 32.0f;     // C -> F
}
static float wxConvWind(float v, uint8_t from, uint8_t to) {
  if (v < -900 || from == to) return v;
  // normalise to km/h, then to target
  float kmh = (from == WX_IMPERIAL) ? v * 1.609344f
            : (from == WX_METRIC_MS) ? v * 3.6f : v;
  return (to == WX_IMPERIAL) ? kmh / 1.609344f
       : (to == WX_METRIC_MS) ? kmh / 3.6f : kmh;
}

// Map a WMO weather-interpretation code to a short label.
const char* App::wxCodeText(int code) {
  switch (code) {
    case 0:  return "Clear";
    case 1:  return "Mainly clear";
    case 2:  return "Partly cloudy";
    case 3:  return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Frzg drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Hvy showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunder+hail";
    default: return "--";
  }
}

// Unit suffix helpers for the configured units.
const char* App::wxTempUnit() { return cfg.wxUnits == WX_IMPERIAL ? "F" : "C"; }
const char* App::wxWindUnit() {
  return cfg.wxUnits == WX_IMPERIAL ? "mph" : (cfg.wxUnits == WX_METRIC_MS ? "m/s" : "km/h");
}

// Fetch current + WX_FORECAST_DAYS-day forecast for the site. Best-effort: leaves
// the cache intact on any failure. Streams the (small) JSON to flash, parses it,
// then rewrites the cache file.
void App::fetchWeather() {
  if (!net.connected()) return;
  const Observer& o = loc.obs();
  if (!(o.lat != 0.0 || o.lon != 0.0)) return;       // no location set -> nothing to do
  setStatus("Weather..."); draw();

  const char* tu = (cfg.wxUnits == WX_IMPERIAL) ? "fahrenheit" : "celsius";
  const char* wu = (cfg.wxUnits == WX_IMPERIAL) ? "mph"
                 : (cfg.wxUnits == WX_METRIC_MS ? "ms" : "kmh");
  String url = String(WEATHER_API_BASE)
             + "?latitude="  + String(o.lat, 4)
             + "&longitude=" + String(o.lon, 4)
             + "&current=temperature_2m,relative_humidity_2m,weather_code,wind_speed_10m,wind_direction_10m"
             + "&daily=weather_code,temperature_2m_max,temperature_2m_min,precipitation_probability_max"
             + "&timezone=auto&forecast_days=" + String(WX_FORECAST_DAYS)
             + "&temperature_unit=" + tu + "&wind_speed_unit=" + wu;

  // Open-Meteo's response for this query is small (~2-3 KB). Stream it to the
  // shared temp file (robust, with retry), then read it back in one exact-size
  // allocation -- avoiding the concat-doubling String growth that intermittently
  // truncated on the fragmented heap. Delete the temp right after.
  if (!net.httpsGetToFileRetry(url, FILE_DL_TMP, 16000, nullptr, 3)) {
    Store::fs().remove(FILE_DL_TMP);
    Serial.printf("[wx] weather fetch failed: code=%d %s\n", net.lastCode, net.lastErr.c_str());
    return;                                            // leave cache intact
  }
  String body = readSmallFile(FILE_DL_TMP, 16000);
  Store::fs().remove(FILE_DL_TMP);
  if (body.length() == 0) return;

  // --- tiny JSON helpers (numbers + first array of a named key) ---
  auto numAfter = [&](const String& key, int from) -> float {
    int idx = body.indexOf(key, from);
    if (idx < 0) return -999;
    int colon = body.indexOf(':', idx);
    if (colon < 0) return -999;
    return (float)atof(body.c_str() + colon + 1);
  };

  // current object
  int cur = body.indexOf("\"current\"");
  if (cur < 0) return;                                // malformed -> keep cache
  float t  = numAfter("\"temperature_2m\"", cur);
  float rh = numAfter("\"relative_humidity_2m\"", cur);
  float wc = numAfter("\"weather_code\"", cur);
  float ws = numAfter("\"wind_speed_10m\"", cur);
  float wd = numAfter("\"wind_direction_10m\"", cur);

  // daily arrays: each "key":[v0,v1,...]. Anchor the search to the "daily" object
  // so we don't latch onto the scalar "weather_code" inside "current".
  int dailyAt = body.indexOf("\"daily\"");
  int daySearchFrom = (dailyAt >= 0) ? dailyAt : 0;
  auto fillIntArray = [&](const char* key, int* out, int maxN) -> int {
    int idx = body.indexOf(String("\"") + key + "\"", daySearchFrom);
    if (idx < 0) return 0;
    int lb = body.indexOf('[', idx); if (lb < 0) return 0;
    int rb = body.indexOf(']', lb);  if (rb < 0) return 0;
    String arr = body.substring(lb + 1, rb);
    int n = 0, pos = 0;
    while (n < maxN) {
      int comma = arr.indexOf(',', pos);
      String tok = (comma < 0) ? arr.substring(pos) : arr.substring(pos, comma);
      tok.trim();
      if (tok.length() && tok != "null") out[n++] = (int)lround(atof(tok.c_str()));
      else if (tok.length()) out[n++] = 0;
      if (comma < 0) break; pos = comma + 1;
    }
    return n;
  };
  auto fillFloatArray = [&](const char* key, float* out, int maxN) -> int {
    int idx = body.indexOf(String("\"") + key + "\"", daySearchFrom);
    if (idx < 0) return 0;
    int lb = body.indexOf('[', idx); if (lb < 0) return 0;
    int rb = body.indexOf(']', lb);  if (rb < 0) return 0;
    String arr = body.substring(lb + 1, rb);
    int n = 0, pos = 0;
    while (n < maxN) {
      int comma = arr.indexOf(',', pos);
      String tok = (comma < 0) ? arr.substring(pos) : arr.substring(pos, comma);
      tok.trim();
      out[n++] = (tok.length() && tok != "null") ? (float)atof(tok.c_str()) : 0.0f;
      if (comma < 0) break; pos = comma + 1;
    }
    return n;
  };

  int codes[WX_FORECAST_DAYS], pops[WX_FORECAST_DAYS];
  float his[WX_FORECAST_DAYS], los[WX_FORECAST_DAYS];
  int nC = fillIntArray("weather_code", codes, WX_FORECAST_DAYS);
  int nH = fillFloatArray("temperature_2m_max", his, WX_FORECAST_DAYS);
  int nL = fillFloatArray("temperature_2m_min", los, WX_FORECAST_DAYS);
  int nP = fillIntArray("precipitation_probability_max", pops, WX_FORECAST_DAYS);
  // The first weather_code array we find is the *daily* one only if the current
  // block doesn't also carry it; guard by requiring temps to have parsed.
  int nDays = nH; if (nL < nDays) nDays = nL;
  if (nDays > WX_FORECAST_DAYS) nDays = WX_FORECAST_DAYS;

  // Commit (only if we got a sane current temperature or at least one day).
  bool ok = (t > -900) || (nDays > 0);
  if (!ok) return;
  wxTempNow = t; wxHumidNow = (rh > -900) ? (int)lround(rh) : -1;
  wxCodeNow = (wc > -900) ? (int)lround(wc) : -1;
  wxWindNow = ws; wxWindDirNow = (wd > -900) ? (int)lround(wd) : -1;
  wxDayCount = nDays;
  for (int i = 0; i < nDays; ++i) {
    wxDayCode[i] = (i < nC) ? codes[i] : 0;
    wxDayHi[i]   = his[i];
    wxDayLo[i]   = los[i];
    wxDayPop[i]  = (i < nP) ? pops[i] : 0;
    wxDayEpoch[i] = (long)(nowUtc() + (time_t)i * 86400);   // approximate day labels
  }
  wxEpoch = nowUtc();
  wxCachedUnits = cfg.wxUnits;

  // Persist a compact cache: header line then one line per day.
  File w = LittleFS.open(FILE_WEATHER, "w");
  if (w) {
    w.printf("%d %ld %.1f %d %d %.1f %d %d\n", (int)wxCachedUnits, (long)wxEpoch,
             wxTempNow, wxHumidNow, wxCodeNow, wxWindNow, wxWindDirNow, wxDayCount);
    for (int i = 0; i < wxDayCount; ++i)
      w.printf("%ld %d %.1f %.1f %d\n", wxDayEpoch[i], wxDayCode[i],
               wxDayHi[i], wxDayLo[i], wxDayPop[i]);
    w.close();
  }
  setStatus("");
}

// Restore cached weather at boot so the screen has data offline.
bool App::loadWeatherCache() {
  File f = LittleFS.open(FILE_WEATHER, "r");
  if (!f) return false;
  String hdr = f.readStringUntil('\n');
  if (hdr.length() == 0) { f.close(); return false; }
  int u, rh, code, wd, nd; long ep; float t, ws;
  if (sscanf(hdr.c_str(), "%d %ld %f %d %d %f %d %d",
             &u, &ep, &t, &rh, &code, &ws, &wd, &nd) != 8) { f.close(); return false; }
  wxCachedUnits = (uint8_t)u; wxEpoch = (time_t)ep;
  wxTempNow = t; wxHumidNow = rh; wxCodeNow = code; wxWindNow = ws; wxWindDirNow = wd;
  if (nd > WX_FORECAST_DAYS) nd = WX_FORECAST_DAYS;
  wxDayCount = 0;
  for (int i = 0; i < nd; ++i) {
    String ln = f.readStringUntil('\n');
    if (ln.length() == 0) break;
    long de; int dc, dp; float hi, lo;
    if (sscanf(ln.c_str(), "%ld %d %f %f %d", &de, &dc, &hi, &lo, &dp) == 5) {
      wxDayEpoch[wxDayCount] = de; wxDayCode[wxDayCount] = dc;
      wxDayHi[wxDayCount] = hi; wxDayLo[wxDayCount] = lo; wxDayPop[wxDayCount] = dp;
      wxDayCount++;
    }
  }
  f.close();
  return true;
}

// Atmospheric-density scale for the decay point estimate. In AUTO mode, derive a
// continuous scale from the live F10.7 flux (piecewise-linear: ~70 sfu solar-min
// -> 0.35x, ~150 mean -> 1.0x, ~250 solar-max -> 3.0x), clamped to the bracket.
// Without a cached flux, AUTO falls back to mean. Otherwise use the fixed level.
static double solarDensityScale(uint8_t act);   // defined with the decay model below
double App::decayDensityScale() const {
  if (cfg.solarAct == SOLAR_AUTO) {
    if (spaceF107 <= 0) return 1.0;                 // no data yet -> mean
    double f = spaceF107;
    double s;
    if (f <= 70)       s = 0.35;
    else if (f <= 150) s = 0.35 + (f - 70) * (1.0 - 0.35) / 80.0;
    else if (f <= 250) s = 1.0  + (f - 150) * (3.0 - 1.0) / 100.0;
    else               s = 3.0;
    return s;
  }
  return solarDensityScale(cfg.solarAct);
}

// ---- "Cache all transponders" via small per-sat batches across reboots ------
// The SatNOGS per-sat request is small and reliable, but ~90 of them in one boot
// exhausts the tiny LWIP socket pool (connection-refused wall) and the link's
// RSSI drifts over a long run. So we cache TX_CACHE_BATCH sats per boot, persist
// the next index to a marker file, and reboot: each boot starts with a pristine
// socket pool and a fresh WiFi association. setup() calls resumeCacheIfPending()
// to continue automatically until all sats are done.

// Read the saved next-index from the marker (returns -1 if no resume pending).
// The marker holds "index stallCount" on one line; stallCount (optional, default
// 0) counts consecutive boots that failed to advance past `index`.
static int txResumeRead(int* stall = nullptr) {
  if (stall) *stall = 0;
  File f = Store::fs().open(FILE_TX_RESUME, "r");
  if (!f) return -1;
  String s = f.readStringUntil('\n');
  f.close();
  s.trim();
  if (s.length() == 0) return -1;
  int sp = s.indexOf(' ');
  if (sp >= 0) {
    if (stall) *stall = s.substring(sp + 1).toInt();
    return s.substring(0, sp).toInt();
  }
  return s.toInt();
}

// Persist the next-index (and stall count) to cache. idx >= count means
// "done" -> remove marker.
static void txResumeWrite(int idx, int count, int stall = 0) {
  if (idx >= count) { if (Store::fs().exists(FILE_TX_RESUME)) Store::fs().remove(FILE_TX_RESUME); return; }
  File f = Store::fs().open(FILE_TX_RESUME, "w");
  if (!f) return;
  f.print(idx); f.print(' '); f.println(stall);
  f.close();
}

// Cache one batch of sats starting at `start`. Returns the index of the next sat
// still needing caching. Normally that's start+TX_CACHE_BATCH (clamped), but if a
// sat fails all its retries (the socket pool is exhausted for this boot), we stop
// and return THAT sat's index -- so the next boot re-attempts it in a fresh pool
// instead of skipping it. This guarantees every sat is eventually cached.
int App::cacheTxBatch(int start) {
  int n = db.count();
  // Free the two rigctld/rotctld listener sockets for the batch (the pool is
  // tiny). serviceRigctld()/serviceRotctld() rebuild them next loop tick.
  if (rigd) { rigdCli.stop(); rigd->stop(); delete rigd; rigd = nullptr; rigdBuf = ""; }
  if (rotd) { rotdCli.stop(); rotd->stop(); delete rotd; rotd = nullptr; rotdBuf = ""; }

  int end = start + TX_CACHE_BATCH; if (end > n) end = n;
  for (int i = start; i < end; ++i) {
    SatEntry& s = db.at(i);
    setStatus("TX " + String(i+1) + "/" + String(n) + ": " + s.name); draw();
    String url = String(SATNOGS_TX_URL) + String((unsigned long)s.norad);
    // Per-sat retry (3 attempts) so a single transient -1/-11 doesn't lose a sat.
    bool ok = net.httpsGetToFileRetry(url, SatDb::txCachePath(s.norad).c_str(),
                                      200000, nullptr, 3);
    if (!ok) {
      // All retries failed: the pool is wedged for this boot. Stop here and let
      // the next boot resume from THIS sat with a clean pool, rather than losing it.
      Serial.printf("[tx] sat %lu failed all retries; deferring to next boot\n",
                    (unsigned long)s.norad);
      return i;
    }
    delay(40);   // be gentle on the API
  }
  return end;
}

// User-triggered "cache all": don't cache inline here -- this boot has already
// spent socket-pool slots on the GP download + AMSAT fetch, which is exactly why
// the first batch used to hit the -1 wall a few sats in. Instead, set the resume
// marker to 0 and reboot, so EVERY batch (including the first) runs in a pristine
// boot. resumeCacheIfPending() in setup() then does the actual work, batch by
// batch, rebooting between each until all sats are cached.
void App::doCacheAllTransponders() {
  int n = db.count();
  if (n == 0) { setStatus("No sats. Update GP first."); return; }
  txResumeWrite(0, n);                  // mark a fresh run pending from index 0
  Serial.printf("[tx] starting batched cache of %d sats, rebooting\n", n);
  setStatus("Caching transponders - rebooting...", 2000);
  draw(); delay(1500);
  ESP.restart();
}

// Called at the end of setup(): if a cache run is pending (marker present),
// continue the next batch and reboot, until every sat is cached.
void App::resumeCacheIfPending() {
  int stall = 0;
  int start = txResumeRead(&stall);
  if (start < 0) return;                 // nothing pending
  int n = db.count();
  if (n == 0) { txResumeWrite(n, n); return; }   // no sats -> clear stale marker
  if (start >= n) { txResumeWrite(n, n); return; }

  // A run is in progress: show the Update menu (not Home) on every resume boot
  // so the user sees where the caching lives and the progress status sits there.
  screen = SCR_UPDATE; lastDrawMs = 0; draw();

  if (!net.connected() && !connectWifiCfg()) {
    // Can't make progress without WiFi; leave the marker so a later boot retries.
    setStatus("Cache paused: no WiFi", 2500);
    return;
  }
  int next = cacheTxBatch(start);

  // Guard against an infinite reboot loop: if a batch makes zero progress (the
  // very first sat failed all retries even in this fresh boot), count the stall.
  // After 2 such boots, skip that one stubborn sat so the run can continue.
  if (next == start) {
    if (stall + 1 >= 2) {
      Serial.printf("[tx] sat index %d stuck after retries; skipping it\n", start);
      next = start + 1;
      stall = 0;
    } else {
      stall++;
    }
  } else {
    stall = 0;
  }

  txResumeWrite(next, n, stall);
  if (next >= n) {
    Serial.printf("[tx] cache complete: %d satellites\n", n);
    setStatus("Cached all " + String(n) + " transponders", 3000);
    return;
  }
  Serial.printf("[tx] batch done to %d/%d, rebooting to continue\n", next, n);
  setStatus("Caching " + String(next) + "/" + String(n) + " - rebooting...", 2000);
  draw(); delay(1500);
  ESP.restart();
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
    if (line.length() == 0 || line[0] == '#' || line[0] == ';') continue;  // blank/comment
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
      if (t[0] == '#' || t[0] == ';') { out += t; out += '\n'; continue; }  // keep comments
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
    if (line.length() == 0 || line[0] == '#' || line[0] == ';') continue;  // blank/comment
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
      if (t[0] == '#' || t[0] == ';') { out += t; out += '\n'; continue; }  // keep comments
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
  if (screen == SCR_TRACK || screen == SCR_BIG || screen == SCR_POLAR || screen == SCR_MANUAL || screen == SCR_MANUALBIG) return;  // don't disturb Doppler
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
  if (!cfg.aosAlarm || !timeIsSet()) return;       // all alarm sounds gated off here
  time_t now = nowUtc();
  // --- TCA and LOS chirps for a pass already in progress ---
  if (alarmLos) {
    if (now >= alarmLos) {                          // pass over: LOS tone, then disarm
      if (!(alarmPassMarks & 2)) { alarmPassMarks |= 2; beep(1200, 120); delay(90); beep(900, 160);
        if (cfg.irBeacon) irBeacon.flash(IR_N_LOS); }
      alarmTca = alarmLos = 0; alarmPassMarks = 0;
    } else if (alarmTca && now >= alarmTca && !(alarmPassMarks & 1)) {
      alarmPassMarks |= 1; beep(2200, 110); delay(70); beep(2200, 110);   // TCA (peak elevation)
      if (cfg.irBeacon) irBeacon.flash(IR_N_TCA);
    }
  }
  if (nextAos == 0) return;
  if (alarmAos != nextAos) { alarmAos = nextAos; alarmMarks = 0; }  // new target
  long dt = (long)(nextAos - now);
  if (dt <= 60 && !(alarmMarks & 1)) { alarmMarks |= 1; beep(1500, 80);
    if (cfg.irBeacon) irBeacon.flash(IR_N_T60); }
  if (dt <= 30 && !(alarmMarks & 2)) { alarmMarks |= 2; beep(1500, 80);
    if (cfg.irBeacon) irBeacon.flash(IR_N_T30); }
  if (dt <= 10 && !(alarmMarks & 4)) { alarmMarks |= 4; beep(1800, 90);
    if (cfg.irBeacon) irBeacon.flash(IR_N_T10); }
  if (dt <= 0  && !(alarmMarks & 8)) {
    alarmMarks |= 8;
    beep(2600, 250); delay(120); beep(2600, 250);          // AOS!
    if (cfg.irBeacon) irBeacon.flash(IR_N_AOS);
    aosFlashUntil = millis() + 8000;
    strncpy(aosFlashName, nextAosName, sizeof(aosFlashName) - 1);
    aosFlashName[sizeof(aosFlashName) - 1] = 0;
    // Arm TCA/LOS chirps from the schedule entry whose AOS just fired.
    alarmTca = alarmLos = 0; alarmPassMarks = 0;
    for (int i = 0; i < schedN; ++i) {
      if (sched[i].aos == nextAos && sched[i].los > now) {
        alarmLos = sched[i].los;
        alarmTca = sched[i].aos + (sched[i].los - sched[i].aos) / 2;  // mid-pass ~= peak el
        break;
      }
    }
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

// Delete the manualIdx-th manual transponder (0-based, in file order) for this
// sat by rewriting FILE_MTX without that line. Returns true if a line was removed.
bool App::deleteManualTx(uint32_t norad, int manualIdx) {
  char path[32]; snprintf(path, sizeof(path), FILE_MTX, (unsigned long)norad);
  File f = Store::fs().open(path, "r");
  if (!f) return false;
  String out; int seen = 0; bool removed = false;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    String t = line; t.trim();
    if (t.length() == 0) continue;
    if (seen == manualIdx) { seen++; removed = true; continue; }  // drop this one
    seen++; out += t; out += '\n';
  }
  f.close();
  if (!removed) return false;
  if (out.length() == 0) { Store::fs().remove(path); return true; }
  File w = Store::fs().open(path, "w");
  if (!w) return false;
  w.print(out); w.close();
  return true;
}

// ===========================================================================
// ===========================================================================
//  rigctld server (item 2): a minimal Hamlib NET rigctl TCP server so a PC can
//  drive the rig CardSat is connected to (wired CI-V/CAT or Icom LAN). The
//  single selectable VFO maps to CardSat's two legs: VFOA = downlink (Sub/RX),
//  VFOB = uplink (Main/TX). Supports f/F (freq), m/M (mode), v/V (vfo), t/T
//  (ptt), q (quit) plus the \dump_state and \chk_vfo handshakes some library
//  clients issue on connect. One client at a time; non-blocking, pumped each loop.
// ===========================================================================
static const char* rigdModeName(RigMode m) {
  switch (m) {
    case RM_LSB: return "LSB";  case RM_USB:  return "USB";
    case RM_CW:  return "CW";   case RM_FM:   return "FM";
    case RM_AM:  return "AM";   case RM_DATA: return "PKTUSB";
  }
  return "USB";
}
static long rigdPassband(RigMode m) {
  switch (m) {
    case RM_FM: return 15000;  case RM_AM: return 6000;
    case RM_CW: return 500;    default:    return 2400;
  }
}

void App::serviceRigctld() {
  if (netFetchActive()) return;   // listeners stay freed during an outbound fetch
  // Start/stop the listener with the enable flag and WiFi state.
  if (cfg.rigdEnable && net.connected()) {
    if (!rigd) { rigd = new WiFiServer(cfg.rigdPort); rigd->begin(); rigd->setNoDelay(true); }
  } else {
    if (rigd) { rigdCli.stop(); rigd->stop(); delete rigd; rigd = nullptr; rigdBuf = ""; }
    return;
  }
  // Accept one client at a time.
  if (!rigdCli || !rigdCli.connected()) {
    WiFiClient nc = rigd->available();
    if (nc) { rigdCli = nc; rigdBuf = ""; }
  }
  if (!rigdCli || !rigdCli.connected()) return;
  // Consume available bytes, dispatching on each complete line.
  int guard = 0;
  while (rigdCli.available() && guard++ < 256) {
    char ch = (char)rigdCli.read();
    if (ch == '\r') continue;
    if (ch == '\n') { rigdHandleLine(rigdBuf); rigdBuf = ""; }
    else if (rigdBuf.length() < 120) rigdBuf += ch;
  }
}

void App::rigdHandleLine(const String& lineIn) {
  String line = lineIn; line.trim();
  if (!line.length()) return;
  auto rprt = [&](int code) { rigdCli.printf("RPRT %d\n", code); };

  // Long-form handshakes some Hamlib library clients send on connect.
  if (line == "\\dump_state") {
    rigdCli.print("0\n2\n2\n"
      "150000.000000 1500000000.000000 0x1ff -1 -1 0x10000003 0x3\n0 0 0 0 0 0 0\n"
      "150000.000000 1500000000.000000 0x1ff -1 -1 0x10000003 0x3\n0 0 0 0 0 0 0\n"
      "0 0\n0 0\n0\n0\n0\n0\n0x0\n0x0\n0x0\n0x0\n0x0\n0x0\n");
    return;
  }
  if (line == "\\chk_vfo")       { rigdCli.print("CHKVFO 0\n"); return; }
  if (line == "\\get_powerstat") { rigdCli.print("1\n"); return; }

  char cmd = line.charAt(0);
  String arg = (line.length() > 1) ? line.substring(1) : "";
  arg.trim();
  switch (cmd) {
    case 'f': {                                  // get_freq
      uint32_t hz = 0;
      bool ok = rig && (rigdVfo == 1 ? rig->readMainFreq(hz) : rig->readSubFreq(hz));
      if (!ok) hz = (rigdVfo == 1) ? rigdLastMain : rigdLastSub;
      rigdCli.printf("%lu\n", (unsigned long)hz);
    } break;
    case 'F': {                                  // set_freq <hz>
      uint32_t hz = (uint32_t)strtoul(arg.c_str(), nullptr, 10);
      bool ok = rig && (rigdVfo == 1 ? rig->setMainFreq(hz) : rig->setSubFreq(hz));
      if (ok) { if (rigdVfo == 1) rigdLastMain = hz; else rigdLastSub = hz; }
      rprt(ok ? 0 : -1);
    } break;
    case 'm': {                                  // get_mode -> "<MODE>\n<passband>\n"
      RigMode m = (rigdVfo == 1) ? rigdMainMode : rigdSubMode;
      rigdCli.printf("%s\n%ld\n", rigdModeName(m), rigdPassband(m));
    } break;
    case 'M': {                                  // set_mode <MODE> [passband]
      String mode = arg; int sp = mode.indexOf(' '); if (sp >= 0) mode = mode.substring(0, sp);
      RigMode m = Rig::modeFromString(mode);
      bool ok = rig && (rigdVfo == 1 ? rig->setMainMode(m) : rig->setSubMode(m));
      if (ok) { if (rigdVfo == 1) rigdMainMode = m; else rigdSubMode = m; }
      rprt(ok ? 0 : -1);
    } break;
    case 'v': rigdCli.print(rigdVfo == 1 ? "VFOB\n" : "VFOA\n"); break;   // get_vfo
    case 'V':                                    // set_vfo <VFOA|VFOB>
      rigdVfo = (arg.indexOf("VFOB") >= 0 || arg.indexOf("Sub") >= 0) ? 1 : 0;
      rprt(0); break;
    case 't': { bool tx = false; if (rig) rig->readPtt(tx); rigdCli.print(tx ? "1\n" : "0\n"); } break;
    case 'T': rprt(0); break;                    // set_ptt: accepted but not relayed
    case 'q': case 'Q': rigdCli.stop(); break;   // quit
    default: rprt(-1); break;                    // unsupported command
  }
}

// ===========================================================================
//  rotctld server (item 4): a minimal Hamlib NET rotctl TCP server so a PC can
//  drive the GS-232 rotator wired to CardSat. Supports P (set_pos), p (get_pos),
//  S (stop), q (quit) plus the \dump_state handshake. Commands are passed to the
//  active rotator backend verbatim (no CardSat az-range/offset remap - the
//  external client owns calibration). A set_pos disengages CardSat's own
//  tracking so the two don't fight. One client at a time; pumped each loop.
// ===========================================================================
void App::serviceRotctld() {
  if (netFetchActive()) return;   // listeners stay freed during an outbound fetch
  if (cfg.rotdEnable && net.connected()) {
    if (!rotd) { rotd = new WiFiServer(cfg.rotdPort); rotd->begin(); rotd->setNoDelay(true); }
  } else {
    if (rotd) { rotdCli.stop(); rotd->stop(); delete rotd; rotd = nullptr; rotdBuf = ""; }
    return;
  }
  if (!rotdCli || !rotdCli.connected()) {
    WiFiClient nc = rotd->available();
    if (nc) { rotdCli = nc; rotdBuf = ""; }
  }
  if (!rotdCli || !rotdCli.connected()) return;
  int guard = 0;
  while (rotdCli.available() && guard++ < 256) {
    char ch = (char)rotdCli.read();
    if (ch == '\r') continue;
    if (ch == '\n') { rotdHandleLine(rotdBuf); rotdBuf = ""; }
    else if (rotdBuf.length() < 120) rotdBuf += ch;
  }
}

void App::rotdHandleLine(const String& lineIn) {
  String line = lineIn; line.trim();
  if (!line.length()) return;
  auto rprt = [&](int code) { rotdCli.printf("RPRT %d\n", code); };

  if (line == "\\dump_state") {                  // minimal rotator state
    rotdCli.print("0\n2\n0.000000 360.000000\n0.000000 90.000000\n0\n0\n");
    return;
  }
  char cmd = line.charAt(0);
  String arg = (line.length() > 1) ? line.substring(1) : "";
  arg.trim();
  switch (cmd) {
    case 'P': {                                  // set_pos <az> <el>
      float az = 0, el = 0;
      int sp = arg.indexOf(' ');
      if (sp > 0) { az = arg.substring(0, sp).toFloat(); el = arg.substring(sp + 1).toFloat(); }
      rotOut = false;                            // external controller takes over
      bool ok = rot && rot->point(az, el);
      rprt(ok ? 0 : -1);
    } break;
    case 'p': {                                  // get_pos -> "<az>\n<el>\n"
      float az = 0, el = 0;
      if (rot && rot->readPos(az, el)) rotdCli.printf("%.1f\n%.1f\n", az, el);
      else rprt(-1);
    } break;
    case 'S': if (rot) rot->stop(); rprt(rot ? 0 : -1); break;   // stop
    case 'q': case 'Q': rotdCli.stop(); break;                   // quit
    default: rprt(-1); break;                                    // unsupported
  }
}

// ===========================================================================
//  Mobile web control page (opt-in, served over the WiFi LAN)
//
//  A deliberately tiny HTTP server in the same cooperative style as the rigctld
//  and rotctld servers above: accept one client, read the request without
//  blocking, answer, move on. The served page is a single self-contained file
//  (kept in PROGMEM so it never lands on the fragmented no-PSRAM heap) that polls
//  /api/status and posts to /api/select and /api/cmd. Commands reuse the exact
//  keypad handlers, so the web UI drives the same, already-correct code paths as
//  the physical keys -- it never re-implements radio/rotator logic.
// ===========================================================================

static const char WEB_PAGE[] PROGMEM = R"HTMLPAGE(<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>CardSat</title>
<style>
:root{--bg:#0b0e13;--fg:#e6edf3;--mut:#8b96a5;--grn:#39d353;--org:#f0883e;--cy:#4ad;--pan:#161b22;--bd:#2a313c;--acc:#316dca}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px/1.4 system-ui,-apple-system,Segoe UI,Roboto,sans-serif}
header{padding:10px 14px;background:var(--pan);border-bottom:1px solid var(--bd);position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:10px}
header b{font-size:18px}.tag{margin-left:auto;font-size:13px;color:var(--mut);font-variant-numeric:tabular-nums}.tag.pass{color:var(--grn);font-weight:600}
main{padding:12px;max-width:980px;margin:0 auto}
.cols{display:grid;grid-template-columns:1fr;gap:12px}
@media(min-width:760px){.cols{grid-template-columns:1fr 1fr;align-items:start}.span2{grid-column:1/-1}}
.card{background:var(--pan);border:1px solid var(--bd);border-radius:12px;padding:12px;margin-bottom:0}
.freq{font-variant-numeric:tabular-nums;font-size:clamp(26px,7vw,34px);font-weight:700;letter-spacing:.5px}
.rx{color:var(--grn)}.tx{color:var(--org)}.lab{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.4px}
.row{display:flex;gap:8px;flex-wrap:wrap;align-items:center}.row>*{flex:1}
select,button,input{font:inherit;color:var(--fg);background:#1d242e;border:1px solid var(--bd);border-radius:10px;padding:12px;min-height:48px}
input{width:100%}input:focus,select:focus{outline:2px solid var(--acc);border-color:var(--acc)}
button{cursor:pointer;-webkit-tap-highlight-color:transparent}button:active{background:#283040}.on{outline:2px solid var(--grn)}
.grid{display:grid;grid-template-columns:repeat(4,1fr);gap:8px}@media(max-width:420px){.grid{grid-template-columns:repeat(2,1fr)}}
.az{font-size:22px;font-variant-numeric:tabular-nums}
table{width:100%;border-collapse:collapse;font-size:14px}td,th{padding:6px 4px;border-bottom:1px solid var(--bd);text-align:left}th{color:var(--mut);font-weight:600}.mut{color:var(--mut)}
.tools{display:flex;gap:8px;flex-wrap:wrap}.flt{flex:0 0 auto;width:auto;min-width:120px}
.skywrap{display:flex;justify-content:center;padding:4px 0}svg{max-width:100%;height:auto}
.hint{font-size:12px;color:var(--mut);margin-top:6px}
.fld{display:flex;gap:8px;align-items:center;margin-top:8px}.fld label{flex:0 0 auto;width:84px;color:var(--mut);font-size:13px}.fld input{flex:1}.fld button{flex:0 0 auto;min-width:64px}
.pillrow{display:flex;gap:6px;flex-wrap:wrap;margin-top:8px}.sm{min-width:auto;min-height:auto;padding:8px 12px}
</style></head><body>
<header><b>CardSat</b><span class="tag" id="tag">connecting...</span></header>
<main>
<div class="card span2"><div class="tools">
<input id="search" placeholder="filter..." style="flex:1;min-width:120px">
<select id="sat" style="flex:2;min-width:150px"></select>
<button id="fav" class="flt" title="favourite">&#9734;</button>
<button id="go" class="flt">Track</button></div></div>
<div class="cols">
<div class="card">
<div class="lab">Downlink (RX)</div><div class="freq rx" id="rx">--.-----</div>
<div class="lab" style="margin-top:8px">Uplink (TX)</div><div class="freq tx" id="tx">--.-----</div>
<div class="row" style="margin-top:10px">
<div class="az lab">Az <span id="az" style="color:var(--fg)">--</span>&deg;</div>
<div class="az lab">El <span id="el" style="color:var(--fg)">--</span>&deg;</div>
<div class="lab">Mode <span id="mode" style="color:var(--fg)">--</span></div></div>
<div class="skywrap"><svg id="sky" viewBox="0 0 220 220" width="220" height="220" role="img" aria-label="sky plot">
<circle cx="110" cy="110" r="100" fill="#0d1117" stroke="#2a313c"/>
<circle cx="110" cy="110" r="66" fill="none" stroke="#222a35"/>
<circle cx="110" cy="110" r="33" fill="none" stroke="#222a35"/>
<line x1="110" y1="10" x2="110" y2="210" stroke="#222a35"/><line x1="10" y1="110" x2="210" y2="110" stroke="#222a35"/>
<text x="110" y="22" fill="#8b96a5" font-size="11" text-anchor="middle">N</text>
<text x="110" y="206" fill="#8b96a5" font-size="11" text-anchor="middle">S</text>
<text x="200" y="114" fill="#8b96a5" font-size="11" text-anchor="middle">E</text>
<text x="20" y="114" fill="#8b96a5" font-size="11" text-anchor="middle">W</text>
<polyline id="arc" fill="none" stroke="#316dca" stroke-width="2" opacity=".7" points=""/>
<circle id="dot" cx="110" cy="110" r="6" fill="#39d353" stroke="#0b0e13" stroke-width="2" style="display:none"/>
</svg></div>
<div class="hint" id="aos">&mdash;</div></div>
<div class="card">
<div class="row" style="margin-bottom:8px">
<button id="mRadio" class="on">Radio</button><button id="mMan">Manual</button><button id="mOrb">Orbit</button></div>
<div id="radCtl">
<div class="lab">Transponder</div>
<select id="tpsel" style="width:100%;margin-top:4px"></select>
<div class="grid" style="margin-top:10px">
<button data-k=",">Tune -</button><button data-k="/">Tune +</button>
<button data-k="t">TX&#x2192;</button><button data-k="d">Mode</button>
<button id="brad" data-k="r">Radio</button><button id="brot" data-k="o">Rotator</button>
<button data-k="m">CAL</button><button data-k="x">Center</button></div>
<div class="lab" style="margin-top:12px">Direct calibration (Hz)</div>
<div class="fld"><label for="caldl">RX cal</label><input id="caldl" type="number" inputmode="numeric" step="100" placeholder="0"><button id="setdl">Set</button></div>
<div class="fld"><label for="calul">TX cal</label><input id="calul" type="number" inputmode="numeric" step="100" placeholder="0"><button id="setul">Set</button></div>
<div class="hint" id="pbline"></div>
<div class="pillrow"><button id="calzero" class="flt sm">Zero cal</button></div></div>
<div id="manCtl" style="display:none">
<div class="lab" id="mhl">HOLD</div><div class="freq" id="hold" style="color:#fff">--.-----</div>
<div class="lab" id="mtl" style="margin-top:8px">TUNE &#x2192;</div><div class="freq" id="mtune" style="color:var(--cy)">--.-----</div>
<div class="grid" style="margin-top:10px">
<button data-m=",">Tune -</button><button data-m="/">Tune +</button>
<button data-m="u">Swap leg</button><button data-m="t">TX&#x2192;</button>
<button data-m="m">CAL</button><button data-m="x">Center</button></div></div>
<div id="orbCtl" style="display:none">
<div class="lab">Orbital analysis</div>
<table id="orbTbl"><tr><td class="mut">&mdash;</td></tr></table></div></div>
<div class="card span2"><div class="lab" style="margin-bottom:6px">Upcoming passes</div>
<table id="passes"><tr><td class="mut">&mdash;</td></tr></table></div>
</div></main>
<script>
var lastSat=0,mode='rad',passList=[],lastTxKey='';
function j(u,o){return fetch(u,o).then(r=>r.json())}
function pad(n){return(n<10?'0':'')+n}
function hhmm(t){var d=new Date(t*1000);return pad(d.getUTCHours())+':'+pad(d.getUTCMinutes())+'Z'}
function dur(s){return Math.round(s/60)+'m'}
function ms(s){s=Math.max(0,Math.round(s));return Math.floor(s/60)+':'+pad(s%60)}
function gid(i){return document.getElementById(i)}
function setMode(m){mode=m;
gid('mRadio').classList.toggle('on',m=='rad');gid('mMan').classList.toggle('on',m=='man');gid('mOrb').classList.toggle('on',m=='orb');
gid('radCtl').style.display=(m=='rad')?'':'none';gid('manCtl').style.display=(m=='man')?'':'none';gid('orbCtl').style.display=(m=='orb')?'':'none';
if(m=='orb')orbit()}
function orow(k,v){return '<tr><td class="mut">'+k+'</td><td>'+v+'</td></tr>'}
function orbit(){j('/api/orbit').then(o=>{var t=gid('orbTbl');
if(!o.ok){t.innerHTML='<tr><td class="mut">need clock + elements</td></tr>';return}
var h='';h+=orow('Altitude',Math.round(o.alt)+' km');h+=orow('Footprint',Math.round(o.footprint)+' km');
h+=orow('Period',o.period+' min');h+=orow('Apo/Peri',Math.round(o.apo)+'/'+Math.round(o.peri)+' km');
h+=orow('Incl/Ecc',o.incl+' / '+o.ecc);h+=orow('Decay',o.decayDays<0?'n/a':(o.decayDays>36500?'stable':o.decayDays+' d'));
h+=orow('Range rate',o.rangeRate+' km/s');h+=orow('Sublat/lon',o.subLat+', '+o.subLon);
h+=orow('Sunlit',o.sunlit?'yes':'eclipse');h+=orow('Beta',o.beta+'\u00b0');
h+=orow('Node drift',o.nodeDrift+'\u00b0/day'+(o.sunSync?' (sun-sync)':''));
h+=orow('Mean/True anom',o.meanAnom+'\u00b0 / '+o.trueAnom+'\u00b0');
h+=orow('To peri/apo',Math.round(o.toPeri/60)+'/'+Math.round(o.toApo/60)+' min');
if(o.outlookN)h+=orow('Outlook',o.outlookN+' passes, '+o.outlookHi+' >30\u00b0');
t.innerHTML=h;
})}
function aedom(az,el){var r=100*(90-Math.max(0,el))/90,a=az*Math.PI/180;return[110+r*Math.sin(a),110-r*Math.cos(a)]}
function drawArc(arc){var el=gid('arc');if(!arc||!arc.length){el.setAttribute('points','');return;}var pts=arc.filter(p=>p[1]>=0).map(p=>{var c=aedom(p[0],p[1]);return c[0].toFixed(1)+','+c[1].toFixed(1)}).join(' ');el.setAttribute('points',pts);}
function loadSats(){j('/api/sats').then(a=>{window._sats=a;renderSats('')})}
function renderSats(q){var s=gid('sat');var cur=s.value;s.innerHTML='';
(window._sats||[]).filter(x=>!q||x.name.toLowerCase().indexOf(q)>=0).forEach(x=>{
var o=document.createElement('option');o.value=x.n;o.dataset.fav=x.fav?1:0;
o.textContent=x.name+(x.fav?' \u2605':'');if(x.n==cur)o.selected=true;s.appendChild(o)});reflectFav()}
function reflectFav(){var s=gid('sat');var o=s.options[s.selectedIndex];gid('fav').innerHTML=(o&&o.dataset.fav=='1')?'\u2605':'\u2606'}
function loadTx(){j('/api/tx').then(o=>{var s=gid('tpsel');s.innerHTML='';
o.list.forEach(x=>{var op=document.createElement('option');op.value=x.i;
op.textContent=x.d+(x.m?(' ('+x.m+')'):'');if(x.i==o.cur)op.selected=true;s.appendChild(op)});
if(!o.list.length){var op=document.createElement('option');op.textContent='(none)';s.appendChild(op)}})}
function passes(){j('/api/passes').then(d=>{var a=(d&&d.passes)?d.passes:[];passList=a;window._arc=(d&&d.arc)?d.arc:[];var t=gid('passes');
if(!a.length){t.innerHTML='<tr><td class="mut">no passes</td></tr>';drawArc([]);return}
var h='<tr><th>AOS</th><th>Peak</th><th>El</th><th>Dur</th><th>Az</th></tr>';
a.forEach(p=>{h+='<tr><td>'+hhmm(p.aos)+'</td><td>'+hhmm(p.tca)+'</td><td>'+p.el+'&deg;</td><td>'+dur(p.los-p.aos)+'</td><td>'+p.azaos+'&rarr;'+p.azlos+'</td></tr>'});
t.innerHTML=h})}
function updCountdown(){var tag=gid('tag'),aos=gid('aos'),now=Date.now()/1000;
if(!passList.length){return}
var p=passList[0];
if(now>=p.aos&&now<=p.los){tag.textContent='IN PASS \u2014 LOS in '+ms(p.los-now);tag.classList.add('pass');aos.textContent='Max el '+p.el+'\u00b0 at '+hhmm(p.tca)}
else if(now<p.aos){tag.classList.remove('pass');var nm=document.title;tag.textContent=(window._satname||'')||'idle';aos.textContent='Next AOS in '+ms(p.aos-now)+' ('+hhmm(p.aos)+', max '+p.el+'\u00b0)'}
else{aos.textContent='\u2014'}}
function tick(){j('/api/status').then(s=>{window._satname=s.name;
gid('tag').classList.contains('pass')||(gid('tag').textContent=s.name||'no sat');
gid('rx').textContent=s.rx;gid('tx').textContent=s.tx;
gid('az').textContent=Math.round(s.az);gid('el').textContent=Math.round(s.el);
gid('mode').textContent=s.mode;
gid('brad').classList.toggle('on',s.radio);gid('brot').classList.toggle('on',s.rot);
gid('hold').textContent=s.hold;gid('mtune').textContent=s.tune;
gid('mhl').textContent=s.fixup?'HOLD uplink (park TX here)':'HOLD downlink (park RX here)';
gid('mtl').textContent=s.fixup?'TUNE \u2192 downlink (follow)':'TUNE \u2192 uplink (follow)';
/* sky dot + next-pass arc */
var dot=gid('dot');if(s.el>=0){var p=aedom(s.az,s.el);dot.setAttribute('cx',p[0].toFixed(1));dot.setAttribute('cy',p[1].toFixed(1));dot.style.display='';gid('arc').setAttribute('points','')}else{dot.style.display='none';drawArc(window._arc||[])}
/* cal placeholders */
if(document.activeElement!=gid('caldl'))gid('caldl').placeholder=s.caldl;
if(document.activeElement!=gid('calul'))gid('calul').placeholder=s.calul;
gid('pbline').textContent=s.lin?('Linear passband: '+Math.round(s.pbOff/1000)+' / '+Math.round(s.pbBw/1000)+' kHz'):'';
/* transponder dropdown sync */
var key=s.norad+':'+s.txn;if(key!=lastTxKey){lastTxKey=key;loadTx()}else{var ts=gid('tpsel');if(ts.value!=s.txi&&s.txi>=0)ts.value=s.txi}
if(s.norad&&s.norad!=lastSat){lastSat=s.norad;passes()}
updCountdown();
}).catch(e=>{gid('tag').textContent='offline';gid('tag').classList.remove('pass')})}
gid('go').onclick=function(){var n=gid('sat').value;j('/api/select?norad='+n,{method:'POST'}).then(()=>{lastSat=0;lastTxKey='';tick()})}
gid('search').oninput=function(){renderSats(this.value.toLowerCase())}
gid('sat').onchange=reflectFav;
gid('fav').onclick=function(){var s=gid('sat'),o=s.options[s.selectedIndex];if(!o)return;
j('/api/fav?norad='+o.value,{method:'POST'}).then(r=>{o.dataset.fav=r.fav?1:0;
o.textContent=o.textContent.replace(/ \u2605$/,'')+(r.fav?' \u2605':'');reflectFav()})}
gid('tpsel').onchange=function(){j('/api/tx?i='+this.value,{method:'POST'}).then(tick)}
gid('setdl').onclick=function(){var v=gid('caldl').value;if(v==='')return;j('/api/cal?dl='+parseInt(v,10),{method:'POST'}).then(()=>{gid('caldl').value='';tick()})}
gid('setul').onclick=function(){var v=gid('calul').value;if(v==='')return;j('/api/cal?ul='+parseInt(v,10),{method:'POST'}).then(()=>{gid('calul').value='';tick()})}
gid('calzero').onclick=function(){j('/api/cal?dl=0&ul=0',{method:'POST'}).then(tick)}
gid('mRadio').onclick=function(){setMode('rad')}
gid('mMan').onclick=function(){setMode('man')}
gid('mOrb').onclick=function(){setMode('orb')}
document.querySelectorAll('button[data-k]').forEach(b=>{b.onclick=function(){j('/api/cmd?k='+encodeURIComponent(b.dataset.k),{method:'POST'}).then(tick)}})
document.querySelectorAll('button[data-m]').forEach(b=>{b.onclick=function(){j('/api/cmd?man=1&k='+encodeURIComponent(b.dataset.m),{method:'POST'}).then(tick)}})
loadSats();tick();setInterval(tick,1500);setInterval(updCountdown,1000);
</script></body></html>
)HTMLPAGE";

// Tear down the three LAN listeners (rigctld, rotctld, web) and their connected
// clients, freeing their sockets. The ESP32 has a small LWIP socket pool and TLS
// needs a free socket plus a large contiguous buffer; with the listeners (and a
// kept-alive browser connection) holding sockets, an outbound HTTPS connect for a
// GP/data download can be refused ("connection refused") or starved of heap. The
// service functions rebuild each listener on the next loop tick once the download
// sequence is done, so this is a transient suspend, not a disable.
void App::suspendNetServers() {
  if (rigd) { rigdCli.stop(); rigd->stop(); delete rigd; rigd = nullptr; rigdBuf = ""; }
  if (rotd) { rotdCli.stop(); rotd->stop(); delete rotd; rotd = nullptr; rotdBuf = ""; }
  if (webd) { webdCli.stop(); webd->stop(); delete webd; webd = nullptr; webdBuf = ""; }
}

// The full-screen 240x135x16bpp sprite is a single ~64 KB contiguous allocation
// that otherwise caps the largest free heap block around 31 KB -- too small for
// mbedTLS to allocate its handshake buffers, so every TLS connect returns -1. We
// free the sprite for the duration of an outbound fetch (the loop isn't drawing
// during a blocking download) and recreate it afterwards. drawMemoIndicator and
// the per-screen draws are skipped while it's gone via canvasFreed.
// The 8bpp display sprite is only ~32 KB, so it stays allocated through every
// TLS session: with it kept, ~85 KB of heap remains free, which is enough for
// the mbedTLS handshake. We therefore never delete/recreate the sprite (the
// 16bpp 64 KB block could not be reliably reallocated on IDF 5.4.x, which froze
// the screen). These remain as no-ops so the trampoline call sites and the
// canvasFreed contract stay intact; canvasFreed is never set, so draw() always
// runs. Socket suspension around fetches is handled separately in the trampoline.
void App::freeCanvasForTls() {
  // intentionally a no-op: the 8bpp sprite is small enough to keep allocated
}
void App::restoreCanvasAfterTls() {
  // nothing to restore; the sprite is never freed
}
void App::tickCanvasRestore() {
  // no-op; retained for the loop call site
}

// Static glue so the UI-agnostic Net layer can free our listener sockets around
// every outbound TLS session (see Net::onTlsBusy). On busy=true we drop the
// listeners and mark a fetch in progress; on busy=false we clear that mark once
// the outermost fetch unwinds. While the mark is set, the service*() functions
// refuse to rebuild the listeners -- so across a multi-fetch update (GP, AMSAT,
// space wx, weather) the sockets stay freed for the whole burst even if the main
// loop runs between fetches, giving the outbound TLS connects maximum headroom.
App* App::s_self = nullptr;
int  App::s_fetchDepth = 0;
void App::tlsBusyTrampoline(bool busy) {
  if (!s_self) return;
  if (busy) {
    if (s_fetchDepth++ == 0) {
      s_self->suspendNetServers();
      s_self->freeCanvasForTls();   // free ~64 KB sprite for the TLS handshake heap
    }
  } else if (s_fetchDepth > 0) {
    if (--s_fetchDepth == 0) s_self->restoreCanvasAfterTls();
  }
}
bool App::netFetchActive() { return s_fetchDepth > 0; }

void App::serviceWebd() {
  if (netFetchActive()) return;   // listeners stay freed during an outbound fetch
  // Start/stop the listener with the enable flag and WiFi state.
  if (cfg.webEnable && net.connected()) {
    if (!webd) { webd = new WiFiServer(cfg.webPort); webd->begin(); webd->setNoDelay(true); }
  } else {
    if (webd) { webdCli.stop(); webd->stop(); delete webd; webd = nullptr; webdBuf = ""; }
    return;
  }
  // Accept one client at a time.
  if (!webdCli || !webdCli.connected()) {
    WiFiClient nc = webd->available();
    if (nc) { webdCli = nc; webdBuf = ""; }
  }
  if (!webdCli || !webdCli.connected()) return;
  // Read the request line + headers without blocking. We only need the request
  // line (method + path); we drain the rest until a blank line, then respond.
  int guard = 0;
  while (webdCli.available() && guard++ < 1024) {
    char ch = (char)webdCli.read();
    if (ch == '\r') continue;
    if (ch == '\n') {
      if (webdBuf.length() == 0) {          // blank line: end of headers
        webdHandleRequest(webdReqLine);
        webdCli.stop();
        webdReqLine = "";
        return;
      }
      if (webdReqLine.length() == 0) webdReqLine = webdBuf;  // first line = request line
      webdBuf = "";
    } else if (webdBuf.length() < 200) {
      webdBuf += ch;
    }
  }
}

// Route one HTTP request by method + path (only the verbs/paths we serve).
void App::webdHandleRequest(const String& reqLine) {
  // reqLine looks like "GET /api/status?x=1 HTTP/1.1"
  int sp1 = reqLine.indexOf(' ');
  int sp2 = reqLine.indexOf(' ', sp1 + 1);
  if (sp1 < 0 || sp2 < 0) { webdCli.print(F("HTTP/1.1 400 Bad Request\r\n\r\n")); return; }
  String method = reqLine.substring(0, sp1);
  String path   = reqLine.substring(sp1 + 1, sp2);

  if (path == "/" || path.startsWith("/index")) { webdSendPage(); return; }
  if (path.startsWith("/api/status")) { webdSendStatusJson(); return; }
  if (path.startsWith("/api/sats"))   { webdSendSatsJson();   return; }
  if (path.startsWith("/api/passes")) { webdSendPassesJson(); return; }
  if (path.startsWith("/api/orbit"))  { webdSendOrbitJson();  return; }
  if (path == "/api/tx" || path.startsWith("/api/tx?")) {
    if (method != "POST") { webdSendTxJson(); return; }
    // POST /api/tx?i=N -- select transponder by index
    int q = path.indexOf("i=");
    int i = (q >= 0) ? (int)strtol(path.c_str() + q + 2, nullptr, 10) : -1;
    bool ok = false;
    if (i >= 0 && i < activeTxCount) {
      curTx = i; onTransponderChanged();
      if (radioOut && rig) {                          // mirror the on-device 't' cycle
        rig->enableSatMode(cfg.satMode && !txReceiveOnly());   // off for receive-only birds
        applyTransponderModes(activeTx[curTx]);
        lastRxSet = 0; lastUlHz = 0; uplinkDeferTicks = 0;
      }
      lastDrawMs = 0; ok = true;
    }
    webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                    "Connection:close\r\n\r\n"));
    webdCli.print(ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    return;
  }
  if (method == "POST" && path.startsWith("/api/cal")) {
    // POST /api/cal?dl=N&ul=N -- set RX/TX calibration directly (Hz). Either may
    // be omitted; values clamp to a sane +/-100 kHz to avoid fat-finger errors.
    auto getHz = [&](const char* key, int32_t cur)->int32_t {
      String k = String(key) + "=";
      int q = path.indexOf(k);
      if (q < 0) return cur;
      long v = strtol(path.c_str() + q + k.length(), nullptr, 10);
      if (v < -100000) v = -100000; if (v > 100000) v = 100000;
      return (int32_t)v;
    };
    calDl = getHz("dl", calDl);
    calUl = getHz("ul", calUl);
    cfg.calDlHz = calDl; cfg.calUlHz = calUl; cfg.save();
    { SatEntry* cs = activeSat(); if (cs) saveCalForSat(cs->norad); }
    lastDrawMs = 0;
    webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                    "Connection:close\r\n\r\n{\"ok\":true}"));
    return;
  }

  if (method == "POST" && path.startsWith("/api/select")) {
    int q = path.indexOf("norad=");
    uint32_t n = (q >= 0) ? (uint32_t)strtoul(path.c_str() + q + 6, nullptr, 10) : 0;
    bool ok = (n != 0) && webdSelectSat(n);
    webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                    "Connection:close\r\n\r\n"));
    webdCli.print(ok ? F("{\"ok\":true}") : F("{\"ok\":false}"));
    return;
  }
  if (method == "POST" && path.startsWith("/api/fav")) {
    int q = path.indexOf("norad=");
    uint32_t n = (q >= 0) ? (uint32_t)strtoul(path.c_str() + q + 6, nullptr, 10) : 0;
    bool nowFav = false;
    if (n != 0) { toggleFav(n); nowFav = isFav(n); }
    webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                    "Connection:close\r\n\r\n"));
    String o = "{\"ok\":"; o += (n ? "true" : "false");
    o += ",\"fav\":"; o += (nowFav ? "true" : "false"); o += "}";
    webdCli.print(o);
    return;
  }
  if (method == "POST" && path.startsWith("/api/cmd")) {
    int q = path.indexOf("k=");
    char k = 0;
    if (q >= 0) {
      String kv = path.substring(q + 2);
      int amp = kv.indexOf('&'); if (amp >= 0) kv = kv.substring(0, amp);
      // single char, URL-decoded for the few we use (',' '/' letters arrive raw;
      // '%2C'/'%2F' handled for safety).
      if (kv.startsWith("%2C") || kv.startsWith("%2c")) k = ',';
      else if (kv.startsWith("%2F") || kv.startsWith("%2f")) k = '/';
      else if (kv.length() > 0) k = kv[0];
    }
    // Only accept the in-place Track controls the page exposes; ignore anything
    // that would navigate or that isn't a safe live control. With man=1 the key
    // is run in Manual-calculator context instead (adds 'u' to swap the held leg).
    bool manual = (path.indexOf("man=1") >= 0);
    bool allowed = (k==','||k=='/'||k=='t'||k=='d'||k=='r'||k=='o'||k=='m'||k=='x'||k=='s'||k=='y'
                    || (manual && (k=='u'||k=='m'||k==','||k=='/'||k=='t'||k=='x'||k=='s')));
    if (allowed) {
      Screen save = screen;
      if (manual) {
        screen = SCR_MANUAL;          // run the control in the Manual calculator
        keyManual(k, false, false);
        if (screen == SCR_MANUAL) screen = save;
      } else {
        screen = SCR_TRACK;           // run the control in its native context
        keyTrack(k, false, false);
        if (screen == SCR_TRACK) screen = save;   // don't yank the device's own view
      }
      lastDrawMs = 0;
    }
    webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                    "Connection:close\r\n\r\n{\"ok\":true}"));
    return;
  }
  webdCli.print(F("HTTP/1.1 404 Not Found\r\nConnection:close\r\n\r\n"));
}

void App::webdSendPage() {
  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:text/html; charset=utf-8\r\n"
                  "Cache-Control:no-store\r\nConnection:close\r\n\r\n"));
  // Stream the PROGMEM page in chunks so we never copy it whole into RAM.
  const char* p = WEB_PAGE;
  char buf[128]; size_t i = 0;
  char c;
  while ((c = pgm_read_byte(p++)) != 0) {
    buf[i++] = c;
    if (i == sizeof(buf)) { webdCli.write((const uint8_t*)buf, i); i = 0; }
  }
  if (i) webdCli.write((const uint8_t*)buf, i);
}

void App::webdSendStatusJson() {
  SatEntry* s = activeSat();
  LiveLook L = (s && timeIsSet()) ? pred.look(nowUtc()) : LiveLook();
  uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
  if (s && activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
    Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
  }
  const char* mtag = (tuneMode == TM_FULL) ? "FULL" : (tuneMode == TM_DL) ? "DL"
                   : (tuneMode == TM_UL) ? "UL" : (trackMode == 0 ? "TUNE" : "CAL");

  // Manual-mode legs: the HOLD leg (parked nominal, no Doppler) and the TUNE leg
  // (Doppler-corrected, round-trip on a linear bird) -- the same numbers the
  // on-device Manual calculator shows, so the web page can offer manual tuning.
  bool haveUp = (ulOp != 0);
  bool fixUp  = haveUp && manFixUp;
  uint32_t holdHz = 0, tuneHz = 0;
  if (s && activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    bool linear = t.isLinear && t.bandwidth() > 0;
    int64_t dnPark = (int64_t)dlOp + calDl;
    int64_t upPark = (int64_t)ulOp + calUl;
    if (!fixUp) {
      holdHz = (uint32_t)(dnPark > 0 ? dnPark : 0);          // hold downlink
      tuneHz = (haveUp && linear)
             ? Predictor::uplinkForFixedDownlink(dlOp, ulOp, t.invert, L.rangeRate, calDl, calUl)
             : tx;
    } else {
      holdHz = (uint32_t)(upPark > 0 ? upPark : 0);          // hold uplink
      tuneHz = linear
             ? Predictor::downlinkForFixedUplink(dlOp, ulOp, t.invert, L.rangeRate, calDl, calUl)
             : rx;
    }
  }

  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                  "Connection:close\r\n\r\n"));
  String j = "{";
  j += "\"name\":\""; j += jsonEsc(s ? s->name : ""); j += "\",";
  j += "\"norad\":"; j += (s ? s->norad : 0); j += ",";
  j += "\"rx\":\""; j += (activeTxCount > 0 ? fmtMHz(rx) : String("--.-----")); j += "\",";
  j += "\"tx\":\""; j += ((activeTxCount > 0 && ulOp) ? fmtMHz(tx) : String("--.-----")); j += "\",";
  j += "\"az\":"; j += String(L.az, 1); j += ",";
  j += "\"el\":"; j += String(L.el, 1); j += ",";
  j += "\"mode\":\""; j += mtag; j += "\",";
  j += "\"radio\":"; j += (radioOut ? "true" : "false"); j += ",";
  j += "\"rot\":"; j += (rotOut ? "true" : "false"); j += ",";
  j += "\"fixup\":"; j += (fixUp ? "true" : "false"); j += ",";
  j += "\"haveup\":"; j += (haveUp ? "true" : "false"); j += ",";
  // Transponder context for the web radio panel: current index, count, the
  // current transponder's label, and the linear passband position (Hz from the
  // low edge) + total bandwidth so the page can show/set the operating point.
  j += "\"txi\":"; j += (activeTxCount > 0 ? curTx : -1); j += ",";
  j += "\"txn\":"; j += activeTxCount; j += ",";
  j += "\"txd\":\""; j += (activeTxCount > 0 ? jsonEsc(activeTx[curTx].desc) : String("")); j += "\",";
  { uint32_t bw = (activeTxCount > 0) ? activeTx[curTx].bandwidth() : 0;
    bool lin = (activeTxCount > 0) && activeTx[curTx].isLinear && bw > 0;
    j += "\"lin\":"; j += (lin ? "true" : "false"); j += ",";
    j += "\"pbOff\":"; j += (long)pbOffset; j += ",";
    j += "\"pbBw\":"; j += (long)bw; j += ","; }
  j += "\"caldl\":"; j += (long)calDl; j += ",";
  j += "\"calul\":"; j += (long)calUl; j += ",";
  j += "\"hold\":\""; j += (holdHz ? fmtMHz(holdHz) : String("--.-----")); j += "\",";
  j += "\"tune\":\""; j += (tuneHz ? fmtMHz(tuneHz) : String("--.-----")); j += "\"";
  j += "}";
  webdCli.print(j);
}

void App::webdSendSatsJson() {
  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                  "Connection:close\r\n\r\n["));
  // Favorites first; if none are marked, fall back to the whole catalog (capped).
  bool any = false; int emitted = 0;
  for (int i = 0; i < favN && emitted < 60; ++i) {
    int idx = db.indexOfNorad(favs[i]);
    if (idx < 0) continue;
    SatEntry& s = db.at(idx);
    if (any) webdCli.print(',');
    String o = "{\"n\":"; o += s.norad; o += ",\"name\":\""; o += jsonEsc(s.name); o += "\",\"fav\":true}";
    webdCli.print(o); any = true; emitted++;
  }
  if (!any) {
    int n = db.count();
    for (int i = 0; i < n && emitted < 60; ++i) {
      SatEntry& s = db.at(i);
      if (any) webdCli.print(',');
      String o = "{\"n\":"; o += s.norad; o += ",\"name\":\""; o += jsonEsc(s.name);
      o += "\",\"fav\":"; o += (isFav(s.norad) ? "true" : "false"); o += "}";
      webdCli.print(o); any = true; emitted++;
    }
  }
  webdCli.print(']');
}

void App::webdSendPassesJson() {
  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                  "Connection:close\r\n\r\n{\"passes\":["));
  SatEntry* s = activeSat();
  PassPredict pp[8];
  int n = 0;
  if (s && timeIsSet()) {
    n = pred.predictPasses(nowUtc(), cfg.minPassEl, pp, 8);
    for (int i = 0; i < n; ++i) {
      if (i) webdCli.print(',');
      String o = "{\"aos\":"; o += (uint32_t)pp[i].aos;
      o += ",\"los\":"; o += (uint32_t)pp[i].los;
      o += ",\"tca\":"; o += (uint32_t)pp[i].tca;
      o += ",\"el\":"; o += (int)(pp[i].maxEl + 0.5f);
      o += ",\"azaos\":"; o += (int)(pp[i].azAos + 0.5f);
      o += ",\"azlos\":"; o += (int)(pp[i].azLos + 0.5f);
      o += "}";
      webdCli.print(o);
    }
  }
  // Next-pass arc: az/el samples across the first upcoming pass, so the web sky
  // plot can draw the approaching pass while the satellite is below the horizon.
  webdCli.print(F("],\"arc\":["));
  if (s && timeIsSet() && n > 0) {
    pred.setSite(loc.obs()); pred.setSat(*s);
    const int NA = 24;
    double span = (double)(pp[0].los - pp[0].aos); if (span < 1) span = 1;
    for (int i = 0; i < NA; ++i) {
      time_t t = pp[0].aos + (time_t)llround(span * i / (double)(NA - 1));
      LiveLook L = pred.look(t);
      if (i) webdCli.print(',');
      String o = "["; o += String(L.az, 0); o += ","; o += String(L.el, 0); o += "]";
      webdCli.print(o);
    }
  }
  webdCli.print(F("]}"));
}

// GET /api/orbit -- the orbital-analysis numbers, the same values the nine
// on-device Orbit pages show, as one JSON object. buildOrbit() populates the
// member fields (decay, ascending node, pass-window stats, next-pass sunlit
// fraction); the per-page scalars below re-derive from the elements with the
// identical formulas drawOrbit() uses, so the web view never disagrees with the
// device. Two pages are curves: "track" (sub-points over the next ~2 orbits) and
// "doppler" (Hz across the next pass) are emitted as short arrays.
void App::webdSendOrbitJson() {
  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                  "Connection:close\r\n\r\n"));
  SatEntry* s = activeSat();
  if (!s || !timeIsSet() || s->meanMotion <= 0) { webdCli.print(F("{\"ok\":false}")); return; }

  buildOrbit(true);                              // populate the member fields (headless)
  pred.setSite(loc.obs()); pred.setSat(*s);
  time_t now = nowUtc();

  const double MU = 398600.4418, RE = 6378.137, D2R = 0.017453292519943295;
  const double C_KM = 299792.458, J2 = 0.00108262998905, DPD = 57.29577951308232 * 86400.0;
  double mm = s->meanMotion;
  double periodMin = (mm > 0) ? 1440.0 / mm : 0;
  double nrad = mm * 6.283185307179586 / 86400.0;
  double a = (nrad > 0) ? pow(MU / (nrad * nrad), 1.0 / 3.0) : 0;
  double apo = a * (1 + s->ecc) - RE, peri = a * (1 - s->ecc) - RE;
  auto fpDia = [&](double h){ return (h > 0) ? 2.0 * RE * acos(RE / (RE + h)) : 0.0; };

  LiveLook L = pred.look(now);
  double altNow = L.satAltKm;
  double maNow = fmod(s->ma + 360.0 * mm * (now - s->epochUnix) / 86400.0, 360.0);
  if (maNow < 0) maNow += 360.0;

  // J2 secular drifts.
  double ci = cos(s->incl * D2R), pp2 = a * (1.0 - s->ecc * s->ecc);
  double ReP2 = (pp2 > 0) ? (RE / pp2) * (RE / pp2) : 0.0;
  double Odot = -1.5 * nrad * J2 * ReP2 * ci * DPD;
  double Wdot = 0.75 * nrad * J2 * ReP2 * (5 * ci * ci - 1) * DPD;

  // Beta angle + eclipse fraction over one orbit.
  double beta = pred.betaAngleDeg(now, s->incl, s->raan);
  double hMean = a - RE;
  double betaStar = (hMean > 0) ? acos(RE / (RE + hMean)) / D2R : 0.0;
  int Necl = 120, ecl = 0;
  time_t period = (time_t)(periodMin * 60.0 + 0.5);
  for (int k = 0; period > 0 && k < Necl; ++k)
    if (!pred.sunlitAt(now + (time_t)((double)period * k / Necl))) ecl++;
  double fEcl = Necl ? (double)ecl / Necl : 0.0;

  // Orbit position: true anomaly via equation of centre, time to peri/apo.
  double periodS = (mm > 0) ? 86400.0 / mm : 0.0, e = s->ecc, M = maNow * D2R;
  double nu = M + (2*e - 0.25*e*e*e) * sin(M) + 1.25*e*e * sin(2*M)
            + (13.0/12.0)*e*e*e * sin(3*M);
  double u = fmod(s->argp + nu / D2R, 360.0); if (u < 0) u += 360.0;
  double toPeri = (360.0 - maNow) / 360.0 * periodS;
  double toApo  = fmod((180.0 - maNow + 360.0), 360.0) / 360.0 * periodS;

  auto num = [](double v, int dp){ char b[24]; snprintf(b, sizeof(b), "%.*f", dp, v); return String(b); };

  String j = "{\"ok\":true,";
  j += "\"name\":\""; j += s->name; j += "\",\"norad\":"; j += s->norad; j += ",";
  // page 0: info
  j += "\"alt\":"; j += num(altNow,0); j += ",\"footprint\":"; j += num(fpDia(altNow),0); j += ",";
  j += "\"period\":"; j += num(periodMin,1); j += ",\"apo\":"; j += num(apo,0);
  j += ",\"peri\":"; j += num(peri,0); j += ",\"sma\":"; j += num(a,0); j += ",";
  j += "\"incl\":"; j += num(s->incl,2); j += ",\"ecc\":"; j += num(s->ecc,5);
  j += ",\"bstar\":"; j += num(s->bstar,6); j += ",";
  j += "\"decayDays\":"; j += num(orbDecayDays,1);
  j += ",\"decayLo\":"; j += num(orbDecayLo,1); j += ",\"decayHi\":"; j += num(orbDecayHi,1); j += ",";
  j += "\"ascLon\":"; j += num(orbAscLon,1); j += ",\"ascT\":"; j += (uint32_t)orbAscT; j += ",";
  // page 1: live
  j += "\"az\":"; j += num(L.az,1); j += ",\"el\":"; j += num(L.el,1);
  j += ",\"range\":"; j += num(L.rangeKm,0); j += ",\"rangeRate\":"; j += num(L.rangeRate,3); j += ",";
  j += "\"subLat\":"; j += num(L.subLat,2); j += ",\"subLon\":"; j += num(L.subLon,2);
  j += ",\"sunlit\":"; j += (L.sunlit ? "true" : "false");
  j += ",\"eclDepth\":"; j += num(pred.eclipseDepthDeg(now),1); j += ",";
  // page 2: next pass
  j += "\"hasPass\":"; j += (orbHasPass ? "true" : "false");
  if (orbHasPass) {
    j += ",\"pAos\":"; j += (uint32_t)orbPass.aos; j += ",\"pLos\":"; j += (uint32_t)orbPass.los;
    j += ",\"pMaxEl\":"; j += num(orbPass.maxEl,1); j += ",\"pAzAos\":"; j += num(orbPass.azAos,0);
    j += ",\"pAzLos\":"; j += num(orbPass.azLos,0); j += ",\"pSunPct\":"; j += num(orbSunPct,0);
  }
  j += ",";
  // page 5: nodal / J2
  j += "\"nodeDrift\":"; j += num(Odot,3); j += ",\"perigDrift\":"; j += num(Wdot,3);
  j += ",\"sunSync\":"; j += ((fabs(Odot - 0.98565) < 0.05) ? "true" : "false"); j += ",";
  // page 6: sun / beta
  j += "\"beta\":"; j += num(beta,1); j += ",\"betaStar\":"; j += num(betaStar,1);
  j += ",\"eclFrac\":"; j += num(fEcl*100.0,0); j += ",";
  // page 7: outlook
  j += "\"outlookN\":"; j += orbOutlookN; j += ",\"outlookHi\":"; j += orbOutlookHi;
  j += ",\"bestEl\":"; j += num(orbBestEl,0); j += ",\"bestT\":"; j += (uint32_t)orbBestT;
  j += ",\"longestMin\":"; j += num(orbLongestMin,1); j += ",\"avgGapH\":"; j += num(orbAvgGapH,1); j += ",";
  // page 8: orbit position
  j += "\"meanAnom\":"; j += num(maNow,1); j += ",\"trueAnom\":"; j += num(nu/D2R,1);
  j += ",\"argLat\":"; j += num(u,1); j += ",\"toPeri\":"; j += (long)toPeri;
  j += ",\"toApo\":"; j += (long)toApo; j += ",\"argp\":"; j += num(s->argp,1);
  j += ",\"raan\":"; j += num(s->raan,1);
  webdCli.print(j);

  // page 3: ground track -- sub-points over the next ~2 orbits (downsampled).
  webdCli.print(F(",\"track\":["));
  { int N = 36; double pS = (mm > 0) ? 86400.0 / mm : 0;
    for (int i = 0; i <= N; ++i) {
      LiveLook P = pred.look(now + (time_t)(pS * 2 * i / N));
      if (i) webdCli.print(',');
      String o = "["; o += num(P.subLat,1); o += ","; o += num(P.subLon,1); o += "]";
      webdCli.print(o);
    }
  }
  webdCli.print(']');

  // page 4: doppler curve across the next pass (Hz at the beacon ref freq).
  webdCli.print(F(",\"doppler\":["));
  if (orbHasPass) {
    int N = 40; double dur = (double)(orbPass.los - orbPass.aos), fbHz = cfg.beaconMHz * 1e6;
    for (int i = 0; i < N; ++i) {
      double t = (double)orbPass.aos + dur * i / (N - 1);
      long dop = lround(-pred.rangeRateAt(t) / C_KM * fbHz);
      if (i) webdCli.print(',');
      webdCli.print(dop);
    }
  }
  webdCli.print(F("],\"beaconMHz\":"));
  webdCli.print(num(cfg.beaconMHz, 3));
  webdCli.print('}');
}
// (set the predictor, load transponders, recenter the passband). Returns false
// if the NORAD isn't in the catalog.
bool App::webdSelectSat(uint32_t norad) {
  int idx = db.indexOfNorad(norad);
  if (idx < 0) return false;
  SatEntry& s = db.at(idx);
  satSel = idx;
  pred.setSite(loc.obs());
  pred.setSat(s);
  ensureTransponders(s);
  onTransponderChanged();
  return true;
}

// GET /api/tx -- the transponder list for the active satellite, plus the current
// index, so the web radio panel can show a labelled dropdown and select by index.
void App::webdSendTxJson() {
  webdCli.print(F("HTTP/1.1 200 OK\r\nContent-Type:application/json\r\n"
                  "Connection:close\r\n\r\n"));
  String j = "{\"cur\":"; j += (activeTxCount > 0 ? curTx : -1);
  j += ",\"list\":[";
  for (int i = 0; i < activeTxCount; ++i) {
    if (i) j += ',';
    j += "{\"i\":"; j += i;
    j += ",\"d\":\""; j += jsonEsc(activeTx[i].desc); j += "\"";
    j += ",\"m\":\""; j += jsonEsc(activeTx[i].mode); j += "\"";
    j += ",\"lin\":"; j += (activeTx[i].isLinear ? "true" : "false"); j += "}";
  }
  j += "]}";
  webdCli.print(j);
}

// ---------------------------------------------------------------------------
//  Low-precision Sun & Moon topocentric az/el (degrees) for an observer.
//  Sun ~0.01 deg; Moon ~a few arc-min after the listed perturbations and a
//  topocentric-parallax correction - ample for pointing an antenna (sun/moon
//  noise, EME aiming). Self-contained; no dependence on the SGP4 propagator.
// ---------------------------------------------------------------------------
static void skyObjAzEl(time_t t, double obsLatDeg, double obsLonDeg, bool moon,
                       double& azOut, double& elOut) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232,
               TWO_PI_ = 6.283185307179586;
  double d  = ((double)t - 946728000.0) / 86400.0;          // days since J2000.0
  double ecl = (23.4393 - 3.563e-7 * d) * D2R;              // mean obliquity
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  if (gmst < 0) gmst += 360.0;
  double ra = 0, dec = 0, rEarthRad = 0;                    // ra/dec in radians
  if (!moon) {
    double g = (357.529 + 0.98560028 * d) * D2R;
    double L = fmod(280.459 + 0.98564736 * d, 360.0) * D2R;
    double lon = L + (1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;
    double X = cos(lon), Y = cos(ecl) * sin(lon), Z = sin(ecl) * sin(lon);
    ra = atan2(Y, X); dec = atan2(Z, sqrt(X * X + Y * Y));
  } else {
    double dS = ((double)t - 946598400.0) / 86400.0;        // Schlyter day count
    double N = (125.1228 - 0.0529538083 * dS) * D2R;
    double inc = 5.1454 * D2R;
    double w = (318.0634 + 0.1643573223 * dS) * D2R;
    double aa = 60.2666, ee = 0.054900;
    double M = (115.3654 + 13.0649929509 * dS) * D2R;
    double E = M + ee * sin(M) * (1 + ee * cos(M));
    for (int k = 0; k < 3; ++k) E -= (E - ee * sin(E) - M) / (1 - ee * cos(E));
    double xv = aa * (cos(E) - ee), yv = aa * sqrt(1 - ee * ee) * sin(E);
    double v = atan2(yv, xv), r = sqrt(xv * xv + yv * yv);
    double xh = r * (cos(N) * cos(v + w) - sin(N) * sin(v + w) * cos(inc));
    double yh = r * (sin(N) * cos(v + w) + cos(N) * sin(v + w) * cos(inc));
    double zh = r * (sin(v + w) * sin(inc));
    double lon = atan2(yh, xh), lat = atan2(zh, sqrt(xh * xh + yh * yh));
    double Ms = (356.0470 + 0.9856002585 * dS) * D2R;
    double ws = (282.9404 + 4.70935e-5 * dS) * D2R;
    double Ls = ws + Ms, Lm = N + w + M, Dm = Lm - Ls, F = Lm - N;
    lon += (-1.274 * sin(M - 2*Dm) + 0.658 * sin(2*Dm) - 0.186 * sin(Ms)
            - 0.059 * sin(2*M - 2*Dm) - 0.057 * sin(M - 2*Dm + Ms)
            + 0.053 * sin(M + 2*Dm) + 0.046 * sin(2*Dm - Ms) + 0.041 * sin(M - Ms)
            - 0.035 * sin(Dm) - 0.031 * sin(M + Ms) - 0.015 * sin(2*F - 2*Dm)
            + 0.011 * sin(M - 4*Dm)) * D2R;
    lat += (-0.173 * sin(F - 2*Dm) - 0.055 * sin(M - F - 2*Dm)
            - 0.046 * sin(M + F - 2*Dm) + 0.033 * sin(F + 2*Dm)
            + 0.017 * sin(2*M + F)) * D2R;
    r += (-0.58 * cos(M - 2*Dm) - 0.46 * cos(2*Dm));
    double xg = r * cos(lon) * cos(lat), yg = r * sin(lon) * cos(lat), zg = r * sin(lat);
    double xe = xg, ye = yg * cos(ecl) - zg * sin(ecl), ze = yg * sin(ecl) + zg * cos(ecl);
    ra = atan2(ye, xe); dec = atan2(ze, sqrt(xe * xe + ye * ye)); rEarthRad = r;
  }
  double latR = obsLatDeg * D2R;
  double lst = (gmst + obsLonDeg) * D2R;                    // local sidereal time
  double ha = lst - ra;
  if (moon && rEarthRad > 0) {                              // topocentric parallax
    double mpar = asin(1.0 / rEarthRad);
    double gclat = latR - 0.1924 * D2R * sin(2 * latR);
    double rho = 0.99833 + 0.00167 * cos(2 * latR);
    double g2 = atan2(tan(gclat), cos(ha));
    ra  -= mpar * rho * cos(gclat) * sin(ha) / cos(dec);
    dec -= mpar * rho * sin(gclat) * sin(g2 - dec) / sin(g2);
    ha = lst - ra;
  }
  double sinAlt = sin(latR) * sin(dec) + cos(latR) * cos(dec) * cos(ha);
  double alt = asin(sinAlt);
  double cosA = (sin(dec) - sin(latR) * sinAlt) / (cos(latR) * cos(alt));
  if (cosA > 1) cosA = 1; if (cosA < -1) cosA = -1;
  double A = acos(cosA);
  if (sin(ha) > 0) A = TWO_PI_ - A;
  azOut = A * R2D; elOut = alt * R2D;
}

// Compute the mode-aware, TCA-adaptive CAT write deadband and the TCA-tapered
// predictive lead range rate. See the constants in app.h for the breakpoints.
// All three ideas are gated to do nothing harmful at low slew / low range rate,
// so on slow passes this reduces to "base threshold, no lead".
uint32_t App::dopplerThreshAndLead(double rrNow, uint32_t centerHz, bool linear,
                                   double nowSec, double* leadRrOut) {
  // Sample the range rate ~1 s ahead once; used for both slew (item 2) and the
  // lead-blend slope (item 3). One extra propagation per service tick.
  double rrAhead = pred.rangeRateAt(nowSec + 1.0);
  double slopeKmS2 = fabs(rrAhead - rrNow) / 1.0;        // km/s^2

  // (2) Doppler slew at the higher leg frequency: |f * d(rr)/dt / c|, Hz/s.
  double slewHzS = fabs((double)centerHz * slopeKmS2 / 299792458.0);

  // Base deadband by mode (user-tunable in Settings), then adaptively tighten
  // when slew is high.
  uint32_t base = linear ? cfg.doppThreshLinHz : cfg.doppThreshFmHz;
  uint32_t thresh = base;
  if (slewHzS > DOPP_SLEW_START_HZS) {
    uint32_t reduced = base / 2; if (reduced < DOPP_THRESH_MIN_HZ) reduced = DOPP_THRESH_MIN_HZ;
    if (reduced > base) reduced = base;                  // never raise above base
    if (slewHzS >= DOPP_SLEW_FULL_HZS) {
      thresh = reduced;
    } else {
      float f = (float)((slewHzS - DOPP_SLEW_START_HZS) /
                        (DOPP_SLEW_FULL_HZS - DOPP_SLEW_START_HZS));
      thresh = (uint32_t)lround(base - f * (double)(base - reduced));
    }
  }

  // (3) TCA-tapered predictive lead. Blend toward the look-ahead range rate in
  // proportion to the slope (how fast Doppler is changing), then taper that
  // blend to zero as |range rate| -> 0 (near TCA a forward lead overshoots).
  double leadRr = rrNow;
  if (leadRrOut && cfg.doppLeadMs > 0) {
    float slopeBlend;
    if (slopeKmS2 <= DOPP_LEAD_SLOPE_START)      slopeBlend = 0.0f;
    else if (slopeKmS2 >= DOPP_LEAD_SLOPE_FULL)  slopeBlend = 1.0f;
    else slopeBlend = (float)((slopeKmS2 - DOPP_LEAD_SLOPE_START) /
                              (DOPP_LEAD_SLOPE_FULL - DOPP_LEAD_SLOPE_START));
    float taper = (float)fmin(fabs(rrNow) / DOPP_LEAD_TAPER_KMS, 1.0);
    float blend = slopeBlend * taper;
    if (blend > 0.0f) {
      // Range rate at now + lead, then blend in by `blend`. Lead capped small.
      double rrLead = pred.rangeRateAt(nowSec + (double)cfg.doppLeadMs / 1000.0);
      leadRr = rrNow + (rrLead - rrNow) * blend;
    }
  }
  if (leadRrOut) *leadRrOut = leadRr;
  return thresh;
}

void App::loop() {
  M5Cardputer.update();
  if (rig) rig->service();    // net CAT (Icom LAN): advance connect + keepalives
  if (rot) rot->service();    // self-driven rotators (Yaesu direct) run their loop here
  serviceRigctld();           // rigctld TCP server: let a PC drive the rig via CardSat
  serviceRotctld();           // rotctld TCP server: let a PC drive the wired rotator
  serviceWebd();              // mobile web control page (opt-in, over the WiFi LAN)
  memo.poll();                // voice memo: append one block if recording (SD only)
  irBeacon.service();         // IR pass-alert beacon: advance any queued flashes
  loraPoll();                 // LoRa messaging: drain any received frame (no-op if off)
  // If the socket-recovery path exhausted its hard resets, surface a reboot
  // prompt once (don't reboot silently). The user decides; the flag is cleared
  // so we don't re-enter while they're on the prompt.
  if (net.recoverExhausted && screen != SCR_NETREBOOT) {
    net.recoverExhausted = false;
    netRebootReturn = screen;
    screen = SCR_NETREBOOT; lastDrawMs = 0;
  }
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
  tickCanvasRestore();         // retry sprite realloc if a post-fetch restore failed

  if (satsatJobPhase != 0) satsatJobTick();   // advance the incremental sat-to-sat search

  // Accelerometer (tilt) passband tuning -- opt-in, ADV-only, self-gated to the
  // Track/Big/Manual screens in TUNE mode. Keep the backlight awake while it's
  // actively moving the passband so the operator can see the readout change.
  if (cfg.tiltTune && imuReady) {
    int32_t before = pbOffset;
    serviceTiltTune();
    if (pbOffset != before) lastInputMs = millis();   // count tilt as activity
  }

  // Keyboard
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto ks = M5Cardputer.Keyboard.keysState();
    char c = ks.word.empty() ? 0 : ks.word.front();
    lastInputMs = millis();
    if (screenAsleep) {                       // first key just wakes the display
      M5Cardputer.Display.setBrightness(cfg.bright);
      screenAsleep = false;
    } else {
      handleKey(c, ks.enter, ks.del);
    }
    draw();
  }

  // Real-time Doppler service -- runs whenever the radio is engaged ('r'),
  // independent of the current screen, so CAT keeps tracking while you make a log
  // entry, browse passes, etc. The radioOut guard makes it a no-op otherwise.
  uint32_t ms = millis();
  if (radioOut && rig && rig->ready() && ms - lastDoppMs >= effectiveCatRateMs()) {
      lastDoppMs = ms;
      SatEntry* s = activeSat();
      if (s && activeTxCount > 0 && timeIsSet()) {
        LiveLook L = pred.look(nowUtc());
        // Sub-second Doppler: recompute the range rate at the exact instant --
        // the integer-second look() value lags near TCA. Velocity-based.
        { struct timeval tv; gettimeofday(&tv, nullptr);
          L.rangeRate = pred.rangeRateAt((double)tv.tv_sec + tv.tv_usec * 1e-6); }
        Transponder& t = activeTx[curTx];
        uint32_t dlOp, ulOp, rx, tx;
        // Mode-aware, TCA-adaptive write deadband (1,2) + TCA-tapered lead (3).
        // centerHz = higher leg, used for the slew estimate. Lead-adjust the
        // range rate that feeds the Doppler correction.
        double nowSec; { struct timeval tv2; gettimeofday(&tv2, nullptr);
                         nowSec = (double)tv2.tv_sec + tv2.tv_usec * 1e-6; }
        uint32_t centerHz = (t.uplink > t.downlink) ? t.uplink : t.downlink;
        double leadRr = L.rangeRate;
        uint32_t threshHz = dopplerThreshAndLead(L.rangeRate, centerHz, t.isLinear,
                                                 nowSec, &leadRr);
        bool otr   = (tuneMode == TM_FULL || tuneMode == TM_DL) &&
                     t.isLinear && rig->canReadFreq();
        bool drvDL = (tuneMode != TM_UL);   // downlink driven in FULL/DL/HOLD
        bool drvUL = (tuneMode != TM_DL);   // uplink driven in FULL/UL/HOLD
        if (otr) {
          // ---- One True Rule (KB5MU): hold a constant frequency AT THE
          // SATELLITE while the operator tunes the rig's knob. Read the downlink
          // the operator is on, back out Doppler to recover their chosen spot in
          // the passband, then Doppler-correct BOTH legs around that fixed
          // satellite frequency. Let go of the knob and nothing drifts.
          uint32_t rxNow; bool txNow = false;
          bool transmitting = rig->readPtt(txNow) && txNow;
          bool knobMoved = false;
          // Skip the knob read on (re)sync (lastRxSet==0): PUSH our current
          // passband point to the rig instead of adopting whatever freq it is
          // parked on (push-then-track). Also skip while transmitting -- the rig
          // reports the TX VFO then, which would look like a wild dial jump.
          if (lastRxSet != 0 && !transmitting && rigReadDownlinkFreq(rxNow)) {
            double beta = L.rangeRate * 1000.0 / 299792458.0;
            // lastRxSet is the actual read-back freq, so a difference beyond the
            // threshold is a deliberate operator knob move, not rig rounding.
            uint32_t dHz = (rxNow > lastRxSet) ? rxNow - lastRxSet : lastRxSet - rxNow;
            if (dHz > KNOB_MOVE_HZ) {
              double dlSat = ((double)rxNow - (double)calDl) / (1.0 - beta);
              int32_t off = (int32_t)llround(dlSat - (double)t.downlink);
              int32_t bw  = (int32_t)t.bandwidth();
              if (off < 0) off = 0; if (off > bw) off = bw;
              pbOffset = off;                       // new fixed satellite point
              knobMoved = true;
            }
          }
          Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
          Predictor::dopplerFreqs(dlOp, ulOp, leadRr, calDl, calUl, rx, tx);
          // Downlink first; read it back (unless transmitting) so the rig's
          // rounding can't later look like a knob move.
          bool dlWrote = (drvDL && t.downlink && driveDownlink(rx, !transmitting, threshHz));
          // Uplink with a one-tick defer after a downlink write or operator knob
          // move (OscarWatch "defer uplink after a dial move"): consume any pending
          // defer, drive the uplink only if not currently deferred, then re-arm if
          // this tick wrote/moved the downlink. The "&& ulOk" guard means that
          // during a fast Doppler slew (downlink writing every tick) the uplink
          // still services every other tick instead of starving.
          driveUplinkDeferred(tx, threshHz, (dlWrote || knobMoved), drvUL && t.uplink);
        } else {
          Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
          Predictor::dopplerFreqs(dlOp, ulOp, leadRr, calDl, calUl, rx, tx);
          bool dlWrote = (drvDL && t.downlink && driveDownlink(rx, false, threshHz));  // HOLD/UL: no knob follow
          driveUplinkDeferred(tx, threshHz, dlWrote, drvUL && t.uplink);
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
          // Decide once per pass whether to flip (moves the az discontinuity of a
          // high pass onto the elevation axis); hold the choice until LOS so it
          // can't toggle mid-pass. Derive it here if we engaged mid-pass.
          time_t nowT = nowUtc();
          if (nowT >= rotFlipUntil) {
            time_t losT = nowT; double aa, ee;
            for (int k = 1; k <= 120; ++k) {            // forward-scan to LOS (<=60 min)
              time_t tt = nowT + (time_t)k * 30;
              if (!pred.azelAt(tt, aa, ee) || ee < 0.0) break;
              losT = tt;
            }
            rotFlipPass = passNeedsFlip(nowT, losT);
            rotFlipUntil = losT;
          }
          float az = (float)L.az + cfg.rotAzOff;
          float el = (float)L.el + cfg.rotElOff;
          if (cfg.rotFlip && rotFlipPass) { az += 180.0f; el = 180.0f - el; }
          while (az >= 360.0f) az -= 360.0f;
          while (az < 0.0f)    az += 360.0f;
          // 450-overlap lookahead: predict the bearing cfg.rotAzLookSec ahead
          // and pre-commit to the 361-450 band if a north wrap is imminent. GUARDS:
          // (1) only on a 450 rotator, (2) never when flipped (az+180 makes the
          // overlap reasoning meaningless -- OscarWatch nulls the ahead-az here).
          // cfg.rotAzLookSec == 0 disables the lookahead (reactive overlap only).
          rotAz450PreCommit = false;
          if (cfg.rotAzRange == ROT_AZ_450 && cfg.rotAzLookSec > 0 &&
              !(cfg.rotFlip && rotFlipPass)) {
            double aAz, aEl;
            if (pred.azelAt(nowT + (time_t)cfg.rotAzLookSec, aAz, aEl)) {
              float ahead = (float)aAz + cfg.rotAzOff;
              while (ahead >= 360.0f) ahead -= 360.0f;
              while (ahead < 0.0f)    ahead += 360.0f;
              // Currently low/east of north and about to wrap west across north:
              // commit to the overlap now so the post-north move is short.
              if (az <= 90.0f && ahead > 270.0f) rotAz450PreCommit = true;
            }
          }
          float elMax = cfg.rotFlip ? 180.0f : 90.0f;
          if (el < 0) el = 0; if (el > elMax) el = elMax;
          if (lastAzCmd < -500.0f ||
              fabsf(az - lastAzCmd) >= (float)cfg.rotDeadband ||
              fabsf(el - lastElCmd) >= (float)cfg.rotDeadband) {
            rotPoint(az, el);
            lastAzCmd = az; lastElCmd = el; rotParked = false;
          }
        } else {                                        // below horizon
          rotAz450PreCommit = false;   // clear any hint set but not consumed above
          // Pre-position: slew to the next rise bearing shortly before AOS so a
          // slow rotator is already aimed when the satellite appears.
          time_t now = nowUtc();
          bool prePos = false;
          if (cfg.rotLeadSec > 0) {
            if (!rotPassValid || now >= rotPass.los || rotPassNorad != s->norad) {
              if (rotPassNorad != s->norad || ms - lastRotPassMs > 30000) {
                lastRotPassMs = ms; rotPassNorad = s->norad;
                PassPredict p;
                rotPassValid = (pred.predictPasses(now, cfg.minPassEl, &p, 1) >= 1);
                if (rotPassValid) { rotPass = p;
                  rotFlipPass = passNeedsFlip(rotPass.aos, rotPass.los);
                  rotFlipUntil = rotPass.los; }
              }
            }
            if (rotPassValid && rotPass.aos > now &&
                (rotPass.aos - now) <= (time_t)cfg.rotLeadSec) {
              float az = (float)rotPass.azAos + cfg.rotAzOff;
              float el = (float)cfg.rotElOff;            // aim at the horizon (el 0)
              if (cfg.rotFlip && rotFlipPass) { az += 180.0f; el = 180.0f - el; }
              while (az >= 360.0f) az -= 360.0f;
              while (az < 0.0f)    az += 360.0f;
              float elMax = cfg.rotFlip ? 180.0f : 90.0f;
              if (el < 0) el = 0; if (el > elMax) el = elMax;
              if (lastAzCmd < -500.0f ||
                  fabsf(az - lastAzCmd) >= (float)cfg.rotDeadband ||
                  fabsf(el - lastElCmd) >= (float)cfg.rotDeadband) {
                rotPoint(az, el);
                lastAzCmd = az; lastElCmd = el; rotParked = false;
              }
              prePos = true;
            }
          }
          if (!prePos && !rotParked) {                  // nothing imminent: park once
            rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl);
            rotParked = true; lastAzCmd = lastElCmd = -999.0f;
          }
        }
      }
    }
    // Sun/Moon rotator pointing (independent of satellite tracking; smOut is set
    // only while the Sun/Moon screen is open). Slow rotator -> ~1 Hz + deadband.
    if (smOut && rot && rot->ready() && ms - lastRotMs > 1000) {
      lastRotMs = ms;
      Observer o = loc.obs();
      if (o.valid && timeIsSet()) {
        double az, el; skyObjAzEl(nowUtc(), o.lat, o.lon, smSel == 1, az, el);
        if (el < 0.0) {                        // object set: park once, resume on rise
          if (!rotParked) { rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl);
                            rotParked = true; lastAzCmd = lastElCmd = -999.0f; }
        } else {
          float a = (float)az + cfg.rotAzOff, e = (float)el + cfg.rotElOff;
          while (a >= 360.0f) a -= 360.0f; while (a < 0.0f) a += 360.0f;
          if (e < 0) e = 0; if (e > 90) e = 90;
          if (lastAzCmd < -500.0f ||
              fabsf(a - lastAzCmd) >= (float)cfg.rotDeadband ||
              fabsf(e - lastElCmd) >= (float)cfg.rotDeadband) {
            rotPoint(a, e); lastAzCmd = a; lastElCmd = e; rotParked = false;
          }
        }
      }
    }
  // Redraw cadence is still screen-dependent (the radio/rotator service above is
  // not): refresh the live screens periodically; static ones redraw on keypress.
  if (screen == SCR_TRACK || screen == SCR_BIG || screen == SCR_MANUALBIG || screen == SCR_POLAR || screen == SCR_WORLDMAP ||
      screen == SCR_ROTMAN || screen == SCR_GPS || screen == SCR_MANUAL ||
      (screen == SCR_ORBIT && orbitPage <= 2) || screen == SCR_SUNMOON ||
      screen == SCR_GRID || screen == SCR_STATES || screen == SCR_DXCC ||
      (screen == SCR_MEMOS && memo.isRecording()) || screen == SCR_OSCAR || screen == SCR_GLOBE || screen == SCR_DXDOPP || screen == SCR_SKYMAP || screen == SCR_GPSPOS) {
    if (ms - lastDrawMs > 500) { lastDrawMs = ms; draw(); }
  } else if (screen == SCR_PASSES || screen == SCR_HOME ||
             screen == SCR_SCHEDULE || screen == SCR_PASSDETAIL) {
    if (ms - lastDrawMs > 1000) { lastDrawMs = ms; draw(); }  // live clock / countdown
  }

  // While an AOS alarm is flashing or counting down, animate on any screen.
  long dt = (nextAos && timeIsSet()) ? (long)(nextAos - nowUtc()) : 999999;
  bool alarmActive = (millis() < aosFlashUntil) || (cfg.aosAlarm && dt <= 60 && dt > -2);
  if (alarmActive && ms - lastDrawMs > 500) { lastDrawMs = ms; draw(); }

  // Screen power management: blank the backlight after inactivity. Never while
  // actively tracking (radio/rotator) or alarming; any key wakes it (above).
  if (!screenAsleep) {
    if (cfg.dimSecs && !radioOut && !rotOut && !alarmActive &&
        (millis() - lastInputMs) > (uint32_t)cfg.dimSecs * 1000) {
      M5Cardputer.Display.setBrightness(0);
      screenAsleep = true;
    }
  } else if (alarmActive) {                    // an AOS alarm wakes the display
    M5Cardputer.Display.setBrightness(cfg.bright);
    screenAsleep = false;
  }
}

// ===========================================================================
//  Input dispatch
// ===========================================================================
// Capture the current screen to /CardSat/Screenshots/shot_NNNN.bmp on the SD
// card (24-bit BMP, no library). Bound to the 'b' key; a short beep confirms.
// Requires SD-card storage; a low beep means no SD (running on LittleFS).
void App::takeScreenshot() {
  if (!Store::onSD()) { beep(300, 120); return; }
  fs::FS& fsx = Store::fs();
  if (!fsx.exists("/CardSat/Screenshots")) fsx.mkdir("/CardSat/Screenshots");

  static uint32_t seq = 1;
  char path[48];
  do { snprintf(path, sizeof(path), "/CardSat/Screenshots/shot_%04lu.bmp",
                (unsigned long)seq++); }
  while (fsx.exists(path));

  File fp = fsx.open(path, "w");
  if (!fp) { beep(300, 120); return; }

  const int W = canvas.width(), H = canvas.height();
  const uint32_t rowBytes = (uint32_t)W * 3;
  const uint32_t imgSize  = rowBytes * (uint32_t)H;
  const uint32_t fileSize = 54 + imgSize;
  uint8_t hdr[54]; memset(hdr, 0, sizeof(hdr));
  hdr[0]='B'; hdr[1]='M';
  hdr[2]=fileSize; hdr[3]=fileSize>>8; hdr[4]=fileSize>>16; hdr[5]=fileSize>>24;
  hdr[10]=54;                       // pixel-data offset
  hdr[14]=40;                       // info header size
  hdr[18]=W & 0xFF; hdr[19]=(W>>8) & 0xFF;
  hdr[22]=H & 0xFF; hdr[23]=(H>>8) & 0xFF;   // +H => bottom-up rows
  hdr[26]=1;                        // planes
  hdr[28]=24;                       // bits per pixel
  hdr[34]=imgSize; hdr[35]=imgSize>>8; hdr[36]=imgSize>>16; hdr[37]=imgSize>>24;
  hdr[38]=0x13; hdr[39]=0x0B;       // 2835 ppm (x)
  hdr[42]=0x13; hdr[43]=0x0B;       // 2835 ppm (y)
  fp.write(hdr, 54);

  // Read each row as RGB888 with the documented readRectRGB(): M5GFX converts
  // from the sprite's native format (no RGB565 byte-order ambiguity), and the
  // public RGBColor type exposes named .r/.g/.b members (layout-independent).
  // A 24-bit BMP stores pixels as B,G,R, so emit them in that order.
  static RGBColor line[256];
  static uint8_t  row[256 * 3];
  for (int y = H - 1; y >= 0; --y) {            // BMP rows are bottom-up
    canvas.readRectRGB(0, y, W, 1, line);
    for (int x = 0; x < W; ++x) {
      row[x*3 + 0] = line[x].b;                 // BMP stores BGR
      row[x*3 + 1] = line[x].g;
      row[x*3 + 2] = line[x].r;
    }
    fp.write(row, rowBytes);
  }
  fp.close();
  beep(1800, 40);                   // short confirmation chirp
}

void App::handleKey(char c, bool enter, bool back) {
  // Hidden screenshot hotkey: 'b' saves a BMP of the screen to the SD card.
  // Skipped during text entry (SCR_EDIT) so it can still be typed normally.
  if (c == 'b' && screen != SCR_EDIT) { takeScreenshot(); return; }
  // Help is reachable with 'h' from anywhere except while typing (SCR_EDIT).
  if (c == 'h' && screen != SCR_EDIT && screen != SCR_HELP) {
    helpReturn = screen; helpScroll = 0; screen = SCR_HELP; lastDrawMs = 0; return;
  }
  switch (screen) {
    case SCR_HOME:     keyHome(c, enter, back); break;
    case SCR_SATLIST:  keySatList(c, enter, back); break;
    case SCR_SCHEDULE: keySchedule(c, enter, back); break;
    case SCR_PASSES:   keyPasses(c, enter, back); break;
    case SCR_PASSDETAIL: keyPassDetail(c, enter, back); break;
    case SCR_TRACK:    keyTrack(c, enter, back); break;
    case SCR_BIG:      keyBig(c, enter, back); break;
    case SCR_MANUALBIG: keyManualBig(c, enter, back); break;
    case SCR_MANUAL:   keyManual(c, enter, back); break;
    case SCR_POLAR:    keyPolar(c, enter, back); break;
    case SCR_PASSPOLAR: keyPassPolar(c, enter, back); break;
    case SCR_MUTUAL:   keyMutual(c, enter, back); break;
    case SCR_VIS:      keyVis(c, enter, back); break;
    case SCR_ILLUM:    keyIllum(c, enter, back); break;
    case SCR_LOCATION: keyLocation(c, enter, back); break;
    case SCR_UPDATE:   keyUpdate(c, enter, back); break;
    case SCR_SETTINGS: keySettings(c, enter, back); break;
    case SCR_EDIT:     keyEdit(c, enter, back); break;
    case SCR_WIFISCAN: keyWifiScan(c, enter, back); break;
    case SCR_ABOUT:    keyAbout(c, enter, back); break;
    case SCR_NETREBOOT: keyNetReboot(c, enter, back); break;
    case SCR_MEMOS:    keyMemos(c, enter, back); break;
    case SCR_OSCAR:    keyOscar(c, enter, back); break;
    case SCR_GLOBE:    keyGlobe(c, enter, back); break;
    case SCR_DXDOPP:   keyDxDopp(c, enter, back); break;
    case SCR_SKYMAP:   keySkyMap(c, enter, back); break;
    case SCR_GPSPOS:   keyGpsPos(c, enter, back); break;
    case SCR_SATSAT:   keySatSat(c, enter, back); break;
    case SCR_MESSAGES: keyMessages(c, enter, back); break;
    case SCR_LOG:      keyLog(c, enter, back); break;
    case SCR_LOGENTRY: keyLogEntry(c, enter, back); break;
    case SCR_LOGLIST:  keyLogList(c, enter, back); break;
    case SCR_WORLDMAP: keyWorldMap(c, enter, back); break;
    case SCR_ROTMAN:   keyRotMan(c, enter, back); break;
    case SCR_GPS:      keyGps(c, enter, back); break;
    case SCR_HELP:     keyHelp(c, enter, back); break;
    case SCR_CATTEST:  keyCatTest(c, enter, back); break;
    case SCR_ORBIT:    keyOrbit(c, enter, back); break;
    case SCR_SIM:      keySim(c, enter, back); break;
    case SCR_SUNMOON:  keySunMoon(c, enter, back); break;
    case SCR_SPACEWX:  keySpaceWx(c, enter, back); break;
    case SCR_TXDB:     keyTxDb(c, enter, back); break;
    case SCR_EQX:      keyEqx(c, enter, back); break;
    case SCR_QRZ:      keyQrz(c, enter, back); break;
    case SCR_WEATHER:  keyWeather(c, enter, back); break;
    case SCR_GRID:     keyGrid(c, enter, back); break;
    case SCR_STATES:   keyStates(c, enter, back); break;
    case SCR_DXCC:     keyDxcc(c, enter, back); break;
    case SCR_GPSRC:    keyGpSrc(c, enter, back); break;
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
  const int N = 15;
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
      case 4: mapHi = -1; mapReturn = SCR_HOME; screen = SCR_WORLDMAP; lastDrawMs = 0; break;  // all footprints
      case 5: screen = SCR_SUNMOON; lastDrawMs = 0; break;
      case 6: screen = SCR_SPACEWX; lastDrawMs = 0; break;
      case 7: // Weather: refresh on entry if WiFi already up, else show cache
        if (net.connected()) {
          const Observer& o = loc.obs();
          if (o.lat != 0.0 || o.lon != 0.0) fetchWeather();
        }
        screen = SCR_WEATHER; lastDrawMs = 0; break;
      case 8: qrzHaveResult = false; qrzScroll = 0; screen = SCR_QRZ; lastDrawMs = 0; break;
      case 9: screen = SCR_LOCATION; break;
      case 10: screen = SCR_UPDATE; break;
      case 11: setSel = 0; setCat = -1; screen = SCR_SETTINGS; break;
      case 12: logMenuSel = 0; screen = SCR_LOG; break;
      case 13: msgScroll = 0; screen = SCR_MESSAGES; lastDrawMs = 0; break;
      case 14: screen = SCR_ABOUT; break;
    }
  }
}

// ---- Voice-memo browser (SCR_MEMOS) ---------------------------------------
// (Re)enumerate the memos on the SD card into memos[], newest first.
void App::buildMemoList() {
  memoN = VoiceMemo::listMemos(memos, MEMO_LIST_MAX);
  if (memoSel >= memoN) memoSel = memoN > 0 ? memoN - 1 : 0;
}

// Poll the keyboard directly for a cancel during blocking playback. Any keypress
// (or the back/ESC key) cancels. Static so it can be passed as a plain function
// pointer to VoiceMemo::playMemo.
static bool memoPlaybackCancel() {
  M5Cardputer.update();
  return M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
}

void App::drawMemos() {
  header("Voice Memos");
  canvas.setTextSize(1);

  if (!Store::onSD()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("SD card required for voice memos.");
    footer("` back");
    return;
  }

  // While a standalone recording is running, show a prominent REC banner; the
  // blinking badge is also drawn by drawMemoIndicator() via the normal overlay.
  if (memo.isRecording()) {
    canvas.fillRect(0, 40, 240, 40, CL_BLACK);
    canvas.setTextColor(CL_RED, CL_BLACK);
    canvas.setTextSize(2);
    canvas.setCursor(40, 48);
    canvas.printf("REC  %lus", (unsigned long)memo.elapsedMs() / 1000);
    canvas.setTextSize(1);
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(30, 70); canvas.print("Recording a standalone memo");
    footer("any key = stop & save");
    return;
  }

  if (memoN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 42); canvas.print("No voice memos yet.");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 54); canvas.print("n = record  ('v' on a Track screen too)");
    footer("n new  r refresh  ` back");
    return;
  }

  // Column header.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 18); canvas.print("Date   Time   Satellite    Len");

  const int VIS = 8;
  if (memoSel < memoScroll)            memoScroll = memoSel;
  if (memoSel > memoScroll + VIS - 1)  memoScroll = memoSel - VIS + 1;
  if (memoScroll < 0) memoScroll = 0;

  for (int r = 0; r < VIS && (memoScroll + r) < memoN; ++r) {
    int i = memoScroll + r, y = 30 + r * 11;
    const VoiceMemo::MemoEntry& m = memos[i];
    if (i == memoSel) { canvas.fillRect(0, y - 2, 240, 11, CL_SELBG);
                        canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                canvas.setTextColor(CL_WHITE, CL_BLACK);
    char line[48];
    char sat[10];
    snprintf(sat, sizeof(sat), "%-9.9s", m.sat[0] ? m.sat : "-");
    if (m.haveTime)
      snprintf(line, sizeof(line), "%02d-%02d  %02d:%02d  %s %lu:%02lu",
               m.mon, m.day, m.hh, m.mm, sat,
               (unsigned long)(m.secs / 60), (unsigned long)(m.secs % 60));
    else
      snprintf(line, sizeof(line), "  --     --   %s %lu:%02lu", sat,
               (unsigned long)(m.secs / 60), (unsigned long)(m.secs % 60));
    canvas.setCursor(4, y); canvas.print(line);
  }

  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (memoScroll > 0)              { canvas.setCursor(232, 30);  canvas.print("^"); }
  if (memoScroll + VIS < memoN)    { canvas.setCursor(232, 118); canvas.print("v"); }

  if (memoConfirmDel) {
    canvas.fillRect(20, 50, 200, 34, CL_BLACK);
    canvas.drawRect(20, 50, 200, 34, CL_RED);
    canvas.setTextColor(CL_RED, CL_BLACK);
    canvas.setCursor(28, 56); canvas.print("Delete this memo?");
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(28, 70); canvas.print("ENT = delete   ` = cancel");
    footer("");
  } else {
    footer("ENT play  n new  d del  r refresh  ` back");
  }
}

void App::keyMemos(char c, bool enter, bool back) {
  // If a standalone recording (started here with 'n') is in progress, any key
  // stops it and refreshes the list. This mirrors the toggle behavior on the
  // Track screens but keeps the user on the browser.
  if (memo.isRecording()) {
    memo.stop();
    setStatus("Memo saved");
    buildMemoList();
    memoSel = 0; memoScroll = 0;
    lastDrawMs = 0;
    return;
  }

  if (memoConfirmDel) {
    if (enter) {
      if (memoSel >= 0 && memoSel < memoN) {
        if (VoiceMemo::deleteMemo(memos[memoSel].file)) setStatus("Memo deleted");
        else                                            setStatus("Delete failed");
        buildMemoList();
      }
      memoConfirmDel = false;
    } else if (back) {
      memoConfirmDel = false;
    }
    lastDrawMs = 0;
    return;
  }

  if (isBack(c, back)) { logMenuSel = 3; screen = SCR_LOG; lastDrawMs = 0; return; }

  if (memoN > 0) {
    if (isUp(c))   memoSel = (memoSel + memoN - 1) % memoN;
    if (isDown(c)) memoSel = (memoSel + 1) % memoN;
  }
  if (c == 'n') {                       // record a new standalone memo (no satellite)
    if (memo.start(nullptr)) setStatus("Recording... (any key stops)");
    else                     setStatus(String("Memo: ") + memo.lastError());
    lastDrawMs = 0;
    return;
  }
  if (c == 'r') { buildMemoList(); setStatus("Memo list refreshed"); }
  if (c == 'd' && memoN > 0) { memoConfirmDel = true; }
  if (enter && memoN > 0 && memoSel < memoN) {
    setStatus("Playing... (any key stops)");
    draw();                              // show the status before we block
    bool ok = memo.playMemo(memos[memoSel].file, &memoPlaybackCancel);
    setStatus(ok ? "Playback done" : (String("Play: ") + memo.lastError()));
  }
  lastDrawMs = 0;
}

void App::keyOscar(char c, bool enter, bool back) {
  if (isBack(c, back)) { buildSatView(); screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (c == 'm') { oscarMode = (oscarMode == 0) ? 1 : 0; lastDrawMs = 0; }   // QTH <-> polar
}

void App::keyGlobe(char c, bool enter, bool back) {
  if (isBack(c, back)) { buildSatView(); screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (enter) { globeFollow = true; lastDrawMs = 0; return; }   // re-snap to the sat
  if (c == 'g') {                                              // enter a DX grid (2nd location)
    editTarget = 104; editTitle = "DX grid (Maidenhead)"; editBuf = ""; screen = SCR_EDIT;
    return;
  }
  if (c == 'G') { dxGrid[0] = 0; setStatus("DX cleared"); lastDrawMs = 0; return; }  // clear DX
  // Arrow keys nudge the view; this drops out of auto-follow into free-look.
  const double STEP = 15.0;
  if (isUp(c))    { globeFollow = false; globeViewLat += STEP; if (globeViewLat >  90) globeViewLat =  90; lastDrawMs = 0; }
  if (isDown(c))  { globeFollow = false; globeViewLat -= STEP; if (globeViewLat < -90) globeViewLat = -90; lastDrawMs = 0; }
  if (isLeft(c))  { globeFollow = false; globeViewLon -= STEP; if (globeViewLon < -180) globeViewLon += 360; lastDrawMs = 0; }
  if (isRight(c)) { globeFollow = false; globeViewLon += STEP; if (globeViewLon >  180) globeViewLon -= 360; lastDrawMs = 0; }
}

void App::keySchedule(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (isUp(c)   && schedN) schedSel = (schedSel + schedN - 1) % schedN;
  if (isDown(c) && schedN) schedSel = (schedSel + 1) % schedN;
  if (c == 'r') { setStatus("Computing passes..."); draw(); buildSchedule();
                  lastSchedMs = millis(); setStatus("Schedule updated"); }
  if (c == 'z') { sleepUntilNextPass(); return; }     // deep-sleep until AOS
  if (c == 'm') { mapReturn = SCR_SCHEDULE; screen = SCR_WORLDMAP; lastDrawMs = 0; return; }  // live world map
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
  if (isBack(c, back)) {
    satDelArm = false;
    if (logPickSat) { logPickSat = false; screen = SCR_LOGENTRY; lastDrawMs = 0; return; }
    screen = SCR_HOME; return;
  }
  if (c == 'n') {                              // new manual GP entry
    mtSat = SatEntry();
    editTarget = 310; editTitle = "Sat name"; editBuf = ""; screen = SCR_EDIT;
    return;
  }
  if (c == 'v') { favOnly = !favOnly; buildSatView();
                  setStatus(favOnly ? "Favorites only" : "All satellites"); return; }
  int n = viewN;
  if (n == 0) return;
  if (isUp(c)   && viewSel > 0)     { viewSel--; satDelArm = false; }
  if (isDown(c) && viewSel < n - 1) { viewSel++; satDelArm = false; }
  if (c == '{') viewSel = max(0, viewSel - 10);
  if (c == '}') viewSel = min(n - 1, viewSel + 10);
  if (c == 'f') {                              // toggle favorite for selected
    toggleFav(db.at(view[viewSel]).norad);
    if (favOnly) buildSatView();               // may shrink the list
  }
  if (c == 'x' && viewN > 0) {                  // delete a hand-entered (manual) sat
    uint32_t nd = db.at(view[viewSel]).norad;
    if (!db.isManualGp(nd)) { setStatus("Only manually-added sats can be deleted");
                              satDelArm = false; return; }
    if (!satDelArm) { satDelArm = true; setStatus("Press x again to delete this sat");
                      return; }
    satDelArm = false;
    if (db.removeManualGp(nd)) {
      // Rebuild the in-memory catalog from the cached GP file + remaining manual
      // sats so the deleted entry is gone without a fragile in-place array removal.
      db.loadGpFromFile(FILE_GP);
      db.loadManualGpFile();
      if (isFav(nd)) toggleFav(nd);             // drop it from favorites too
      buildSatView();
      if (viewSel >= viewN) viewSel = viewN > 0 ? viewN - 1 : 0;
      setStatus("Satellite deleted");
    } else setStatus("Delete failed");
    return;
  }
  if (viewSel < satScroll)      satScroll = viewSel;
  if (viewSel > satScroll + 9)  satScroll = viewSel - 9;
  if (viewN > 0) satSel = view[viewSel];
  if (c == 'o' && viewN > 0) {                     // open the orbital analysis screen
    orbitPage = 0; buildOrbit(); screen = SCR_ORBIT; lastDrawMs = 0; return;
  }
  if (c == 's' && viewN > 0) {                     // open the simulation screen
    simTime = timeIsSet() ? nowUtc() : 0; screen = SCR_SIM; lastDrawMs = 0; return;
  }
  if (c == 't' && viewN > 0) {                      // browse the transponder database
    SatEntry* s = activeSat();
    if (s) { ensureTransponders(*s); txDbScroll = 0; txDbSel = 0; txDbDelArm = false;
             screen = SCR_TXDB; lastDrawMs = 0; }
    return;
  }
  if (c == 'e' && viewN > 0) {                      // EQX table (OSCARLOCATOR)
    eqxDescending = false; buildEqx(); screen = SCR_EQX; lastDrawMs = 0;
    return;
  }
  if (c == 'k' && viewN > 0) {                      // OSCARLOCATOR live polar view
    oscarMode = 1; oscarArcN = 0; oscarArcTriedMs = 0; screen = SCR_OSCAR; lastDrawMs = 0;
    return;
  }
  if (c == '3' && viewN > 0) {                      // 3D globe view (auto-follow)
    globeFollow = true; oscarArcN = 0; oscarArcTriedMs = 0; screen = SCR_GLOBE; lastDrawMs = 0;
    return;
  }
  if (c == '2' && viewN > 0) {                      // sat-to-sat visibility finder
    satsatOther = 0; satsatSel = 0; screen = SCR_SATSAT; lastDrawMs = 0;
    satsatStartJob();
    return;
  }
  if (c == 'd' && viewN > 0) {                      // 10-day pass overview
    visReturn = SCR_SATLIST; visDayOff = 0; buildVis();
    screen = SCR_VIS; lastDrawMs = 0; return;
  }
  if (c == 'i' && viewN > 0) {                      // 60-day illumination raster
    visReturn = SCR_SATLIST; illumDayOff = 0; buildIllum();
    screen = SCR_ILLUM; lastDrawMs = 0; return;
  }
  if (enter && viewN > 0) {
    SatEntry* s = activeSat();
    pred.setSite(loc.obs());
    pred.setSat(*s);
    ensureTransponders(*s);
    onTransponderChanged();
    if (logPickSat) {            // chose a satellite for a log entry
      logPickSat = false;
      seedQsoSatDefaults();      // sat + mode + non-Doppler centre/nominal freqs
      logSel = 0; screen = SCR_LOGENTRY; lastDrawMs = 0; return;
    }
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
  if (c == 'g' && passN > 0 && passSel < passN) {     // workable grids on this pass
    setStatus("Computing pass grids..."); draw();
    gridLive = false; gridScroll = 0; buildGrids(passes[passSel].aos, passes[passSel].los);
    setStatus(""); screen = SCR_GRID; lastDrawMs = 0; return;
  }
  if (c == 'w' && passN > 0 && passSel < passN) {     // workable US states on this pass
    setStatus("Computing pass states..."); draw();
    stateLive = false; stateScroll = 0; buildStates(passes[passSel].aos, passes[passSel].los);
    setStatus(""); screen = SCR_STATES; lastDrawMs = 0; return;
  }
  if (c == 'e' && passN > 0 && passSel < passN) {     // workable DXCC entities on this pass
    setStatus("Computing pass DXCC..."); draw();
    dxccLive = false; dxccScroll = 0; buildDxcc(passes[passSel].aos, passes[passSel].los);
    setStatus(""); screen = SCR_DXCC; lastDrawMs = 0; return;
  }
  if (c == 'v') { visReturn = SCR_PASSES; visDayOff = 0; buildVis();   screen = SCR_VIS;   lastDrawMs = 0; return; }
  if (c == 'i') { visReturn = SCR_PASSES; illumDayOff = 0; buildIllum(); screen = SCR_ILLUM; lastDrawMs = 0; return; }
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
  if (c == 'v') { toggleMemo(); return; }            // SD voice memo (record/stop)
  if (isBack(c, back)) {
    if (radioOut) { radioOut = false; }     // stop sending on first back
    else {
      if (rotOut && rot) { rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl);
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

  if (c == 'd') {                                    // cycle the Doppler tune mode
    if (!linear) setStatus("Tune modes: linear birds only");
    else {
      bool canRead = rig && rig->canReadFreq();
      do { tuneMode = (TuneMode)((tuneMode + 1) & 3); }   // FULL/DL need knob read
      while (!canRead && (tuneMode == TM_FULL || tuneMode == TM_DL));
      lastRxSet = 0; lastUlHz = 0;                    // re-sync both legs to the knob
      setStatus(tuneMode == TM_FULL ? "Tune: FULL knob=passband (OTR)"
              : tuneMode == TM_DL   ? "Tune: downlink only (OTR)"
              : tuneMode == TM_UL   ? "Tune: uplink only"
                                    : "Tune: hold both legs");
    }
  }

  if (trackMode == 0 && linear && tuneMode != TM_FULL && tuneMode != TM_DL) {
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
    if (radioOut && rig) {                           // refresh rig sat-mode + modes
      rig->enableSatMode(cfg.satMode && !txReceiveOnly());   // off for receive-only birds
      applyTransponderModes(activeTx[curTx]);
      lastRxSet = 0; lastUlHz = 0; uplinkDeferTicks = 0;     // re-sync after the routing change
    }
  }
  if (c == 'n' && activeTxCount > 0) {               // jump to the beacon
    // A beacon is a downlink-only entry (uplink == 0). Prefer one whose desc
    // mentions "beacon"; otherwise the first downlink-only entry; else say none.
    int found = -1, firstDlOnly = -1;
    for (int i = 0; i < activeTxCount; ++i) {
      if (activeTx[i].uplink == 0 && activeTx[i].downlink != 0) {
        if (firstDlOnly < 0) firstDlOnly = i;
        String d = activeTx[i].desc; d.toLowerCase();
        if (d.indexOf("beacon") >= 0 || d.indexOf("bcn") >= 0) { found = i; break; }
      }
    }
    if (found < 0) found = firstDlOnly;
    if (found >= 0) {
      curTx = found; onTransponderChanged();
      if (radioOut && rig) {                          // beacon is receive-only: sat mode off, RX on MAIN
        rig->enableSatMode(cfg.satMode && !txReceiveOnly());
        applyTransponderModes(activeTx[curTx]);
        lastRxSet = 0; lastUlHz = 0; uplinkDeferTicks = 0;
      }
      setStatus(String("Beacon: ") + activeTx[curTx].desc);
    } else setStatus("No beacon listed for this sat");
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
      // Command the rig's satellite mode (a no-op on rigs without one). A
      // receive-only transponder (beacon/telemetry, no uplink) turns satellite
      // mode OFF and tunes the downlink on MAIN; a two-way transponder turns it on
      // per the Sat Mode setting. Which physical VFO carries up/downlink otherwise
      // follows the VFO Type setting (see the rigSet*/rigSelect* helpers).
      if (rig) rig->enableSatMode(cfg.satMode && !txReceiveOnly());
      if (activeTxCount > 0) applyTransponderModes(activeTx[curTx]);
      lastDoppMs = 0; lastRxSet = 0; lastUlHz = 0; uplinkDeferTicks = 0;   // re-sync tracking cleanly
      // Bound how long any single CAT read may block the cooperative loop, so a
      // laggy rig (especially the LAN backend) degrades to "no knob update this
      // cycle" instead of stalling the UI/rotator. ~3 reads fit in one cycle.
      if (rig) rig->setReadBudgetMs((uint16_t)constrain((int)effectiveCatRateMs() / 4, 60, 200));
      setStatus("Radio ON");
    } else {
      if (rig && rig->ready() && rig->hasTone() && toneApplied > 0) {
        rigSelectUplink();         // tone lives on the uplink VFO
        rig->setCtcss(false, 0);   // don't leave an encode tone on the operator's rig
      }
      toneApplied = -2.0f;         // force re-apply next time radio goes ON
      setStatus("Radio OFF");
    }
  }
  if (c == 'o') {                                    // toggle rotator pointing
    if (!cfg.rotEnable || !rot) setStatus("Rotator: enable in Settings");
    else {
      if (!rot->ready()) rot->begin();   // (re)establish the link/bridge on demand
      if (!rot->ready())
        setStatus(cfg.rotType == ROT_PST  ? "Rotator: no PstRotator link"
                  : cfg.rotType == ROT_NET ? "Rotator: no rotctl link"
                                           : "Rotator: bridge not found");
      else {
        rotOut = !rotOut;
        if (rotOut) {
          smOut = false;                       // sat tracking takes the rotator
          lastRotMs = 0; lastAzCmd = lastElCmd = -999.0f;
          lastUnwrappedAz = -999.0f; rotParked = false; rotFlipUntil = 0;
          setStatus("Rotator ON");
        } else {
          rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl);
          rotParked = true; setStatus("Rotator OFF (parked)");
        }
      }
    }
  }
  if (c == 'l') { beginQso(); return; }              // log a QSO (radio keeps tracking)
  if (c == 'f') {                                    // open Manual (no-radio) freq calc
    liveReturn = SCR_MANUAL; screen = SCR_MANUAL; lastDrawMs = 0; return;
  }
  if (c == 'g') { gridLive = true; gridScroll = 0; gridBuiltMs = 0;   // workable grids now (radio/rotor keep running)
                  liveReturn = SCR_TRACK; screen = SCR_GRID; lastDrawMs = 0; return; }
  if (c == 'w') { stateLive = true; stateScroll = 0; stateBuiltMs = 0;  // workable US states now
                  liveReturn = SCR_TRACK; screen = SCR_STATES; lastDrawMs = 0; return; }
  if (c == 'e') { dxccLive = true; dxccScroll = 0; dxccBuiltMs = 0;     // workable DXCC entities now
                  liveReturn = SCR_TRACK; screen = SCR_DXCC; lastDrawMs = 0; return; }
  if (c == 'p') { polarPathValid = false; liveReturn = SCR_TRACK; screen = SCR_POLAR;
                  lastDrawMs = 0; return; }                   // live polar
  if (c == 'z') { screen = SCR_BIG; lastDrawMs = 0; return; }  // large-font readout
  if (c == 'y') {                                    // toggle tilt tuning on the fly
    if (!imuReady) setStatus("Tilt: no IMU on this board");
    else {
      cfg.tiltTune = !cfg.tiltTune; cfg.save();
      tiltAccum = 0; lastTiltMs = millis();
      setStatus(cfg.tiltTune ? "Tilt tuning ON" : "Tilt tuning OFF");
    }
  }
  if (enter) {  // persist calibration for THIS satellite (per-sat store)
    SatEntry* s = activeSat();
    if (s) { saveCalForSat(s->norad);
             setStatus("Cal saved: " + String(s->name)); }
  }
}

// Manual (no-radio) mode keys: passband/cal tuning like Track, leg toggle, and
// access to log/polar/grids (which return here), but NO radio or rotator keys.
void App::keyManual(char c, bool enter, bool back) {
  if (c == 'v') { toggleMemo(); return; }            // SD voice memo (record/stop)
  if (isBack(c, back) || c == 'f') { screen = SCR_TRACK; lastDrawMs = 0; return; }

  bool linear = (activeTxCount > 0) && activeTx[curTx].isLinear &&
                activeTx[curTx].bandwidth() > 0;
  bool haveUp = (activeTxCount > 0) && activeTx[curTx].uplink != 0;

  if (c == 'u' && haveUp) {                          // toggle which leg is fixed
    manFixUp = !manFixUp;
    setStatus(manFixUp ? "Fix uplink (hold TX)" : "Fix downlink (hold RX)");
  }

  if (c == 'm') {                                    // toggle TUNE / CAL
    if (linear) trackMode ^= 1;
    else { trackMode = 1; setStatus("Not linear: CAL only"); }
  }

  if (trackMode == 0 && linear) {
    // ---- TUNE: move within the passband (fixed leg the operator dials) ----
    int32_t bw = (int32_t)activeTx[curTx].bandwidth();
    if (isLeft(c))  pbOffset -= tuneStep;
    if (isRight(c)) pbOffset += tuneStep;
    if (pbOffset < 0)  pbOffset = 0;
    if (pbOffset > bw) pbOffset = bw;
    if (c == 's') tuneStep = (tuneStep == 100) ? 1000 : (tuneStep == 1000 ? 5000 : 100);
    if (c == 'x') pbOffset = bw / 2;                 // recenter passband
  } else {
    // ---- CAL: trim oscillator offsets (shared with Track, same store) ----
    if (isLeft(c))  calDl -= calStep;
    if (isRight(c)) calDl += calStep;
    if (isUp(c))    calUl += calStep;
    if (isDown(c))  calUl -= calStep;
    if (c == 's') calStep = (calStep == 10) ? 100 : (calStep == 100 ? 1000 : 10);
    if (c == 'x') { calDl = 0; calUl = 0; }
  }

  if (c == 't' && activeTxCount > 0) {               // cycle transponder
    curTx = (curTx + 1) % activeTxCount;
    onTransponderChanged();
  }
  if (c == 'l') { beginQso(); return; }              // log a QSO (returns here)
  if (c == 'g') { gridLive = true; gridScroll = 0; gridBuiltMs = 0;
                  liveReturn = SCR_MANUAL; screen = SCR_GRID; lastDrawMs = 0; return; }
  if (c == 'w') { stateLive = true; stateScroll = 0; stateBuiltMs = 0;
                  liveReturn = SCR_MANUAL; screen = SCR_STATES; lastDrawMs = 0; return; }
  if (c == 'e') { dxccLive = true; dxccScroll = 0; dxccBuiltMs = 0;
                  liveReturn = SCR_MANUAL; screen = SCR_DXCC; lastDrawMs = 0; return; }
  if (c == 'p') { polarPathValid = false; liveReturn = SCR_MANUAL; screen = SCR_POLAR;
                  lastDrawMs = 0; return; }
  if (c == 'z') { screen = SCR_MANUALBIG; lastDrawMs = 0; return; }  // large-font Manual
  if (enter) {                                       // persist calibration for THIS sat
    SatEntry* s = activeSat();
    if (s) { saveCalForSat(s->norad);
             setStatus("Cal saved: " + String(s->name)); }
  }
}

void App::keyPolar(char c, bool enter, bool back) {
  if (c == 'v') { toggleMemo(); return; }            // SD voice memo (record/stop)
  if (c == 'l') { beginQso(); return; }          // log a QSO (radio keeps tracking)
  // Any of back / ENTER / 'p' returns to the tracking screen.
  if (isBack(c, back) || enter || c == 'p') { screen = liveReturn; lastDrawMs = 0; }
}

// ---- Polar view of one selected pass (from the pass-detail screen) ---------
void App::drawPassPolar() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Pass polar"));
  canvas.setTextSize(1);
  if (!pdValid) { canvas.setTextColor(CL_YELLOW, CL_BLACK);
                  canvas.setCursor(6, 50); canvas.print("No pass data.");
                  footer("` back"); return; }

  const int cx = 66, cy = 70, R = 44;
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
    if (i == mutualSel) { canvas.fillRect(0, y-1, 240, 10, CL_SELBG);
                          canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                  canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s %ld:%02ld %3.0f %3.0f", fmtMDHM(m.start).c_str(),
                  secs/60, secs%60, m.myMaxEl, m.dxMaxEl);
  }
  footer(";/. select  d Doppler  ` back");
}

void App::keyMutual(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_PASSES; return; }
  if (mutualN) {
    if (isUp(c))   mutualSel = (mutualSel + mutualN - 1) % mutualN;
    if (isDown(c)) mutualSel = (mutualSel + 1) % mutualN;
    if (c == 'd' || enter) {                 // Doppler table for this window
      if (!activeSat()) { setStatus("No satellite"); return; }
      dxdWin = mutualSel; dxdRow = 0;
      dxdCenterPassband();                   // start at the centre of a linear passband
      screen = SCR_DXDOPP; lastDrawMs = 0;
      return;
    }
  }
  if (enter) { screen = SCR_PASSES; return; }
}

// ===========================================================================
//  DX Doppler table (SCR_DXDOPP)
//
//  For the selected mutual window and transponder, predict the RX (downlink) and
//  TX (uplink) dial frequencies for BOTH this station and the DX station, every
//  30 s across the window. Three modes:
//    0 TRUE RULE      - operating point fixed in the satellite passband; every
//                       dial Doppler-tracks (each station with its own range-rate)
//    1 FIXED DOWNLINK - the ANCHOR station's RX dial is held constant in real RF;
//                       the passband operating point drifts to absorb its downlink
//                       Doppler, and the other three dials follow.
//    2 FIXED UPLINK   - the ANCHOR station's TX dial is held constant in real RF;
//                       the operating point drifts, the other three dials follow.
//  The anchor (whose dial is fixed, or whose passband point you set in true rule)
//  is one of: my RX, my TX, DX RX, DX TX.
// ===========================================================================

// Set the DX Doppler passband operating point to the centre of the currently
// selected transponder. For a linear (tunable-passband) transponder that is the
// middle of the downlink passband (bandwidth/2 from the low edge); for an FM or
// single-channel transponder there is no passband to offset, so it is 0. This
// mirrors onTransponderChanged() so the DX Doppler table opens on the same
// nominal channel the on-device tracker would centre on.
void App::dxdCenterPassband() {
  if (activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    if (t.isLinear && t.bandwidth() > 0) { dxdPbOff = (int32_t)(t.bandwidth() / 2); return; }
  }
  dxdPbOff = 0;
}

// Core per-step calculator. Fills the four dial frequencies (Hz) at time t.
void App::dxDoppFreqs(time_t t, uint32_t& myRx, uint32_t& myTx,
                      uint32_t& dxRx, uint32_t& dxTx) {
  myRx = myTx = dxRx = dxTx = 0;
  SatEntry* s = activeSat();
  if (!s) return;
  Transponder& tp = activeTx[curTx];

  // Operating point in the passband (satellite frame), before Doppler.
  uint32_t dlOp, ulOp;
  Predictor::passbandFreqs(tp, dxdPbOff, dlOp, ulOp);

  // Range-rate for each station at t (km/s). Switch the predictor site, sample,
  // then restore to my site so the rest of the UI is unaffected.
  pred.setSat(*s);
  Observer dxObs; dxObs.lat = dxLat; dxObs.lon = dxLon; dxObs.altM = 0; dxObs.valid = true;
  pred.setSite(loc.obs());  double rrMe = pred.rangeRateAt((double)t);
  pred.setSite(dxObs);      double rrDx = pred.rangeRateAt((double)t);
  pred.setSite(loc.obs());
  const double C = 299792458.0;
  double bMe = rrMe * 1000.0 / C, bDx = rrDx * 1000.0 / C;

  if (dxdMode == 0) {
    // True rule: same operating point for both, each with its own Doppler.
    Predictor::dopplerFreqs(dlOp, ulOp, rrMe, calDl, calUl, myRx, myTx);
    Predictor::dopplerFreqs(dlOp, ulOp, rrDx, calDl, calUl, dxRx, dxTx);
    return;
  }

  // Fixed-dial modes: work out the satellite-frame operating-point drift `delta`
  // from the anchor station's locked real-RF dial, then derive everybody else.
  // anchorBeta is the locked station's beta; anchorIsDx picks which.
  bool anchorIsDx = (dxdAnchor == 2 || dxdAnchor == 3);
  double aBeta = anchorIsDx ? bDx : bMe;
  double oneMinusA = 1.0 - aBeta; if (oneMinusA == 0.0) oneMinusA = 1e-12;

  // The locked dial value: the true-rule frequency at the *current* operating
  // point and the anchor's beta (this is what the operator set and won't touch).
  double delta = 0.0;
  if (dxdMode == 1) {
    // FIXED DOWNLINK: anchor parks RX. parkedRx = dlOp*(1-aBeta)+calDl held; but
    // to keep a stable real-RF lock across steps we anchor to the *passband* dlOp
    // captured into the dial at entry. Here we treat the locked ground RX as the
    // value that makes the bird emit dlOp at the anchor's CURRENT beta -- i.e. the
    // operator tracked once then stopped, and we show drift from this instant's op.
    double parkedRx = (double)dlOp * oneMinusA + (double)calDl;
    double fdlSat   = (parkedRx - (double)calDl) / oneMinusA;  // bird emit (sat frame)
    delta = fdlSat - (double)dlOp;
  } else {
    // FIXED UPLINK: anchor parks TX.
    double parkedTx = (ulOp ? ((double)ulOp / oneMinusA + (double)calUl) : 0.0);
    double fulSat   = (parkedTx - (double)calUl) * oneMinusA;  // bird hears (sat frame)
    delta = ulOp ? (fulSat - (double)ulOp) : 0.0;
  }

  // Apply the drift to the operating point with the transponder's inversion sense.
  double dlSat, ulSat;
  if (dxdMode == 1) {                       // delta defined on the downlink
    dlSat = (double)dlOp + delta;
    ulSat = tp.invert ? ((double)ulOp - delta) : ((double)ulOp + delta);
  } else {                                  // delta defined on the uplink
    ulSat = (double)ulOp + delta;
    dlSat = tp.invert ? ((double)dlOp - delta) : ((double)dlOp + delta);
  }

  // Now each station's dials from the drifted satellite-frame pair, own Doppler.
  myRx = (uint32_t)llround(dlSat * (1.0 - bMe) + (double)calDl);
  myTx = ulOp ? (uint32_t)llround(ulSat / (1.0 - bMe) + (double)calUl) : 0;
  dxRx = (uint32_t)llround(dlSat * (1.0 - bDx) + (double)calDl);
  dxTx = ulOp ? (uint32_t)llround(ulSat / (1.0 - bDx) + (double)calUl) : 0;
}

static const char* DXD_MODE_NAME[3] = { "True rule", "Fixed DL", "Fixed UL" };
static const char* DXD_ANCHOR_NAME[4] = { "me RX", "me TX", "DX RX", "DX TX" };

void App::drawDxDopp() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + " DX Dopp") : String("DX Doppler"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }
  if (dxdWin >= mutualN) { dxdWin = 0; }
  if (mutualN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 50); canvas.print("No mutual window.");
    footer("` back"); return;
  }
  if (activeTxCount == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 50); canvas.print("No transponder. Pick one");
    canvas.setCursor(6, 62); canvas.print("with 't' on Satellites.");
    footer("` back"); return;
  }
  Transponder& tp = activeTx[curTx];
  MutualWindow& w = mutual[dxdWin];

  // Header line: transponder + mode + anchor.
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(2, 18); canvas.printf("%.18s", tp.desc);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(2, 28);
  canvas.printf("%s", DXD_MODE_NAME[dxdMode]);
  if (dxdMode != 0) canvas.printf("  anc:%s", DXD_ANCHOR_NAME[dxdAnchor]);
  if (tp.isLinear && tp.bandwidth() > 0) {
    canvas.setTextColor(CL_DGREEN, CL_BLACK);
    canvas.printf("  +%ldk", (long)(dxdPbOff / 1000));
  }

  // Column headers.
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(2, 40);   canvas.print("UTC");
  canvas.setCursor(56, 40);  canvas.print("RX (MHz)");
  canvas.setCursor(150, 40); canvas.print("TX (MHz)");
  canvas.drawLine(0, 49, 240, 49, CL_DGREY);

  // Rows: each 30 s step takes TWO lines (me, then DX) so the four frequencies
  // never run together on the 240 px display. me = green, DX = cyan; the time is
  // printed once per step on the "me" line.
  time_t span = w.end - w.start; if (span < 0) span = 0;
  int nSteps = (int)(span / 30) + 1;
  const int rowH = 9, firstY = 51, maxSteps = 4;   // 4 steps * 2 lines = 8 lines
  for (int r = 0; r < maxSteps; ++r) {
    int step = dxdRow + r;
    if (step >= nSteps) break;
    time_t t = w.start + (time_t)step * 30;
    if (t > w.end) t = w.end;
    uint32_t mRx, mTx, dRx, dTx;
    dxDoppFreqs(t, mRx, mTx, dRx, dTx);
    int yMe = firstY + (r * 2)     * rowH;
    int yDx = firstY + (r * 2 + 1) * rowH;
    // me line: time + RX + TX
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, yMe);  canvas.print(fmtHM(t).c_str());
    canvas.setTextColor(CL_GREEN, CL_BLACK);
    canvas.setCursor(40, yMe); canvas.print("me");
    canvas.setCursor(56, yMe); canvas.printf("%.4f", mRx / 1e6);
    if (mTx) { canvas.setCursor(150, yMe); canvas.printf("%.4f", mTx / 1e6); }
    // DX line: RX + TX (no time; aligned under me)
    canvas.setTextColor(CL_CYAN, CL_BLACK);
    canvas.setCursor(40, yDx); canvas.print("DX");
    canvas.setCursor(56, yDx); canvas.printf("%.4f", dRx / 1e6);
    if (dTx) { canvas.setCursor(150, yDx); canvas.printf("%.4f", dTx / 1e6); }
  }

  footer("t tx  m mode  a anc  ,/ pb  ;/. `bk");
}

void App::keyDxDopp(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_MUTUAL; lastDrawMs = 0; return; }
  if (mutualN == 0 || activeTxCount == 0) return;
  MutualWindow& w = mutual[dxdWin];
  time_t span = w.end - w.start; if (span < 0) span = 0;
  int nSteps = (int)(span / 30) + 1;
  Transponder& tp = activeTx[curTx];

  if (c == 'm') { dxdMode = (dxdMode + 1) % 3; lastDrawMs = 0; return; }
  if (c == 't') {                                  // cycle the selected transponder
    if (activeTxCount > 0) { curTx = (curTx + 1) % activeTxCount; dxdCenterPassband(); }
    lastDrawMs = 0; return;
  }
  if (c == 'a') { dxdAnchor = (dxdAnchor + 1) % 4; lastDrawMs = 0; return; }
  if (isUp(c))   { if (dxdRow > 0) dxdRow--; lastDrawMs = 0; return; }
  if (isDown(c)) { if (dxdRow < nSteps - 1) dxdRow++; lastDrawMs = 0; return; }
  // passband operating point (linear only): left/right by 1 kHz, shift by 5 kHz.
  if (tp.isLinear && tp.bandwidth() > 0) {
    int32_t bw = (int32_t)tp.bandwidth();
    if (isLeft(c))  { dxdPbOff -= 1000; if (dxdPbOff < 0)  dxdPbOff = 0;  lastDrawMs = 0; }
    if (isRight(c)) { dxdPbOff += 1000; if (dxdPbOff > bw) dxdPbOff = bw; lastDrawMs = 0; }
    if (c == '<')   { dxdPbOff -= 5000; if (dxdPbOff < 0)  dxdPbOff = 0;  lastDrawMs = 0; }
    if (c == '>')   { dxdPbOff += 5000; if (dxdPbOff > bw) dxdPbOff = bw; lastDrawMs = 0; }
  }
}


// ===========================================================================
//  10-day pass overview (InstantTrack "Multiple Days for Single Satellite")
// ===========================================================================
void App::buildVis() {
  visN = 0;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet()) return;
  building = true;
  pred.setSite(loc.obs()); pred.setSat(*s);
  // Window starts at UTC midnight of (today + visDayOff) and spans VIS_DAYS *full*
  // days, so every day-row of the chart is completely filled (the horizon is not
  // cut off partway through the last day at the request time).
  time_t today0 = nowUtc() - (nowUtc() % 86400);
  time_t winStart = today0 + (time_t)visDayOff * 86400;
  time_t winEnd   = winStart + (time_t)VIS_DAYS * 86400;
  visN = pred.predictPasses(winStart, cfg.minPassEl, visPasses, VIS_PASS_MAX, winEnd);
  building = false;
  setStatus("");
}

void App::drawVis() {
  SatEntry* s = activeSat();
  { String hd = s ? (String(s->name) + " 10-day") : String("10-day passes");
    if (visDayOff) hd += " +" + String(visDayOff) + "d";
    header(hd); }
  canvas.setTextSize(1);
  if (building) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 56); canvas.print("Computing passes...");
    footer("` back"); return;
  }
  if (visN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 56); canvas.print("No passes in this 10-day window.");
    footer("` bk  ;/. +/-1d  r recomp"); return;
  }
  const int barX0 = 40, barW = 196;          // 196 px = 24 h
  const int y0 = 19, rowH = 10;
  time_t today0 = nowUtc() - (nowUtc() % 86400);
  time_t mid0   = today0 + (time_t)visDayOff * 86400;   // UTC midnight of the first row
  for (int h = 6; h <= 18; h += 6)           // 06/12/18 h gridlines behind the bars
    canvas.drawFastVLine(barX0 + barW * h / 24, y0, rowH * VIS_DAYS, CL_DGREY);
  for (int d = 0; d < VIS_DAYS; ++d) {
    time_t dayStart = mid0 + (time_t)d * 86400;
    int ry = y0 + d * rowH;
    struct tm tmv; gmtime_r(&dayStart, &tmv);
    char lbl[8]; snprintf(lbl, sizeof(lbl), "%02d/%02d", tmv.tm_mon + 1, tmv.tm_mday);
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, ry + 1); canvas.print(lbl);
    canvas.drawFastHLine(barX0, ry + rowH - 1, barW, CL_DGREY);
    for (int i = 0; i < visN; ++i) {
      time_t a = visPasses[i].aos, b = visPasses[i].los;
      time_t s0 = (a > dayStart) ? a : dayStart;
      time_t s1 = (b < dayStart + 86400) ? b : dayStart + 86400;
      if (s1 <= s0) continue;
      int x1 = barX0 + (int)(barW * (s0 - dayStart) / 86400);
      int x2 = barX0 + (int)(barW * (s1 - dayStart) / 86400);
      int w  = (x2 - x1 < 1) ? 1 : x2 - x1;
      float me = visPasses[i].maxEl;
      uint16_t col = (me < 15.0f) ? CL_DGREEN : (me < 40.0f ? CL_GREEN : CL_YELLOW);
      canvas.fillRect(x1, ry + 1, w, rowH - 3, col);
    }
  }
  // "Now" marker only on today's row (first row when not scrolled into the future).
  if (visDayOff == 0)
    canvas.drawFastVLine(barX0 + (int)(barW * (nowUtc() - mid0) / 86400), y0, rowH, CL_RED);
  footer("` bk  ;/. +/-1d  r recomp");
}

void App::keyVis(char c, bool enter, bool back) {
  if (isBack(c, back) || enter) { screen = visReturn; lastDrawMs = 0; return; }
  if (c == 'r') { buildVis(); lastDrawMs = 0; return; }
  if (isDown(c)) { visDayOff++; buildVis(); lastDrawMs = 0; return; }              // . scroll 1 day forward
  if (isUp(c) && visDayOff > 0) { visDayOff--; buildVis(); lastDrawMs = 0; return; } // ; scroll 1 day back (>= today)
}

// ===========================================================================
//  60-day illumination (DK3WN "illum"): date x orbit-phase sun/shadow raster
// ===========================================================================
void App::buildIllum() {
  illumValid = false;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet() || s->meanMotion <= 0) return;
  building = true;
  setStatus("Computing illumination..."); draw();
  pred.setSite(loc.obs()); pred.setSat(*s);
  illumPeriodMin = 1440.0f / (float)s->meanMotion;
  double periodSec = (double)illumPeriodMin * 60.0;
  // Raster starts at UTC midnight of (today + illumDayOff); each column is one day.
  time_t today0 = nowUtc() - (nowUtc() % 86400);
  time_t now = today0 + (time_t)illumDayOff * 86400;
  const int RB = (ILLUM_ROWS + 7) / 8;
  for (int c = 0; c < ILLUM_DAYS; ++c) {
    for (int b = 0; b < RB; ++b) illumBits[c][b] = 0;
    time_t base = now + (time_t)c * 86400;            // one orbit starting this day
    for (int r = 0; r < ILLUM_ROWS; ++r) {
      time_t t = base + (time_t)llround(periodSec * r / ILLUM_ROWS);
      if (!pred.sunlitAt(t)) illumBits[c][r >> 3] |= (1 << (r & 7));  // set = eclipse
    }
  }
  // current-orbit eclipse fraction (column 0) + live status
  int eclRows = 0;
  for (int r = 0; r < ILLUM_ROWS; ++r)
    if (illumBits[0][r >> 3] & (1 << (r & 7))) eclRows++;
  illumEclMin = illumPeriodMin * eclRows / ILLUM_ROWS;
  illumEclPct = 100.0f * eclRows / ILLUM_ROWS;
  illumSunNow = pred.sunlitAt(now);
  illumNextEclipse = !illumSunNow;
  illumNextSec = -1;
  for (long dt = 15; dt <= (long)periodSec + 120; dt += 15)
    if (pred.sunlitAt(now + dt) != illumSunNow) { illumNextSec = dt; break; }
  building = false;
  illumValid = true;
  setStatus("");
}

void App::drawIllum() {
  SatEntry* s = activeSat();
  { String hd = s ? (String(s->name) + " illum 60d") : String("Illumination");
    if (illumDayOff) hd += " +" + String(illumDayOff) + "d";
    header(hd); }
  canvas.setTextSize(1);
  if (building) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 56); canvas.print("Computing illumination...");
    footer("` back"); return;
  }
  if (!illumValid) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 56);
    canvas.print((s && s->meanMotion > 0) ? "No passes / orbit data." : "No orbit data.");
    footer("` back   r recompute"); return;
  }
  const int px0 = 24, py0 = 18, cw = 3, ph = ILLUM_ROWS;   // 60*3 = 180 wide
  const int pw = ILLUM_DAYS * cw;
  canvas.fillRect(px0, py0, pw, ph, CL_BLACK);             // eclipse = dark ground
  canvas.drawRect(px0 - 1, py0 - 1, pw + 2, ph + 2, CL_GREY);
  for (int c = 0; c < ILLUM_DAYS; ++c) {                   // run-length sunlit spans
    int x = px0 + c * cw, r = 0;
    while (r < ILLUM_ROWS) {
      bool ecl = illumBits[c][r >> 3] & (1 << (r & 7));
      int r0 = r;
      while (r < ILLUM_ROWS &&
             (bool)(illumBits[c][r >> 3] & (1 << (r & 7))) == ecl) r++;
      if (!ecl) canvas.fillRect(x, py0 + r0, cw, r - r0, CL_YELLOW);   // sunlit
    }
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(px0 - 4, py0 + ph + 2);
  if (illumDayOff == 0) canvas.print("now");
  else                  canvas.printf("+%dd", illumDayOff);
  canvas.setCursor(px0 + pw - 22, py0 + ph + 2);
  canvas.printf("+%dd", illumDayOff + ILLUM_DAYS);
  canvas.setCursor(px0 + pw + 5, py0);           canvas.print("1 orbit");
  canvas.setCursor(px0 + pw + 5, py0 + 10);      canvas.printf("%.0fm", illumPeriodMin);
  canvas.setCursor(4, py0 + ph + 11);
  canvas.setTextColor(illumSunNow ? CL_YELLOW : CL_CYAN, CL_BLACK);
  canvas.print(illumSunNow ? "SUN" : "SHADOW");
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.printf("  ecl %.0fm/orbit (%.0f%%)", illumEclMin, illumEclPct);
  if (illumNextSec > 0) {
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(4, py0 + ph + 20);
    canvas.printf("-> %s in %ldm", illumNextEclipse ? "shadow" : "sun",
                  illumNextSec / 60);
  }
  footer("` bk  ,// +/-60d  r recomp");
}

void App::keyIllum(char c, bool enter, bool back) {
  if (isBack(c, back) || enter) { screen = visReturn; lastDrawMs = 0; return; }
  if (c == 'r') { buildIllum(); lastDrawMs = 0; return; }
  if (isRight(c)) { illumDayOff += ILLUM_DAYS; buildIllum(); lastDrawMs = 0; return; }   // / next 60-day window
  if (isLeft(c)) {                                                                       // , previous 60-day window (>= today)
    illumDayOff = (illumDayOff >= ILLUM_DAYS) ? illumDayOff - ILLUM_DAYS : 0;
    buildIllum(); lastDrawMs = 0; return;
  }
}

void App::keyLocation(char c, bool enter, bool back) {
  if (isBack(c, back)) { pred.setSite(loc.obs()); screen = SCR_HOME; return; }
  if (enter) { screen = SCR_GPS; lastDrawMs = 0; return; }   // GPS data + sky plot
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
  if (c == 'v') { screen = SCR_GPSPOS; lastDrawMs = 0; return; }  // live DMS position
}

void App::keyUpdate(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (c == 'k' || enter) { doUpdateGp(); }
  if (c == 'f') { doFastUpdate(); }             // GP + favorites' transponders only
  if (c == 'a') { doCacheAllTransponders(); }   // cache all TX for offline use
  if (c == 'w') {
    setStatus(connectWifiCfg() ? "WiFi connected" : "WiFi failed");
  }
}

// Settings are grouped into categories for a two-level menu. The per-row value
// logic in adj()/ENTER stays keyed by absolute row index; these tables only map
// a category + cursor position to that absolute index. Every row 0..47 appears
// in exactly one category.
static const int SET_CAT_N = 4;
static const char* const SET_CAT_NAME[SET_CAT_N] = {
  "Radio / CAT", "Rotator", "Station / display", "Network / data"
};
static const int SET_RADIO[] = {0,30,1,2,31,32,33,34,21,22,23,24,44,45,46,36,37,62};
static const int SET_ROTOR[] = {8,9,10,11,12,18,47,19,16,17,13,14,15,35,38,39};
static const int SET_STN[]   = {26,3,40,7,54,48,49,25,43};
static const int SET_NET[]   = {4,5,50,51,6,20,41,42,52,53,60,55,56,57,58,59,27,28,29};
static const int* const SET_CAT_ROWS[SET_CAT_N] = { SET_RADIO, SET_ROTOR, SET_STN, SET_NET };
static const int SET_CAT_LEN[SET_CAT_N] = {
  (int)(sizeof(SET_RADIO)/sizeof(int)), (int)(sizeof(SET_ROTOR)/sizeof(int)),
  (int)(sizeof(SET_STN)/sizeof(int)),   (int)(sizeof(SET_NET)/sizeof(int))
};

void App::keySettings(char c, bool enter, bool back) {
  const int N = 40;
  // Top-level category list.
  if (setCat < 0) {
    if (isBack(c, back)) { applyRadioFromCfg(); applyRotatorFromCfg();
                           screen = SCR_HOME; return; }
    if (isUp(c))   setSel = (setSel + SET_CAT_N - 1) % SET_CAT_N;
    if (isDown(c)) setSel = (setSel + 1) % SET_CAT_N;
    if (enter) { setCat = setSel; setSel = 0; }
    return;
  }
  // Inside a category submenu: cursor runs over the category's row list.
  (void)N;
  const int len = SET_CAT_LEN[setCat];
  if (isBack(c, back)) { applyRadioFromCfg(); applyRotatorFromCfg();
                         setSel = setCat; setCat = -1; return; }   // back to category list
  if (isUp(c))   setSel = (setSel + len - 1) % len;
  if (isDown(c)) setSel = (setSel + 1) % len;
  const int sel = SET_CAT_ROWS[setCat][setSel];                   // absolute row index
  if (c == 's' && sel == 4) { startWifiScan(); return; }          // scan from the SSID row

  auto adj = [&](int dir){
    switch (sel) {
      case 0: { int m = (cfg.radioModel + dir + RIG_COUNT) % RIG_COUNT;
                cfg.radioModel = m; cfg.civAddr = RADIOS[m].civAddr;
                cfg.civBaud = RADIOS[m].defaultBaud; cfg.save();
                applyRadioFromCfg(); } break;
      case 2: { uint32_t bs[] = {1200,4800,9600,19200,38400,57600,115200};
                int idx=2; for (int i=0;i<7;i++) if (bs[i]==cfg.civBaud) idx=i;
                idx = (idx + dir + 7) % 7; cfg.civBaud = bs[idx];
                cfg.save(); applyRadioFromCfg(); } break;
      case 3: cfg.minPassEl = constrain(cfg.minPassEl + dir, 0, 30); cfg.save(); break;
      case 40: cfg.solarAct = (uint8_t)((cfg.solarAct + dir + 4) % 4); cfg.save(); break;
      case 43: cfg.wxUnits = (uint8_t)((cfg.wxUnits + dir + 3) % 3); cfg.save(); break;
      case 7: cfg.aosAlarm = !cfg.aosAlarm; cfg.save(); break;
      case 54: cfg.irBeacon = !cfg.irBeacon; cfg.save(); break;
      case 55: cfg.loraEnable = !cfg.loraEnable; cfg.save();
               if (cfg.loraEnable) loraStart(); break;
      case 56: { long f = (long)cfg.loraFreqKHz + dir * 100;   // 100 kHz steps
                 if (f < 150000) f = 150000; if (f > 960000) f = 960000;
                 cfg.loraFreqKHz = (uint32_t)f; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 57: { int sf = (int)cfg.loraSf + dir; if (sf < 7) sf = 12; if (sf > 12) sf = 7;
                 cfg.loraSf = (uint8_t)sf; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 58: { uint32_t bws[] = {62500, 125000, 250000}; int idx = 1;
                 for (int i = 0; i < 3; ++i) if (cfg.loraBwHz == bws[i]) idx = i;
                 idx = (idx + dir + 3) % 3; cfg.loraBwHz = bws[idx]; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 59: { int p = (int)cfg.loraTxDbm + dir; if (p < 0) p = 0; if (p > 22) p = 22;
                 cfg.loraTxDbm = (int8_t)p; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 60: { int r = ((int)cfg.loraRegion + dir + 3) % 3;
                 cfg.loraApplyRegion((uint8_t)r); cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 11: { long p = (long)cfg.rotPort + dir; if (p < 1) p = 65535;
                 if (p > 65535) p = 1; cfg.rotPort = (uint16_t)p; cfg.save(); } break;
      case 12: { uint32_t bs[] = {1200,4800,9600};
                int idx=2; for (int i=0;i<3;i++) if (bs[i]==cfg.rotBaud) idx=i;
                idx = (idx + dir + 3) % 3; cfg.rotBaud = bs[idx];
                cfg.save(); applyRotatorFromCfg(); } break;
      case 13: cfg.rotDeadband = constrain((int)cfg.rotDeadband + dir, 1, 15);
               cfg.save(); break;
      case 14: { int a = (int)cfg.rotParkAz + dir*5; while (a<0) a+=360; a%=360;
                 cfg.rotParkAz = (uint16_t)a; cfg.save(); } break;
      case 15: { const uint16_t opts[] = {0,30,60,120,180,300}; int idx=3;
                 for (int i=0;i<6;i++) if (opts[i]==cfg.rotLeadSec) idx=i;
                 idx = (idx + dir + 6) % 6; cfg.rotLeadSec = opts[idx];
                 cfg.save(); rotPassValid = false; } break;
      case 16: cfg.rotAzOff = constrain((int)cfg.rotAzOff + dir, -180, 180);
               cfg.save(); break;
      case 17: cfg.rotElOff = constrain((int)cfg.rotElOff + dir, -30, 30);
               cfg.save(); break;
      case 18: cfg.rotAzRange = (uint8_t)((cfg.rotAzRange + dir + 3) % 3);
               cfg.save(); lastAzCmd = lastUnwrappedAz = -999.0f; break;
      case 19: cfg.rotFlip = !cfg.rotFlip; cfg.save();
               lastAzCmd = lastElCmd = -999.0f; break;
      case 21: cfg.vfoType = (cfg.vfoType == VFO_MAIN_UP_SUB_DOWN)
                             ? VFO_MAIN_DOWN_SUB_UP : VFO_MAIN_UP_SUB_DOWN;
               cfg.save(); break;
      case 22: cfg.satMode = !cfg.satMode; cfg.save(); break;
      case 23: { long v = (long)cfg.catRateMs + dir*10; if (v < 10) v = 10;
                 cfg.catRateMs = (uint32_t)v; cfg.save(); } break;
      case 24: { long v = (long)cfg.catDelayMs + dir*2; if (v < 0) v = 0;
                 if (v > 200) v = 200; cfg.catDelayMs = (uint16_t)v; cfg.save();
                 if (rig) rig->setCmdDelay(cfg.catDelayMs); } break;
      case 25: { const uint16_t opts[] = {0,30,60,120,300}; int idx=0;
                 for (int i=0;i<5;i++) if (opts[i]==cfg.dimSecs) idx=i;
                 idx = (idx + dir + 5) % 5; cfg.dimSecs = opts[idx];
                 cfg.save(); lastInputMs = millis(); } break;
      case 30: cfg.catType = (uint8_t)((cfg.catType + dir + 3) % 3);
               cfg.save(); applyRadioFromCfg(); break;
      case 32: { long p = (long)cfg.catPort + dir; if (p < 1) p = 65535;
                 if (p > 65535) p = 1; cfg.catPort = (uint16_t)p;
                 cfg.save(); applyRadioFromCfg(); } break;
      case 36: cfg.rigdEnable = !cfg.rigdEnable; cfg.save(); break;
      case 37: { long p = (long)cfg.rigdPort + dir; if (p < 1) p = 65535;
                 if (p > 65535) p = 1; cfg.rigdPort = (uint16_t)p; cfg.save(); } break;
      case 38: cfg.rotdEnable = !cfg.rotdEnable; cfg.save(); break;
      case 39: { long p = (long)cfg.rotdPort + dir; if (p < 1) p = 65535;
                 if (p > 65535) p = 1; cfg.rotdPort = (uint16_t)p; cfg.save(); } break;
      case 53: { long p = (long)cfg.webPort + dir; if (p < 1) p = 65535;
                 if (p > 65535) p = 1; cfg.webPort = (uint16_t)p; cfg.save(); } break;
      case 44: { long v = (long)cfg.doppThreshFmHz + dir*25; if (v < 0) v = 0;
                 if (v > 2000) v = 2000; cfg.doppThreshFmHz = (uint16_t)v; cfg.save(); } break;
      case 45: { long v = (long)cfg.doppThreshLinHz + dir*10; if (v < 0) v = 0;
                 if (v > 1000) v = 1000; cfg.doppThreshLinHz = (uint16_t)v; cfg.save(); } break;
      case 46: { long v = (long)cfg.doppLeadMs + dir*5; if (v < 0) v = 0;
                 if (v > 100) v = 100; cfg.doppLeadMs = (uint16_t)v; cfg.save(); } break;
      case 47: { long v = (long)cfg.rotAzLookSec + dir; if (v < 0) v = 0;
                 if (v > 10) v = 10; cfg.rotAzLookSec = (uint8_t)v; cfg.save(); } break;
      case 48: { long v = (long)cfg.bright + dir*16; if (v < 10) v = 10;
                 if (v > 255) v = 255; cfg.bright = (uint8_t)v;
                 M5Cardputer.Display.setBrightness(cfg.bright);   // live preview
                 cfg.save(); } break;
      case 49: if (!imuReady) setStatus("No IMU on this board");
               else { cfg.tiltTune = !cfg.tiltTune; cfg.save();
                      tiltAccum = 0; lastTiltMs = millis(); } break;
    }
  };
  if (isLeft(c))  adj(-1);
  if (isRight(c)) adj(+1);
  if (enter) {
    switch (sel) {
      case 1: editTarget = 200; editTitle = "CI-V addr (hex)";
              editBuf = String(cfg.civAddr, HEX); screen = SCR_EDIT; break;
      case 4: editTarget = 201; editTitle = "WiFi SSID";
              editBuf = cfg.ssid; screen = SCR_EDIT; break;
      case 5: editTarget = 202; editTitle = "WiFi password";
              editBuf = cfg.pass; screen = SCR_EDIT; break;
      case 50: editTarget = 217; editTitle = "WiFi 2 SSID";
               editBuf = cfg.ssid2; screen = SCR_EDIT; break;
      case 51: editTarget = 218; editTitle = "WiFi 2 password";
               editBuf = cfg.pass2; screen = SCR_EDIT; break;
      case 52: cfg.webEnable = !cfg.webEnable; cfg.save();
               setStatus(cfg.webEnable ? "Web control ON" : "Web control OFF"); break;
      case 6: setStatus(connectWifiCfg() ? "WiFi OK" : "WiFi FAIL");
              break;
      case 62: runCatTest(); break;
      case 7: cfg.aosAlarm = !cfg.aosAlarm; cfg.save(); break;
      case 54: cfg.irBeacon = !cfg.irBeacon; cfg.save(); break;
      case 55: cfg.loraEnable = !cfg.loraEnable; cfg.save();
               if (cfg.loraEnable) loraStart(); break;
      case 56: editTarget = 701; editTitle = "LoRa freq (MHz)";
               editBuf = String(cfg.loraFreqKHz / 1000.0, 3); screen = SCR_EDIT; break;
      case 57: { int sf = (int)cfg.loraSf + 1; if (sf > 12) sf = 7;
                 cfg.loraSf = (uint8_t)sf; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 58: { uint32_t bws[] = {62500, 125000, 250000}; int idx = 1;
                 for (int i = 0; i < 3; ++i) if (cfg.loraBwHz == bws[i]) idx = i;
                 idx = (idx + 1) % 3; cfg.loraBwHz = bws[idx]; cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm); } break;
      case 59: editTarget = 702; editTitle = "LoRa TX power (dBm 0-22)";
               editBuf = String(cfg.loraTxDbm); screen = SCR_EDIT; break;
      case 60: { int r = ((int)cfg.loraRegion + 1) % 3;
                 cfg.loraApplyRegion((uint8_t)r); cfg.save();
                 if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                                 cfg.loraBwHz, cfg.loraTxDbm);
                 setStatus(r == 0 ? "US 33cm 906.875" : r == 1 ? "EU 70cm 433.775"
                                                               : "JP 430 431.000"); } break;
      case 11: editTarget = 206; editTitle = "Net rotator port";
               editBuf = String(cfg.rotPort); screen = SCR_EDIT; break;
      case 20: gpSrcSel = 0; gpSrcScroll = 0; screen = SCR_GPSRC; lastDrawMs = 0; break;
      case 26: editTarget = 204; editTitle = "My callsign";
               editBuf = cfg.myCall; screen = SCR_EDIT; break;
      case 41: editTarget = 214; editTitle = "QRZ username";
               editBuf = cfg.qrzUser; screen = SCR_EDIT; break;
      case 42: editTarget = 215; editTitle = "QRZ password";
               editBuf = cfg.qrzPass; screen = SCR_EDIT; break;
      case 27: {
        bool ok = copyFile(FILE_CFG, FILE_CFG_BAK) && copyFile(FILE_FAVS, FILE_FAVS_BAK);
        setStatus(ok ? "Backed up to SD" : "Backup failed");
      } break;
      case 28: {
        if (!Store::fs().exists(FILE_CFG_BAK)) { setStatus("No backup found"); break; }
        bool ok = copyFile(FILE_CFG_BAK, FILE_CFG);
        copyFile(FILE_FAVS_BAK, FILE_FAVS);
        if (ok) { cfg.load(); calDl = cfg.calDlHz; calUl = cfg.calUlHz;
                  loadFavs(); applyRadioFromCfg(); applyRotatorFromCfg();
                  buildSatView(); if (timeIsSet() && favN) buildSchedule();
                  setStatus("Restored from SD"); }
        else setStatus("Restore failed");
      } break;
      case 29: editTarget = 400; editTitle = "Type ERASE to wipe all";
               editBuf = ""; screen = SCR_EDIT; break;
      case 30: cfg.catType = (uint8_t)((cfg.catType + 1) % 3);
               cfg.save(); applyRadioFromCfg(); break;
      case 31: editTarget = 207;
               editTitle = (cfg.catType == CAT_RIGCTL) ? "rigctld host (IP)" : "Radio LAN host (IP)";
               editBuf = cfg.catHost; screen = SCR_EDIT; break;
      case 32: editTarget = 211;
               editTitle = (cfg.catType == CAT_RIGCTL) ? "rigctld port" : "Radio LAN port";
               editBuf = String(cfg.catPort); screen = SCR_EDIT; break;
      case 33: editTarget = 208; editTitle = "Radio LAN user";
               editBuf = cfg.catUser; screen = SCR_EDIT; break;
      case 34: editTarget = 209; editTitle = "Radio LAN password";
               editBuf = cfg.catPass; screen = SCR_EDIT; break;
      case 35: {                              // manual rotator control screen
        rotOut = false; smOut = false;        // manual takes over from auto-track
        float a, e;
        if (rot && rot->readPos(a, e)) { manAz = a; manEl = e; }
        else { manAz = (float)cfg.rotParkAz; manEl = (float)cfg.rotParkEl; }
        screen = SCR_ROTMAN; lastDrawMs = 0;
      } break;
      default: adj(+1); break;
    }
  }
}

static Screen editHome(int t) {
  if (t == 700) return SCR_MESSAGES;    // LoRa message compose (cancel)
  if (t == 600) return SCR_LOG;         // LoTW SAT_NAME prompt (abort export)
  if (t >= 500) return SCR_LOGENTRY;    // QSO log field edit
  if (t == 340) return SCR_TRACK;       // CTCSS tone override
  if (t >= 400) return SCR_SETTINGS;    // reset confirmation
  if (t >= 320) return SCR_PASSES;      // manual transponder
  if (t >= 310) return SCR_SATLIST;     // manual GP entry
  if (t >= 300) return SCR_LOCATION;    // manual time
  if (t == 216) return SCR_QRZ;         // QRZ callsign prompt
  if (t == 104) return SCR_GLOBE;       // DX grid entered from the globe
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
      case 104: {                                   // DX grid for the globe / mutual
        double dlat, dlon; String g = editBuf; g.trim(); g.toUpperCase();
        if (Location::gridToLatLon(g, dlat, dlon)) {
          strncpy(dxGrid, g.c_str(), sizeof(dxGrid) - 1); dxGrid[sizeof(dxGrid)-1] = 0;
          dxLat = dlat; dxLon = dlon;
          setStatus(String("DX set: ") + dxGrid);
        } else setStatus("Bad grid (e.g. FM18lw)");
      } break;
      case 200: cfg.civAddr = (uint8_t)strtol(editBuf.c_str(), nullptr, 16); break;
      case 201: strncpy(cfg.ssid, editBuf.c_str(), sizeof(cfg.ssid)-1);
                cfg.ssid[sizeof(cfg.ssid)-1] = 0; break;
      case 202: strncpy(cfg.pass, editBuf.c_str(), sizeof(cfg.pass)-1);
                cfg.pass[sizeof(cfg.pass)-1] = 0; break;
      case 217: strncpy(cfg.ssid2, editBuf.c_str(), sizeof(cfg.ssid2)-1);
                cfg.ssid2[sizeof(cfg.ssid2)-1] = 0; break;
      case 218: strncpy(cfg.pass2, editBuf.c_str(), sizeof(cfg.pass2)-1);
                cfg.pass2[sizeof(cfg.pass2)-1] = 0; break;
      case 203: strncpy(cfg.gpUrl, editBuf.c_str(), sizeof(cfg.gpUrl)-1);
                cfg.gpUrl[sizeof(cfg.gpUrl)-1] = 0; break;
      case 204: strncpy(cfg.myCall, editBuf.c_str(), sizeof(cfg.myCall)-1);
                cfg.myCall[sizeof(cfg.myCall)-1] = 0; break;
      case 214: strncpy(cfg.qrzUser, editBuf.c_str(), sizeof(cfg.qrzUser)-1);
                cfg.qrzUser[sizeof(cfg.qrzUser)-1] = 0; qrzSessionKey = ""; break;
      case 215: strncpy(cfg.qrzPass, editBuf.c_str(), sizeof(cfg.qrzPass)-1);
                cfg.qrzPass[sizeof(cfg.qrzPass)-1] = 0; qrzSessionKey = ""; break;
      case 210: { double v = editBuf.toFloat(); if (v >= 0.1 && v <= 50000.0) cfg.beaconMHz = v;
                  cfg.save(); screen = SCR_ORBIT; orbitPage = 4; lastDrawMs = 0;
                  setStatus("Saved"); return; }
      case 216: {                                   // QRZ callsign lookup
        String call = editBuf; call.trim(); call.toUpperCase();
        screen = SCR_QRZ; lastDrawMs = 0;
        if (call.length() == 0) { qrzHaveResult = false; return; }
        setStatus("QRZ: looking up " + call + "..."); draw();
        String err;
        if (qrzLookup(call, err)) { qrzHaveResult = true; setStatus(""); }
        else { qrzHaveResult = false; setStatus("QRZ: " + err); }
        return; }

      // ---- rotctld (network rotator) host / port ----
      case 205: strncpy(cfg.rotHost, editBuf.c_str(), sizeof(cfg.rotHost)-1);
                cfg.rotHost[sizeof(cfg.rotHost)-1] = 0;
                cfg.save(); applyRotatorFromCfg();
                screen = SCR_SETTINGS; setStatus("Saved"); return;
      case 206: { long p = editBuf.toInt(); if (p < 1) p = 1; if (p > 65535) p = 65535;
                  cfg.rotPort = (uint16_t)p; cfg.save(); applyRotatorFromCfg();
                  screen = SCR_SETTINGS; setStatus("Saved"); return; }

      // ---- Icom LAN (network CAT) host / user / password / port ----
      case 207: strncpy(cfg.catHost, editBuf.c_str(), sizeof(cfg.catHost)-1);
                cfg.catHost[sizeof(cfg.catHost)-1] = 0;
                cfg.save(); applyRadioFromCfg();
                screen = SCR_SETTINGS; setStatus("Saved"); return;
      case 208: strncpy(cfg.catUser, editBuf.c_str(), sizeof(cfg.catUser)-1);
                cfg.catUser[sizeof(cfg.catUser)-1] = 0;
                cfg.save(); applyRadioFromCfg();
                screen = SCR_SETTINGS; setStatus("Saved"); return;
      case 209: strncpy(cfg.catPass, editBuf.c_str(), sizeof(cfg.catPass)-1);
                cfg.catPass[sizeof(cfg.catPass)-1] = 0;
                cfg.save(); applyRadioFromCfg();
                screen = SCR_SETTINGS; setStatus("Saved"); return;
      case 211: { long p = editBuf.toInt(); if (p < 1) p = 1; if (p > 65535) p = 65535;
                  cfg.catPort = (uint16_t)p; cfg.save(); applyRadioFromCfg();
                  screen = SCR_SETTINGS; setStatus("Saved"); return; }

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
      // ---- QSO log fields ----
      case 500: strncpy(qso.call, editBuf.c_str(), sizeof(qso.call)-1);
                qso.call[sizeof(qso.call)-1]=0; screen=SCR_LOGENTRY; return;
      case 501: strncpy(qso.rstS, editBuf.c_str(), sizeof(qso.rstS)-1);
                qso.rstS[sizeof(qso.rstS)-1]=0; screen=SCR_LOGENTRY; return;
      case 502: strncpy(qso.rstR, editBuf.c_str(), sizeof(qso.rstR)-1);
                qso.rstR[sizeof(qso.rstR)-1]=0; screen=SCR_LOGENTRY; return;
      case 503: strncpy(qso.grid, editBuf.c_str(), sizeof(qso.grid)-1);
                qso.grid[sizeof(qso.grid)-1]=0; screen=SCR_LOGENTRY; return;
      case 504: strncpy(qso.notes, editBuf.c_str(), sizeof(qso.notes)-1);
                qso.notes[sizeof(qso.notes)-1]=0; screen=SCR_LOGENTRY; return;
      case 505: {   // Date YYYY-MM-DD (UTC); keep the time-of-day
        int Y, Mo, D;
        if (sscanf(editBuf.c_str(), "%d-%d-%d", &Y, &Mo, &D) == 3) {
          time_t base = qso.utc ? (time_t)qso.utc
                                : (timeIsSet() ? nowUtc() : (time_t)1735689600);
          struct tm g; gmtime_r(&base, &g);
          g.tm_year = Y - 1900; g.tm_mon = Mo - 1; g.tm_mday = D;
          qso.utc = (uint32_t)mktime(&g);            // TZ=UTC0 -> UTC epoch
        } else setStatus("Use YYYY-MM-DD");
        screen = SCR_LOGENTRY; return;
      }
      case 506: {   // Time HH:MM:SS (UTC); keep the date
        int H, Mi, S = 0;
        if (sscanf(editBuf.c_str(), "%d:%d:%d", &H, &Mi, &S) >= 2) {
          time_t base = qso.utc ? (time_t)qso.utc
                                : (timeIsSet() ? nowUtc() : (time_t)1735689600);
          struct tm g; gmtime_r(&base, &g);
          g.tm_hour = H; g.tm_min = Mi; g.tm_sec = S;
          qso.utc = (uint32_t)mktime(&g);
        } else setStatus("Use HH:MM:SS");
        screen = SCR_LOGENTRY; return;
      }
      case 507: { double m = atof(editBuf.c_str());
                  qso.dlHz = (uint32_t)llround(m * 1e6); screen = SCR_LOGENTRY; return; }
      case 508: { double m = atof(editBuf.c_str());
                  qso.ulHz = (uint32_t)llround(m * 1e6); screen = SCR_LOGENTRY; return; }

      // ---- LoTW SAT_NAME prompt during ADIF export ----
      case 600: {
        String sn = editBuf; sn.trim();
        if (!sn.length()) { setStatus("Enter a SAT_NAME (max 6)"); return; }
        bool isNew = !Store::fs().exists(FILE_LOTW);
        File lf = Store::fs().open(FILE_LOTW, "a");
        if (lf) {
          if (isNew) lf.print("SAT_NAME,AMSAT_NAME\n");
          lf.print(sn); lf.print(","); lf.print(exportSats[exportPendIdx]);
          lf.print("\n"); lf.close();
        }
        exportPendIdx++;
        if (exportPendIdx < exportPendN) { promptNextLotw(); return; }
        bool ok = exportAdif();
        setStatus(ok ? "Exported qso_log.adi" : "Export failed");
        screen = SCR_LOG; return;
      }

      // ---- factory reset confirmation ----
      case 400: {
        if (editBuf == "ERASE") { factoryReset(); return; }  // formats + reboots
        setStatus("Reset cancelled");
        screen = SCR_SETTINGS; return;
      }

      // ---- send a LoRa text message (broadcast) ----
      case 700: {
        String t = editBuf;
        if (t.length()) loraSendCurrent(t.c_str());
        screen = SCR_MESSAGES; lastDrawMs = 0; return;
      }
      // ---- LoRa frequency (MHz) ----
      case 701: {
        double mhz = editBuf.toFloat();
        if (mhz >= 150.0 && mhz <= 960.0) {
          cfg.loraFreqKHz = (uint32_t)lround(mhz * 1000.0); cfg.save();
          if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                          cfg.loraBwHz, cfg.loraTxDbm);
          setStatus("LoRa freq set");
        } else setStatus("Range 150-960 MHz");
        screen = SCR_SETTINGS; return;
      }
      // ---- LoRa TX power (dBm) ----
      case 702: {
        int p = editBuf.toInt(); if (p < 0) p = 0; if (p > 22) p = 22;
        cfg.loraTxDbm = (int8_t)p; cfg.save();
        if (lora.ready()) lora.setRadio(cfg.loraFreqKHz, cfg.loraSf,
                                        cfg.loraBwHz, cfg.loraTxDbm);
        setStatus("LoRa TX power set");
        screen = SCR_SETTINGS; return;
      }
    }
    if (editTarget <= 200 && editTarget != 104) {
      loc.setManual(cfg.lat, cfg.lon, cfg.altM);
      pred.setSite(loc.obs());
    }
    cfg.save();
    applyRadioFromCfg();
    screen = editHome(editTarget);
    setStatus("Saved");
    return;
  }
  if (c >= 32 && c < 127) {
    if (editTarget == 103 || editTarget == 104 || editTarget == 204 || editTarget == 216 ||
        editTarget == 330 ||
        editTarget == 500 || editTarget == 503 || editTarget == 600) {
      if      (c >= 'a' && c <= 'z') c -= 32;   // uppercase by default ...
      else if (c >= 'A' && c <= 'Z') c += 32;   // ... with shift for lowercase
    }
    if (!(editTarget == 600 && editBuf.length() >= 6)) editBuf += c;  // LoTW 6-char cap
  }
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
  String trk;                                   // background Sun/Moon tracking tag
  int trkX = 0;
  if (smOut && screen != SCR_SUNMOON) {
    trk = smSel ? "MOON" : "SUN";
    trkX = rightLimit - (int)trk.length() * 6 - 6;
    rightLimit = trkX;
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
  if (trk.length()) {                           // Sun/Moon tracking runs in background
    canvas.setTextColor(CL_ORANGE, CL_BLUE);
    canvas.setCursor(trkX, 4);
    canvas.print(trk);
  }
}
void App::drawAbout() {
  header("About");
  canvas.setTextSize(1);
  int y = 22; const int dy = 11;
  auto line = [&](const String& s){
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(s); y += dy;
  };
  line(String("CardSat v") + FW_VERSION);
  line("by Paul Stoetzer, N8HM");
  line(String("Built ") + __DATE__);
  line(String("Storage: ") + (Store::onSD() ? "microSD" : "internal flash"));
  {
    double minAge = -1.0;
    for (int i = 0; i < db.count(); ++i) {
      double a = gpAgeDays(db.at(i));
      if (minAge < 0 || a < minAge) minAge = a;
    }
    String g = String("GP: ") + db.count() + " sats";
    if (db.count() && timeIsSet() && minAge >= 0) g += ", " + String(minAge, 1) + "d old";
    line(g);
  }
  line(String("WiFi: ") + (net.connected() ? WiFi.localIP().toString() : String("not connected")));
  {
    int b = M5.Power.getBatteryLevel();
    String s = String("Battery: ") + (b < 0 ? String("n/a") : String(b) + "%");
    if ((int)M5.Power.isCharging() == 1) s += " (charging)";  // 1=charging; ignore "unknown" (2)
    line(s);
  }
  line(String("Free heap: ") + String(ESP.getFreeHeap() / 1024) + " KB");
  {
    uint32_t up = millis() / 1000;
    char b[28];
    snprintf(b, sizeof(b), "Uptime: %luh %lum",
             (unsigned long)(up / 3600), (unsigned long)((up % 3600) / 60));
    line(String(b));
  }
  footer("` back");
}

void App::keyAbout(char c, bool enter, bool back) {
  if (isBack(c, back) || enter) screen = SCR_HOME;
}

// Network-recovery reboot prompt. Shown when the socket-failure recovery path has
// exhausted its hard WiFi resets and the connection still won't come back -- a
// reboot is the last-resort cure, but we ask rather than reboot under the user.
void App::drawNetReboot() {
  header("Network problem");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_RED, CL_BLACK);
  canvas.setCursor(6, 26); canvas.print("WiFi/socket recovery failed.");
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 44); canvas.print("Repeated connections were");
  canvas.setCursor(6, 54); canvas.print("refused and resetting WiFi");
  canvas.setCursor(6, 64); canvas.print("did not recover it.");
  canvas.setCursor(6, 82); canvas.print("A reboot usually clears this.");
  canvas.setTextColor(CL_YELLOW, CL_BLACK);
  canvas.setCursor(6, 102); canvas.print("ENTER / y = reboot now");
  canvas.setCursor(6, 112); canvas.print("`  /  n = keep running");
  footer("ENTER reboot   ` cancel");
}

void App::keyNetReboot(char c, bool enter, bool back) {
  if (enter || c == 'y') {
    setStatus("Rebooting..."); draw(); delay(300);
    ESP.restart();
    return;
  }
  if (isBack(c, back) || c == 'n') {
    net.failedResets = 0;                    // user declined; reset the reboot counter
    screen = netRebootReturn; lastDrawMs = 0;
  }
}

// ===================== QSO logging =========================================
static const char* bandFor(double mhz) {            // ADIF 3.1.7 Band enumeration
  if (mhz >= 28    && mhz <= 29.7)  return "10m";
  if (mhz >= 50    && mhz <= 54)    return "6m";
  if (mhz >= 144   && mhz <= 148)   return "2m";
  if (mhz >= 222   && mhz <= 225)   return "1.25m";
  if (mhz >= 420   && mhz <= 450)   return "70cm";
  if (mhz >= 902   && mhz <= 928)   return "33cm";
  if (mhz >= 1240  && mhz <= 1300)  return "23cm";
  if (mhz >= 2300  && mhz <= 2450)  return "13cm";
  if (mhz >= 3300  && mhz <= 3500)  return "9cm";
  if (mhz >= 5650  && mhz <= 5925)  return "6cm";
  if (mhz >= 10000 && mhz <= 10500) return "3cm";
  return "";
}
static void adifField(String& out, const char* name, const String& val) {
  if (!val.length()) return;
  out += "<"; out += name; out += ":"; out += String(val.length()); out += ">";
  out += val; out += " ";
}

static void writeQsoCsv(File& f, const PendingQso& q) {
  auto cl = [](const char* s) {
    String o = s; o.replace(",", " "); o.replace("\n", " "); o.replace("\r", " ");
    return o;
  };
  String notes = q.notes; notes.replace("\n", " "); notes.replace("\r", " ");
  f.printf("%lu,%s,%s,%s,%lu,%lu,%s,%s,%s,%s,%s,%s\n",
           (unsigned long)q.utc, cl(q.call).c_str(), cl(q.sat).c_str(),
           cl(q.mode).c_str(), (unsigned long)q.dlHz, (unsigned long)q.ulHz,
           cl(q.rstS).c_str(), cl(q.rstR).c_str(), cl(q.myGrid).c_str(),
           cl(q.grid).c_str(), cl(q.myCall).c_str(), notes.c_str());
}

static bool parseQsoCsv(const String& line, PendingQso& q) {
  String f[12]; int n = 0, start = 0;
  for (int i = 0; i < (int)line.length() && n < 11; ++i)
    if (line[i] == ',') { f[n++] = line.substring(start, i); start = i + 1; }
  f[n++] = line.substring(start);
  if (n < 11) return false;
  memset(&q, 0, sizeof(q));
  q.utc = strtoul(f[0].c_str(), nullptr, 10);
  strncpy(q.call,   f[1].c_str(),  sizeof(q.call)-1);
  strncpy(q.sat,    f[2].c_str(),  sizeof(q.sat)-1);
  strncpy(q.mode,   f[3].c_str(),  sizeof(q.mode)-1);
  q.dlHz = strtoul(f[4].c_str(), nullptr, 10);
  q.ulHz = strtoul(f[5].c_str(), nullptr, 10);
  strncpy(q.rstS,   f[6].c_str(),  sizeof(q.rstS)-1);
  strncpy(q.rstR,   f[7].c_str(),  sizeof(q.rstR)-1);
  strncpy(q.myGrid, f[8].c_str(),  sizeof(q.myGrid)-1);
  strncpy(q.grid,   f[9].c_str(),  sizeof(q.grid)-1);
  if (n >= 12) {                                   // new schema: mycall before notes
    strncpy(q.myCall, f[10].c_str(), sizeof(q.myCall)-1);
    strncpy(q.notes,  f[11].c_str(), sizeof(q.notes)-1);
  } else {                                         // legacy schema: no mycall column
    strncpy(q.notes,  f[10].c_str(), sizeof(q.notes)-1);
  }
  return true;
}

// Fill qso.sat / qso.mode and the *non-Doppler* default frequencies (the centre
// of a linear transponder's passband, or the nominal downlink/uplink for an FM /
// single-channel bird) from the currently active satellite + transponder. Used
// for manual log entries; leaves dl/ul at 0 if the sat has no transponders.
void App::seedQsoSatDefaults() {
  SatEntry* s = activeSat();
  if (!s) return;
  strncpy(qso.sat, s->name, sizeof(qso.sat) - 1); qso.sat[sizeof(qso.sat) - 1] = 0;
  if (activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    strncpy(qso.mode, t.isLinear ? "SSB" : (t.mode[0] ? t.mode : "FM"),
            sizeof(qso.mode) - 1); qso.mode[sizeof(qso.mode) - 1] = 0;
    int32_t off = (t.isLinear && t.bandwidth() > 0) ? (int32_t)(t.bandwidth() / 2) : 0;
    uint32_t dl, ul; Predictor::passbandFreqs(t, off, dl, ul);
    qso.dlHz = dl; qso.ulHz = ul;
  }
}

// Snapshot the auto fields (time, my grid/call) and open the entry screen. When
// invoked while actively working a pass (from the Track/Polar screen, clock set),
// the frequencies are the live Doppler-corrected values. Otherwise -- e.g. "New
// QSO entry" from the Log menu -- the satellite, frequencies, time and date all
// start as editable defaults (non-Doppler centre/nominal), so a QSO can be logged
// after the fact. Radio control, if engaged, keeps running.
void App::beginQso() {
  logReturn = screen;
  bool live = (screen == SCR_TRACK || screen == SCR_BIG || screen == SCR_POLAR || screen == SCR_MANUAL || screen == SCR_MANUALBIG);
  memset(&qso, 0, sizeof(qso));
  strncpy(qso.rstS, "59", sizeof(qso.rstS) - 1);
  strncpy(qso.rstR, "59", sizeof(qso.rstR) - 1);
  qso.utc = timeIsSet() ? (uint32_t)nowUtc() : 0;
  String mg = loc.toGrid(loc.obs().lat, loc.obs().lon);
  strncpy(qso.myGrid, mg.c_str(), sizeof(qso.myGrid) - 1);
  strncpy(qso.myCall, cfg.myCall, sizeof(qso.myCall) - 1);
  SatEntry* s = activeSat();
  if (s && live && activeTxCount > 0 && timeIsSet()) {
    strncpy(qso.sat, s->name, sizeof(qso.sat) - 1);
    Transponder& t = activeTx[curTx];
    strncpy(qso.mode, t.isLinear ? "SSB" : (t.mode[0] ? t.mode : "FM"),
            sizeof(qso.mode) - 1);
    pred.setSite(loc.obs()); pred.setSat(*s);
    LiveLook L = pred.look(nowUtc());
    struct timeval tv; gettimeofday(&tv, nullptr);
    L.rangeRate = pred.rangeRateAt((double)tv.tv_sec + tv.tv_usec * 1e-6);
    uint32_t dlOp, ulOp, rx, tx;
    Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
    Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
    qso.dlHz = rx; qso.ulHz = tx;
  } else if (activeTxCount > 0) {
    seedQsoSatDefaults();          // pre-fill from the selected sat (non-Doppler)
  }
  logSel = 0; logEditIdx = -1;
  screen = SCR_LOGENTRY;
}

bool App::saveQso() {
  bool isNew = !Store::fs().exists(FILE_LOG);
  File f = Store::fs().open(FILE_LOG, "a");
  if (!f) return false;
  if (isNew) f.print("utc,call,sat,mode,dl,ul,rsts,rstr,mygrid,grid,mycall,notes\n");
  writeQsoCsv(f, qso);
  f.close();
  return true;
}

int App::qsoCount() {
  if (!Store::fs().exists(FILE_LOG)) return 0;
  File f = Store::fs().open(FILE_LOG, "r");
  if (!f) return 0;
  int n = 0;
  while (f.available()) {
    String l = f.readStringUntil('\n'); l.trim();
    if (l.length() && !l.startsWith("utc,")) n++;
  }
  f.close();
  return n;
}

// -- LoTW SAT_NAME mapping ---------------------------------------------------
// LoTW limits SAT_NAME to 6 chars and uses its own names, so CardSat's (AMSAT)
// satellite name often needs translating. The external /CardSat/lotw_sats.csv
// ("SAT_NAME,AMSAT_NAME" rows) is consulted first so it can be updated without
// reflashing; then this built-in table of the non-identity cases.
static const struct { const char* amsat; const char* lotw; } LOTW_SATS[] = {
  {"AO-07","AO-7"}, {"ISS","ARISS"}, {"LILACSAT-2","CAS-3H"},
  {"SONATE-2","SONATE"}, {"Taurus 1","TAURUS"},
  {"TEVEL2-1","TEV2-1"}, {"TEVEL2-2","TEV2-2"}, {"TEVEL2-3","TEV2-3"},
  {"TEVEL2-4","TEV2-4"}, {"TEVEL2-5","TEV2-5"}, {"TEVEL2-6","TEV2-6"},
  {"TEVEL2-7","TEV2-7"}, {"TEVEL2-8","TEV2-8"}, {"TEVEL2-9","TEV2-9"},
};
static const int LOTW_SATS_N = sizeof(LOTW_SATS) / sizeof(LOTW_SATS[0]);

// Resolve to a LoTW SAT_NAME. Returns 0 if resolved (a mapping was found, or the
// name already fits in 6 chars), or 1 if there is no mapping and the name is too
// long to use as-is (caller should prompt). 'out' always gets a usable value.
static int lotwSatResolve(const char* amsat, char out[7]) {
  File f = Store::fs().open(FILE_LOTW, "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n'); line.trim();
      if (!line.length() || line.startsWith("SAT_NAME")) continue;
      int comma = line.indexOf(',');
      if (comma < 0) continue;
      String sn = line.substring(0, comma);     sn.trim();
      String an = line.substring(comma + 1);     an.trim();
      if (an.equalsIgnoreCase(amsat)) {
        strncpy(out, sn.c_str(), 6); out[6] = 0;
        f.close(); return 0;
      }
    }
    f.close();
  }
  for (int i = 0; i < LOTW_SATS_N; ++i)
    if (!strcasecmp(LOTW_SATS[i].amsat, amsat)) {
      strncpy(out, LOTW_SATS[i].lotw, 6); out[6] = 0; return 0;
    }
  strncpy(out, amsat, 6); out[6] = 0;
  return (strlen(amsat) > 6) ? 1 : 0;
}

void App::promptNextLotw() {
  editTarget = 600;
  editTitle  = String("LoTW name: ") + exportSats[exportPendIdx];
  editBuf    = "";
  screen     = SCR_EDIT;
}

// Export entry point from the Log menu: find any logged satellites whose LoTW
// SAT_NAME can't be resolved automatically and prompt for each (persisting the
// answer to lotw_sats.csv), then write the ADIF.
void App::beginAdifExport() {
  exportPendN = 0; exportPendIdx = 0;
  String seen[24]; int seenN = 0;
  File f = Store::fs().open(FILE_LOG, "r");
  if (f) {
    while (f.available()) {
      String line = f.readStringUntil('\n');
      while (line.length() && (line[line.length()-1]=='\r' || line[line.length()-1]=='\n'))
        line.remove(line.length()-1);
      if (!line.length() || line.startsWith("utc,")) continue;
      PendingQso q;
      if (!parseQsoCsv(line, q) || !q.sat[0]) continue;
      bool dup = false;
      for (int i = 0; i < seenN; ++i) if (seen[i].equalsIgnoreCase(q.sat)) { dup = true; break; }
      if (dup) continue;
      if (seenN < 24) seen[seenN++] = q.sat;
      char nm[7];
      if (lotwSatResolve(q.sat, nm) && exportPendN < 16)   // 1 => needs a prompt
        exportSats[exportPendN++] = q.sat;
    }
    f.close();
  }
  if (exportPendN == 0) {
    bool ok = exportAdif();
    setStatus(ok ? "Exported qso_log.adi" : "No log to export");
    return;
  }
  promptNextLotw();
}

bool App::exportAdif() {
  File in = Store::fs().open(FILE_LOG, "r");
  if (!in) return false;
  File out = Store::fs().open(FILE_ADIF, "w");
  if (!out) { in.close(); return false; }
  out.print("CardSat ADIF export\n");
  out.print("<ADIF_VER:5>3.1.7 <PROGRAMID:7>CardSat ");
  { String ver = FW_VERSION;                          // <PROGRAMVERSION:len>value
    out.print("<PROGRAMVERSION:"); out.print(ver.length()); out.print(">");
    out.print(ver); out.print(" "); }
  if (timeIsSet()) {                                  // <CREATED_TIMESTAMP:15>YYYYMMDD HHMMSS
    time_t now = time(nullptr); struct tm* g = gmtime(&now);
    char ts[16]; strftime(ts, sizeof(ts), "%Y%m%d %H%M%S", g);
    out.print("<CREATED_TIMESTAMP:15>"); out.print(ts); out.print(" ");
  }
  out.print("<EOH>\n");
  while (in.available()) {
    String line = in.readStringUntil('\n');
    while (line.length() && (line[line.length()-1] == '\r' ||
                             line[line.length()-1] == '\n'))
      line.remove(line.length() - 1);
    if (!line.length() || line.startsWith("utc,")) continue;
    PendingQso q;
    if (!parseQsoCsv(line, q)) continue;
    time_t tt = (time_t)q.utc; struct tm* g = gmtime(&tt);
    char d[9] = "", tm6[7] = "";
    if (g) { strftime(d, sizeof(d), "%Y%m%d", g); strftime(tm6, sizeof(tm6), "%H%M%S", g); }
    double dlM = q.dlHz / 1e6, ulM = q.ulHz / 1e6;
    String rec;
    adifField(rec, "CALL", q.call);
    if (q.utc) { adifField(rec, "QSO_DATE", String(d)); adifField(rec, "TIME_ON", String(tm6)); }
    adifField(rec, "MODE", q.mode);
    char satnm[7]; lotwSatResolve(q.sat, satnm);
    adifField(rec, "SAT_NAME", satnm);
    adifField(rec, "PROP_MODE", "SAT");
    if (ulM > 0) { adifField(rec, "FREQ", String(ulM, 4)); adifField(rec, "BAND", bandFor(ulM)); }
    if (dlM > 0) { adifField(rec, "FREQ_RX", String(dlM, 4)); adifField(rec, "BAND_RX", bandFor(dlM)); }
    adifField(rec, "RST_SENT", q.rstS);
    adifField(rec, "RST_RCVD", q.rstR);
    adifField(rec, "GRIDSQUARE", q.grid);
    adifField(rec, "MY_GRIDSQUARE", q.myGrid);
    adifField(rec, "STATION_CALLSIGN", q.myCall);
    adifField(rec, "COMMENT", q.notes);
    rec += "<EOR>\n";
    out.print(rec);
  }
  in.close(); out.close();
  return true;
}

void App::drawLog() {
  header("Log");
  canvas.setTextSize(1);
  const char* items[] = { "New QSO entry", "View / edit log", "Export to ADIF", "Voice Memos" };
  for (int i = 0; i < 4; ++i) {
    int y = 26 + i*14;
    if (i == logMenuSel) { canvas.fillRect(0, y-2, 240, 13, CL_SELBG);
                           canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                   canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(items[i]);
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 26 + 4*14 + 6);
  canvas.printf("%d logged  (qso_log.csv)", qsoCount());
  footer("; / . move  ENT  ` back");
}

void App::keyLog(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  const int N = 4;
  if (isUp(c))   logMenuSel = (logMenuSel + N - 1) % N;
  if (isDown(c)) logMenuSel = (logMenuSel + 1) % N;
  if (enter) {
    if (logMenuSel == 0) beginQso();
    else if (logMenuSel == 1) { loadLog(); screen = SCR_LOGLIST; }
    else if (logMenuSel == 2) beginAdifExport();
    else { buildMemoList(); memoSel = 0; memoScroll = 0; memoConfirmDel = false;
           screen = SCR_MEMOS; lastDrawMs = 0; }
  }
}

void App::drawLogEntry() {
  header(logEditIdx >= 0 ? "Edit QSO" : "Log QSO");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 18);
  canvas.printf("MyGrid %s  MyCall %s", qso.myGrid[0] ? qso.myGrid : "-",
                qso.myCall[0] ? qso.myCall : "-");

  const int LF = 11;
  char dbuf[16], tbuf[16], dlb[16], ulb[16];
  if (qso.utc) { time_t tt = (time_t)qso.utc; struct tm g; gmtime_r(&tt, &g);
                 strftime(dbuf, sizeof(dbuf), "%Y-%m-%d", &g);
                 strftime(tbuf, sizeof(tbuf), "%H:%M:%SZ", &g); }
  else { strcpy(dbuf, "(set)"); strcpy(tbuf, "(set)"); }
  if (qso.dlHz) snprintf(dlb, sizeof(dlb), "%.4f", qso.dlHz / 1e6); else strcpy(dlb, "(set)");
  if (qso.ulHz) snprintf(ulb, sizeof(ulb), "%.4f", qso.ulHz / 1e6); else strcpy(ulb, "(set)");
  const char* labels[LF] = { "Date", "Time", "Sat", "DL MHz", "UL MHz",
                             "Call", "Mode", "RST S", "RST R", "Grid", "Notes" };
  const char* vals[LF]   = { dbuf, tbuf, qso.sat[0] ? qso.sat : "(pick)", dlb, ulb,
                             qso.call, qso.mode, qso.rstS, qso.rstR, qso.grid, qso.notes };
  const int VIS = 8;
  int scroll = (logSel >= VIS) ? (logSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && scroll + v < LF; ++v) {
    int i = scroll + v;
    int y = 30 + v*11;
    bool sel = (i == logSel);
    if (sel) { canvas.fillRect(0, y-1, 240, 11, CL_BLUE);
               canvas.setTextColor(CL_WHITE, CL_BLUE); }
    else        canvas.setTextColor(CL_CYAN, CL_BLACK);
    canvas.setCursor(4, y); canvas.printf("%-6s %.27s", labels[i], vals[i]);
  }
  footer(logEditIdx >= 0 ? "ENT edit  s save  x del  ` back"
                         : "ENT edit  s save  ` cancel");
}

void App::keyLogEntry(char c, bool enter, bool back) {
  const int LF = 11;
  if (c != 'x') logDelArm = false;             // any other key disarms delete
  if (isBack(c, back)) { logEditIdx = -1; screen = logReturn; lastDrawMs = 0; return; }
  if (isUp(c))   logSel = (logSel + LF - 1) % LF;
  if (isDown(c)) logSel = (logSel + 1) % LF;
  if (c == 'x' && logEditIdx >= 0) {           // delete this entry (two-press)
    if (!logDelArm) { logDelArm = true; setStatus("Press x again to delete"); return; }
    bool ok = rewriteLog(logFirstIdx + logEditIdx, nullptr, true);
    setStatus(ok ? "QSO deleted" : "Delete failed");
    logDelArm = false; logEditIdx = -1;
    if (ok) loadLog();
    screen = logReturn; lastDrawMs = 0; return;
  }
  if (c == 's') {
    if (!qso.call[0]) { setStatus("Call required to log"); return; }
    bool ok = (logEditIdx >= 0) ? rewriteLog(logFirstIdx + logEditIdx, &qso, false)
                                : saveQso();
    setStatus(ok ? (logEditIdx >= 0 ? "QSO updated" : "QSO logged") : "Log write failed");
    if (ok && logReturn == SCR_LOGLIST) loadLog();
    logEditIdx = -1;
    screen = logReturn; lastDrawMs = 0; return;
  }
  if (enter) {
    char b[20];
    if (logSel == 6) {                          // Mode: toggle SSB<->CW on linear
      if (strcmp(qso.mode, "FM") != 0) {
        strncpy(qso.mode, strcmp(qso.mode, "CW") == 0 ? "SSB" : "CW",
                sizeof(qso.mode) - 1);
        qso.mode[sizeof(qso.mode) - 1] = 0;
      } else setStatus("FM mode is fixed");
      return;
    }
    if (logSel == 2) {                          // Sat: pick from the satellite list
      logPickSat = true; screen = SCR_SATLIST; lastDrawMs = 0; return;
    }
    switch (logSel) {
      case 0: editTarget = 505; editTitle = "Date YYYY-MM-DD (UTC)";
              if (qso.utc) { time_t tt=(time_t)qso.utc; struct tm g; gmtime_r(&tt,&g);
                             strftime(b,sizeof(b),"%Y-%m-%d",&g); editBuf=b; }
              else editBuf = ""; break;
      case 1: editTarget = 506; editTitle = "Time HH:MM:SS (UTC)";
              if (qso.utc) { time_t tt=(time_t)qso.utc; struct tm g; gmtime_r(&tt,&g);
                             strftime(b,sizeof(b),"%H:%M:%S",&g); editBuf=b; }
              else editBuf = ""; break;
      case 3: editTarget = 507; editTitle = "Downlink MHz";
              if (qso.dlHz) { snprintf(b,sizeof(b),"%.4f",qso.dlHz/1e6); editBuf=b; }
              else editBuf = ""; break;
      case 4: editTarget = 508; editTitle = "Uplink MHz";
              if (qso.ulHz) { snprintf(b,sizeof(b),"%.4f",qso.ulHz/1e6); editBuf=b; }
              else editBuf = ""; break;
      case 5:  editTarget = 500; editTitle = "Callsign";   editBuf = qso.call; break;
      case 7:  editTarget = 501; editTitle = "RST sent";   editBuf = qso.rstS; break;
      case 8:  editTarget = 502; editTitle = "RST rcvd";   editBuf = qso.rstR; break;
      case 9:  editTarget = 503; editTitle = "Their grid"; editBuf = qso.grid; break;
      case 10: editTarget = 504; editTitle = "Notes";      editBuf = qso.notes; break;
    }
    screen = SCR_EDIT;
  }
}

void App::loadLog() {
  logRecN = 0;
  int total = qsoCount();
  logFirstIdx = (total > LOG_VIEW_MAX) ? total - LOG_VIEW_MAX : 0;
  logListSel = 0;
  if (!Store::fs().exists(FILE_LOG)) return;
  File f = Store::fs().open(FILE_LOG, "r");
  if (!f) return;
  int idx = 0;
  while (f.available()) {
    String line = f.readStringUntil('\n');
    while (line.length() && (line[line.length()-1] == '\r' || line[line.length()-1] == '\n'))
      line.remove(line.length() - 1);
    if (!line.length() || line.startsWith("utc,")) continue;
    if (idx >= logFirstIdx && logRecN < LOG_VIEW_MAX)
      if (parseQsoCsv(line, logRecs[logRecN])) logRecN++;
    idx++;
  }
  f.close();
  if (logRecN > 0) logListSel = logRecN - 1;       // default to the newest
}

// Edit (rec, del=false) or delete (del=true) one data row by file index, by
// streaming the CSV through a temp file -- no full-RAM load, no data loss.
bool App::rewriteLog(int fileIdx, const PendingQso* rec, bool del) {
  File in = Store::fs().open(FILE_LOG, "r");
  if (!in) return false;
  const char* tmp = "/CardSat/qso_log.tmp";
  File out = Store::fs().open(tmp, "w");
  if (!out) { in.close(); return false; }
  int idx = 0;
  while (in.available()) {
    String line = in.readStringUntil('\n');
    while (line.length() && (line[line.length()-1] == '\r' || line[line.length()-1] == '\n'))
      line.remove(line.length() - 1);
    if (line.startsWith("utc,")) { out.print(line); out.print("\n"); continue; }
    if (!line.length()) continue;
    if (idx == fileIdx) {
      if (!del && rec) writeQsoCsv(out, *rec);       // replace; delete = skip
    } else {
      out.print(line); out.print("\n");
    }
    idx++;
  }
  in.close(); out.close();
  Store::fs().remove(FILE_LOG);
  Store::fs().rename(tmp, FILE_LOG);
  return true;
}

void App::drawLogList() {
  header("QSO Log");
  canvas.setTextSize(1);
  if (logRecN == 0) {
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("No QSOs logged yet.");
    footer("` back");
    return;
  }
  const int VIS = 8;
  int top = logListSel - VIS/2;
  if (top > logRecN - VIS) top = logRecN - VIS;
  if (top < 0) top = 0;
  for (int r = 0; r < VIS && top + r < logRecN; ++r) {
    int i = top + r, y = 20 + r*12;
    PendingQso& q = logRecs[i];
    bool sel = (i == logListSel);
    if (sel) { canvas.fillRect(0, y-1, 240, 11, CL_BLUE);
               canvas.setTextColor(CL_WHITE, CL_BLUE); }
    else        canvas.setTextColor(CL_CYAN, CL_BLACK);
    char tb[16];
    if (q.utc) { time_t tt=(time_t)q.utc; struct tm* g=gmtime(&tt);
                 strftime(tb, sizeof(tb), "%m-%d %H:%MZ", g); }
    else strcpy(tb, "--");
    canvas.setCursor(4, y);
    canvas.printf("%s %-9.9s %.8s", tb, q.call[0] ? q.call : "(nocall)", q.sat);
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 116); canvas.printf("%d/%d", logListSel + 1, logRecN);
  footer("; / . move  ENT edit  ` back");
}

void App::keyLogList(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_LOG; return; }
  if (logRecN == 0) return;
  if (isUp(c))   logListSel = (logListSel + logRecN - 1) % logRecN;
  if (isDown(c)) logListSel = (logListSel + 1) % logRecN;
  if (enter) {
    qso = logRecs[logListSel];
    logEditIdx = logListSel;
    logReturn = SCR_LOGLIST;
    logSel = 0; logDelArm = false;
    screen = SCR_LOGENTRY;
  }
}

void App::footer(const String& t) {
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setTextSize(1);
  canvas.setCursor(2, 127);
  canvas.print(t);
}

void App::draw() {
  if (canvasFreed) return;   // sprite freed for a TLS fetch; nothing to draw to
  canvas.fillScreen(CL_BLACK);
  switch (screen) {
    case SCR_HOME:     drawHome(); break;
    case SCR_SATLIST:  drawSatList(); break;
    case SCR_SCHEDULE: drawSchedule(); break;
    case SCR_PASSES:   drawPasses(); break;
    case SCR_PASSDETAIL: drawPassDetail(); break;
    case SCR_TRACK:    drawTrack(); break;
    case SCR_BIG:      drawBig(); break;
    case SCR_MANUALBIG: drawManualBig(); break;
    case SCR_MANUAL:   drawManual(); break;
    case SCR_POLAR:    drawPolar(); break;
    case SCR_PASSPOLAR: drawPassPolar(); break;
    case SCR_MUTUAL:   drawMutual(); break;
    case SCR_VIS:      drawVis(); break;
    case SCR_ILLUM:    drawIllum(); break;
    case SCR_LOCATION: drawLocation(); break;
    case SCR_UPDATE:   drawUpdate(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_EDIT:     drawEdit(); break;
    case SCR_WIFISCAN: drawWifiScan(); break;
    case SCR_ABOUT:    drawAbout(); break;
    case SCR_NETREBOOT: drawNetReboot(); break;
    case SCR_MEMOS:    drawMemos(); break;
    case SCR_OSCAR:    drawOscar(); break;
    case SCR_GLOBE:    drawGlobe(); break;
    case SCR_DXDOPP:   drawDxDopp(); break;
    case SCR_SKYMAP:   drawSkyMap(); break;
    case SCR_GPSPOS:   drawGpsPos(); break;
    case SCR_SATSAT:   drawSatSat(); break;
    case SCR_MESSAGES: drawMessages(); break;
    case SCR_LOG:      drawLog(); break;
    case SCR_LOGENTRY: drawLogEntry(); break;
    case SCR_LOGLIST:  drawLogList(); break;
    case SCR_WORLDMAP: drawWorldMap(); break;
    case SCR_ROTMAN:   drawRotMan(); break;
    case SCR_GPS:      drawGps(); break;
    case SCR_HELP:     drawHelp(); break;
    case SCR_CATTEST:  drawCatTest(); break;
    case SCR_ORBIT:    drawOrbit(); break;
    case SCR_SIM:      drawSim(); break;
    case SCR_SPACEWX:  drawSpaceWx(); break;
    case SCR_TXDB:     drawTxDb(); break;
    case SCR_EQX:      drawEqx(); break;
    case SCR_QRZ:      drawQrz(); break;
    case SCR_WEATHER:  drawWeather(); break;
    case SCR_SUNMOON:  drawSunMoon(); break;
    case SCR_GRID:     drawGrid(); break;
    case SCR_STATES:   drawStates(); break;
    case SCR_DXCC:     drawDxcc(); break;
    case SCR_GPSRC:    drawGpSrc(); break;
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
  // Voice-memo REC badge: overlay on the Track-family screens while recording.
  if (screen == SCR_TRACK || screen == SCR_BIG || screen == SCR_MANUAL ||
      screen == SCR_MANUALBIG || screen == SCR_POLAR)
    drawMemoIndicator();
  canvas.pushSprite(0, 0);
}

// ---------------------------------------------------------------------------
//  World map: live sub-satellite point of every favorite on an equirectangular
//  lat/lon grid, with the home QTH marked. Reached with 'm' from Next Passes
//  (all favs). Sunlit sats are yellow, eclipsed cyan. Refreshed on the 500 ms
//  live cadence. The basemap is a 30-degree graticule (equator + prime meridian
//  emphasised), not a coastline -- accurate and compact for the 240px screen.
// ---------------------------------------------------------------------------
static const int16_t COAST[] = {
  // NAmerica
  -165,60, -160,66, -156,71, -140,70, -125,70, -110,68, -95,69, -82,73, -78,68, -64,60,
  -56,51, -66,45, -70,42, -74,40, -76,35, -81,31, -80,25, -84,30, -90,29, -94,29, -97,26,
  -97,22, -105,20, -105,23, -110,24, -110,30, -114,31, -117,33, -122,37, -124,40, -124,46,
  -123,48, -130,54, -135,58, -147,60, -158,57, -165,60,
  999,999,
  // CentAm
  -92,15, -87,13, -83,9, -80,9, -77,8, -82,15, -88,17, -92,15,
  999,999,
  // SAmerica
  -78,8, -72,11, -62,10, -50,0, -44,-2, -35,-6, -38,-13, -48,-25, -58,-35, -62,-40,
  -65,-48, -69,-52, -75,-50, -74,-44, -73,-37, -71,-30, -71,-18, -78,-8, -81,-5, -80,2,
  -78,8,
  999,999,
  // Africa
  -16,15, -17,21, -10,30, 0,36, 10,34, 11,37, 20,32, 25,32, 32,31, 34,28, 43,12, 51,12,
  48,5, 41,-2, 40,-10, 35,-18, 32,-26, 26,-34, 20,-35, 18,-30, 13,-17, 9,-1, 9,4, -5,5,
  -8,4, -13,8, -16,15,
  999,999,
  // Madagascar
  44,-16, 50,-15, 50,-25, 45,-25, 44,-16,
  999,999,
  // Europe
  -10,36, -9,43, -2,43, -2,48, -5,48, 0,49, 2,51, -3,54, -2,58, 5,60, 10,64, 15,68, 24,71,
  28,70, 30,66, 22,60, 28,60, 30,60, 27,56, 20,55, 13,54, 8,54, 4,52, 0,50, -2,48, 3,43,
  6,44, 12,44, 15,40, 18,40, 16,43, 13,45, 20,42, 23,40, 26,40, 28,41, 27,37, 22,37, 15,37,
  8,38, -6,36, -10,36,
  999,999,
  // Asia
  28,41, 36,36, 36,30, 35,28, 43,30, 48,30, 57,25, 56,26, 50,30, 50,40, 53,42, 48,46,
  52,46, 60,45, 70,43, 76,40, 67,38, 62,38, 70,30, 61,25, 67,24, 75,8, 80,8, 80,15, 87,21,
  89,22, 91,16, 98,16, 95,5, 104,1, 104,10, 109,11, 108,18, 112,21, 122,24, 120,30, 122,31,
  118,38, 124,40, 122,40, 130,43, 135,48, 142,54, 135,55, 140,60, 150,59, 160,61, 170,66,
  180,66, 180,70, 160,70, 140,73, 110,74, 100,77, 75,73, 68,73, 55,72, 45,66, 40,66, 33,67,
  40,73, 55,71, 60,70, 73,72, 70,76, 95,78, 105,76, 130,72, 142,72, 160,69, 170,68,
  999,999,
  // India
  70,22, 73,15, 77,8, 80,13, 84,18, 87,21,
  999,999,
  // Arabia
  35,28, 43,30, 48,30, 57,25, 52,17, 43,12, 40,20, 35,28,
  999,999,
  // Australia
  114,-22, 114,-34, 122,-34, 131,-32, 138,-35, 141,-38, 147,-43, 150,-37, 153,-31, 153,-25,
  146,-19, 142,-11, 136,-12, 132,-11, 130,-14, 122,-18, 114,-22,
  999,999,
  // NewGuinea
  131,-1, 141,-3, 150,-7, 143,-9, 134,-9, 131,-1,
  999,999,
  // NZ
  166,-46, 171,-44, 174,-41, 178,-38, 173,-41, 168,-44, 166,-46,
  999,999,
  // Japan
  130,31, 135,34, 140,36, 142,40, 141,45, 138,37, 133,34, 130,31,
  999,999,
  // UK
  -5,50, -3,54, -5,58, -7,57, -6,53, -5,50,
  999,999,
  // Greenland
  -45,60, -50,64, -53,67, -50,70, -45,76, -30,82, -20,76, -22,70, -30,68, -38,65, -43,60,
  -45,60,
  999,999,
  // Iceland
  -24,65, -14,66, -15,64, -22,64, -24,65,
  999,999,
  // Antarctica
  -180,-72, -150,-75, -100,-74, -60,-70, -58,-64, -20,-70, 20,-69, 60,-67, 100,-66,
  140,-66, 170,-72, 180,-71,
  999,999,
};
static const int COAST_N = sizeof(COAST)/sizeof(COAST[0]);

// World-map longitude projection with recenter support. The map is equirectangular;
// shifting the center longitude is just a horizontal wrap. mapShiftLon() returns
// the longitude relative to the current map center, wrapped into (-180, 180], so
// both the x-pixel mapping and the date-line seam test work in the same shifted
// space. With cfg.mapCenterLon == 0 these reduce to the original behavior.
static inline double mapShiftLon(double lon, double centerLon) {
  double d = lon - centerLon;
  while (d < -180.0) d += 360.0;
  while (d >  180.0) d -= 360.0;
  return d;                                  // (-180, 180], 0 == map center
}
// Map a longitude to an x pixel within [MX, MX+MW), given the map center.
static inline int mapLonToX(double lon, double centerLon, int MX, int MW) {
  double d = mapShiftLon(lon, centerLon);    // -180..180 across the map width
  return MX + (int)lround((d + 180.0) / 360.0 * MW);
}

void App::drawWorldMap() {
  header("World Map");
  const int MX = 0, MY = 16, MW = 240, MH = 108;     // map rect (y 16..124)
  const uint16_t GRID = CL_MGREY;                      // dim grey graticule
  const double centerLon = (double)cfg.mapCenterLon; // 0 = classic 0-centered view
  canvas.fillRect(MX, MY, MW, MH, CL_BLACK);

  // Coarse public-domain coastline outline (land), drawn under the graticule.
  // The seam test is done in shifted space: skip a segment whose two endpoints
  // land on opposite halves of the (recentered) map, i.e. it crosses the seam.
  { int px = 0, py = 0; double pds = 0; bool pen = false;
    for (int i = 0; i + 1 < COAST_N; i += 2) {
      int lo = COAST[i], la = COAST[i + 1];
      if (lo == 999) { pen = false; continue; }
      double ds = mapShiftLon(lo, centerLon);
      int x = mapLonToX(lo, centerLon, MX, MW);
      int y = MY + (int)lround((90.0 - la) / 180.0 * MH);
      if (pen && fabs(ds - pds) < 180.0) canvas.drawLine(px, py, x, y, CL_DGREEN);
      px = x; py = y; pds = ds; pen = true;
    }
  }

  // Graticule every 30 deg; emphasise the equator and the prime meridian.
  for (int lon = -180; lon < 180; lon += 30) {
    int x = mapLonToX(lon, centerLon, MX, MW);
    if (x > MX + MW - 1) x = MX + MW - 1; if (x < MX) x = MX;
    canvas.drawLine(x, MY, x, MY + MH - 1, lon == 0 ? CL_DGREEN : GRID);
  }
  for (int lat = -60; lat <= 60; lat += 30) {
    int y = MY + (90 - lat) * MH / 180;
    canvas.drawLine(MX, y, MX + MW - 1, y, lat == 0 ? CL_DGREEN : GRID);
  }
  canvas.drawRect(MX, MY, MW, MH, GRID);

  // Home QTH: white cross + label.
  Observer o = loc.obs();
  if (o.valid) {
    int qx = mapLonToX(o.lon, centerLon, MX, MW);
    int qy = MY + (int)lround((90.0  - o.lat) / 180.0 * MH);
    canvas.drawLine(qx - 3, qy, qx + 3, qy, CL_WHITE);
    canvas.drawLine(qx, qy - 3, qx, qy + 3, CL_WHITE);
    canvas.setTextSize(1); canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(qx + 4, qy - 3); canvas.print("QTH");
  }

  canvas.setTextSize(1);
  if (favN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, MY + 8); canvas.print("No favorites - star sats with 'f'");
    footer("` back");
    return;
  }
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, MY + 8); canvas.print("Clock not set (NTP or GPS)");
    footer("` back");
    return;
  }

  // Plot each favorite's current sub-point.
  time_t now = nowUtc();
  pred.setSite(loc.obs());
  int shown = 0;
  for (int i = 0; i < favN; ++i) {
    int idx = db.indexOfNorad(favs[i]);
    if (idx < 0) continue;
    SatEntry& s = db.at(idx);
    if (!pred.setSat(s)) continue;
    LiveLook L = pred.look(now);
    double lon = L.subLon; while (lon < -180) lon += 360; while (lon > 180) lon -= 360;
    double lat = L.subLat; if (lat > 90) lat = 90; if (lat < -90) lat = -90;
    int x = mapLonToX(lon, centerLon, MX, MW);
    int y = MY + (int)lround((90.0 - lat) / 180.0 * MH);
    bool hi = (mapHi == i);
    uint16_t col = (mapHi >= 0 && !hi) ? CL_MGREY            // dim others when one is highlighted
                 : (L.sunlit ? CL_YELLOW : CL_CYAN);       // sunlit vs eclipse
    // Ground footprint: the small circle on the sphere at central angle beta
    // where the satellite sits on the horizon, cos(beta) = Re/(Re+h). Sampled in
    // azimuth and drawn as an outline; segments crossing the seam are skipped
    // (tested in recentered space so the skip tracks the moved date line).
    if (L.satAltKm > 1.0) {
      const double D2R = 0.0174532925199433, R2D = 57.2957795130823, RE = 6371.0;
      double beta = acos(RE / (RE + L.satAltKm));
      double la1 = lat * D2R, lo1 = lon * D2R, sb = sin(beta), cb = cos(beta);
      double s1 = sin(la1), c1 = cos(la1);
      int pfx = 0, pfy = 0; double pfds = 0; bool have = false;
      for (int a = 0; a <= 360; a += 15) {
        double az = a * D2R;
        double la2 = asin(s1 * cb + c1 * sb * cos(az));
        double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
        double lo2d = lo2 * R2D; while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
        double ds = mapShiftLon(lo2d, centerLon);
        int fx = mapLonToX(lo2d, centerLon, MX, MW);
        int fy = MY + (int)lround((90.0 - la2 * R2D) / 180.0 * MH);
        if (have && fabs(ds - pfds) < 180.0) canvas.drawLine(pfx, pfy, fx, fy, col);
        pfx = fx; pfy = fy; pfds = ds; have = true;
      }
    }
    canvas.fillCircle(x, y, hi ? 3 : 2, col);
    canvas.drawCircle(x, y, hi ? 4 : 3, CL_BLACK);   // halo for contrast over grid
    if (mapHi < 0 || hi) {
      char lbl[8]; int k = 0;
      for (const char* p = s.name; *p && k < 7; ++p) lbl[k++] = *p;
      lbl[k] = 0;
      int lx = x + 5; if (lx + k * 6 > MX + MW) lx = x - 5 - k * 6;
      canvas.setTextColor(hi ? CL_WHITE : col, CL_BLACK);
      canvas.setCursor(lx, y - 3); canvas.print(lbl);
    }
    shown++;
  }
  SatEntry* a = activeSat(); if (a) pred.setSat(*a);   // restore propagator

  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(2, MY + 1); canvas.printf("%d favs", shown);
  footer(cfg.mapCenterLon != 0 ? "` bk  f hi  c=ctr 0   yel/cyn sun/ecl"
                               : "` bk  f hi  c=ctr QTH yel/cyn sun/ecl");
}

void App::keyWorldMap(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = mapReturn; lastDrawMs = 0; return; }
  if (c == 'f' && favN > 0) {                 // cycle highlighted favorite (-1 = none)
    if (++mapHi >= favN) mapHi = -1;
    lastDrawMs = 0;
  }
  if (c == 'c') {                             // toggle: center on QTH <-> classic 0-deg
    Observer o = loc.obs();
    if (cfg.mapCenterLon != 0) cfg.mapCenterLon = 0;        // back to 0-centered
    else if (o.valid)          cfg.mapCenterLon = (int16_t)lround(o.lon);  // center on QTH
    cfg.save();
    lastDrawMs = 0;
  }
}

// ---------------------------------------------------------------------------
//  Manual rotator control: jog the antenna by hand (arrows move the target and
//  send immediately), with the commanded target and the live read-back position
//  shown. Reached from Settings -> "Rotator: manual control"; entering it
//  disengages auto-tracking so manual and tracking don't fight.
// ---------------------------------------------------------------------------
void App::drawRotMan() {
  header("Rotator");
  canvas.setTextSize(1);
  if (!cfg.rotEnable || !rot) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 52); canvas.print("Enable the rotator in Settings.");
    footer("` back");
    return;
  }
  float elMax = cfg.rotFlip ? 180.0f : 90.0f;

  // Commanded target.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 22); canvas.print("TARGET  az / el");
  canvas.setTextSize(3); canvas.setTextColor(CL_GREEN, CL_BLACK);
  canvas.setCursor(6, 34);   canvas.printf("%03.0f", manAz);
  canvas.setCursor(126, 34); canvas.printf("%02.0f", manEl);

  // Live read-back position.
  float aaz, ael; bool ok = rot->readPos(aaz, ael);
  canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 72); canvas.print("ACTUAL  az / el");
  canvas.setTextSize(2); canvas.setTextColor(ok ? CL_CYAN : CL_GREY, CL_BLACK);
  canvas.setCursor(6, 84);   if (ok) canvas.printf("%05.1f", aaz); else canvas.print(" ---");
  canvas.setCursor(126, 84); if (ok) canvas.printf("%04.1f", ael); else canvas.print("---");

  int32_t rcA, rcE; bool isYaesu = rot->rawPos(rcA, rcE);
  canvas.setTextSize(1); canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 110);
  if (isYaesu) canvas.printf("ADC az %ld  el %ld  rot:%s",
                             (long)rcA, (long)rcE, rot->ready() ? "ok" : "--");
  else         canvas.printf("step %d deg   el max %.0f  rot:%s",
                             manStep, elMax, rot->ready() ? "ok" : "--");
  footer(isYaesu ? "1az0 2azF 3el0 4elF  x stop ` bk"
                 : ",/ az  ;. el  s step  x stop  ` bk");
}

void App::keyRotMan(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_SETTINGS; lastDrawMs = 0; return; }
  if (!cfg.rotEnable || !rot) return;
  // Yaesu direct: capture the current ADC reading as an axis endpoint, then
  // rebuild the backend so the new calibration takes effect immediately.
  { int32_t ra, re; bool cap = true;
    if (rot->rawPos(ra, re)) {
      if      (c == '1') cfg.rotAzCnt0 = (int16_t)ra;
      else if (c == '2') cfg.rotAzCntF = (int16_t)ra;
      else if (c == '3') cfg.rotElCnt0 = (int16_t)re;
      else if (c == '4') cfg.rotElCntF = (int16_t)re;
      else cap = false;
      if (cap) { cfg.save(); applyRotatorFromCfg();
                 setStatus("Calibration captured"); lastDrawMs = 0; return; }
    }
  }
  float elMax = cfg.rotFlip ? 180.0f : 90.0f;
  bool moved = false;
  if (isLeft(c))  { manAz -= manStep; moved = true; }
  if (isRight(c)) { manAz += manStep; moved = true; }
  if (isUp(c))    { manEl += manStep; moved = true; }
  if (isDown(c))  { manEl -= manStep; moved = true; }
  while (manAz >= 360.0f) manAz -= 360.0f;
  while (manAz < 0.0f)    manAz += 360.0f;
  if (manEl < 0)     manEl = 0;
  if (manEl > elMax) manEl = elMax;
  if (c == 's') { manStep = (manStep == 1) ? 5 : (manStep == 5 ? 10 : 1); lastDrawMs = 0; return; }
  if (c == 'x' || c == 'k') { rot->stop(); setStatus("Rotator stopped"); lastDrawMs = 0; return; }
  if (moved || enter) { rotPoint(manAz, manEl); lastDrawMs = 0; }
}

// ---------------------------------------------------------------------------
//  GPS data + GNSS sky plot. Left: fix state, used/in-view counts, position.
//  Right: a polar sky plot (zenith at centre, horizon at the rim, North up) of
//  every satellite reported in view, coloured by signal strength (C/No).
// ---------------------------------------------------------------------------
void App::drawGps() {
  header("GPS");
  bool fix = loc.gpsHasFix();
  const Observer& o = loc.obs();
  canvas.setTextSize(1);
  int y = 22;
  canvas.setTextColor(fix ? CL_GREEN : CL_RED, CL_BLACK);
  canvas.setCursor(4, y); canvas.printf("Fix: %s", fix ? "yes" : "no"); y += 12;
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(4, y); canvas.printf("Used:%d", loc.gpsSats());      y += 12;
  canvas.setCursor(4, y); canvas.printf("View:%d", loc.gpsViewCount()); y += 13;
  if (o.valid) {
    canvas.setCursor(4, y); canvas.printf("%.4f", o.lat);             y += 11;
    canvas.setCursor(4, y); canvas.printf("%.4f", o.lon);             y += 11;
    canvas.setCursor(4, y); canvas.print(Location::toGrid(o.lat, o.lon)); y += 11;
    canvas.setCursor(4, y); canvas.printf("%.0fm", o.altM);
  } else {
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(4, y); canvas.print("no position");
  }
  if (!cfg.useGps) {
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(4, 116); canvas.print("GPS off");
  }

  // Sky plot.
  const int cx = 174, cy = 75, R = 50;
  canvas.drawCircle(cx, cy, R, CL_GREY);          // horizon (el 0)
  canvas.drawCircle(cx, cy, R * 2 / 3, CL_MGREY);   // el 30
  canvas.drawCircle(cx, cy, R / 3, CL_MGREY);       // el 60
  canvas.drawFastVLine(cx, cy - R, 2 * R, CL_MGREY);
  canvas.drawFastHLine(cx - R, cy, 2 * R, CL_MGREY);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(cx - 2, cy - R - 9); canvas.print("N");
  const double D2R = 0.0174532925199433;
  int n = loc.gpsViewCount();
  for (int i = 0; i < n; i++) {
    const GpsSat& s = loc.gpsView(i);
    if (s.el < 0 || s.az < 0) continue;
    double rr = R * (90.0 - s.el) / 90.0;
    int x  = cx + (int)lround(rr * sin(s.az * D2R));
    int yy = cy - (int)lround(rr * cos(s.az * D2R));
    uint16_t col = (s.snr >= 40) ? CL_GREEN : (s.snr >= 25) ? CL_YELLOW
                 : (s.snr >  0)  ? CL_ORANGE : CL_GREY;
    canvas.fillCircle(x, yy, 2, col);
  }
  footer("` back   green=strong grey=weak");
}

void App::keyGps(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_LOCATION; lastDrawMs = 0; return; }
}

// ---------------------------------------------------------------------------
//  Help: scrollable usage + key reference, reachable with 'h' from any screen
//  (except while typing). Section headers start in column 0; key lines are
//  indented. Scroll with ; / . and return with ` to wherever you came from.
// ---------------------------------------------------------------------------
void App::drawHelp() {
  header("Help / Keys");
  static const char* H[] = {
    "GLOBAL",
    " `  back / home",
    " h  open this help",
    " b  screenshot to SD",
    " ;/.  up/down",
    " ,//  left/right",
    "HOME",
    " ENT  open selected item",
    " (menu scrolls)",
    "SUN / MOON",
    " ;/. pick Sun or Moon",
    " o rotor track  x stop",
    " s sky sources (planets,",
    "   radio sources)",
    " parks while body is set",
    " takes rotor from sat trk",
    "SATELLITES",
    " ENT  toggle favorite",
    " o  orbital analysis",
    " s  simulation (time)",
    " e  EQX table",
    " k  OSCARLOCATOR view",
    " 2  sat-to-sat windows",
    " arrows scroll the list",
    " AMSAT: dot=heard",
    "  sq=telemetry  ring=no",
    "ORBIT ANALYSIS",
    " ,// flip pages (9)",
    " info/live/pass/trk/dop/",
    "  nodal/sun-beta/outlook/",
    "  orbit-pos",
    " info: footprint=max QSO",
    " dop: f beacon freq",
    " nodal: J2 drift, LTAN,",
    "  sun-sync, repeat, max",
    " r recompute",
    "SIMULATION",
    " ,// step time +/-",
    " ;/. step size  x now",
    "NEXT PASSES (favs)",
    " ENT track  m world map",
    " r refresh  z deep-sleep",
    "PASSES",
    " ENT/t track  d detail",
    " g workable grids",
    " w workable US states",
    " e workable DXCC",
    " v 10-day  i illum  x DX",
    "MUTUAL WINDOWS (x)",
    " co-visibility with a DX",
    " ;/. scroll  d Doppler",
    "DX DOPPLER TABLE (Mutual d)",
    " both stns RX/TX per 30s",
    " m mode  a anchor dial",
    " ,/ linear passband pt",
    "TRACK (selected sat)",
    " r  engage radio (Doppler)",
    " arrows tune / adjust",
    " m TUNE/CAL  d tune mode",
    " t next TX  n beacon",
    " c CTCSS tone",
    " o rotator  p polar",
    " z big readout  l log QSO",
    " v voice memo (SD)",
    " y tilt tuning (ADV)",
    " g grids now  p polar",
    " w US states now",
    " e DXCC now",
    "BIG READOUT (z)",
    " large RX/TX + az/el;",
    " radio+rotator keep going",
    " ,// tune  t TX  z/` back",
    "MANUAL MODE (f, no radio)",
    " read Doppler, tune by hand",
    " u swap HOLD/TUNE leg",
    " ,// passband  m CAL",
    " z big view  l log",
    "TRANSPONDER DB (t)",
    " sat's TX/beacon entries",
    " ;/. select (* = manual)",
    " x delete manual (2-press)",
    "EQX TABLE (e)",
    " equator-crossing UTC+lon",
    " (OSCARLOCATOR reference)",
    " d asc/desc  r recompute",
    "OSCARLOCATOR (k)",
    " live azimuthal-equidist.",
    " plot: sub-point + foot-",
    " print + QTH range ring",
    " + ground track (AOS/LOS)",
    " m  toggle QTH / polar",
    "3D GLOBE (3)",
    " wireframe Earth, follows",
    "  selected sat; all favs",
    " + footprint + trail",
    " + day/night terminator",
    " g DX 2nd loc (grid)",
    " arrows turn  ENT follow",
    "VOICE MEMOS (Log)",
    " browse memos newest-first",
    " ENT play  n new standalone",
    " d delete  r refresh",
    "WORKABLE GRIDS",
    " grids under footprint",
    " ;/. {} scroll",
    "LOCATION",
    " e/o/a edit lat/lon/alt",
    " g grid  p GPS  s source",
    " c set clock  v position",
    " ENT  GPS data + sky plot",
    "GPS POSITION (Location v)",
    " DMS lat/lon max precision",
    " speed, course, grid live",
    "GPS SKY PLOT",
    " GNSS sats by az/el",
    " green=strong  grey=weak",
    "WORLD MAP",
    " all footprints + sun",
    " f cycle highlighted fav",
    "SPACE WX (menu)",
    " solar flux + Kp + A idx",
    " HF/sat outlook  r refresh",
    "WEATHER (menu)",
    " current + forecast (site)",
    " r refresh  cached offline",
    "QRZ LOOKUP (menu)",
    " callsign -> name/grid/QTH",
    " needs QRZ XML sub + WiFi",
    "SETTINGS",
    " ;/. move  ,// change",
    " ENT select / edit field",
    " row: Run CAT self-test",
    " row: Rotator manual ctrl",
    " row: Rigctld server +port",
    " row: GP source picker",
    "GP / ELEMENTS SOURCE",
    " ;/. pick  {} page",
    " AMSAT / CelesTrak / URL",
    " ENT select",
    "CAT SELF-TEST (Settings)",
    " Radio/CAT > Run CAT self-",
    "  test: checks freq set+",
    "  read, mode, MAIN/SUB,",
    "  sat mode, PTT, CTCSS",
    " PASS/FAIL on screen +",
    "  serial (115200)",
    " safe: never keys TX,",
    "  restores the dial",
    " ;/. scroll  ` back",
    "ROTATOR MANUAL",
    " ,// az   ;/. el",
    " s step   x stop",
    " Yaesu cal: 1 az0 2 azF",
    "  3 el0 4 el180 (at jack)",
    "RIGCTLD SERVER",
    " PC drives the rig via",
    " CardSat over TCP (Hamlib)",
    " VFOA=downlink VFOB=uplink",
    "ROTCTLD SERVER",
    " PC drives the wired",
    " GS-232 rotator via CardSat",
    "NETWORK RADIO (rigctl)",
    " CAT type=rigctl drives a",
    " remote rig over rigctld",
    "UPDATE",
    " k GP+clock  a cache TX",
    " w WiFi connect only",
    "LOG",
    " ENT new / browse / ADIF",
    "MESSAGES (LoRa)",
    " CardSat-to-CardSat chat",
    " n write/send  r retry",
    " ;/. scroll  (needs Cap",
    " LoRa + RadioLib build)",
    "ABOUT",
    " build, IP, free heap,",
    " diagnostics",
  };
  const int total = (int)(sizeof(H) / sizeof(H[0]));
  const int rows = 9;
  if (helpScroll > total - rows) helpScroll = total - rows;
  if (helpScroll < 0) helpScroll = 0;
  canvas.setTextSize(1);
  for (int i = 0; i < rows && (helpScroll + i) < total; i++) {
    const char* s = H[helpScroll + i];
    bool hdr = (s[0] != ' ');
    canvas.setTextColor(hdr ? CL_CYAN : CL_WHITE, CL_BLACK);
    canvas.setCursor(4, 20 + i * 12); canvas.print(s);
  }
  footer("; / . scroll   ` back");
}

void App::keyHelp(char c, bool enter, bool back) {
  (void)enter;
  if (isUp(c))   { helpScroll--; lastDrawMs = 0; }
  if (isDown(c)) { helpScroll++; lastDrawMs = 0; }
  if (isBack(c, back)) { screen = helpReturn; lastDrawMs = 0; return; }
}

// ===========================================================================
//  CAT self-test (Settings -> Radio / CAT -> "Run CAT self-test")
//
//  Exercises every CAT function the active backend exposes and reports a
//  PASS/FAIL/INFO line for each, both on the device screen (SCR_CATTEST) and on
//  the serial monitor at 115200. It is deliberately NON-DESTRUCTIVE:
//    * It never keys the transmitter (PTT is only READ, never set).
//    * It saves the dial state, drives test values, then restores the dial and
//      re-syncs the Doppler send-guards so normal tracking resumes cleanly.
//  Frequency-set "PASS" means the rig accepted the command; where read-back is
//  supported it additionally confirms the value came back within tolerance.
// ===========================================================================

void App::catLog(const String& line) {
  Serial.print("[CAT-TEST] "); Serial.println(line);     // echo to serial monitor
  if (catCount < CATTEST_MAX) catLines[catCount++] = line;
}

void App::catStep(const String& name, bool ok, const String& detail) {
  if (ok) catPass++; else catFail++;
  String l = (ok ? "[PASS] " : "[FAIL] ") + name;
  if (detail.length()) l += ": " + detail;
  catLog(l);
}

void App::runCatTest() {
  catCount = 0; catScroll = 0; catPass = 0; catFail = 0;

  if (!rig || !rig->ready()) {
    catLog("Radio not ready.");
    catLog("Turn CAT on (Track 'r')");
    catLog("or check CAT type/wiring.");
    screen = SCR_CATTEST; lastDrawMs = 0;
    return;
  }

  catLog(String("Rig: ") + rig->name());
  catLog(String("Addr 0x") + String(rig->address(), HEX) +
         "  read=" + (rig->canReadFreq() ? "Y" : "N") +
         " sat=" + (rig->hasSatMode() ? "Y" : "N") +
         " tone=" + (rig->hasTone() ? "Y" : "N"));

  // Save current dial so we can restore it afterwards.
  uint32_t savedRx = lastRxSet, savedUl = lastUlHz;

  // Pick safe in-band test frequencies (2 m downlink, 70 cm uplink). These are
  // RX/standby tunes; we never transmit.
  const uint32_t TEST_DL = 145900000UL;   // 145.900 MHz
  const uint32_t TEST_UL = 435100000UL;   // 435.100 MHz
  const uint32_t TOL     = 50;            // Hz tolerance for read-back compare

  // --- Downlink frequency set (+ read-back verify if supported) ----------
  {
    bool ok = rig->setSubFreq(TEST_DL);
    if (ok && rig->canReadFreq()) {
      uint32_t back = 0;
      if (rig->readSubFreq(back)) {
        uint32_t d = back > TEST_DL ? back - TEST_DL : TEST_DL - back;
        catStep("Downlink set+read", d <= TOL,
                String(back) + " Hz (set " + String(TEST_DL) + ")");
      } else {
        catStep("Downlink set", ok, "set OK, read no-reply");
      }
    } else {
      catStep("Downlink set", ok);
    }
  }

  // --- Uplink frequency set (+ read-back verify if supported) ------------
  {
    bool ok = rig->setMainFreq(TEST_UL);
    if (ok && rig->canReadFreq()) {
      uint32_t back = 0;
      if (rig->readMainFreq(back)) {
        uint32_t d = back > TEST_UL ? back - TEST_UL : TEST_UL - back;
        catStep("Uplink set+read", d <= TOL,
                String(back) + " Hz (set " + String(TEST_UL) + ")");
      } else {
        catStep("Uplink set", ok, "set OK, read no-reply");
      }
    } else {
      catStep("Uplink set", ok);
    }
  }

  // --- Mode set on both legs --------------------------------------------
  catStep("Downlink mode USB", rig->setSubMode(RM_USB));
  catStep("Uplink mode FM",    rig->setMainMode(RM_FM));

  // --- MAIN/SUB band select (Icom: real frames; others: no-op -> INFO) ---
  if (rig->canReadFreq()) {
    rig->selectMainBand();
    rig->selectSubBand();
    catLog("[INFO] Band select sent (MAIN+SUB)");
  }

  // --- Satellite mode toggle (only if the rig has a CAT command) ---------
  if (rig->hasSatMode()) {
    bool on  = rig->enableSatMode(true);
    bool off = rig->enableSatMode(false);   // leave it OFF (CardSat drives bands)
    catStep("Sat mode on/off", on && off);
  } else {
    catLog("[INFO] Sat mode: no CAT cmd");
  }

  // --- PTT / transmit-state read (READ ONLY -- never keys TX) ------------
  {
    bool tx = false;
    if (rig->readPtt(tx)) catStep("PTT read", true, tx ? "TX" : "RX");
    else                  catLog("[INFO] PTT read: not supported");
  }

  // --- CTCSS encoder (only if supported; set a tone then turn it off) -----
  if (rig->hasTone()) {
    bool on  = rig->setCtcss(true, 67.0f);
    bool off = rig->setCtcss(false, 0);     // leave the encoder OFF
    catStep("CTCSS 67.0 on/off", on && off);
  } else {
    catLog("[INFO] CTCSS: not supported");
  }

  // --- Restore the dial and re-sync the Doppler send-guards --------------
  if (savedRx) rig->setSubFreq(savedRx);
  if (savedUl) rig->setMainFreq(savedUl);
  rig->selectSubBand();          // leave access on the downlink for the operator
  lastRxSet = savedRx; lastUlHz = savedUl; uplinkDeferTicks = 0;

  catLog(String("Done: ") + String(catPass) + " pass, " +
         String(catFail) + " fail");

  screen = SCR_CATTEST; lastDrawMs = 0;
}

void App::drawCatTest() {
  header("CAT self-test");
  canvas.setTextSize(1);
  const int rows = 9;
  if (catScroll > catCount - rows) catScroll = catCount - rows;
  if (catScroll < 0) catScroll = 0;
  for (int i = 0; i < rows && (catScroll + i) < catCount; i++) {
    const String& s = catLines[catScroll + i];
    uint16_t col = CL_WHITE;
    if      (s.startsWith("[PASS]")) col = CL_GREEN;
    else if (s.startsWith("[FAIL]")) col = CL_RED;
    else if (s.startsWith("[INFO]")) col = CL_CYAN;
    canvas.setTextColor(col, CL_BLACK);
    canvas.setCursor(4, 20 + i * 12);
    canvas.print(s.substring(0, 38));
  }
  footer("; / . scroll   ` back");
}

void App::keyCatTest(char c, bool enter, bool back) {
  (void)enter;
  if (isUp(c))   { catScroll--; lastDrawMs = 0; }
  if (isDown(c)) { catScroll++; lastDrawMs = 0; }
  if (isBack(c, back)) { setSel = 0; setCat = -1; screen = SCR_SETTINGS; lastDrawMs = 0; return; }
}

// ===========================================================================
//  Orbital analysis screen (off Satellites). Multi-page: Info / Live geometry /
//  Next pass / forward Ground track / Doppler curve. Heavy per-orbit analysis
//  (ascending node, next pass, eclipse, optical visibility) is computed once in
//  buildOrbit(); the live pages (0-2) re-render with a single fresh look().
// ===========================================================================
// --- rudimentary orbital-decay estimate (exponential-atmosphere drag) -------
static double expAtmosphere(double hkm) {
  static const double B[][3] = {
    {100,5.297e-7,5.877},{110,9.661e-8,7.263},{120,2.438e-8,9.473},
    {130,8.484e-9,12.636},{140,3.845e-9,16.149},{150,2.070e-9,22.523},
    {180,5.464e-10,29.740},{200,2.789e-10,37.105},{250,7.248e-11,45.546},
    {300,2.418e-11,53.628},{350,9.518e-12,53.298},{400,3.725e-12,58.515},
    {450,1.585e-12,60.828},{500,6.967e-13,63.822},{600,1.454e-13,71.835},
    {700,3.614e-14,88.667},{800,1.170e-14,124.64},{900,5.245e-15,181.05},
    {1000,3.019e-15,268.00}
  };
  const int N = (int)(sizeof(B) / sizeof(B[0]));
  if (hkm >= 1100) return 0.0;            // above the table -> treat as drag-free
  if (hkm < 100)  hkm = 100;
  int i = 0; for (; i < N - 1; ++i) if (hkm < B[i + 1][0]) break;
  return B[i][1] * exp(-(hkm - B[i][0]) / B[i][2]);
}

// Density scale factors for the assumed solar-activity level. Thermospheric
// density at LEO altitudes varies ~an order of magnitude over the solar cycle;
// these bracket the static table (tuned to roughly cycle-mean conditions).
static double solarDensityScale(uint8_t act) {
  switch (act) {
    case SOLAR_LOW:  return 0.35;   // solar minimum
    case SOLAR_HIGH: return 3.0;    // solar maximum
    default:         return 1.0;    // cycle mean
  }
}

// Days to reentry from B* + exponential atmosphere, using a King-Hele style
// decay that lets the orbit circularize: drag is strongest at perigee, so the
// apogee comes down faster than the perigee while the perigee altitude is what
// governs the drag. We track perigee/apogee radii (rp, ra) separately and apply
// the per-orbit energy loss preferentially to apogee until the orbit is nearly
// circular, then bring both down together. Cd*A/m = 12.741621 * B* (m^2/kg).
// densScale brackets solar activity. Order-of-magnitude only: no attitude, lift,
// or short-term space weather. Returns -1 = rising/no data, 1e9 = effectively
// stable, otherwise estimated days to a ~120 km perigee.
static double estimateDecayDays(const SatEntry& s, double densScale) {
  if (s.bstar <= 0 || s.meanMotion <= 0 || densScale <= 0) return -1;
  const double MU = 3.986004418e14, RE = 6.378137e6, TP = 6.283185307179686;
  // Cd*A/m from B*: the textbook SGP4 conversion (12.741621*B*) leaves LEO
  // lifetimes ~3x too long against observed reentries (an un-reboosted ISS at
  // ~420 km comes down in ~1-2 yr, not ~10). Calibrating the per-rev decay
  // against ISS-class, ~400 km and ~550 km objects at mean solar activity puts
  // the multiplier near 38 (about 3x the textbook value -- consistent with a
  // reference-density normalization slip). Still order-of-magnitude: B* itself
  // is frequently a fitted fudge term, so treat the result as a coarse cue.
  double CdAm = 38.0 * s.bstar;                            // m^2/kg (calibrated)
  double nn = s.meanMotion * TP / 86400.0;                 // rad/s
  double a  = pow(MU / (nn * nn), 1.0 / 3.0);              // m
  double e  = s.ecc; if (e < 0) e = 0; if (e > 0.95) e = 0.95;
  double rp = a * (1.0 - e);                               // perigee radius (m)
  double ra = a * (1.0 + e);                               // apogee radius (m)
  double tDays = 0;
  for (int it = 0; it < 200000; ++it) {
    double hp = rp - RE;                                    // perigee altitude (m)
    if (hp < 120e3) return tDays;                           // reentry
    double rho = expAtmosphere(hp / 1000.0) * densScale;    // density at perigee
    if (rho <= 0) return 1e9;                               // above atmosphere table
    a = 0.5 * (rp + ra);
    double T = TP * sqrt(a * a * a / MU);                   // orbital period (s)
    // Energy-equivalent SMA decay rate, evaluated with perigee density (drag
    // acts mainly near perigee). dadt < 0.
    double dadt = -2.0 * TP * CdAm * rho * a * a / T;       // m/s
    if (dadt >= 0) return 1e9;
    // Adaptive step: shrink near the end so the fast final plunge isn't overshot.
    double margin = hp - 120e3;
    double dt = -(margin * 0.25 + 1000.0) / dadt;           // ~quarter of remaining drop
    double capDays = (hp < 200e3) ? 0.25 : (hp < 350e3 ? 5.0 : 30.0);
    if (dt > capDays * 86400.0) dt = capDays * 86400.0;
    if (dt < 1.0) dt = 1.0;
    double da = dadt * dt;                                  // total SMA decrement (<0)
    // King-Hele: while eccentric, drag at perigee removes energy mainly from
    // apogee. Pull apogee down at ~ (ra/rp) the rate of perigee so the orbit
    // circularizes; once nearly circular, both fall together.
    double ecur = (ra - rp) / (ra + rp);
    if (ecur > 1e-3) {
      double ratio = ra / rp;                               // > 1
      double dra = 2.0 * da * ratio / (1.0 + ratio);        // apogee share
      double drp = 2.0 * da / (1.0 + ratio);                // perigee share (smaller)
      ra += dra; rp += drp;
      if (ra < rp) ra = rp;                                 // clamp at circular
    } else {
      ra += da; rp += da;                                   // circular: uniform
    }
    tDays += dt / 86400.0;
    if (tDays > 36500.0) return 1e9;                        // > 100 yr
  }
  return tDays;
}

static String fmtDecay(double days) {
  if (days < 0)       return "n/a";
  if (days >= 36500)  return "stable";
  if (days < 1)       return "<1 d";
  if (days < 100)     return "~" + String((long)lround(days)) + "d";
  if (days < 730)     return "~" + String((long)lround(days / 30.0)) + "mo";
  return "~" + String(days / 365.0, 1) + "yr";
}

// Terse form (no "~") for the range bracket display.
static String fmtDecayShort(double days) {
  if (days < 0)       return "n/a";
  if (days >= 36500)  return "stable";
  if (days < 1)       return "<1d";
  if (days < 100)     return String((long)lround(days)) + "d";
  if (days < 730)     return String((long)lround(days / 30.0)) + "mo";
  return String(days / 365.0, 1) + "yr";
}

static const char* solarActLabel(uint8_t a) {
  return a == SOLAR_LOW ? "min" : (a == SOLAR_HIGH ? "max"
       : a == SOLAR_AUTO ? "auto" : "mean");
}

void App::buildOrbit(bool quiet) {
  orbHasPass = false; orbEcl = false; orbVisible = false; orbSunPct = 0;
  orbAscT = 0; orbAscLon = 0; orbEclT0 = 0; orbEclT1 = 0;
  orbDecayDays = -1; orbDecayLo = -1; orbDecayHi = -1;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet() || s->meanMotion <= 0) return;
  if (!quiet) { setStatus("Analyzing orbit..."); draw(); }
  pred.setSite(loc.obs()); pred.setSat(*s);
  time_t now = nowUtc();
  double periodSec = 86400.0 / s->meanMotion;

  // Next ascending node: step up to one period, find a subLat - -> + crossing,
  // then bisect to refine the time and read its sub-longitude.
  double prevLat = pred.look(now).subLat; time_t prevT = now;
  for (long dt = 30; dt <= (long)periodSec + 60; dt += 30) {
    LiveLook L = pred.look(now + dt);
    if (prevLat < 0 && L.subLat >= 0) {
      time_t a = prevT, b = now + dt;
      for (int k = 0; k < 14; ++k) {
        time_t m = a + (b - a) / 2;
        if (pred.look(m).subLat < 0) a = m; else b = m;
      }
      orbAscT = b; orbAscLon = pred.look(b).subLon; break;
    }
    prevLat = L.subLat; prevT = now + dt;
  }

  // Next pass + eclipse timing + optical-visibility flag.
  PassPredict pp[1];
  int np = pred.predictPasses(now, cfg.minPassEl, pp, 1);
  if (np > 0) {
    orbPass = pp[0]; orbHasPass = true;
    int total = 0, sun = 0; bool prevSun = true;
    for (time_t t = orbPass.aos; t <= orbPass.los; t += 15) {
      LiveLook L = pred.look(t);
      total++; if (L.sunlit) sun++; else orbEcl = true;
      if (t > orbPass.aos) {
        if (prevSun && !L.sunlit && orbEclT0 == 0) orbEclT0 = t;                 // entered shadow
        if (!prevSun && L.sunlit && orbEclT1 == 0 && orbEclT0 != 0) orbEclT1 = t; // left shadow
      }
      prevSun = L.sunlit;
      if (L.sunlit && L.el > 0 && L.sunEl < -6.0) orbVisible = true;             // sunlit sat, dark sky
    }
    orbSunPct = total ? (100.0f * sun / total) : 0;
  }
  orbDecayDays = estimateDecayDays(*s, decayDensityScale());
  orbDecayLo   = estimateDecayDays(*s, solarDensityScale(SOLAR_LOW));   // longest life
  orbDecayHi   = estimateDecayDays(*s, solarDensityScale(SOLAR_HIGH));  // shortest life

  // Pass outlook over the next ORB_OUTLOOK_DAYS days: count passes, find the best
  // (highest) one, the longest, how many clear 30 deg, and the mean spacing.
  // Use ONE predictPasses call over the whole window (the SGP4 nextpass cursor
  // advances correctly through consecutive passes) -- the same pattern buildVis
  // uses. Repeated single-pass re-inits mis-behave for high orbits like AO-7.
  orbOutlookN = 0; orbOutlookHi = 0; orbBestEl = 0; orbBestT = 0; orbBestDur = 0;
  orbLongestMin = 0; orbAvgGapH = 0;
  {
    time_t winEnd = now + (time_t)ORB_OUTLOOK_DAYS * 86400;
    // Reuse the 10-day overview buffer (only live while that screen is open; it
    // is rebuilt on entry there, so borrowing it here is safe).
    int n = pred.predictPasses(now, cfg.minPassEl, visPasses, VIS_PASS_MAX, winEnd);
    orbOutlookN = n;
    for (int i = 0; i < n; ++i) {
      const PassPredict& p = visPasses[i];
      int dur = (int)(p.los - p.aos);
      if (p.maxEl > orbBestEl) { orbBestEl = p.maxEl; orbBestT = p.aos; orbBestDur = dur; }
      if (dur / 60.0f > orbLongestMin) orbLongestMin = dur / 60.0f;
      if (p.maxEl >= 30.0f) orbOutlookHi++;
    }
    if (n > 1)
      orbAvgGapH = (float)(visPasses[n - 1].aos - visPasses[0].aos) / (n - 1) / 3600.0f;
  }
  if (!quiet) setStatus("");
}

void App::drawOrbit() {
  SatEntry* s = activeSat();
  static const char* PG[] = { "Info", "Live", "Pass", "Track", "Doppler", "Nodal",
                              "Sun/B", "Outlook", "OrbPos" };
  { String h = (s ? String(s->name) : String("Orbit")) + " " + PG[orbitPage] +
               " " + String(orbitPage + 1) + "/9";
    header(h); }
  canvas.setTextSize(1);
  if (!s) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
            canvas.print("No satellite."); footer("` back"); return; }

  const double MU = 398600.4418, RE = 6378.137, TWO_PI_ = 6.283185307179586;
  const double C_KM = 299792.458;
  double mm = s->meanMotion;                         // rev/day
  double periodMin = (mm > 0) ? 1440.0 / mm : 0;
  double n = mm * TWO_PI_ / 86400.0;                 // rad/s
  double a = (n > 0) ? pow(MU / (n * n), 1.0 / 3.0) : 0;   // semi-major axis, km
  double apo = a * (1 + s->ecc) - RE, peri = a * (1 - s->ecc) - RE;
  time_t now = timeIsSet() ? nowUtc() : 0;

  int y = 18; const int LH = 9;
  auto row = [&](const char* k, const String& v) {
    canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, y);  canvas.print(k);
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(98, y); canvas.print(v);
    y += LH;
  };

  if (orbitPage == 0) {                              // ---------- Satellite info ----------
    double altNow = 0;
    if (now) { pred.setSat(*s); altNow = pred.look(now).satAltKm; }
    // Footprint circle diameter (km) = 2*Re*acos(Re/(Re+h)). This is the widest
    // surface span both able to see the bird at once -> longest possible QSO.
    auto fpDia = [&](double h) -> double {
      return (h > 0) ? 2.0 * RE * acos(RE / (RE + h)) : 0.0;
    };
    double ageD = now ? (now - s->epochUnix) / 86400.0 : 0;
    long revNow = (long)s->revAtEpoch +
                  (now ? (long)floor((now - s->epochUnix) / (periodMin * 60.0)) : 0);
    row("NORAD",        String(s->norad));
    row("Altitude",     String(altNow, 0) + " km");
    row("Footprint",    String(fpDia(altNow), 0) + " km dia");
    row("Period",       String(periodMin, 1) + " min");
    row("Apo/Peri",     String(apo, 0) + "/" + String(peri, 0) + " km");
    row("Fp apo/peri",  String(fpDia(apo), 0) + "/" + String(fpDia(peri), 0) + " km");
    row("Incl/Ecc",     String(s->incl, 2) + " / " + String(s->ecc, 5));
    row("SMA (a)",      String(a, 0) + " km");
    row("B* / decay",   String(s->bstar, 6) + " " + fmtDecay(orbDecayDays));
    if (orbDecayDays >= 0 && orbDecayDays < 36500)        // show the solar bracket
      row("Decay rng",  fmtDecayShort(orbDecayHi) + "-" + fmtDecayShort(orbDecayLo) +
                        " (" + solarActLabel(cfg.solarAct) + ")");
    row("Age/Rev",      String(ageD, 2) + "d / " + String(revNow));
    if (orbAscT) {
      struct tm tmv; gmtime_r(&orbAscT, &tmv);
      char b[28]; snprintf(b, sizeof(b), "%.1f%c %02d:%02dZ", fabs(orbAscLon),
                           orbAscLon < 0 ? 'W' : 'E', tmv.tm_hour, tmv.tm_min);
      row("Asc node", String(b));
    } else row("Asc node", "--");
    footer("` bk  ,// page  r refresh");
    return;
  }

  if (orbitPage == 1) {                              // ---------- Live geometry ----------
    if (!now) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
                canvas.print("Clock not set."); footer("` bk  ,// page"); return; }
    pred.setSat(*s); LiveLook L = pred.look(now);
    row("Az / El",    String(L.az, 1) + " / " + String(L.el, 1));
    row("Range",      String(L.rangeKm, 0) + " km");
    row("Range rate", String(L.rangeRate, 3) + " km/s");
    row("Dop 145.8",  String((long)lround(-L.rangeRate / C_KM * 145800000.0)) + " Hz");
    row("Dop 435.0",  String((long)lround(-L.rangeRate / C_KM * 435000000.0)) + " Hz");
    row("Sub pt",     String(L.subLat, 2) + "," + String(L.subLon, 2));
    double maNow = fmod(s->ma + 360.0 * mm * (now - s->epochUnix) / 86400.0, 360.0);
    if (maNow < 0) maNow += 360.0;
    int phase = ((int)lround(maNow * 256.0 / 360.0)) & 255;
    row("MA / phase", String(maNow, 0) + " / " + String(phase));
    canvas.setTextColor(L.sunlit ? CL_YELLOW : CL_CYAN, CL_BLACK);
    canvas.setCursor(2, y); canvas.print(L.sunlit ? "SUNLIT" : "ECLIPSE");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.printf("  sun el %.0f  ecl dep %+.1f", L.sunEl, pred.eclipseDepthDeg(now));
    footer("` bk  ,// page");
    return;
  }

  if (orbitPage == 2) {                              // ---------- Next pass ----------
    if (!orbHasPass) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
      canvas.print("No upcoming pass."); footer("` bk  ,// page  r refresh"); return; }
    auto hms = [](long sec) -> String { if (sec < 0) sec = 0; char b[12];
      snprintf(b, sizeof(b), "%ld:%02ld", sec / 60, sec % 60); return String(b); };
    row("AOS in",   hms((long)(orbPass.aos - now)));
    row("Duration", hms((long)(orbPass.los - orbPass.aos)));
    row("Max el",   String(orbPass.maxEl, 1) + " deg");
    row("AOS az",   String(orbPass.azAos, 0) + " deg");
    row("LOS az",   String(orbPass.azLos, 0) + " deg");
    row("Sunlit",   String(orbSunPct, 0) + " %");
    if (orbEcl && orbEclT0) {
      String e = "+" + hms((long)(orbEclT0 - orbPass.aos));
      if (orbEclT1) e += " to +" + hms((long)(orbEclT1 - orbPass.aos));
      row("Ecl (pass)", e);
      // deepest point in Earth's shadow during the pass (PREDICT-style depth)
      double dMax = -90.0;
      for (time_t t = orbPass.aos; t <= orbPass.los; t += 15) {
        double d = pred.eclipseDepthDeg(t); if (d > dMax) dMax = d;
      }
      if (dMax > 0) row("Ecl depth", String(dMax, 1) + " deg peak");
    } else row("Ecl (pass)", orbEcl ? "yes" : "none");
    if (now) {                                       // slant range + 1-way path delay
      pred.setSite(loc.obs()); pred.setSat(*s);
      double rA = pred.look(orbPass.aos).rangeKm, rT = pred.look(orbPass.tca).rangeKm,
             rL = pred.look(orbPass.los).rangeKm;
      row("Rng A/T/L", String(rA, 0) + "/" + String(rT, 0) + "/" + String(rL, 0));
      row("Delay TCA", String(rT / 299.792458, 1) + " ms 1-way");
    }
    canvas.setTextColor(orbVisible ? CL_YELLOW : CL_GREY, CL_BLACK);
    canvas.setCursor(2, y); canvas.print(orbVisible ? "Optically VISIBLE pass"
                                                    : "Not optically visible");
    footer("` bk  ,// page  r refresh");
    return;
  }

  if (orbitPage == 3) {                              // ---------- Forward ground track ----------
    const int MX = 0, MY = 16, MW = 240, MH = 92;
    canvas.fillRect(MX, MY, MW, MH, CL_BLACK);
    { int px = 0, py = 0, plo = 0; bool pen = false;       // coastline
      for (int i = 0; i + 1 < COAST_N; i += 2) {
        int lo = COAST[i], la = COAST[i + 1];
        if (lo == 999) { pen = false; continue; }
        int x = MX + (lo + 180) * MW / 360, yy = MY + (90 - la) * MH / 180;
        if (pen && abs(lo - plo) < 180) canvas.drawLine(px, py, x, yy, CL_DGREEN);
        px = x; py = yy; plo = lo; pen = true; } }
    canvas.drawFastHLine(MX, MY + MH / 2, MW, CL_MGREY);     // equator
    if (now) {
      pred.setSat(*s);
      double periodSec = 86400.0 / mm; const int N = 80;
      int pxo = 0, pyo = 0; double plon = 0; bool pen = false;
      for (int i = 0; i <= N; ++i) {
        LiveLook L = pred.look(now + (time_t)(periodSec * 2 * i / N));
        double lon = L.subLon; while (lon < -180) lon += 360; while (lon > 180) lon -= 360;
        int x = MX + (int)lround((lon + 180.0) / 360.0 * MW);
        int yy = MY + (int)lround((90.0 - L.subLat) / 180.0 * MH);
        if (pen && fabs(lon - plon) < 180) canvas.drawLine(pxo, pyo, x, yy, CL_CYAN);
        pxo = x; pyo = yy; plon = lon; pen = true;
      }
      LiveLook L0 = pred.look(now);
      double lon0 = L0.subLon; while (lon0 < -180) lon0 += 360; while (lon0 > 180) lon0 -= 360;
      int cx = MX + (int)lround((lon0 + 180.0) / 360.0 * MW);
      int cy = MY + (int)lround((90.0 - L0.subLat) / 180.0 * MH);
      canvas.fillCircle(cx, cy, 2, CL_YELLOW);
    }
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, MY + MH + 2); canvas.print("ground track: next 2 orbits");
    footer("` bk  ,// page");
    return;
  }

  if (orbitPage == 6) {                              // ---------- Sun / beta angle ----------
    const double D2R = 0.017453292519943295;
    double beta = now ? pred.betaAngleDeg(now, s->incl, s->raan) : 0.0;
    // beta* (analytic full-sun threshold) is shown as context, but the actual
    // full-sun/eclipse verdict and fraction come from sampling one full orbit
    // with the SAME geometric shadow test the illumination screen uses -- so the
    // two screens never disagree near the threshold.
    double hMean = a - RE;                                  // km (a, RE in km here)
    double betaStar = (hMean > 0) ? acos(RE / (RE + hMean)) / D2R : 0.0;
    double periodMin2 = (mm > 0) ? 1440.0 / mm : 0.0;
    double fEcl = 0.0; bool fullSun = true;
    if (now && periodMin2 > 0) {
      time_t period = (time_t)(periodMin2 * 60.0 + 0.5);
      int N = 180, ecl = 0;                                 // sample the orbit at ~N points
      for (int k = 0; k < N; ++k) {
        time_t t = now + (time_t)((double)period * k / N);
        if (!pred.sunlitAt(t)) ecl++;
      }
      fEcl = (double)ecl / N;
      fullSun = (ecl == 0);
    }
    row("Beta now",   String(beta, 1) + " deg");
    row("Sunlight",   fullSun ? "full-sun orbit" : "eclipsed each rev");
    row("Beta*",      "+/-" + String(betaStar, 1) + " deg (full-sun)");
    if (!fullSun) {
      row("Eclipse",  String(fEcl * 100.0, 0) + "% /orbit");
      row("Ecl time", String(fEcl * periodMin2, 1) + " min/orbit");
    } else {
      row("Eclipse",  "none (continuous sun)");
    }
    // Scan ahead to the next full-sun window edge (beta crossing +/-beta*).
    if (now) {
      time_t found = 0; bool wantFull = !fullSun;
      for (long d = 1; d <= 180; ++d) {
        double b = pred.betaAngleDeg(now + (time_t)d * 86400, s->incl, s->raan);
        if ((fabs(b) >= betaStar) == wantFull) { found = now + (time_t)d * 86400; break; }
      }
      if (found) {
        struct tm tmv; gmtime_r(&found, &tmv);
        char b[20]; snprintf(b, sizeof(b), "%02d/%02d", tmv.tm_mon + 1, tmv.tm_mday);
        long dd = (long)((found - now) / 86400);
        row(wantFull ? "-> full sun" : "-> eclipses",
            String(b) + " (" + String(dd) + "d)");
      } else {
        row(wantFull ? "-> full sun" : "-> eclipses", ">180d");
      }
    }
    footer("` bk  ,// page  r refresh");
    return;
  }

  if (orbitPage == 7) {                              // ---------- Pass outlook ----------
    if (!now) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
                canvas.print("Clock not set."); footer("` bk  ,// page  r refresh"); return; }
    canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(2, y);
    canvas.printf("Next %d days", ORB_OUTLOOK_DAYS); y += LH;
    if (orbOutlookN == 0) {
      canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(2, y);
      canvas.print("No passes above mask.");
      footer("` bk  ,// page  r refresh"); return;
    }
    auto hms = [](long sec) -> String { if (sec < 0) sec = 0; char b[12];
      snprintf(b, sizeof(b), "%ld:%02ld", sec / 60, sec % 60); return String(b); };
    row("Passes",   String(orbOutlookN) + " (" + String(orbOutlookHi) + " >30 deg)");
    row("Longest",  String(orbLongestMin, 1) + " min");
    row("Avg gap",  String(orbAvgGapH, 1) + " h");
    // Best pass: when, how high, how long, and its sunlit state.
    if (orbBestT) {
      struct tm tmv; gmtime_r(&orbBestT, &tmv);
      char d[16]; snprintf(d, sizeof(d), "%02d/%02d %02d:%02dZ",
                           tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
      row("Best el",  String(orbBestEl, 0) + " deg");
      row("Best at",  String(d));
      row("Best in",  hms((long)(orbBestT - now)));
      row("Best dur", hms((long)orbBestDur));
    }
    footer("` bk  ,// page  r refresh");
    return;
  }

  if (orbitPage == 8) {                              // ---------- Orbit position ----------
    if (!now) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
                canvas.print("Clock not set."); footer("` bk  ,// page"); return; }
    const double D2R = 0.017453292519943295, TWO_PI2 = 6.283185307179586;
    double periodS = (mm > 0) ? 86400.0 / mm : 0.0;
    // Mean anomaly now (deg) advanced from epoch.
    double maNow = fmod(s->ma + 360.0 * mm * (now - s->epochUnix) / 86400.0, 360.0);
    if (maNow < 0) maNow += 360.0;
    // Time to perigee (MA = 0/360) and apogee (MA = 180), via mean motion.
    double toPeri = (360.0 - maNow) / 360.0 * periodS;     // s until next MA=0
    double toApo  = fmod((180.0 - maNow + 360.0), 360.0) / 360.0 * periodS;
    // Argument of latitude u = argp + true anomaly (approx with MA for small e is
    // poor; use a 3-term equation-of-centre expansion for the true anomaly).
    double M = maNow * D2R, e = s->ecc;
    double nu = M + (2*e - 0.25*e*e*e) * sin(M)
                  + 1.25*e*e * sin(2*M)
                  + (13.0/12.0)*e*e*e * sin(3*M);           // true anomaly (rad)
    double u = fmod(s->argp + nu / D2R, 360.0); if (u < 0) u += 360.0;
    long revNow = (long)s->revAtEpoch +
                  (long)floor((now - s->epochUnix) / periodS);
    auto hms = [](long sec) -> String { if (sec < 0) sec = 0; char b[12];
      snprintf(b, sizeof(b), "%ld:%02ld", sec / 60, sec % 60); return String(b); };
    row("Mean anom", String(maNow, 1) + " deg");
    row("True anom", String(nu / D2R, 1) + " deg");
    row("Arg lat",   String(u, 1) + " deg");
    row("To perigee",hms((long)toPeri));
    row("To apogee", hms((long)toApo));
    row("Arg peri",  String(s->argp, 1) + " deg");
    row("RAAN",      String(s->raan, 1) + " deg");
    row("Rev now",   String(revNow));
    row("Epoch age", String((now - s->epochUnix) / 86400.0, 2) + " d");
    footer("` bk  ,// page");
    return;
  }

  if (orbitPage == 5) {                              // ---------- Orbit dynamics (J2) ----------
    const double D2R = 0.017453292519943295;
    const double J2 = 0.00108262998905, DPD = 57.29577951308232 * 86400.0;  // rad/s -> deg/day
    double ci = cos(s->incl * D2R);
    double pp = a * (1.0 - s->ecc * s->ecc);
    double ReP2 = (pp > 0) ? (RE / pp) * (RE / pp) : 0.0;
    double Odot = -1.5 * n * J2 * ReP2 * ci * DPD;             // node regression, deg/day
    double Wdot = 0.75 * n * J2 * ReP2 * (5 * ci * ci - 1) * DPD;  // apsidal, deg/day
    row("Revs/day",    String(mm, 4));
    row("Node drift",  String(Odot, 3) + " /day");
    row("Perig drift", String(Wdot, 3) + " /day");
    row("Sun-sync",    (fabs(Odot - 0.98565) < 0.05) ? "yes" : "no");
    if (now) {                                                // local time of ascending node
      double d = ((double)now - 946728000.0) / 86400.0;       // days since J2000
      double Ls = 280.460 + 0.9856474 * d;
      double g  = (357.528 + 0.9856003 * d) * D2R;
      double lam = (Ls + 1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;
      double eps = 23.439 * D2R;
      double ra = atan2(cos(eps) * sin(lam), cos(lam)) / D2R;  // Sun RA, deg
      double lt = fmod((s->raan - ra) / 15.0 + 12.0 + 48.0, 24.0);
      int hh = (int)lt, mi = (int)((lt - hh) * 60.0 + 0.5);
      if (mi >= 60) { mi -= 60; hh = (hh + 1) % 24; }
      char b[8]; snprintf(b, sizeof(b), "%02d:%02d", hh, mi);
      row("LTAN", String(b));
    } else row("LTAN", "--");
    { double best = 1e9; int bP = 0, bQ = 0;                  // repeat ground track
      for (int P = 1; P <= 30; ++P) { int Q = (int)lround(mm * P);
        double err = fabs(mm - (double)Q / (double)P);
        if (err < best) { best = err; bP = P; bQ = Q; } }
      if (bP && best < 0.015) row("Repeat trk", String(bQ) + " rev/" + String(bP) + "d");
      else                    row("Repeat trk", "none <30d");
    }
    { double rApo = a * (1.0 + s->ecc);                       // longest pass (overhead, apogee)
      double lamA = (rApo > RE) ? acos(RE / rApo) : 0.0;
      double wApo = sqrt(MU * pp) / (rApo * rApo);            // rad/s at apogee
      double tmax = (wApo > 0) ? 2.0 * lamA / wApo / 60.0 : 0.0;
      row("Max pass",  String(tmax, 1) + " min");
    }
    footer("` bk  ,// page  r refresh");
    return;
  }

  // orbitPage == 4                                  // ---------- Doppler curve ----------
  {
    const int PX = 30, PY = 20, PW = 204, PH = 90;
    canvas.drawRect(PX - 1, PY - 1, PW + 2, PH + 2, CL_MGREY);
    if (!orbHasPass) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
      canvas.print("No upcoming pass."); footer("` bk  ,// page"); return; }
    pred.setSat(*s);
    static float buf[PW > 0 ? PW : 1];
    double dur = (double)(orbPass.los - orbPass.aos);
    double fbHz = cfg.beaconMHz * 1e6;
    float mn = 1e9f, mx = -1e9f; double maxRR = 0;
    for (int i = 0; i < PW; ++i) {
      double t = (double)orbPass.aos + dur * i / (PW - 1);
      double rr = pred.rangeRateAt(t); if (fabs(rr) > maxRR) maxRR = fabs(rr);
      float dop = (float)(-rr / C_KM * fbHz);                          // Hz @ beacon freq
      buf[i] = dop; if (dop < mn) mn = dop; if (dop > mx) mx = dop;
    }
    if (mx <= mn) mx = mn + 1;
    int zy = PY + (int)((mx - 0.0f) / (mx - mn) * (PH - 1));
    if (zy >= PY && zy < PY + PH) canvas.drawFastHLine(PX, zy, PW, CL_DGREY);   // 0 Hz line
    int ppx = 0, ppy = 0;
    for (int i = 0; i < PW; ++i) {
      int x = PX + i, yv = PY + (int)((mx - buf[i]) / (mx - mn) * (PH - 1));
      if (i) canvas.drawLine(ppx, ppy, x, yv, CL_GREEN);
      ppx = x; ppy = yv;
    }
    float peak = (fabs(mx) > fabs(mn) ? fabs(mx) : fabs(mn)) / 1000.0f;
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, PY - 2);        canvas.printf("%+.0f", mx);
    canvas.setCursor(2, PY + PH - 6);   canvas.printf("%+.0f", mn);
    canvas.setCursor(2, PY + PH + 2);   canvas.printf("@%g MHz  pk %.1f kHz  RR %.2f",
                                                      cfg.beaconMHz, peak, maxRR);
    footer("` bk  ,// page  f freq");
    return;
  }
}

void App::keyOrbit(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (isRight(c)) { if (++orbitPage > 8) orbitPage = 0; lastDrawMs = 0; return; }
  if (isLeft(c))  { if (--orbitPage < 0) orbitPage = 8; lastDrawMs = 0; return; }
  if (c == 'r')   { buildOrbit(); lastDrawMs = 0; return; }
  if (c == 'f' && orbitPage == 4) {                   // edit Doppler-page beacon freq
    editTarget = 210; editTitle = "Beacon freq (MHz)";
    editBuf = String(cfg.beaconMHz, 3); screen = SCR_EDIT; lastDrawMs = 0; return;
  }
}

// ===========================================================================
//  EQX table (off Satellites): equatorial crossing times + longitudes for use
//  with an OSCARLOCATOR plotting board. Each EQX is an ascending-node crossing
//  (sub-satellite latitude going south -> north). Times are UTC; longitudes are
//  printed West-positive (W = +), the convention printed on Oscarlocator dials.
//  Covers a rolling 3-day window from now, computed on-device from the selected
//  sat's GP elements (no network needed). Modeled on AO-7_OSCARLOCATOR (N8HM).
// ===========================================================================
void App::buildEqx() {
  eqxN = 0; eqxScroll = 0;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet() || s->meanMotion <= 0) return;
  setStatus(eqxDescending ? "Computing crossings (desc)..." : "Computing EQX table...");
  draw();
  pred.setSite(loc.obs()); pred.setSat(*s);

  const time_t now    = nowUtc();
  const time_t window = (time_t)(3 * 86400);         // 3 days
  const time_t end    = now + window;
  // Coarse step a fraction of the period so we never skip a node; each node type
  // recurs once per orbit. 30 s is plenty for LEO/HEO amateur sats.
  const long   step   = 30;

  double prevLat = pred.look(now).subLat;
  time_t prevT   = now;
  for (time_t t = now + step; t <= end && eqxN < EQX_MAX; t += step) {
    LiveLook L = pred.look(t);
    // Ascending node: lat crosses 0 going - -> + (northbound).
    // Descending node: lat crosses 0 going + -> - (southbound).
    bool cross = eqxDescending ? (prevLat >= 0 && L.subLat < 0)
                               : (prevLat < 0 && L.subLat >= 0);
    if (cross) {
      time_t a = prevT, b = t;                       // bracket, then bisect
      for (int k = 0; k < 18; ++k) {
        time_t m = a + (b - a) / 2;
        double lm = pred.look(m).subLat;
        // keep [a,b] straddling the crossing for whichever direction we want
        bool firstHalf = eqxDescending ? (lm >= 0) : (lm < 0);
        if (firstHalf) a = m; else b = m;
      }
      double lonE = pred.look(b).subLon;             // East-positive, -180..180
      double lonW = -lonE; while (lonW < 0) lonW += 360.0; while (lonW >= 360.0) lonW -= 360.0;
      if (lonW < 0.05 || lonW > 359.95) lonW = 0.0;  // avoid "-0.0 W" at the meridian
      eqxT[eqxN]    = b;
      eqxLonW[eqxN] = (float)lonW;
      eqxN++;
    }
    prevLat = L.subLat; prevT = t;
  }
  setStatus(eqxN ? "" : "No crossings found");
}

void App::drawEqx() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + (eqxDescending ? " DEQX" : " EQX"))
           : String("EQX table"));
  canvas.setTextSize(1);
  if (!s) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
            canvas.print("No satellite."); footer("` back"); return; }
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 52);
    canvas.print("Clock not set.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 66);
    canvas.print("Set UTC (GPS or Location).");
    footer("` back"); return;
  }
  if (eqxN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 52);
    canvas.print(eqxDescending ? "No descending crossings." : "No ascending crossings.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 66);
    canvas.print("r recompute   ` back");
    footer("` back"); return;
  }
  // Column header
  canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(4, 17);
  canvas.print(eqxDescending ? "Date  Desc UTC    Long (W+)"
                             : "Date    EQX UTC    Long (W+)");
  const int LH = 9, ROWS = 10;                       // 10 data rows under the head
  if (eqxScroll > eqxN - 1) eqxScroll = 0;
  int y = 28;
  int lastMday = -1;
  for (int e = eqxScroll; e < eqxN && e < eqxScroll + ROWS; ++e) {
    struct tm g; gmtime_r(&eqxT[e], &g);
    char date[8] = "      ";
    if (g.tm_mday != lastMday) { snprintf(date, sizeof(date), "%02d/%02d", g.tm_mon + 1, g.tm_mday);
                                 lastMday = g.tm_mday; }
    char line[40];
    snprintf(line, sizeof(line), "%-6s %02d:%02d:%02d   %5.1f W",
             date, g.tm_hour, g.tm_min, g.tm_sec, eqxLonW[e]);
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(4, y);
    canvas.print(line);
    y += LH;
  }
  // scrollbar hints + position
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (eqxScroll > 0)              { canvas.setCursor(232, 28);  canvas.print("^"); }
  if (eqxScroll + ROWS < eqxN)    { canvas.setCursor(232, 112); canvas.print("v"); }
  int last = eqxScroll + ROWS; if (last > eqxN) last = eqxN;
  { char c[28]; snprintf(c, sizeof(c), "%d-%d/%d", eqxScroll + 1, last, eqxN);
    footer(String("`bk ;/. scr d ") + (eqxDescending ? "asc" : "desc") + " r recalc " + c); }
}

void App::keyEqx(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_SATLIST; lastDrawMs = 0; return; }
  const int ROWS = 10;
  if (isDown(c)) { if (eqxScroll + ROWS < eqxN) eqxScroll += ROWS; lastDrawMs = 0; return; }
  if (isUp(c))   { eqxScroll -= ROWS; if (eqxScroll < 0) eqxScroll = 0; lastDrawMs = 0; return; }
  if (c == 'd')  { eqxDescending = !eqxDescending; buildEqx(); lastDrawMs = 0; return; }
  if (c == 'r')  { buildEqx(); lastDrawMs = 0; return; }
}

// ===========================================================================
//  Simulation screen (off Satellites): freeze a UTC time and scrub it to see
//  where the selected satellite will be. A frozen snapshot (not live).
// ===========================================================================
static const long  SIM_STEP[]  = { 60, 600, 3600, 21600, 86400 };
static const char* SIM_STEPL[] = { "1 min", "10 min", "1 hr", "6 hr", "1 day" };

void App::drawSim() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + (simMap ? " sim map" : " sim")) : String("Simulation"));
  canvas.setTextSize(1);
  if (!s) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
            canvas.print("No satellite."); footer("` back"); return; }
  if (!timeIsSet()) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
            canvas.print("Clock not set (NTP/GPS)."); footer("` back"); return; }
  if (simTime == 0) simTime = nowUtc();

  struct tm tmv; gmtime_r(&simTime, &tmv);
  char ts[28]; snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d:%02dZ",
                        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                        tmv.tm_hour, tmv.tm_min, tmv.tm_sec);

  pred.setSite(loc.obs()); pred.setSat(*s);
  LiveLook L = pred.look(simTime);

  if (simMap) {
    // ---- World-map view: sub-point + footprint at the simulated instant. ----
    const int MX = 0, MY = 16, MW = 240, MH = 92;      // map rect (y 16..108)
    const uint16_t GRID = CL_MGREY;
    canvas.fillRect(MX, MY, MW, MH, CL_BLACK);
    { int px = 0, py = 0, plo = 0; bool pen = false;
      for (int i = 0; i + 1 < COAST_N; i += 2) {
        int lo = COAST[i], la = COAST[i + 1];
        if (lo == 999) { pen = false; continue; }
        int x = MX + (int)lround((lo + 180.0) / 360.0 * MW);
        int y = MY + (int)lround((90.0 - la) / 180.0 * MH);
        if (pen && abs(lo - plo) < 180) canvas.drawLine(px, py, x, y, CL_DGREEN);
        px = x; py = y; plo = lo; pen = true;
      }
    }
    for (int lon = -180; lon <= 180; lon += 30) {
      int x = MX + (lon + 180) * MW / 360; if (x > MX + MW - 1) x = MX + MW - 1;
      canvas.drawLine(x, MY, x, MY + MH - 1, lon == 0 ? CL_DGREEN : GRID);
    }
    for (int lat = -60; lat <= 60; lat += 30) {
      int y = MY + (90 - lat) * MH / 180;
      canvas.drawLine(MX, y, MX + MW - 1, y, lat == 0 ? CL_DGREEN : GRID);
    }
    canvas.drawRect(MX, MY, MW, MH, GRID);

    Observer o = loc.obs();
    if (o.valid) {
      int qx = MX + (int)lround((o.lon + 180.0) / 360.0 * MW);
      int qy = MY + (int)lround((90.0  - o.lat) / 180.0 * MH);
      canvas.drawLine(qx - 3, qy, qx + 3, qy, CL_WHITE);
      canvas.drawLine(qx, qy - 3, qx, qy + 3, CL_WHITE);
    }

    double lon = L.subLon; while (lon < -180) lon += 360; while (lon > 180) lon -= 360;
    double lat = L.subLat; if (lat > 90) lat = 90; if (lat < -90) lat = -90;
    int x = MX + (int)lround((lon + 180.0) / 360.0 * MW);
    int y = MY + (int)lround((90.0 - lat) / 180.0 * MH);
    uint16_t col = L.sunlit ? CL_YELLOW : CL_CYAN;
    if (L.satAltKm > 1.0) {                            // ground footprint outline
      const double D2R = 0.0174532925199433, R2D = 57.2957795130823, RE = 6371.0;
      double beta = acos(RE / (RE + L.satAltKm));
      double la1 = lat * D2R, lo1 = lon * D2R, sb = sin(beta), cb = cos(beta);
      double s1 = sin(la1), c1 = cos(la1);
      int pfx = 0, pfy = 0; double pflon = 0; bool have = false;
      for (int a = 0; a <= 360; a += 15) {
        double az = a * D2R;
        double la2 = asin(s1 * cb + c1 * sb * cos(az));
        double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
        double lo2d = lo2 * R2D; while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
        int fx = MX + (int)lround((lo2d + 180.0) / 360.0 * MW);
        int fy = MY + (int)lround((90.0 - la2 * R2D) / 180.0 * MH);
        if (have && fabs(lo2d - pflon) < 180.0) canvas.drawLine(pfx, pfy, fx, fy, col);
        pfx = fx; pfy = fy; pflon = lo2d; have = true;
      }
    }
    canvas.fillCircle(x, y, 2, col);
    canvas.drawCircle(x, y, 3, CL_BLACK);
    SatEntry* a = activeSat(); if (a) pred.setSat(*a);  // restore propagator

    // Time + step strip below the map.
    canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(2, MY + MH + 1); canvas.print(ts);
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(2, MY + MH + 11);
    canvas.printf("El %.0f  step %s  %s", L.el, SIM_STEPL[simStepIdx],
                  L.el > 0 ? "VIS" : "---");
    footer("` bk  m data  ,// time  ;/. step");
    return;
  }

  int y = 18; const int LH = 11;
  auto row = [&](const char* k, const String& v) {
    canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, y);  canvas.print(k);
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(86, y); canvas.print(v);
    y += LH;
  };
  canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(2, y); canvas.print(ts); y += LH;

  long d = (long)(simTime - nowUtc()); long ad = d < 0 ? -d : d;
  char ds[28]; snprintf(ds, sizeof(ds), "%c%ldd %02ldh %02ldm", d < 0 ? '-' : '+',
                        ad / 86400, (ad % 86400) / 3600, (ad % 3600) / 60);
  row("Offset", ds);
  row("Step",   SIM_STEPL[simStepIdx]);

  row("Az / El",  String(L.az, 1) + " / " + String(L.el, 1));
  row("Range",    String(L.rangeKm, 0) + " km");
  row("Sub pt",   String(L.subLat, 2) + "," + String(L.subLon, 2));
  row("Altitude", String(L.satAltKm, 0) + " km");
  canvas.setTextColor(L.el > 0 ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(2, y); canvas.print(L.el > 0 ? "ABOVE horizon" : "below horizon");
  canvas.setTextColor(L.sunlit ? CL_YELLOW : CL_CYAN, CL_BLACK);
  canvas.printf("  %s", L.sunlit ? "sunlit" : "eclipse");
  footer("` bk  m map  ,// time  ;/. step  x now");
}

void App::keySim(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (c == 'm')   { simMap = !simMap; lastDrawMs = 0; return; }
  if (simTime == 0 && timeIsSet()) simTime = nowUtc();
  if (isRight(c)) { simTime += SIM_STEP[simStepIdx]; lastDrawMs = 0; return; }
  if (isLeft(c))  { simTime -= SIM_STEP[simStepIdx]; lastDrawMs = 0; return; }
  if (isDown(c))  { if (++simStepIdx > 4) simStepIdx = 4; lastDrawMs = 0; return; }
  if (isUp(c))    { if (--simStepIdx < 0) simStepIdx = 0; lastDrawMs = 0; return; }
  if (c == 'x')   { if (timeIsSet()) simTime = nowUtc(); lastDrawMs = 0; return; }
}

// ===========================================================================
//  Sun / Moon tracking screen (off the main menu). Live az/el for both; pick
//  one (;/.) and engage the rotator on it (o); parks on exit. Pointing runs in
//  the loop (smOut) at ~1 Hz, like tracking a pass.
// ===========================================================================
void App::drawSunMoon() {
  header("Sun / Moon");
  canvas.setTextSize(1);
  Observer o = loc.obs();
  if (!o.valid || !timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print(!o.valid ? "Set your location first." : "Clock not set (NTP/GPS).");
    footer("` back"); return;
  }
  time_t now = nowUtc();
  double azv[2], elv[2];
  skyObjAzEl(now, o.lat, o.lon, false, azv[0], elv[0]);   // Sun
  skyObjAzEl(now, o.lat, o.lon, true,  azv[1], elv[1]);   // Moon
  static const char* nm[2] = { "SUN", "MOON" };
  const uint16_t SUNCOL = CL_YELLOW, MOONCOL = CL_CYAN;
  const uint16_t bodyCol[2] = { SUNCOL, MOONCOL };

  if (smGraphic) {
    // ---- Graphical sky dome: zenith at centre, N up, elevation = radius. ----
    // Bodies below the horizon are shown faintly just outside the rim so their
    // azimuth is still readable. The selected body gets a ring, not a fill bar.
    const int cx = 62, cy = 68, R = 42;
    drawPolarGrid(cx, cy, R);
    auto domeXY = [&](double az, double el, int& x, int& y) {
      double e = el; if (e > 90) e = 90; if (e < -6) e = -6;
      double rr = (e >= 0) ? R * (90.0 - e) / 90.0 : R + 4;   // below: just outside rim
      double a  = az * (M_PI / 180.0);
      x = cx + (int)lround(rr * sin(a));
      y = cy - (int)lround(rr * cos(a));
    };
    for (int i = 0; i < 2; ++i) {
      int x, y; domeXY(azv[i], elv[i], x, y);
      bool up = elv[i] > 0;
      uint16_t col = up ? bodyCol[i] : CL_GREY;
      if (i == 0) {                                  // Sun: filled disc + rays
        canvas.fillCircle(x, y, 5, col);
        if (up) for (int a = 0; a < 360; a += 45) {
          double ar = a * (M_PI / 180.0);
          canvas.drawLine(x + (int)lround(7*sin(ar)), y - (int)lround(7*cos(ar)),
                          x + (int)lround(9*sin(ar)), y - (int)lround(9*cos(ar)), col);
        }
      } else {                                       // Moon: disc with a bite (crescent)
        canvas.fillCircle(x, y, 5, col);
        canvas.fillCircle(x + 2, y - 1, 4, CL_BLACK);
      }
      if (smSel == i) canvas.drawCircle(x, y, 9, CL_GREEN);   // selection ring
    }

    // ---- Compact data panel on the right. ----
    int px = 124;
    for (int i = 0; i < 2; ++i) {
      int y = 22 + i * 40;
      bool sel = (smSel == i);
      canvas.setTextSize(2);
      canvas.setTextColor(sel ? CL_GREEN : bodyCol[i], CL_BLACK);
      canvas.setCursor(px, y); canvas.print(nm[i]);
      canvas.setTextSize(1);
      canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(px, y + 18);  canvas.print("Az");
      canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(px + 18, y + 18); canvas.printf("%.1f", azv[i]);
      canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(px + 64, y + 18); canvas.print("El");
      canvas.setTextColor(elv[i] > 0 ? CL_WHITE : CL_GREY, CL_BLACK);
      canvas.setCursor(px + 82, y + 18); canvas.printf("%.1f", elv[i]);
      canvas.setTextColor(elv[i] > 0 ? CL_GREEN : CL_GREY, CL_BLACK);
      canvas.setCursor(px, y + 28); canvas.print(elv[i] > 0 ? "above horizon" : "below horizon");
    }
  } else {
    // ---- Data-list view (compact, no full-width highlight bar). ----
    for (int i = 0; i < 2; ++i) {
      int y = 24 + i * 36;
      bool sel = (smSel == i);
      if (sel) { canvas.drawRect(2, y - 4, 236, 32, CL_GREEN); }   // outline, not fill
      canvas.setTextColor(sel ? CL_GREEN : bodyCol[i], CL_BLACK);
      canvas.setTextSize(2); canvas.setCursor(8, y); canvas.print(nm[i]); canvas.setTextSize(1);
      canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(96, y);      canvas.print("Az");
      canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(114, y);     canvas.printf("%.1f", azv[i]);
      canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(96, y + 13); canvas.print("El");
      canvas.setTextColor(elv[i] > 0 ? CL_WHITE : CL_GREY, CL_BLACK);
      canvas.setCursor(114, y + 13); canvas.printf("%.1f", elv[i]);
      canvas.setTextColor(elv[i] > 0 ? CL_GREEN : CL_GREY, CL_BLACK);
      canvas.setCursor(186, y + 6); canvas.print(elv[i] > 0 ? "up" : "down");
    }
  }

  bool rok = rot && rot->ready();
  canvas.setTextColor(smOut ? CL_GREEN : (rok ? CL_GREY : CL_ORANGE), CL_BLACK);
  canvas.setCursor(8, 116);
  const char* rs = !smOut ? (rok ? "off" : "n/c")
                  : (elv[smSel] <= 0 ? (smSel ? "MOON set (parked)" : "SUN set (parked)")
                                     : (smSel ? "tracking MOON" : "tracking SUN"));
  canvas.printf("Rotator: %s", rs);
  footer("` bk  ;/. pick  g view  o rotor  s sky  x stop");
}

void App::keySunMoon(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) {
    if (smOut && rot) rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl);   // park on exit
    smOut = false; screen = SCR_HOME; lastDrawMs = 0; return;
  }
  if (isUp(c) || isDown(c)) { smSel ^= 1; lastAzCmd = lastElCmd = -999.0f; lastDrawMs = 0; return; }
  if (c == 'g') { smGraphic = !smGraphic; lastDrawMs = 0; return; }
  if (c == 'o') {
    if (!rot || !rot->ready()) { setStatus("Rotator not ready"); return; }
    smOut = !smOut; lastAzCmd = lastElCmd = -999.0f;
    if (smOut) { rotOut = false;             // Sun/Moon takes the rotator
                 setStatus(smSel ? "Tracking Moon" : "Tracking Sun"); }
    else { rotPoint((float)cfg.rotParkAz, (float)cfg.rotParkEl); setStatus("Rotator OFF (parked)"); }
    lastDrawMs = 0; return;
  }
  if (c == 'x') { if (rot) rot->stop(); smOut = false; lastDrawMs = 0; return; }
  if (c == 's') { skySel = 0; screen = SCR_SKYMAP; lastDrawMs = 0; return; }  // sky plot
}

// ===========================================================================
//  Celestial sky plot (SCR_SKYMAP): planets and strong radio sources on a sky
//  dome (zenith centre, N up, elevation = radius), reached with `s` from the
//  Sun/Moon screen. Useful for antenna pointing (Sun/Moon/planet) and as a
//  reference for the strongest cosmic radio sources crossing the sky.
// ===========================================================================

// Equatorial (RA/Dec, degrees, J2000-ish) -> horizontal (az/el) for the observer
// at time t. The same conversion used inside skyObjAzEl, factored out so fixed
// catalogue sources and computed planet positions can share it.
static void raDecToAzEl(time_t t, double latDeg, double lonDeg,
                        double raDeg, double decDeg, double& az, double& el) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232,
               TWO_PI_ = 6.283185307179586;
  double d = ((double)t - 946728000.0) / 86400.0;
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  if (gmst < 0) gmst += 360.0;
  double ra = raDeg * D2R, dec = decDeg * D2R, latR = latDeg * D2R;
  double lst = (gmst + lonDeg) * D2R, ha = lst - ra;
  double sinAlt = sin(latR) * sin(dec) + cos(latR) * cos(dec) * cos(ha);
  double alt = asin(sinAlt);
  double cosA = (sin(dec) - sin(latR) * sinAlt) / (cos(latR) * cos(alt));
  if (cosA > 1) cosA = 1; if (cosA < -1) cosA = -1;
  double A = acos(cosA);
  if (sin(ha) > 0) A = TWO_PI_ - A;
  az = A * R2D; el = alt * R2D;
}

// Low-precision heliocentric -> geocentric RA/Dec for the classical planets,
// using Schlyter's elements (good to ~1-2 arcmin, far finer than a 112 px dome).
static void planetRaDec(int p, time_t t, double& raDeg, double& decDeg) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232;
  double d = ((double)t - 946728000.0) / 86400.0;          // days since J2000
  double ecl = (23.4393 - 3.563e-7 * d) * D2R;
  // Orbital elements: N, i, w, a, e, M (deg / AU), linear in d (Schlyter).
  // p: 0 Mercury 1 Venus 2 Mars 3 Jupiter 4 Saturn.
  struct El { double N0,Nr,i0,ir,w0,wr,a0,ar,e0,er,M0,Mr; };
  static const El E[5] = {
    { 48.3313,3.24587e-5, 7.0047,5.00e-8, 29.1241,1.01444e-5, 0.387098,0,      0.205635,5.59e-10, 168.6562,4.0923344368},
    { 76.6799,2.46590e-5, 3.3946,2.75e-8, 54.8910,1.38374e-5, 0.723330,0,      0.006773,-1.302e-9,48.0052,1.6021302244},
    { 49.5574,2.11081e-5, 1.8497,-1.78e-8,286.5016,2.92961e-5,1.523688,0,      0.093405,2.516e-9, 18.6021,0.5240207766},
    {100.4542,2.76854e-5, 1.3030,-1.557e-7,273.8777,1.64505e-5,5.20256,0,      0.048498,4.469e-9, 19.8950,0.0830853001},
    {113.6634,2.38980e-5, 2.4886,-1.081e-7,339.3939,2.97661e-5,9.55475,0,      0.055546,-9.499e-9,316.9670,0.0334442282},
  };
  const El& e = E[p];
  auto rad = [&](double deg){ return deg * D2R; };
  double N = rad(e.N0 + e.Nr * d), i = rad(e.i0 + e.ir * d), w = rad(e.w0 + e.wr * d);
  double a = e.a0, ecc = e.e0 + e.er * d, M = rad(e.M0 + e.Mr * d);
  M = fmod(M, 2 * M_PI); if (M < 0) M += 2 * M_PI;
  double Ea = M + ecc * sin(M) * (1 + ecc * cos(M));
  for (int k = 0; k < 5; ++k) Ea -= (Ea - ecc * sin(Ea) - M) / (1 - ecc * cos(Ea));
  double xv = a * (cos(Ea) - ecc), yv = a * sqrt(1 - ecc * ecc) * sin(Ea);
  double v = atan2(yv, xv), r = sqrt(xv * xv + yv * yv);
  double xh = r * (cos(N) * cos(v + w) - sin(N) * sin(v + w) * cos(i));
  double yh = r * (sin(N) * cos(v + w) + cos(N) * sin(v + w) * cos(i));
  double zh = r * (sin(v + w) * sin(i));
  // Sun's geocentric position (Earth heliocentric, negated).
  double Ms = rad(356.0470 + 0.9856002585 * d), ws = rad(282.9404 + 4.70935e-5 * d);
  double es = 0.016709 - 1.151e-9 * d;
  double Es = Ms + es * sin(Ms) * (1 + es * cos(Ms));
  double xvs = cos(Es) - es, yvs = sqrt(1 - es * es) * sin(Es);
  double lonsun = atan2(yvs, xvs) + ws, rs = sqrt(xvs * xvs + yvs * yvs);
  double xs = rs * cos(lonsun), ys = rs * sin(lonsun);
  // Geocentric ecliptic -> equatorial.
  double xg = xh + xs, yg = yh + ys, zg = zh;
  double xe = xg;
  double ye = yg * cos(ecl) - zg * sin(ecl);
  double ze = yg * sin(ecl) + zg * cos(ecl);
  raDeg = atan2(ye, xe) * R2D; if (raDeg < 0) raDeg += 360.0;
  decDeg = atan2(ze, sqrt(xe * xe + ye * ye)) * R2D;
}

// Catalogue of strong cosmic radio sources (fixed J2000 RA/Dec, degrees) plus a
// couple of bright orientation stars. Names kept short for the 240 px display.
struct SkySrc { const char* name; double ra, dec; bool rf; };
static const SkySrc SKY_RF[] = {
  { "CasA",  350.85,  58.81, true },   // brightest galactic radio source
  { "CygA",  299.87,  40.73, true },   // radio galaxy
  { "SgrA",  266.42, -29.01, true },   // galactic centre
  { "TauA",   83.63,  22.01, true },   // Crab nebula / pulsar
  { "VirA",  187.71,  12.39, true },   // M87
  { "Sun",     0.0,    0.0,  true },   // placeholder (computed live)
  { "Polaris",37.95,  89.26, false },  // orientation
  { "Vega",  279.23,  38.78, false },
  { "Antares",247.35,-26.43, false },
};
static const int SKY_RF_N = sizeof(SKY_RF) / sizeof(SKY_RF[0]);
static const char* PLANET_NAME[5] = { "Mercury", "Venus", "Mars", "Jupiter", "Saturn" };

void App::drawSkyMap() {
  header("Sky sources");
  canvas.setTextSize(1);
  Observer o = loc.obs();
  if (!o.valid || !timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print(!o.valid ? "Set your location first." : "Clock not set (NTP/GPS).");
    footer("` back"); return;
  }
  time_t now = nowUtc();

  // Build the combined object list: 5 planets, then the catalogue sources.
  const int NP = 5, NTOT = NP + SKY_RF_N;
  double az[32], el[32]; const char* nm[32]; bool isRf[32];
  for (int p = 0; p < NP; ++p) {
    double ra, dec; planetRaDec(p, now, ra, dec);
    raDecToAzEl(now, o.lat, o.lon, ra, dec, az[p], el[p]);
    nm[p] = PLANET_NAME[p]; isRf[p] = false;
  }
  for (int i = 0; i < SKY_RF_N; ++i) {
    int k = NP + i;
    if (i == 5) {  // "Sun" entry: compute live
      skyObjAzEl(now, o.lat, o.lon, false, az[k], el[k]);
    } else {
      raDecToAzEl(now, o.lat, o.lon, SKY_RF[i].ra, SKY_RF[i].dec, az[k], el[k]);
    }
    nm[k] = SKY_RF[i].name; isRf[k] = SKY_RF[i].rf;
  }
  if (skySel < 0) skySel = NTOT - 1;
  if (skySel >= NTOT) skySel = 0;

  // Sky dome: zenith centre, N up, elevation = radius (same as Sun/Moon graphic).
  const int cx = 62, cy = 70, R = 50;
  drawPolarGrid(cx, cy, R);
  auto domeXY = [&](double a, double e, int& x, int& y) {
    double ee = e; if (ee > 90) ee = 90; if (ee < -6) ee = -6;
    double rr = (ee >= 0) ? R * (90.0 - ee) / 90.0 : R + 4;
    double ar = a * (M_PI / 180.0);
    x = cx + (int)lround(rr * sin(ar));
    y = cy - (int)lround(rr * cos(ar));
  };
  for (int k = 0; k < NTOT; ++k) {
    int x, y; domeXY(az[k], el[k], x, y);
    bool up = el[k] > 0;
    uint16_t col = isRf[k] ? (up ? CL_ORANGE : CL_DGREY)
                           : (up ? CL_CYAN   : CL_DGREY);
    if (isRf[k]) {                       // RF source: small cross
      canvas.drawLine(x - 2, y, x + 2, y, col);
      canvas.drawLine(x, y - 2, x, y + 2, col);
    } else {                             // planet: filled dot
      canvas.fillCircle(x, y, 2, col);
    }
    if (k == skySel) canvas.drawCircle(x, y, 5, CL_GREEN);
  }

  // Right-hand readout for the selected object.
  int px = 122;
  canvas.setTextSize(2);
  canvas.setTextColor(isRf[skySel] ? CL_ORANGE : CL_CYAN, CL_BLACK);
  canvas.setCursor(px, 20); canvas.printf("%.10s", nm[skySel]);
  canvas.setTextSize(1);
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(px, 44); canvas.print("Az");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(px + 18, 44); canvas.printf("%.1f%c", az[skySel], 248);
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(px, 56); canvas.print("El");
  canvas.setTextColor(el[skySel] > 0 ? CL_WHITE : CL_GREY, CL_BLACK);
  canvas.setCursor(px + 18, 56); canvas.printf("%.1f%c", el[skySel], 248);
  canvas.setTextColor(el[skySel] > 0 ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(px, 70); canvas.print(el[skySel] > 0 ? "above horizon" : "below horizon");
  canvas.setTextColor(isRf[skySel] ? CL_ORANGE : CL_CYAN, CL_BLACK);
  canvas.setCursor(px, 86); canvas.print(isRf[skySel] ? "radio source" : "planet");
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(px, 104); canvas.printf("%d/%d", skySel + 1, NTOT);

  footer(";/. select  ` back");
}

void App::keySkyMap(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_SUNMOON; lastDrawMs = 0; return; }
  const int NTOT = 5 + SKY_RF_N;
  if (isUp(c) || isLeft(c))    { skySel = (skySel + NTOT - 1) % NTOT; lastDrawMs = 0; }
  if (isDown(c) || isRight(c)) { skySel = (skySel + 1) % NTOT;        lastDrawMs = 0; }
}

// ===========================================================================
//  Live GPS position (SCR_GPSPOS): latitude/longitude in degrees-minutes-seconds
//  to full precision, plus ground speed, course, altitude and grid square.
//  Reached with `p` from the Location screen.
// ===========================================================================

// Format a signed decimal degree as D M' S.sss" with a hemisphere letter.
static String fmtDMS(double deg, bool isLat) {
  char hemi = isLat ? (deg >= 0 ? 'N' : 'S') : (deg >= 0 ? 'E' : 'W');
  double a = fabs(deg);
  int d = (int)a;
  double mfull = (a - d) * 60.0;
  int m = (int)mfull;
  double s = (mfull - m) * 60.0;        // seconds, keep 3 decimals (~0.03 m)
  char buf[40];
  snprintf(buf, sizeof(buf), "%d%c%02d' %06.3f\" %c", d, 248, m, s, hemi);
  return String(buf);
}

void App::drawGpsPos() {
  header("GPS position");
  canvas.setTextSize(1);
  bool fix = loc.gpsHasFix();
  const Observer& o = loc.obs();

  if (!fix && !o.valid) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 50); canvas.print("No GPS fix yet.");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 64); canvas.print("Needs a GPS module + sky view.");
    footer("` back"); return;
  }

  // Status line: fix source + satellite count + HDOP.
  canvas.setTextColor(fix ? CL_GREEN : CL_ORANGE, CL_BLACK);
  canvas.setCursor(2, 18);
  if (fix) canvas.printf("FIX  %d sats  HDOP %.1f", loc.gpsSats(), loc.gpsHdop());
  else     canvas.print("No live fix (last known)");

  // Latitude / longitude in DMS.
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, 34); canvas.print("Lat");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(24, 34); canvas.print(fmtDMS(o.lat, true).c_str());
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, 46); canvas.print("Lon");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(24, 46); canvas.print(fmtDMS(o.lon, false).c_str());

  // Decimal degrees (full precision) underneath, for copy/reference.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(24, 58); canvas.printf("%.6f, %.6f", o.lat, o.lon);

  // Altitude + grid.
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, 72); canvas.print("Alt");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(24, 72); canvas.printf("%.1f m", o.altM);
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(120, 72); canvas.print("Grid");
  canvas.setTextColor(CL_CYAN, CL_BLACK);  canvas.setCursor(148, 72); canvas.print(Location::toGrid(o.lat, o.lon).c_str());

  // Speed + course (only meaningful with a live fix).
  double spd = loc.gpsSpeedKmh(), crs = loc.gpsCourseDeg();
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, 88);  canvas.print("Spd");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(24, 88);
  canvas.printf("%.1f km/h  %.1f kn", spd, spd * 0.539957);
  canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(2, 100); canvas.print("Crs");
  canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(24, 100);
  static const char* CARD[8] = { "N","NE","E","SE","S","SW","W","NW" };
  canvas.printf("%.0f%c %s", crs, 248, CARD[((int)((crs + 22.5) / 45.0)) & 7]);

  footer("` back");
}

void App::keyGpsPos(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_LOCATION; lastDrawMs = 0; return; }
}

// ===========================================================================
//  Sat-to-sat visibility finder (SCR_SATSAT): the windows over the next few days
//  when the selected satellite AND a second satellite (chosen from favourites)
//  are BOTH above the horizon at your location at the same time — e.g. for
//  cross-satellite relay experiments or back-to-back working. Reached with `2`
//  from the Satellites screen.
// ===========================================================================

// Begin (or restart) the incremental sat-to-sat search. Allocates the elevation
// tracks and sets phase 1; the actual sampling/scan runs a chunk per loop() tick
// in satsatJobTick(), so the device never blocks long enough to stall.
void App::satsatStartJob() {
  satsatAbortJob();                  // free any previous run first
  satsatN = 0; satsatComputed = false;
  SatEntry* a = activeSat();
  if (!a || !timeIsSet() || favN == 0) { satsatComputed = true; return; }
  if (satsatOther < 0) satsatOther = 0;
  if (satsatOther >= favN) satsatOther = favN - 1;
  int bIdx = db.indexOfNorad(favs[satsatOther]);
  if (bIdx < 0) { satsatComputed = true; return; }
  if (db.at(bIdx).norad == a->norad) { satsatComputed = true; return; }  // same bird

  satsatElA = (int8_t*)malloc(SATSAT_NS);
  satsatElB = (int8_t*)malloc(SATSAT_NS);
  if (!satsatElA || !satsatElB) { satsatAbortJob(); satsatComputed = true; return; }

  satsatT0 = nowUtc();
  satsatJobPhase = 1; satsatJobI = 0; satsatJobPct = 0;
  // Prime the predictor for sat A; the tick samples from here.
  pred.setSite(loc.obs()); pred.setSat(*a);
}

// Free the job buffers and return to idle. Safe to call anytime.
void App::satsatAbortJob() {
  if (satsatElA) { free(satsatElA); satsatElA = nullptr; }
  if (satsatElB) { free(satsatElB); satsatElB = nullptr; }
  satsatJobPhase = 0; satsatJobI = 0;
}

// Advance the incremental search by one chunk. Called every loop() tick while a
// job is active. Sampling is split into bounded chunks so a single tick stays
// well under the watchdog window; the scan phase is cheap and finishes in one go.
void App::satsatJobTick() {
  if (satsatJobPhase == 0) return;
  if (!satsatElA || !satsatElB) { satsatAbortJob(); satsatComputed = true; return; }

  const int STEP  = 60;                         // seconds between samples
  const int CHUNK = 240;                        // samples processed per tick

  if (satsatJobPhase == 1) {                    // sampling satellite A
    int end = satsatJobI + CHUNK; if (end > SATSAT_NS) end = SATSAT_NS;
    for (int i = satsatJobI; i < end; ++i) {
      double az, el; pred.azelAt(satsatT0 + (time_t)i * STEP, az, el);
      satsatElA[i] = (int8_t)constrain((int)lround(el), -90, 90);
    }
    satsatJobI = end;
    satsatJobPct = (satsatJobI * 50) / SATSAT_NS;        // 0..50%
    if (satsatJobI >= SATSAT_NS) {
      // Switch to sat B: re-point the predictor, restart the index.
      int bIdx = db.indexOfNorad(favs[satsatOther]);
      if (bIdx < 0) { satsatAbortJob(); satsatComputed = true; satsatN = 0; return; }
      SatEntry b = db.at(bIdx);
      pred.setSat(b);
      satsatJobPhase = 2; satsatJobI = 0;
    }
    return;
  }

  if (satsatJobPhase == 2) {                    // sampling satellite B
    int end = satsatJobI + CHUNK; if (end > SATSAT_NS) end = SATSAT_NS;
    for (int i = satsatJobI; i < end; ++i) {
      double az, el; pred.azelAt(satsatT0 + (time_t)i * STEP, az, el);
      satsatElB[i] = (int8_t)constrain((int)lround(el), -90, 90);
    }
    satsatJobI = end;
    satsatJobPct = 50 + (satsatJobI * 50) / SATSAT_NS;   // 50..100%
    if (satsatJobI >= SATSAT_NS) { satsatJobPhase = 3; satsatJobI = 0; }
    return;
  }

  if (satsatJobPhase == 3) {                    // scan the two tracks for overlaps
    satsatN = 0;
    bool inWin = false; time_t wStart = 0; float maxA = 0, maxB = 0;
    for (int i = 0; i < SATSAT_NS; ++i) {
      bool both = (satsatElA[i] > 0 && satsatElB[i] > 0);
      time_t t = satsatT0 + (time_t)i * STEP;
      if (both && !inWin) { inWin = true; wStart = t; maxA = satsatElA[i]; maxB = satsatElB[i]; }
      else if (both) { if (satsatElA[i] > maxA) maxA = satsatElA[i]; if (satsatElB[i] > maxB) maxB = satsatElB[i]; }
      else if (!both && inWin) {
        inWin = false;
        if (satsatN < SATSAT_MAX) {
          satsatWin[satsatN].start = wStart; satsatWin[satsatN].end = t;
          satsatWin[satsatN].maxElA = maxA;  satsatWin[satsatN].maxElB = maxB; satsatN++;
        } else break;
      }
    }
    if (inWin && satsatN < SATSAT_MAX) {
      satsatWin[satsatN].start = wStart;
      satsatWin[satsatN].end = satsatT0 + (time_t)(SATSAT_NS - 1) * STEP;
      satsatWin[satsatN].maxElA = maxA; satsatWin[satsatN].maxElB = maxB; satsatN++;
    }
    // Done: free buffers, restore the predictor to the primary sat for the UI.
    satsatAbortJob();
    satsatComputed = true; satsatJobPct = 100; satsatSel = 0;
    SatEntry* a = activeSat();
    if (a) { pred.setSite(loc.obs()); pred.setSat(*a); }
    lastDrawMs = 0;
    return;
  }
}

void App::drawSatSat() {
  SatEntry* a = activeSat();
  header(a ? (String(a->name) + " + sat") : String("Sat-to-sat"));
  canvas.setTextSize(1);
  if (!a) { footer("` back"); return; }
  if (favN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 50); canvas.print("No favorites. Star sats");
    canvas.setCursor(6, 62); canvas.print("with 'f' to pick a 2nd.");
    footer("` back"); return;
  }
  // While the incremental search runs, show a live progress bar. The actual
  // sampling happens a chunk per loop() tick in satsatJobTick(), so this screen
  // stays responsive (the back key still works) and always finishes.
  if (satsatJobPhase != 0 || !satsatComputed) {
    int bIdx = db.indexOfNorad(favs[satsatOther]);
    const char* bName = (bIdx >= 0) ? db.at(bIdx).name : "?";
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 46); canvas.print("Calculating windows...");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 58); canvas.printf("vs %.20s", bName);
    // progress bar
    int pct = satsatJobPct; if (pct < 0) pct = 0; if (pct > 100) pct = 100;
    const int bx = 6, by = 74, bw = 228, bh = 10;
    canvas.drawRect(bx, by, bw, bh, CL_GREY);
    canvas.fillRect(bx + 1, by + 1, (bw - 2) * pct / 100, bh - 2, CL_GREEN);
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(bx, by + 14); canvas.printf("%d%%  (searching 3 days)", pct);
    footer("` back");
    lastDrawMs = 0;              // keep redrawing so the bar advances
    return;
  }

  // Which 2nd sat.
  int bIdx = db.indexOfNorad(favs[satsatOther]);
  const char* bName = (bIdx >= 0) ? db.at(bIdx).name : "?";
  canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(2, 18);
  canvas.print("2nd:");
  canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(28, 18);
  canvas.printf("%.16s", bName);
  canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(2, 28);
  canvas.print("Both up @ your QTH:");

  if (satsatN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 60); canvas.print("No overlap in 5 days.");
    footer("n next sat  r recompute  `bk"); return;
  }

  // Column header + rows.
  canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(2, 40);
  canvas.print("Start UTC    Dur   elA elB");
  const int VIS = 7;
  int scroll = (satsatSel >= VIS) ? (satsatSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < satsatN; ++v) {
    int i = scroll + v;
    SatSatWin& w = satsatWin[i];
    long secs = (long)(w.end - w.start);
    int y = 50 + v * 10;
    if (i == satsatSel) canvas.fillRect(0, y - 1, 240, 10, CL_SELBG);
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(2, y);
    canvas.print(fmtMDHM(w.start).c_str());
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(96, y);
    canvas.printf("%ld:%02ld", secs / 60, secs % 60);
    canvas.setTextColor(CL_GREEN, CL_BLACK); canvas.setCursor(150, y);
    canvas.printf("%.0f", w.maxElA);
    canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(180, y);
    canvas.printf("%.0f", w.maxElB);
  }

  footer("n next sat  r recompute  `bk");
}

void App::keySatSat(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { satsatAbortJob(); buildSatView(); screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (favN == 0) return;
  if (c == 'n') {                             // cycle the 2nd satellite
    satsatOther = (satsatOther + 1) % favN;
    satsatStartJob(); lastDrawMs = 0; return;
  }
  if (c == 'r') { satsatStartJob(); lastDrawMs = 0; return; }   // recompute
  if (isUp(c))   { if (satsatSel > 0) satsatSel--; lastDrawMs = 0; }
  if (isDown(c)) { if (satsatSel < satsatN - 1) satsatSel++; lastDrawMs = 0; }
}

// ===========================================================================
//  LoRa text messaging (SCR_MESSAGES)
//
//  Broadcast group chat between CardSats sharing a frequency/SF/BW. The wire
//  frame is deliberately tiny and fixed-layout so there is no heap churn:
//      [0]    magic   0xC5
//      [1]    version 0x01
//      [2..15] from callsign, space-padded, 14 bytes
//      [16..] message text, up to 48 bytes (not null-terminated on air)
//  Anything that doesn't start 0xC5 0x01 is ignored (other LoRa users on band).
//  History is a fixed ring of MSG_MAX entries; nothing here allocates.
// ===========================================================================

static const uint8_t LORA_MSG_MAGIC = 0xC5;
static const uint8_t LORA_MSG_VER   = 0x01;
static const int     LORA_FROM_LEN  = 14;
static const int     LORA_TEXT_MAX  = 48;

void App::msgPush(const char* from, const char* text, bool mine, int rssi, int snr) {
  LoraMsg& m = msgRing[msgHead];
  strncpy(m.from, from ? from : "", sizeof(m.from) - 1); m.from[sizeof(m.from)-1] = 0;
  strncpy(m.text, text ? text : "", sizeof(m.text) - 1); m.text[sizeof(m.text)-1] = 0;
  m.mine = mine;
  m.rssi = (int16_t)rssi;
  m.snr  = (int8_t)snr;
  m.tMs  = millis();
  msgHead = (msgHead + 1) % MSG_MAX;
  if (msgCount < MSG_MAX) msgCount++;
  msgScroll = 0;                    // jump to newest on any new message
}

void App::loraStart() {
  int8_t tx = cfg.loraTxDbm; if (tx > 22) tx = 22; if (tx < 0) tx = 0;
  loraStarted = lora.begin(cfg.loraFreqKHz, cfg.loraSf, cfg.loraBwHz, tx);
}

void App::loraPoll() {
  if (!lora.ready()) return;
  uint8_t buf[64]; size_t n = 0; float rssi = 0, snr = 0;
  if (!lora.poll(buf, sizeof(buf), n, rssi, snr)) return;
  if (n < 2 || buf[0] != LORA_MSG_MAGIC || buf[1] != LORA_MSG_VER) return;  // not ours

  char from[LORA_FROM_LEN + 1]; char text[LORA_TEXT_MAX + 1];
  size_t fEnd = 2 + LORA_FROM_LEN;
  size_t fLen = (n >= fEnd) ? LORA_FROM_LEN : (n > 2 ? n - 2 : 0);
  memcpy(from, buf + 2, fLen); from[fLen] = 0;
  for (int i = (int)strlen(from) - 1; i >= 0 && from[i] == ' '; --i) from[i] = 0; // trim pad
  size_t tLen = (n > fEnd) ? (n - fEnd) : 0;
  if (tLen > LORA_TEXT_MAX) tLen = LORA_TEXT_MAX;
  memcpy(text, buf + fEnd, tLen); text[tLen] = 0;

  msgPush(from[0] ? from : "?", text, false, (int)lroundf(rssi), (int)lroundf(snr));
  if (screen == SCR_MESSAGES) lastDrawMs = 0;
}

void App::loraSendCurrent(const char* text) {
  if (!lora.ready()) { setStatus("LoRa radio not ready"); return; }
  uint8_t frame[2 + LORA_FROM_LEN + LORA_TEXT_MAX];
  frame[0] = LORA_MSG_MAGIC; frame[1] = LORA_MSG_VER;
  // from callsign, space-padded to fixed width
  char call[LORA_FROM_LEN + 1];
  strncpy(call, cfg.myCall[0] ? cfg.myCall : "NOCALL", LORA_FROM_LEN); call[LORA_FROM_LEN] = 0;
  for (int i = 0; i < LORA_FROM_LEN; ++i)
    frame[2 + i] = (i < (int)strlen(call)) ? (uint8_t)call[i] : (uint8_t)' ';
  // text (clamped)
  int tLen = (int)strlen(text); if (tLen > LORA_TEXT_MAX) tLen = LORA_TEXT_MAX;
  memcpy(frame + 2 + LORA_FROM_LEN, text, tLen);
  size_t total = 2 + LORA_FROM_LEN + tLen;

  bool ok = lora.sendRaw(frame, total);
  char shown[LORA_TEXT_MAX + 1];
  strncpy(shown, text, LORA_TEXT_MAX); shown[LORA_TEXT_MAX] = 0;
  msgPush("", shown, true, 0, 0);            // echo my own message into history
  setStatus(ok ? "Sent" : "TX failed");
}

void App::drawMessages() {
  header("Messages");
  canvas.setTextSize(1);

  if (!cfg.loraEnable) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("LoRa messaging is off.");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 58); canvas.print("Enable it in Settings,");
    canvas.setCursor(6, 70); canvas.print("then set your callsign.");
    footer("` back");
    return;
  }
  if (!loraStarted || !lora.ready()) {
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(6, 44); canvas.print("Radio not ready.");
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(6, 58); canvas.print("Needs the Cap LoRa + a");
    canvas.setCursor(6, 70); canvas.print("RadioLib build (see docs).");
    footer("r retry   ` back");
    return;
  }

  // Status line: freq / SF / BW + my call.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(2, 19);
  canvas.printf("%.3fMHz SF%d %ldk  %s",
                cfg.loraFreqKHz / 1000.0, cfg.loraSf, (long)(cfg.loraBwHz / 1000),
                cfg.myCall[0] ? cfg.myCall : "NOCALL");
  canvas.drawLine(0, 28, 240, 28, CL_DGREY);

  // Message list: newest at the bottom, scrollable with ;/. (msgScroll counts
  // back from newest). Each message is one or two lines.
  const int VIS = 9, rowY0 = 31, rowH = 10;
  // Build a display order: oldest..newest indices.
  int order[MSG_MAX]; int on = 0;
  for (int i = 0; i < msgCount; ++i) {
    int idx = (msgHead - msgCount + i + MSG_MAX * 2) % MSG_MAX;
    order[on++] = idx;
  }
  // bottom item shown is (newest - msgScroll)
  int bottom = on - 1 - msgScroll; if (bottom < 0) bottom = 0;
  int top = bottom - VIS + 1; if (top < 0) top = 0;
  int y = rowY0;
  for (int r = top; r <= bottom && y < 120; ++r) {
    LoraMsg& m = msgRing[order[r]];
    canvas.setTextColor(m.mine ? CL_CYAN : CL_GREEN, CL_BLACK);
    canvas.setCursor(2, y);
    canvas.printf("%s%s:", m.mine ? ">" : "", m.mine ? "me" : m.from);
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.printf(" %.30s", m.text);
    if (!m.mine) {                        // signal info, dim, right side
      canvas.setTextColor(CL_DGREY, CL_BLACK);
      canvas.setCursor(2, y); // (rssi shown only if room; keep simple)
    }
    y += rowH;
  }

  footer("n write   ;/. scroll   ` back");
}

void App::keyMessages(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_HOME; lastDrawMs = 0; return; }
  if (!cfg.loraEnable) return;
  if ((!loraStarted || !lora.ready())) {
    if (c == 'r') { loraStart(); lastDrawMs = 0; }
    return;
  }
  if (c == 'n') {                          // compose a new message (reuse SCR_EDIT)
    editTarget = 700; editTitle = "Message"; editBuf = ""; screen = SCR_EDIT; return;
  }
  if (isUp(c))   { if (msgScroll < msgCount - 1) msgScroll++; lastDrawMs = 0; }
  if (isDown(c)) { if (msgScroll > 0) msgScroll--; lastDrawMs = 0; }
}

// ===========================================================================
//  Space weather summary (off Home). Shows the solar 10.7 cm flux and the
//  planetary Kp index fetched from NOAA SWPC (with GP updates), plus a plain
//  reading of what they mean for HF and satellite operation. r = refresh.
// ===========================================================================
void App::drawSpaceWx() {
  header("Space Wx");
  canvas.setTextSize(1);
  int y = 20; const int LH = 11;
  auto row = [&](const char* k, const String& v, uint16_t col) {
    canvas.setTextColor(CL_GREY, CL_BLACK);  canvas.setCursor(6, y);  canvas.print(k);
    canvas.setTextColor(col, CL_BLACK);      canvas.setCursor(104, y); canvas.print(v);
    y += LH;
  };
  if (spaceF107 <= 0 && spaceKp < 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print("No data yet.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 70);
    canvas.print("Update GP or press r (WiFi).");
    footer("` back   r refresh");
    return;
  }
  // --- Solar 10.7 cm flux ---
  if (spaceF107 > 0) {
    const char* sLbl; uint16_t sCol;
    if      (spaceF107 < 90)  { sLbl = "low";       sCol = CL_GREY; }
    else if (spaceF107 < 120) { sLbl = "moderate";  sCol = CL_WHITE; }
    else if (spaceF107 < 160) { sLbl = "good";      sCol = CL_GREEN; }
    else                      { sLbl = "very high"; sCol = CL_YELLOW; }
    row("Solar flux", String(spaceF107, 0) + " sfu", sCol);
    row("  (F10.7)", String(sLbl), sCol);
  } else {
    row("Solar flux", "--", CL_GREY);
  }
  // --- Planetary Kp (geomagnetic) ---
  if (spaceKp >= 0) {
    const char* kLbl; uint16_t kCol;
    if      (spaceKp < 4) { kLbl = "quiet";        kCol = CL_GREEN; }
    else if (spaceKp < 5) { kLbl = "unsettled";    kCol = CL_WHITE; }
    else if (spaceKp < 6) { kLbl = "minor storm";  kCol = CL_YELLOW; }
    else if (spaceKp < 7) { kLbl = "mod. storm";   kCol = CL_YELLOW; }
    else                  { kLbl = "major storm";  kCol = CL_RED; }
    row("Kp index", String(spaceKp, 1) + "  " + kLbl, kCol);
  } else {
    row("Kp index", "--", CL_GREY);
  }
  // --- Running A index (daily-equivalent geomagnetic amplitude), if available ---
  if (spaceA >= 0) {
    const char* aLbl; uint16_t aCol;
    if      (spaceA < 8)  { aLbl = "quiet";     aCol = CL_GREEN; }
    else if (spaceA < 16) { aLbl = "unsettled"; aCol = CL_WHITE; }
    else if (spaceA < 30) { aLbl = "active";    aCol = CL_YELLOW; }
    else                  { aLbl = "storm";     aCol = CL_RED; }
    row("A index", String(spaceA, 0) + "  " + aLbl, aCol);
  }
  y += 3;
  // --- Plain-language operating note ---
  canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(6, y); y += LH;
  canvas.print("Operating outlook:");
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  const char* note;
  if (spaceKp >= 5)
    note = "Geomag storm: aurora & VHF\nflutter likely; HF paths\ndisturbed at high lat.";
  else if (spaceF107 >= 120 && spaceKp >= 0 && spaceKp < 4)
    note = "Strong sun, quiet field:\ngood HF & stable sat passes.";
  else if (spaceF107 > 0 && spaceF107 < 90)
    note = "Weak sun: lower HF MUF;\nsat passes unaffected.";
  else
    note = "Settled conditions; normal\nHF and satellite operation.";
  { int ly = y; const char* p = note;
    while (*p) { const char* nl = strchr(p, '\n'); int len = nl ? (int)(nl - p) : (int)strlen(p);
      String line(""); for (int i = 0; i < len; ++i) line += p[i];
      canvas.setCursor(6, ly); canvas.print(line); ly += 10;
      if (!nl) break; p = nl + 1; }
  }
  // freshness
  if (spaceWxEpoch) {
    long ageH = (long)((nowUtc() - spaceWxEpoch) / 3600);
    canvas.setTextColor(CL_GREY, CL_BLACK);
    String age = (ageH < 1) ? String("<1h old")
               : (ageH < 48) ? (String(ageH) + "h old")
               : (String(ageH / 24) + "d old");
    canvas.setCursor(240 - 2 - (int)age.length() * 6, 116); canvas.print(age);
  }
  footer("` back   r refresh");
}

void App::keySpaceWx(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_HOME; lastDrawMs = 0; return; }
  if (c == 'r') {
    if (!net.connected()) connectWifiCfg();
    if (net.connected()) {
      float beforeF = spaceF107, beforeK = spaceKp;
      fetchSpaceWeather();
      bool got = (spaceF107 > 0 || spaceKp >= 0);
      bool changed = (spaceF107 != beforeF || spaceKp != beforeK);
      setStatus(got ? (changed ? "Space wx updated" : "Space wx unchanged")
                    : "Space wx: fetch failed");
    }
    else setStatus("No WiFi");
    lastDrawMs = 0; return;
  }
}

// ===========================================================================
//  Terrestrial weather screen (off Space Wx, key 'w'). Shows current conditions
//  and a short forecast for the operating site, from Open-Meteo. Refreshes on
//  entry when WiFi is up; otherwise shows the cached values with their age.
// ===========================================================================
void App::drawWeather() {
  header("Weather");
  canvas.setTextSize(1);

  if (wxEpoch == 0) {                       // never fetched and nothing cached
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 50);
    canvas.print("No weather data yet.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 66);
    canvas.print("Press r on WiFi, or run");
    canvas.setCursor(6, 76); canvas.print("Update. Needs a location set.");
    footer("` back  r refresh");
    return;
  }

  // --- current conditions (convert cached values to the selected units) ---
  int y = 20;
  float tNow = wxConvTemp(wxTempNow, wxCachedUnits, cfg.wxUnits);
  float windNow = wxConvWind(wxWindNow, wxCachedUnits, cfg.wxUnits);
  if (tNow > -900) {
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setTextSize(2);
    canvas.setCursor(6, y);
    canvas.printf("%d%s", (int)lround(tNow), wxTempUnit());
    canvas.setTextSize(1);
    // condition text to the right of the big temperature
    canvas.setTextColor(CL_CYAN, CL_BLACK);
    canvas.setCursor(96, y + 3);
    canvas.print(wxCodeText(wxCodeNow));
    y += 20;
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (windNow > -900) {
    canvas.setCursor(6, y);
    canvas.printf("Wind %d %s %s", (int)lround(windNow), wxWindUnit(),
                  windDirName(wxWindDirNow));
  }
  if (wxHumidNow >= 0) {
    canvas.setCursor(150, y);
    canvas.printf("RH %d%%", wxHumidNow);
  }
  y += 14;

  // --- forecast: one row per day ---
  static const char* DOW[] = { "Sun","Mon","Tue","Wed","Thu","Fri","Sat" };
  for (int i = 0; i < wxDayCount; ++i) {
    // day label: "Today" for i==0, else weekday from the per-day epoch
    String lbl;
    if (i == 0) lbl = "Today";
    else {
      time_t d = (time_t)wxDayEpoch[i];
      struct tm tmv; gmtime_r(&d, &tmv);
      lbl = DOW[tmv.tm_wday % 7];
    }
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(6, y);
    canvas.print(lbl);
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(46, y);
    String cond = wxCodeText(wxDayCode[i]);
    if (cond.length() > 12) cond = cond.substring(0, 12);
    canvas.print(cond);
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(150, y);
    canvas.printf("%d/%d", (int)lround(wxConvTemp(wxDayHi[i], wxCachedUnits, cfg.wxUnits)),
                           (int)lround(wxConvTemp(wxDayLo[i], wxCachedUnits, cfg.wxUnits)));
    if (wxDayPop[i] > 0) {
      canvas.setTextColor(wxDayPop[i] >= 50 ? CL_CYAN : CL_GREY, CL_BLACK);
      canvas.setCursor(200, y); canvas.printf("%d%%", wxDayPop[i]);
    }
    y += 11;
  }

  // --- freshness + unit cache note ---
  if (wxEpoch) {
    long ageH = (long)((nowUtc() - wxEpoch) / 3600);
    canvas.setTextColor(CL_GREY, CL_BLACK);
    String age = (ageH < 1) ? String("<1h old")
               : (ageH < 48) ? (String(ageH) + "h old")
               : (String(ageH / 24) + "d old");
    canvas.setCursor(240 - 2 - (int)age.length() * 6, 116); canvas.print(age);
  }
  footer("` back  r refresh");
}

void App::keyWeather(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = SCR_HOME; lastDrawMs = 0; return; }
  if (c == 'r') {
    if (!net.connected()) connectWifiCfg();
    if (net.connected()) {
      const Observer& o = loc.obs();
      if (!(o.lat != 0.0 || o.lon != 0.0)) { setStatus("Set a location first"); }
      else {
        time_t before = wxEpoch;
        fetchWeather();
        setStatus(wxEpoch != before ? "Weather updated" : "Weather: fetch failed");
      }
    } else setStatus("No WiFi");
    lastDrawMs = 0; return;
  }
}

// ===========================================================================
//  Transponder database browser (off Satellites, key 't'). Lists every
//  transponder/beacon entry for the selected satellite from the on-device
//  catalog (SatNOGS-sourced) in an easy-to-scan up/down/mode/tone layout.
// ===========================================================================
static String txMHz(uint32_t hz) {
  if (hz == 0) return String("-");
  char b[16]; snprintf(b, sizeof(b), "%.4f", hz / 1.0e6); return String(b);
}

void App::drawTxDb() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + " TX") : String("Transponders"));
  canvas.setTextSize(1);
  if (!s) { canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
            canvas.print("No satellite."); footer("` back"); return; }
  if (activeTxCount == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 52);
    canvas.print("No transponders cached.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 66);
    canvas.print("Update GP/Freq with WiFi on.");
    footer("` back"); return;
  }
  // Each entry is a 3-line block: name; DOWN + mode; UP + tone/flags.
  const int BLOCK = 3, LH = 10, ROWS = 10;          // 10 text rows in the safe band
  const int perPage = ROWS / BLOCK;                 // 3 entries per page
  if (txDbScroll >= activeTxCount) txDbScroll = 0;
  int y = 19;
  for (int e = txDbScroll; e < activeTxCount && e < txDbScroll + perPage; ++e) {
    const Transponder& t = activeTx[e];
    bool manual = (strncmp(t.desc, "manual", 6) == 0);
    bool sel = (e == txDbSel);
    // line 1: index + description, in cyan (selected entry marked with '>')
    canvas.setTextColor(sel ? CL_GREEN : CL_CYAN, CL_BLACK); canvas.setCursor(4, y);
    { String d = (sel ? ">" : " ") + String(e + 1) + ". " + String(t.desc);
      if (manual) d += " *";
      if (d.length() > 38) d = d.substring(0, 37) + "~";
      canvas.print(d); }
    y += LH;
    // line 2: downlink (range if linear) + mode
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(10, y);
    { String dn = "D " + txMHz(t.downlink);
      if (t.downlinkHigh > t.downlink) dn += "-" + txMHz(t.downlinkHigh);
      dn += " " + String(t.mode);
      canvas.print(dn); }
    y += LH;
    // line 3: uplink (range if linear) + tone / invert flag
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(10, y);
    { String up = "U " + txMHz(t.uplink);
      if (t.uplinkHigh > t.uplink) up += "-" + txMHz(t.uplinkHigh);
      if (t.isLinear) up += t.invert ? " inv" : " lin";
      if (t.toneHz > 0) up += " " + String(t.toneHz, 1) + "Hz";
      canvas.print(up); }
    y += LH + 2;                                     // small gap between entries
  }
  // scrollbar hints + count
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (txDbScroll > 0)                       { canvas.setCursor(232, 19);  canvas.print("^"); }
  if (txDbScroll + perPage < activeTxCount) { canvas.setCursor(232, 112); canvas.print("v"); }
  { bool selManual = (txDbSel < activeTxCount &&
                      strncmp(activeTx[txDbSel].desc, "manual", 6) == 0);
    char cnt[24]; snprintf(cnt, sizeof(cnt), "%d/%d", txDbSel + 1, activeTxCount);
    footer(String("`bk ;/. sel ") + (selManual ? "x del " : "") + cnt + " (* manual)"); }
}

void App::keyTxDb(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { txDbDelArm = false; screen = SCR_SATLIST; lastDrawMs = 0; return; }
  if (activeTxCount == 0) return;
  const int perPage = 3;
  if (isUp(c))   { if (txDbSel > 0) txDbSel--; txDbDelArm = false; }
  if (isDown(c)) { if (txDbSel < activeTxCount - 1) txDbSel++; txDbDelArm = false; }
  // keep the selected entry on-screen (page of `perPage` blocks)
  if (txDbSel < txDbScroll)            txDbScroll = (txDbSel / perPage) * perPage;
  if (txDbSel >= txDbScroll + perPage) txDbScroll = (txDbSel / perPage) * perPage;
  if (c == 'x') {                                  // delete (manual entries only)
    SatEntry* s = activeSat();
    bool isManual = (strncmp(activeTx[txDbSel].desc, "manual", 6) == 0);
    if (!s || !isManual) { setStatus("Only manual entries can be deleted"); return; }
    if (!txDbDelArm) { txDbDelArm = true; setStatus("Press x again to delete"); return; }
    // manual index = count of manual entries before this one
    int mIdx = 0;
    for (int e = 0; e < txDbSel; ++e)
      if (strncmp(activeTx[e].desc, "manual", 6) == 0) mIdx++;
    txDbDelArm = false;
    if (deleteManualTx(s->norad, mIdx)) {
      ensureTransponders(*s);                      // reload the list
      if (txDbSel >= activeTxCount) txDbSel = activeTxCount > 0 ? activeTxCount - 1 : 0;
      onTransponderChanged();
      setStatus("Transponder deleted");
    } else setStatus("Delete failed");
    lastDrawMs = 0; return;
  }
  lastDrawMs = 0;
}

// ===========================================================================
//  QRZ.com callsign lookup (off Home). Uses the QRZ XML subscription service:
//  log in with the user's QRZ credentials to get a session key, then query a
//  callsign. Requires WiFi and a QRZ XML-data subscription. API spec:
//  https://www.qrz.com/page/current_spec.html
// ===========================================================================
static String qrzTag(const String& xml, const char* tag) {
  String open = String("<") + tag + ">";
  String close = String("</") + tag + ">";
  int a = xml.indexOf(open); if (a < 0) return String("");
  a += open.length();
  int b = xml.indexOf(close, a); if (b < 0) return String("");
  return xml.substring(a, b);
}

static String qrzUrlEnc(const String& s) {
  String o; const char* hex = "0123456789ABCDEF";
  for (size_t i = 0; i < s.length(); ++i) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') o += c;
    else { o += '%'; o += hex[(c >> 4) & 0xF]; o += hex[c & 0xF]; }
  }
  return o;
}

// Perform a lookup. On success fills the qrz* result fields and returns true.
// On failure returns false and sets err to a user-facing message.
bool App::qrzLookup(const String& call, String& err) {
  const char* base = "https://xmldata.qrz.com/xml/current/";
  // 1. Ensure we have a session key (log in if needed).
  if (qrzSessionKey.length() == 0) {
    String url = String(base) + "?username=" + qrzUrlEnc(cfg.qrzUser) +
                 ";password=" + qrzUrlEnc(cfg.qrzPass) + ";agent=CardSat";
    String body;
    if (!net.httpsGet(url, body, 8000)) { err = "QRZ login: no response"; return false; }
    qrzSessionKey = qrzTag(body, "Key");
    if (qrzSessionKey.length() == 0) {
      String e = qrzTag(body, "Error");
      err = e.length() ? e : "QRZ login failed";
      return false;
    }
  }
  // 2. Query the callsign with the session key.
  String url = String(base) + "?s=" + qrzSessionKey + ";callsign=" + qrzUrlEnc(call);
  String body;
  if (!net.httpsGet(url, body, 16000)) { err = "QRZ query: no response"; return false; }
  // If the key expired, the response carries an Error and no Callsign node; retry once.
  if (body.indexOf("<Callsign>") < 0) {
    String e = qrzTag(body, "Error");
    if (e.indexOf("Session") >= 0 || e.indexOf("Timeout") >= 0 || e.indexOf("invalid") >= 0) {
      qrzSessionKey = "";                       // force re-login and retry
      String url2 = String(base) + "?username=" + qrzUrlEnc(cfg.qrzUser) +
                    ";password=" + qrzUrlEnc(cfg.qrzPass) + ";agent=CardSat";
      String lb;
      if (net.httpsGet(url2, lb, 8000)) qrzSessionKey = qrzTag(lb, "Key");
      if (qrzSessionKey.length()) {
        String q2 = String(base) + "?s=" + qrzSessionKey + ";callsign=" + qrzUrlEnc(call);
        net.httpsGet(q2, body, 16000);
      }
    }
    if (body.indexOf("<Callsign>") < 0) {
      err = e.length() ? e : "Not found";
      return false;
    }
  }
  // 3. Parse the fields we display.
  String fn = qrzTag(body, "fname"), ln = qrzTag(body, "name");
  qrzName = (fn + " " + ln); qrzName.trim();
  if (qrzName.length() == 0) qrzName = qrzTag(body, "name_fmt");
  String a1 = qrzTag(body, "addr1"), a2 = qrzTag(body, "addr2"),
         st = qrzTag(body, "state"), zip = qrzTag(body, "zip");
  qrzAddr = a1;
  { String l2 = a2; if (st.length()) l2 += (l2.length() ? ", " : "") + st;
    if (zip.length()) l2 += " " + zip;
    if (l2.length()) qrzAddr += (qrzAddr.length() ? "\n" : "") + l2; }
  qrzCountry = qrzTag(body, "country");
  qrzGrid    = qrzTag(body, "grid");
  qrzClass   = qrzTag(body, "class");
  qrzCall    = qrzTag(body, "call"); if (qrzCall.length() == 0) qrzCall = call;
  qrzCall.toUpperCase();
  return true;
}

void App::drawQrz() {
  header("QRZ Lookup");
  canvas.setTextSize(1);
  // No WiFi: just say so.
  if (!net.connected()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 50);
    canvas.print("WiFi not connected.");
    canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(6, 66);
    canvas.print("Connect WiFi (Update or");
    canvas.setCursor(6, 76); canvas.print("Settings) and try again.");
    footer("` back");
    return;
  }
  // No credentials: explain the subscription requirement.
  if (cfg.qrzUser[0] == 0 || cfg.qrzPass[0] == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 40);
    canvas.print("QRZ credentials needed.");
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(6, 58);
    canvas.print("Requires a QRZ XML-data");
    canvas.setCursor(6, 68); canvas.print("subscription. Enter your QRZ");
    canvas.setCursor(6, 78); canvas.print("username & password in");
    canvas.setCursor(6, 88); canvas.print("Settings > Network / data.");
    footer("` back");
    return;
  }
  // Result, or a prompt to enter a callsign.
  if (!qrzHaveResult) {
    canvas.setTextColor(CL_WHITE, CL_BLACK); canvas.setCursor(6, 50);
    canvas.print("Press ENTER to enter a");
    canvas.setCursor(6, 60); canvas.print("callsign to look up.");
    footer("ENTER callsign   ` back");
    return;
  }
  int y = 20;
  canvas.setTextColor(CL_CYAN, CL_BLACK); canvas.setCursor(6, y);
  canvas.print(qrzCall); y += 12;
  auto line = [&](const String& s, uint16_t col) {
    if (s.length() == 0) return;
    canvas.setTextColor(col, CL_BLACK); canvas.setCursor(6, y);
    String t = s; if (t.length() > 38) t = t.substring(0, 37) + "~";
    canvas.print(t); y += 10;
  };
  line(qrzName, CL_WHITE);
  // address may contain a newline (two lines)
  { String a = qrzAddr; int nl = a.indexOf('\n');
    if (nl >= 0) { line(a.substring(0, nl), CL_WHITE); line(a.substring(nl + 1), CL_WHITE); }
    else line(a, CL_WHITE); }
  line(qrzCountry, CL_GREY);
  { String extra; if (qrzGrid.length()) extra = "Grid " + qrzGrid;
    if (qrzClass.length()) extra += (extra.length() ? "   " : "") + String("Class ") + qrzClass;
    line(extra, CL_GREEN); }
  footer("ENTER new   ` back");
}

void App::keyQrz(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; lastDrawMs = 0; return; }
  if (!net.connected()) return;
  if (cfg.qrzUser[0] == 0 || cfg.qrzPass[0] == 0) return;
  if (enter) {                                  // open the text editor for a callsign
    editTarget = 216; editTitle = "Callsign to look up";
    editBuf = ""; screen = SCR_EDIT; lastDrawMs = 0; return;
  }
}

// ===========================================================================
//  Workable grid squares: 4-char Maidenhead grids under the satellite footprint.
//  Off Passes (union over the selected pass) and off Track (live "now"). The
//  footprint half-angle is acos(Re/(Re+h)); a grid counts if its centre is
//  within that great-circle radius of the sub-point. A per-grid bitset is used
//  so it handles any altitude (whole-Earth = 32400 grids) with no cap.
// ===========================================================================
// 4-char Maidenhead <-> packed index 0..32399. Index order matches the
// alphabetical grid string, so iterating set bits yields sorted output.
static inline int gridIdx(double lat, double lon) {
  while (lon < -180) lon += 360; while (lon >= 180) lon -= 360;
  if (lat < -90) lat = -90; if (lat > 89.9999) lat = 89.9999;
  double L = lon + 180.0, B = lat + 90.0;
  int fLon = (int)(L / 20.0), fLat = (int)(B / 10.0);
  int sLon = (int)((L - fLon * 20) / 2.0), sLat = (int)(B - fLat * 10);
  return ((fLon * 18 + fLat) * 10 + sLon) * 10 + sLat;     // 0..32399
}
static void gridStr(int idx, char* out) {
  int sLat = idx % 10; idx /= 10; int sLon = idx % 10; idx /= 10;
  int fLat = idx % 18; idx /= 18; int fLon = idx;
  out[0] = 'A' + fLon; out[1] = 'A' + fLat;
  out[2] = '0' + sLon; out[3] = '0' + sLat; out[4] = 0;
}

void App::addFootprintGrids(double subLat, double subLon, double altKm) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232, Re = 6371.0;
  if (altKm < 1) return;
  double coslam = Re / (Re + altKm);                      // = cos(footprint half-angle)
  double lamDeg = acos(coslam) * R2D;
  double sinSub = sin(subLat * D2R), cosSub = cos(subLat * D2R);
  int latLo = (int)floor(subLat - lamDeg), latHi = (int)ceil(subLat + lamDeg);
  for (int la = latLo; la <= latHi; ++la) {
    if (la < -90 || la >= 90) continue;
    double clat = la + 0.5, clatR = clat * D2R;
    double cl = cos(clatR); if (cl < 0.15) cl = 0.15;
    double lonHalf = lamDeg / cl + 2.0;
    int lonLo = (int)floor((subLon - lonHalf) / 2.0) * 2;
    int lonHi = (int)ceil((subLon + lonHalf) / 2.0) * 2;
    double A = sin(clatR) * sinSub, B = cos(clatR) * cosSub;
    for (int lo = lonLo; lo <= lonHi; lo += 2) {
      double c = lo + 1.0;                                 // grid centre longitude
      if (A + B * cos((c - subLon) * D2R) < coslam) continue;   // outside footprint
      int idx = gridIdx(clat, c);
      gridBits[idx >> 3] |= (uint8_t)(1 << (idx & 7));
    }
  }
}

void App::buildGrids(time_t a, time_t b) {
  memset(gridBits, 0, sizeof(gridBits));
  SatEntry* s = activeSat();
  if (!s || !timeIsSet()) { gridN = 0; return; }
  pred.setSite(loc.obs()); pred.setSat(*s);
  int samples = (b > a) ? 1 + (int)((b - a) / 60) : 1;     // ~1 sample/min over the pass
  if (samples > 90) samples = 90; if (samples < 1) samples = 1;
  for (int k = 0; k < samples; ++k) {
    time_t t = (samples > 1) ? a + (time_t)((double)(b - a) * k / (samples - 1)) : a;
    LiveLook L = pred.look(t);
    addFootprintGrids(L.subLat, L.subLon, L.satAltKm);
  }
  int cnt = 0;                                             // popcount the bitset
  for (size_t i = 0; i < sizeof(gridBits); ++i)
    for (uint8_t v = gridBits[i]; v; v &= (uint8_t)(v - 1)) ++cnt;
  gridN = cnt;
}

void App::drawGrid() {
  if (gridLive) {                                  // live: refresh every ~3 s (grids
    uint32_t ms = millis();                        // change slowly; SGP4 + dedup are
    if (!gridBuiltMs || ms - gridBuiltMs > 3000) { // not worth paying every redraw)
      gridBuiltMs = ms; buildGrids(nowUtc(), nowUtc());
    }
  }
  SatEntry* s = activeSat();
  { String h = (s ? String(s->name) : String("Grids")) +
               (gridLive ? " now" : " pass");
    header(h); }
  canvas.setTextSize(1);
  if (gridN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print(timeIsSet() ? "No grids in footprint." : "Clock not set.");
    footer("` back"); return;
  }
  // Workable-grid count on its own line (no room in the header). Right-aligned,
  // and shows the scroll window position when the list spills past one page.
  const int COLS = 6, ROWS = 8, PER = COLS * ROWS;
  if (gridScroll >= gridN) gridScroll = 0;
  { char cnt[40];
    if (gridN > PER) {
      int from = gridScroll + 1, to = gridScroll + PER; if (to > gridN) to = gridN;
      snprintf(cnt, sizeof(cnt), "%d workable  (%d-%d)", gridN, from, to);
    } else {
      snprintf(cnt, sizeof(cnt), "%d workable", gridN);
    }
    canvas.setTextColor(CL_CYAN, CL_BLACK);
    int w = (int)strlen(cnt) * 6; int x = 238 - w; if (x < 4) x = 4;
    canvas.setCursor(x, 19); canvas.print(cnt);
  }
  int seen = 0, drawn = 0;                          // walk set bits in sorted order
  for (int idx = 0; idx < 32400 && drawn < PER; ++idx) {
    if (!(gridBits[idx >> 3] & (1 << (idx & 7)))) continue;
    if (seen++ < gridScroll) continue;
    char g[5]; gridStr(idx, g);
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4 + (drawn % COLS) * 40, 31 + (drawn / COLS) * 11);
    canvas.print(g); ++drawn;
  }
  footer(gridN > PER ? "` bk  ;/. scroll  {} page" : "` back");
}

void App::keyGrid(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = gridLive ? liveReturn : SCR_PASSES; lastDrawMs = 0; return; }
  const int PER = 6 * 8;
  if (isDown(c)) { if (gridScroll + PER < gridN) gridScroll += 6; lastDrawMs = 0; return; }
  if (isUp(c))   { if (gridScroll >= 6) gridScroll -= 6; lastDrawMs = 0; return; }
  if (c == '}')  { if (gridScroll + PER < gridN) gridScroll += PER; lastDrawMs = 0; return; }
  if (c == '{')  { gridScroll -= PER; if (gridScroll < 0) gridScroll = 0; lastDrawMs = 0; return; }
}

// ===========================================================================
//  Workable US states/DC: which states fall under the satellite footprint.
//  Mirrors the workable-grids feature exactly (same footprint walk and UI);
//  the only difference is the per-point lookup -- a point-in-polygon test
//  against bundled simplified state boundaries instead of Maidenhead math.
//  Boundaries are coarse (~0.1 deg); a footprint grazing a border may claim
//  both neighbours, which is acceptable at footprint scale.
// ===========================================================================
// Simplified boundary polygons. Encoding: int16_t (lon*10, lat*10) pairs;
// 32767,32767 separates entities. Order matches STATE_CODE (alphabetical),
// so iterating set bits yields sorted output.
static const int16_t STATEPOLY[] = {
  /*AK*/ -1680,650, -1600,700, -1450,700, -1410,690, -1410,600, -1300,560, -1350,580, -1500,590, -1580,560, -1650,540, -1680,600, -1680,650, 32767,32767,
  /*AL*/ -882,350, -856,350, -852,329, -850,320, -851,310, -876,302, -884,302, -881,319, -882,350, 32767,32767,
  /*AR*/ -946,365, -902,365, -901,350, -912,330, -945,330, -946,365, 32767,32767,
  /*AZ*/ -1148,370, -1090,370, -1090,313, -1111,313, -1148,325, -1146,330, -1148,370, 32767,32767,
  /*CA*/ -1244,420, -1200,420, -1200,390, -1146,350, -1146,325, -1171,325, -1185,340, -1205,345, -1224,372, -1244,400, -1244,420, 32767,32767,
  /*CO*/ -1090,410, -1020,410, -1020,370, -1090,370, -1090,410, 32767,32767,
  /*CT*/ -737,420, -718,420, -718,413, -729,410, -737,410, -737,420, 32767,32767,
  /*DC*/ -771,389, -769,390, -769,388, -770,388, -771,389, 32767,32767,
  /*DE*/ -758,398, -750,398, -750,384, -756,384, -758,398, 32767,32767,
  /*FL*/ -876,304, -850,307, -820,307, -814,307, -800,268, -801,252, -811,251, -828,278, -840,301, -876,304, 32767,32767,
  /*GA*/ -856,350, -831,350, -810,321, -808,307, -830,306, -851,310, -856,350, 32767,32767,
  /*HI*/ -1603,219, -1593,222, -1578,213, -1560,208, -1548,195, -1557,189, -1567,205, -1583,213, -1603,219, 32767,32767,
  /*IA*/ -966,435, -912,435, -901,425, -911,406, -958,406, -966,427, -966,435, 32767,32767,
  /*ID*/ -1172,490, -1160,490, -1160,470, -1140,466, -1130,456, -1110,445, -1110,420, -1170,420, -1172,443, -1169,456, -1172,490, 32767,32767,
  /*IL*/ -915,425, -875,425, -875,410, -870,380, -880,370, -895,370, -906,389, -915,402, -906,425, -915,425, 32767,32767,
  /*IN*/ -875,418, -848,417, -848,391, -860,380, -875,383, -875,418, 32767,32767,
  /*KS*/ -1020,400, -946,400, -946,370, -1020,370, -1020,400, 32767,32767,
  /*KY*/ -895,365, -820,386, -826,372, -840,366, -880,365, -895,365, 32767,32767,
  /*LA*/ -940,330, -910,330, -892,302, -890,290, -913,293, -938,297, -940,330, 32767,32767,
  /*MA*/ -735,428, -709,429, -700,418, -710,415, -718,413, -735,420, -735,428, 32767,32767,
  /*MD*/ -795,397, -758,397, -750,380, -760,379, -770,384, -775,393, -795,393, -795,397, 32767,32767,
  /*ME*/ -711,453, -707,460, -692,475, -680,473, -670,457, -672,445, -700,431, -708,433, -711,453, 32767,32767,
  /*MI*/ -904,466, -870,458, -840,465, -834,450, -824,430, -832,417, -868,417, -865,440, -850,458, -880,460, -904,466, 32767,32767,
  /*MN*/ -972,490, -952,494, -900,481, -920,467, -912,435, -965,435, -966,453, -972,490, 32767,32767,
  /*MO*/ -958,406, -912,406, -901,380, -895,370, -895,360, -946,365, -946,400, -958,406, 32767,32767,
  /*MS*/ -916,350, -881,350, -884,302, -896,302, -916,310, -903,330, -916,350, 32767,32767,
  /*MT*/ -1160,490, -1040,490, -1040,450, -1110,450, -1110,445, -1130,456, -1140,466, -1160,470, -1160,490, 32767,32767,
  /*NC*/ -843,366, -755,366, -758,352, -780,339, -797,348, -843,350, -843,366, 32767,32767,
  /*ND*/ -1040,490, -972,490, -965,460, -1040,460, -1040,490, 32767,32767,
  /*NE*/ -1040,430, -985,430, -964,425, -953,400, -1020,400, -1040,410, -1040,430, 32767,32767,
  /*NH*/ -726,453, -715,453, -707,436, -708,431, -725,427, -726,453, 32767,32767,
  /*NJ*/ -756,414, -739,410, -740,404, -744,394, -749,389, -756,396, -751,406, -756,414, 32767,32767,
  /*NM*/ -1090,370, -1030,370, -1030,320, -1066,320, -1082,318, -1090,313, -1090,370, 32767,32767,
  /*NV*/ -1200,420, -1140,420, -1140,362, -1147,360, -1146,350, -1156,358, -1170,387, -1200,390, -1200,420, 32767,32767,
  /*NY*/ -798,423, -790,433, -765,440, -733,450, -733,436, -735,420, -747,410, -740,405, -720,410, -739,410, -754,420, -798,420, -798,423, 32767,32767,
  /*OH*/ -848,417, -805,419, -805,406, -820,384, -848,391, -848,417, 32767,32767,
  /*OK*/ -1030,370, -944,370, -944,336, -960,337, -990,340, -1000,346, -1030,365, -1030,370, 32767,32767,
  /*OR*/ -1246,462, -1232,460, -1190,460, -1170,460, -1170,420, -1244,420, -1246,462, 32767,32767,
  /*PA*/ -805,420, -798,420, -754,420, -747,414, -750,400, -760,397, -805,397, -805,420, 32767,32767,
  /*RI*/ -719,420, -711,420, -711,414, -715,413, -719,416, -719,420, 32767,32767,
  /*SC*/ -834,352, -800,348, -785,339, -792,330, -815,320, -833,345, -834,352, 32767,32767,
  /*SD*/ -1040,460, -965,460, -964,425, -985,430, -1040,430, -1040,460, 32767,32767,
  /*TN*/ -903,365, -817,366, -837,353, -882,350, -903,350, -903,365, 32767,32767,
  /*TX*/ -1066,320, -1030,320, -1030,365, -1000,365, -1000,346, -960,337, -940,336, -935,300, -970,260, -992,264, -1014,298, -1030,290, -1049,306, -1065,318, -1066,320, 32767,32767,
  /*UT*/ -1140,420, -1110,420, -1110,410, -1090,410, -1090,370, -1140,370, -1140,420, 32767,32767,
  /*VA*/ -837,366, -752,380, -760,370, -770,366, -800,366, -837,366, 32767,32767,
  /*VT*/ -734,450, -715,450, -720,427, -733,428, -734,450, 32767,32767,
  /*WA*/ -1247,484, -1230,482, -1220,490, -1170,490, -1170,460, -1190,460, -1232,460, -1246,463, -1247,484, 32767,32767,
  /*WI*/ -929,454, -904,466, -880,460, -870,454, -878,440, -870,425, -906,425, -929,435, -929,454, 32767,32767,
  /*WV*/ -826,382, -805,406, -795,397, -777,393, -790,385, -817,372, -826,382, 32767,32767,
  /*WY*/ -1110,450, -1040,450, -1040,410, -1110,410, -1110,450, 32767,32767,
};
static const char STATE_CODE[] = "AKALARAZCACOCTDCDEFLGAHIIAIDILINKSKYLAMAMDMEMIMNMOMSMTNCNDNENHNJNMNVNYOHOKORPARISCSDTNTXUTVAVTWAWIWVWY";   // 51 entities x 2 chars
static const int STATE_N = (int)(sizeof(STATE_CODE) - 1) / 2;   // 51

// --- fast-path index tables (start offset + bbox per state polygon) ---
// Precomputed per-entity start offset (int16 index) + lon/lat bbox (lon*10,
// lat*10) for fast footprint rejection. Matches STATEPOLY/STATEPOLY_CODE order.
static const uint16_t STATEPOLY_START[] = {
  0, 26, 46, 60, 76, 100, 112, 126, 138, 150, 172, 188, 208, 224, 248, 270,
  284, 296, 310, 326, 342, 360, 380, 404, 422, 440, 456, 476, 492, 504, 520, 534,
  552, 568, 588, 616, 630, 648, 664, 682, 696, 712, 726, 740, 772, 788, 802, 814,
  834, 854, 870,
};
static const int16_t STATEPOLY_LOMIN[] = {
  -1680, -884, -946, -1148, -1244, -1090, -737, -771, -758, -876, -856, -1603, -966, -1172, -915, -875,
  -1020, -895, -940, -735, -795, -711, -904, -972, -958, -916, -1160, -843, -1040, -1040, -726, -756,
  -1090, -1200, -798, -848, -1030, -1246, -805, -719, -834, -1040, -903, -1066, -1140, -837, -734, -1247,
  -929, -826, -1110,
};
static const int16_t STATEPOLY_LOMAX[] = {
  -1300, -850, -901, -1090, -1146, -1020, -718, -769, -750, -800, -808, -1548, -901, -1110, -870, -848,
  -946, -820, -890, -700, -750, -670, -824, -900, -895, -881, -1040, -755, -965, -953, -707, -739,
  -1030, -1140, -720, -805, -944, -1170, -747, -711, -785, -964, -817, -935, -1090, -752, -715, -1170,
  -870, -777, -1040,
};
static const int16_t STATEPOLY_LAMIN[] = {
  540, 302, 330, 313, 325, 370, 410, 388, 384, 251, 306, 189, 406, 420, 370, 380,
  370, 365, 290, 413, 379, 431, 417, 435, 360, 302, 445, 339, 460, 400, 427, 389,
  313, 350, 405, 384, 336, 420, 397, 413, 320, 425, 350, 260, 370, 366, 427, 460,
  425, 372, 410,
};
static const int16_t STATEPOLY_LAMAX[] = {
  700, 350, 365, 370, 420, 410, 420, 390, 398, 307, 350, 222, 435, 490, 425, 418,
  400, 386, 330, 429, 397, 475, 466, 494, 406, 350, 490, 366, 490, 430, 453, 414,
  370, 420, 450, 419, 370, 462, 420, 420, 352, 460, 366, 365, 420, 380, 450, 490,
  466, 406, 450,
};

// Point-in-polygon for a single state polygon starting at int16 offset s0.
static bool statePipAt(double lon, double lat, int s0) {
  bool inside = false;
  int i = s0; double px = 0, py = 0; bool have = false;
  double x0 = 0, y0 = 0; bool first = true;
  int n = (int)(sizeof(STATEPOLY)/sizeof(STATEPOLY[0]));
  while (i + 1 < n) {
    int a = STATEPOLY[i], b = STATEPOLY[i + 1];
    if (a == 32767 && b == 32767) break;
    double cx = a / 10.0, cy = b / 10.0;
    if (first) { x0 = cx; y0 = cy; first = false; }
    if (have) {
      if (((py > lat) != (cy > lat)) &&
          (lon < (px - cx) * (lat - cy) / (py - cy) + cx)) inside = !inside;
    }
    px = cx; py = cy; have = true; i += 2;
  }
  if (have) {
    if (((py > lat) != (y0 > lat)) &&
        (lon < (px - x0) * (lat - y0) / (py - y0) + x0)) inside = !inside;
  }
  return inside;
}

void App::addFootprintStates(double subLat, double subLon, double altKm) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232, Re = 6371.0;
  if (altKm < 1) return;
  double coslam = Re / (Re + altKm);                     // cos(footprint half-angle)
  double lamDeg = acos(coslam) * R2D;
  double sinSub = sin(subLat * D2R), cosSub = cos(subLat * D2R);
  // Walk a coarse lat/lon mesh over the footprint; each mesh point is rejected
  // against every state's bounding box before any ray-cast, and states already
  // found are skipped. The mesh step (1 deg) is fine vs the ~0.1 deg boundaries.
  int latLo = (int)floor(subLat - lamDeg), latHi = (int)ceil(subLat + lamDeg);
  for (int la = latLo; la <= latHi; ++la) {
    if (la < -90 || la >= 90) continue;
    double clatR = (la + 0.5) * D2R;
    double cl = cos(clatR); if (cl < 0.15) cl = 0.15;
    double lonHalf = lamDeg / cl + 2.0;
    int lonLo = (int)floor(subLon - lonHalf), lonHi = (int)ceil(subLon + lonHalf);
    double A = sin(clatR) * sinSub, B = cos(clatR) * cosSub;
    for (int lo = lonLo; lo <= lonHi; ++lo) {
      double clon = lo + 0.5;
      if (A + B * cos((clon - subLon) * D2R) < coslam) continue;  // outside footprint
      int16_t qlo = (int16_t)lround(clon * 10.0), qla = (int16_t)lround((la + 0.5) * 10.0);
      for (int idx = 0; idx < STATE_N; ++idx) {
        if (stateBits[idx >> 3] & (1 << (idx & 7))) continue;        // already found
        if (qlo < STATEPOLY_LOMIN[idx] || qlo > STATEPOLY_LOMAX[idx] ||
            qla < STATEPOLY_LAMIN[idx] || qla > STATEPOLY_LAMAX[idx]) continue;  // bbox reject
        if (statePipAt(clon, la + 0.5, STATEPOLY_START[idx]))
          stateBits[idx >> 3] |= (uint8_t)(1 << (idx & 7));
      }
    }
  }
}

void App::buildStates(time_t a, time_t b) {
  memset(stateBits, 0, sizeof(stateBits));
  SatEntry* s = activeSat();
  if (!s || !timeIsSet()) { stateN = 0; return; }
  pred.setSite(loc.obs()); pred.setSat(*s);
  int samples = (b > a) ? 1 + (int)((b - a) / 60) : 1;
  if (samples > 90) samples = 90; if (samples < 1) samples = 1;
  for (int k = 0; k < samples; ++k) {
    time_t t = (samples > 1) ? a + (time_t)((double)(b - a) * k / (samples - 1)) : a;
    LiveLook L = pred.look(t);
    addFootprintStates(L.subLat, L.subLon, L.satAltKm);
  }
  int cnt = 0;
  for (size_t i = 0; i < sizeof(stateBits); ++i)
    for (uint8_t v = stateBits[i]; v; v &= (uint8_t)(v - 1)) ++cnt;
  stateN = cnt;
}

void App::drawStates() {
  if (stateLive) {
    uint32_t ms = millis();
    if (!stateBuiltMs || ms - stateBuiltMs > 3000) {
      stateBuiltMs = ms; buildStates(nowUtc(), nowUtc());
    }
  }
  SatEntry* s = activeSat();
  { String h = (s ? String(s->name) : String("States")) +
               (stateLive ? " now" : " pass");
    header(h); }
  canvas.setTextSize(1);
  if (stateN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print(timeIsSet() ? "No US states in footprint." : "Clock not set.");
    footer("` back"); return;
  }
  const int COLS = 6, ROWS = 8, PER = COLS * ROWS;
  if (stateScroll >= stateN) stateScroll = 0;
  { char cnt[40];
    if (stateN > PER) {
      int from = stateScroll + 1, to = stateScroll + PER; if (to > stateN) to = stateN;
      snprintf(cnt, sizeof(cnt), "%d workable  (%d-%d)", stateN, from, to);
    } else {
      snprintf(cnt, sizeof(cnt), "%d workable", stateN);
    }
    canvas.setTextColor(CL_CYAN, CL_BLACK);
    int w = (int)strlen(cnt) * 6; int x = 238 - w; if (x < 4) x = 4;
    canvas.setCursor(x, 19); canvas.print(cnt);
  }
  int seen = 0, drawn = 0;
  for (int idx = 0; idx < STATE_N && drawn < PER; ++idx) {
    if (!(stateBits[idx >> 3] & (1 << (idx & 7)))) continue;
    if (seen++ < stateScroll) continue;
    char g[3]; g[0] = STATE_CODE[idx * 2]; g[1] = STATE_CODE[idx * 2 + 1]; g[2] = 0;
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4 + (drawn % COLS) * 40, 31 + (drawn / COLS) * 11);
    canvas.print(g); ++drawn;
  }
  footer(stateN > PER ? "` bk  ;/. scroll  {} page" : "` back");
}

void App::keyStates(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = stateLive ? liveReturn : SCR_PASSES; lastDrawMs = 0; return; }
  const int PER = 6 * 8;
  if (isDown(c)) { if (stateScroll + PER < stateN) stateScroll += 6; lastDrawMs = 0; return; }
  if (isUp(c))   { if (stateScroll >= 6) stateScroll -= 6; lastDrawMs = 0; return; }
  if (c == '}')  { if (stateScroll + PER < stateN) stateScroll += PER; lastDrawMs = 0; return; }
  if (c == '{')  { stateScroll -= PER; if (stateScroll < 0) stateScroll = 0; lastDrawMs = 0; return; }
}

// ===========================================================================
//  Workable DXCC entities (full ~340-entity list, hybrid model):
//   - Major countries as simplified polygons (DXCCPOLY) - precise borders.
//   - The long tail of islands/micro-entities as points (DXCCPT) from cty.dat
//     - workable when the representative point is within the footprint plus a
//     small claim radius. Indices 0..DXCCPOLY_N-1 are polygons; the next
//     DXCCPT_N indices are points. Combined into one 43-byte bitset.
// ===========================================================================
// Simplified boundary polygons for the major DXCC country entities.
// int16_t (lon*10, lat*10) pairs; 32767,32767 separates entities; order
// matches DXCCPOLY_CODE. These are the large landmasses; smaller entities
// are handled as points (see DXCCPT). Coarse borders: a footprint near a
// border may also list the neighbour.
static const int16_t DXCCPOLY[] = {
  /*3D2*/ 1765,-193, 1795,-193, 1795,-163, 1765,-163, 1765,-193, 32767,32767,
  /*3V*/ 77,320, 113,320, 113,370, 77,370, 77,320, 32767,32767,
  /*3W*/ 1040,95, 1090,95, 1090,235, 1040,235, 1040,95, 32767,32767,
  /*4J*/ 465,391, 495,391, 495,415, 465,415, 465,391, 32767,32767,
  /*4L*/ 415,410, 455,410, 455,430, 415,430, 415,410, 32767,32767,
  /*4O*/ 187,423, 199,423, 199,433, 187,433, 187,423, 32767,32767,
  /*4S*/ 794,61, 820,61, 820,95, 794,95, 794,61, 32767,32767,
  /*4X*/ 343,300, 357,300, 357,330, 343,330, 343,300, 32767,32767,
  /*5A*/ 100,330, 250,320, 250,220, 100,240, 100,330, 32767,32767,
  /*5H*/ 310,-110, 390,-110, 390,-10, 310,-10, 310,-110, 32767,32767,
  /*5N*/ 27,64, 70,43, 85,45, 95,65, 140,115, 145,130, 130,135, 35,135, 27,110, 27,64, 32767,32767,
  /*5R*/ 435,-260, 505,-260, 505,-120, 435,-120, 435,-260, 32767,32767,
  /*5W*/ -1726,-143, -1714,-143, -1714,-133, -1726,-133, -1726,-143, 32767,32767,
  /*5Z*/ 345,-30, 415,-30, 415,40, 345,40, 345,-30, 32767,32767,
  /*6W*/ -162,130, -128,130, -128,160, -162,160, -162,130, 32767,32767,
  /*7O*/ 430,130, 510,130, 510,180, 430,180, 430,130, 32767,32767,
  /*7Q*/ 328,-175, 358,-175, 358,-95, 328,-95, 328,-175, 32767,32767,
  /*7X*/ -20,350, 80,370, 90,320, 30,240, -50,280, -20,350, 32767,32767,
  /*8P*/ -599,128, -591,128, -591,136, -599,136, -599,128, 32767,32767,
  /*8R*/ -604,35, -574,35, -574,65, -604,65, -604,35, 32767,32767,
  /*9A*/ 139,437, 189,437, 189,471, 139,471, 139,437, 32767,32767,
  /*9G*/ -30,43, 6,43, 6,113, -30,113, -30,43, 32767,32767,
  /*9K*/ 468,285, 488,285, 488,301, 468,301, 468,285, 32767,32767,
  /*9L*/ -128,73, -108,73, -108,97, -128,97, -128,73, 32767,32767,
  /*9M2*/ 985,10, 1045,10, 1045,60, 985,60, 985,10, 32767,32767,
  /*9M6*/ 1110,20, 1190,20, 1190,70, 1110,70, 1110,20, 32767,32767,
  /*9N*/ 805,267, 875,267, 875,297, 805,297, 805,267, 32767,32767,
  /*9Q*/ 140,-90, 320,-90, 320,50, 140,50, 140,-90, 32767,32767,
  /*9V*/ 1036,12, 1040,12, 1040,16, 1036,16, 1036,12, 32767,32767,
  /*9Y*/ -620,97, -604,97, -604,113, -620,113, -620,97, 32767,32767,
  /*A2*/ 200,-260, 290,-260, 290,-180, 200,-180, 200,-260, 32767,32767,
  /*A3*/ -1757,-220, -1747,-220, -1747,-204, -1757,-204, -1757,-220, 32767,32767,
  /*A4*/ 535,185, 595,185, 595,245, 535,245, 535,185, 32767,32767,
  /*A5*/ 885,265, 925,265, 925,285, 885,285, 885,265, 32767,32767,
  /*A6*/ 525,227, 565,227, 565,253, 525,253, 525,227, 32767,32767,
  /*A7*/ 508,247, 516,247, 516,259, 508,259, 508,247, 32767,32767,
  /*A9*/ 502,258, 508,258, 508,264, 502,264, 502,258, 32767,32767,
  /*AP*/ 610,250, 750,370, 770,350, 710,280, 670,240, 610,250, 32767,32767,
  /*BV*/ 1202,224, 1218,224, 1218,250, 1202,250, 1202,224, 32767,32767,
  /*BY*/ 750,400, 900,470, 1200,500, 1320,470, 1250,420, 1225,405, 1220,370, 1210,320, 1220,305, 1170,240, 1100,210, 1080,185, 980,220, 850,280, 790,320, 750,400, 32767,32767,
  /*C9*/ 320,-240, 390,-240, 390,-120, 320,-120, 320,-240, 32767,32767,
  /*CE*/ -700,-184, -670,-220, -680,-370, -735,-450, -757,-500, -710,-530, -720,-400, -715,-300, -700,-184, 32767,32767,
  /*CM*/ -849,219, -800,232, -741,201, -777,199, -820,215, -849,219, 32767,32767,
  /*CN*/ -110,285, -30,285, -30,355, -110,355, -110,285, 32767,32767,
  /*CP*/ -690,-210, -600,-210, -600,-130, -690,-130, -690,-210, 32767,32767,
  /*CT*/ -93,372, -67,372, -67,422, -93,422, -93,372, 32767,32767,
  /*CT3*/ -174,323, -164,323, -164,331, -174,331, -174,323, 32767,32767,
  /*CU*/ -310,375, -250,375, -250,395, -310,395, -310,375, 32767,32767,
  /*CX*/ -578,-343, -542,-343, -542,-313, -578,-313, -578,-343, 32767,32767,
  /*D4*/ -255,143, -225,143, -225,167, -255,167, -255,143, 32767,32767,
  /*DL*/ 60,510, 90,545, 140,539, 130,505, 125,480, 76,476, 61,490, 60,510, 32767,32767,
  /*DU*/ 1200,185, 1240,180, 1265,80, 1220,60, 1200,120, 1200,185, 32767,32767,
  /*E7*/ 163,430, 193,430, 193,454, 163,454, 163,430, 32767,32767,
  /*EA*/ -93,430, 33,424, 7,388, -20,367, -60,360, -95,387, -93,430, 32767,32767,
  /*EA8*/ -170,275, -140,275, -140,291, -170,291, -170,275, 32767,32767,
  /*EI*/ -105,516, -55,516, -55,550, -105,550, -105,516, 32767,32767,
  /*EK*/ 440,392, 460,392, 460,412, 440,412, 440,392, 32767,32767,
  /*EL*/ -109,49, -79,49, -79,79, -109,79, -109,49, 32767,32767,
  /*EP*/ 450,265, 610,265, 610,385, 450,385, 450,265, 32767,32767,
  /*ER*/ 270,460, 300,460, 300,484, 270,484, 270,460, 32767,32767,
  /*ES*/ 230,579, 280,579, 280,595, 230,595, 230,579, 32767,32767,
  /*ET*/ 345,40, 445,40, 445,140, 345,140, 345,40, 32767,32767,
  /*EU*/ 234,517, 324,517, 324,553, 234,553, 234,517, 32767,32767,
  /*EX*/ 705,400, 785,400, 785,430, 705,430, 705,400, 32767,32767,
  /*EY*/ 685,370, 745,370, 745,404, 685,404, 685,370, 32767,32767,
  /*EZ*/ 530,360, 630,360, 630,420, 530,420, 530,360, 32767,32767,
  /*F*/ -48,484, 25,511, 82,489, 75,438, 30,430, -15,433, -18,463, -48,484, 32767,32767,
  /*FK*/ 1640,-230, 1670,-230, 1670,-200, 1640,-200, 1640,-230, 32767,32767,
  /*FM*/ -615,141, -605,141, -605,151, -615,151, -615,141, 32767,32767,
  /*FO*/ -1505,-186, -1485,-186, -1485,-166, -1505,-166, -1505,-186, 32767,32767,
  /*FY*/ -540,28, -520,28, -520,52, -540,52, -540,28, 32767,32767,
  /*G*/ -55,500, -30,535, -30,550, -20,575, -40,586, 17,525, 15,510, -55,500, 32767,32767,
  /*HA*/ 165,460, 225,460, 225,484, 165,484, 165,460, 32767,32767,
  /*HB*/ 65,461, 99,461, 99,475, 65,475, 65,461, 32767,32767,
  /*HC*/ -805,-40, -765,-40, -765,10, -805,10, -805,-40, 32767,32767,
  /*HH*/ -737,183, -713,183, -713,197, -737,197, -737,183, 32767,32767,
  /*HI*/ -725,179, -689,179, -689,195, -725,195, -725,179, 32767,32767,
  /*HK*/ -790,90, -710,110, -670,60, -670,-40, -700,-40, -790,20, -790,90, 32767,32767,
  /*HL*/ 1261,345, 1295,345, 1295,385, 1261,385, 1261,345, 32767,32767,
  /*HP*/ -818,75, -782,75, -782,95, -818,95, -818,75, 32767,32767,
  /*HR*/ -883,138, -847,138, -847,158, -883,158, -883,138, 32767,32767,
  /*HS*/ 980,80, 1055,140, 1050,180, 1000,200, 990,140, 1000,70, 980,80, 32767,32767,
  /*HZ*/ 370,170, 530,170, 530,310, 370,310, 370,170, 32767,32767,
  /*I*/ 70,459, 136,465, 130,420, 185,401, 156,380, 124,380, 80,440, 70,459, 32767,32767,
  /*J3*/ -621,117, -613,117, -613,125, -621,125, -621,117, 32767,32767,
  /*JA*/ 1295,335, 1302,310, 1409,355, 1458,434, 1416,455, 1295,335, 32767,32767,
  /*JT*/ 880,490, 1160,500, 1180,460, 1000,420, 900,450, 880,490, 32767,32767,
  /*JY*/ 350,295, 380,295, 380,325, 350,325, 350,295, 32767,32767,
  /*K*/ -1247,484, -950,490, -830,420, -825,450, -770,440, -670,450, -700,410, -755,350, -810,250, -840,300, -900,290, -970,260, -990,265, -1030,290, -1065,318, -1147,325, -1171,325, -1244,400, -1247,484, 32767,32767,
  /*KH2*/ 1445,131, 1451,131, 1451,137, 1445,137, 1445,131, 32767,32767,
  /*KH6*/ -1603,219, -1548,195, -1557,189, -1567,205, -1583,213, -1603,219, 32767,32767,
  /*KH8*/ -1711,-146, -1703,-146, -1703,-140, -1711,-140, -1711,-146, 32767,32767,
  /*KL*/ -1680,650, -1600,700, -1410,700, -1410,600, -1300,560, -1500,590, -1650,540, -1680,600, -1680,650, 32767,32767,
  /*KP4*/ -680,177, -650,177, -650,187, -680,187, -680,177, 32767,32767,
  /*LA*/ 50,580, 110,590, 150,680, 290,700, 200,695, 120,650, 50,620, 50,580, 32767,32767,
  /*LU*/ -660,-220, -580,-200, -540,-260, -530,-340, -570,-380, -620,-400, -650,-450, -680,-520, -720,-500, -695,-400, -690,-300, -660,-220, 32767,32767,
  /*LX*/ 57,494, 65,494, 65,502, 57,502, 57,494, 32767,32767,
  /*LY*/ 214,545, 264,545, 264,561, 214,561, 214,545, 32767,32767,
  /*LZ*/ 217,413, 287,413, 287,441, 217,441, 217,413, 32767,32767,
  /*OA*/ -813,-40, -750,-5, -690,-110, -700,-184, -760,-140, -813,-40, 32767,32767,
  /*OD*/ 353,333, 363,333, 363,345, 353,345, 353,333, 32767,32767,
  /*OE*/ 100,466, 180,466, 180,486, 100,486, 100,466, 32767,32767,
  /*OH*/ 210,603, 250,600, 275,602, 305,615, 315,625, 300,685, 270,700, 230,685, 210,650, 210,630, 210,603, 32767,32767,
  /*OK*/ 125,485, 185,485, 185,511, 125,511, 125,485, 32767,32767,
  /*OM*/ 170,480, 220,480, 220,494, 170,494, 170,480, 32767,32767,
  /*ON*/ 30,499, 60,499, 60,513, 30,513, 30,499, 32767,32767,
  /*OX*/ -550,600, -200,700, -200,830, -600,800, -730,780, -550,600, 32767,32767,
  /*OY*/ -76,615, -64,615, -64,625, -76,625, -76,615, 32767,32767,
  /*OZ*/ 70,545, 120,545, 120,575, 70,575, 70,545, 32767,32767,
  /*P2*/ 1410,-30, 1500,-60, 1550,-70, 1470,-100, 1410,-90, 1410,-30, 32767,32767,
  /*P5*/ 1255,380, 1285,380, 1285,420, 1255,420, 1255,380, 32767,32767,
  /*PA*/ 43,512, 69,512, 69,532, 43,532, 43,512, 32767,32767,
  /*PJ2*/ -695,119, -685,119, -685,125, -695,125, -695,119, 32767,32767,
  /*PY*/ -730,-73, -729,-98, -644,-229, -576,-302, -535,-337, -420,-230, -407,-208, -390,-179, -350,-95, -346,-75, -350,-52, -510,42, -600,52, -640,42, -698,18, -730,-73, 32767,32767,
  /*PZ*/ -574,29, -544,29, -544,53, -574,53, -574,29, 32767,32767,
  /*S2*/ 880,215, 920,215, 920,265, 880,265, 880,215, 32767,32767,
  /*S5*/ 136,455, 160,455, 160,467, 136,467, 136,455, 32767,32767,
  /*SM*/ 110,585, 128,553, 145,553, 165,562, 190,580, 195,630, 242,658, 235,685, 190,685, 150,660, 125,610, 110,585, 32767,32767,
  /*SP*/ 141,539, 239,544, 235,503, 190,490, 147,508, 141,539, 32767,32767,
  /*ST*/ 240,90, 360,90, 360,210, 240,210, 240,90, 32767,32767,
  /*SU*/ 250,320, 340,315, 360,220, 250,220, 250,320, 32767,32767,
  /*SV*/ 200,400, 260,415, 265,385, 230,350, 215,375, 200,400, 32767,32767,
  /*T8*/ 1340,70, 1350,70, 1350,80, 1340,80, 1340,70, 32767,32767,
  /*TA*/ 260,400, 440,410, 440,375, 360,360, 280,365, 260,390, 260,400, 32767,32767,
  /*TF*/ -230,636, -150,636, -150,662, -230,662, -230,636, 32767,32767,
  /*TG*/ -918,140, -888,140, -888,170, -918,170, -918,140, 32767,32767,
  /*TI*/ -853,86, -827,86, -827,112, -853,112, -853,86, 32767,32767,
  /*TJ*/ 100,10, 150,10, 150,100, 100,100, 100,10, 32767,32767,
  /*TR*/ 90,-43, 140,-43, 140,27, 90,27, 90,-43, 32767,32767,
  /*TU*/ -85,45, -25,45, -25,105, -85,105, -85,45, 32767,32767,
  /*TZ*/ -90,100, 30,100, 30,240, -90,240, -90,100, 32767,32767,
  /*UA*/ 280,600, 300,680, 600,690, 1000,730, 1400,720, 1600,690, 1800,660, 1600,600, 1350,550, 1300,500, 1200,500, 900,500, 600,520, 480,520, 400,500, 400,550, 300,550, 280,560, 280,600, 32767,32767,
  /*UK*/ 565,380, 705,380, 705,450, 565,450, 565,380, 32767,32767,
  /*UN*/ 500,550, 800,540, 870,490, 800,425, 520,420, 470,500, 500,550, 32767,32767,
  /*UR*/ 221,484, 240,505, 270,516, 310,523, 355,510, 402,495, 400,470, 380,475, 365,453, 335,460, 315,466, 296,454, 282,454, 227,479, 221,484, 32767,32767,
  /*V5*/ 125,-275, 225,-275, 225,-165, 125,-165, 125,-275, 32767,32767,
  /*VE*/ -1410,690, -950,690, -800,730, -640,600, -560,510, -660,450, -790,430, -825,420, -890,480, -950,490, -1230,490, -1300,540, -1410,600, -1410,690, 32767,32767,
  /*VK*/ 1130,-220, 1220,-180, 1300,-120, 1370,-120, 1420,-110, 1460,-190, 1530,-250, 1530,-320, 1500,-375, 1410,-385, 1290,-320, 1150,-340, 1140,-260, 1130,-220, 32767,32767,
  /*VU*/ 682,237, 775,81, 799,95, 898,220, 880,264, 745,345, 710,320, 682,237, 32767,32767,
  /*XE*/ -1171,325, -1065,318, -1030,290, -970,260, -975,210, -905,215, -867,215, -875,185, -900,178, -920,150, -965,160, -995,165, -1050,200, -1100,240, -1140,280, -1120,300, -1171,325, 32767,32767,
  /*XU*/ 1030,105, 1070,105, 1070,145, 1030,145, 1030,105, 32767,32767,
  /*XW*/ 1005,155, 1055,155, 1055,215, 1005,215, 1005,155, 32767,32767,
  /*XZ*/ 920,210, 980,280, 1010,210, 990,110, 940,160, 920,210, 32767,32767,
  /*YA*/ 610,310, 710,310, 710,370, 610,370, 610,310, 32767,32767,
  /*YB*/ 950,60, 980,20, 1040,15, 1060,-20, 1060,-70, 1140,-85, 1200,-95, 1310,-80, 1410,-90, 1410,-20, 1320,10, 1200,30, 1080,20, 1000,55, 950,60, 32767,32767,
  /*YI*/ 400,295, 470,295, 470,365, 400,365, 400,295, 32767,32767,
  /*YJ*/ 1665,-180, 1685,-180, 1685,-140, 1665,-140, 1665,-180, 32767,32767,
  /*YK*/ 360,330, 410,330, 410,370, 360,370, 360,330, 32767,32767,
  /*YL*/ 223,561, 273,561, 273,577, 223,577, 223,561, 32767,32767,
  /*YN*/ -870,111, -834,111, -834,147, -870,147, -870,111, 32767,32767,
  /*YO*/ 203,480, 287,480, 297,452, 227,437, 203,460, 203,480, 32767,32767,
  /*YS*/ -900,132, -880,132, -880,144, -900,144, -900,132, 32767,32767,
  /*YU*/ 183,420, 233,420, 233,460, 183,460, 183,420, 32767,32767,
  /*YV*/ -734,110, -710,120, -640,107, -600,95, -600,10, -660,10, -675,60, -720,70, -734,90, -734,110, 32767,32767,
  /*Z2*/ 263,-215, 333,-215, 333,-165, 263,-165, 263,-215, 32767,32767,
  /*Z3*/ 205,409, 229,409, 229,423, 205,423, 205,409, 32767,32767,
  /*Z6*/ 193,419, 207,419, 207,433, 193,433, 193,419, 32767,32767,
  /*ZA*/ 192,397, 208,397, 208,423, 192,423, 192,397, 32767,32767,
  /*ZL*/ 1730,-350, 1780,-375, 1780,-410, 1745,-415, 1700,-460, 1665,-460, 1700,-430, 1720,-400, 1730,-350, 32767,32767,
  /*ZP*/ -610,-265, -550,-265, -550,-205, -610,-205, -610,-265, 32767,32767,
  /*ZS*/ 169,-287, 200,-284, 250,-260, 294,-224, 313,-224, 329,-260, 310,-299, 280,-327, 256,-340, 220,-349, 184,-344, 170,-315, 164,-287, 169,-287, 32767,32767,
};
static const char DXCCPOLY_CODE[] = "3D2 3V 3W 4J 4L 4O 4S 4X 5A 5H 5N 5R 5W 5Z 6W 7O 7Q 7X 8P 8R 9A 9G 9K 9L 9M2 9M6 9N 9Q 9V 9Y A2 A3 A4 A5 A6 A7 A9 AP BV BY C9 CE CM CN CP CT CT3 CU CX D4 DL DU E7 EA EA8 EI EK EL EP ER ES ET EU EX EY EZ F FK FM FO FY G HA HB HC HH HI HK HL HP HR HS HZ I J3 JA JT JY K KH2 KH6 KH8 KL KP4 LA LU LX LY LZ OA OD OE OH OK OM ON OX OY OZ P2 P5 PA PJ2 PY PZ S2 S5 SM SP ST SU SV T8 TA TF TG TI TJ TR TU TZ UA UK UN UR V5 VE VK VU XE XU XW XZ YA YB YI YJ YK YL YN YO YS YU YV Z2 Z3 Z6 ZA ZL ZP ZS";
static const int DXCCPOLY_N = 161;

// Point entities (the DXCC "long tail": islands and micro-entities) from
// cty.dat. int16_t (lon*10, lat*10) per entity; order matches DXCCPT_CODE.
// An entity is workable if its representative point is within the satellite
// footprint plus a small claim radius. Together with the DXCCPOLY country
// polygons this yields the full ~340-entity DXCC list.
static const int16_t DXCCPT[] = {
  124, 419, 1142, 99, 74, 437, 567, -104, 575, -204, 634, -197, 103, 17, 56, -14, 1750, -220, 1771, -125, 315, -266, -107, 110,
  34, -544, -906, -688, 60, 462, -740, 408, 1260, -88, 330, 350, -105, 206, 94, 176, 13, 84, 326, 19, -775, 182, 279, -292,
  734, 42, 144, 359, 267, -142, 298, -32, 298, -18, 1177, 151, 1167, 207, 1669, -5, 16, 426, -164, 134, -760, 242, -801, -263,
  -1094, -271, -788, -336, 0, -900, -599, 439, -600, 470, 185, -125, 433, -116, 390, 150, 343, 313, -1611, -100, -1579, -219, -1698, -190,
  30, 396, -53, 359, -617, 161, 452, -129, -628, 179, 1583, -199, -1495, -234, -1092, 103, -1401, -89, -562, 468, 555, -211, -630, 181,
  473, -116, 427, -170, 545, -159, 518, -464, 693, -490, 775, -378, -1762, -133, -45, 542, -67, 547, -22, 492, -42, 568, -26, 494,
  -37, 523, 1600, -90, 1658, -107, 96, 471, -910, -8, -817, 126, -816, 40, 125, 419, 93, 402, 424, 118, -148, 120, -610, 139,
  -614, 154, -612, 132, 1540, 243, 1422, 270, 160, 780, -83, 710, -750, 200, 1457, 152, -1760, 0, -1695, 167, -1774, 282, -1621, 59,
  -1780, 290, -1712, -110, 1666, 193, -750, 184, -648, 177, -679, 181, 204, 601, 190, 600, -700, 125, -682, 122, -631, 176, -631, 181,
  -324, -38, -290, 0, -293, -205, 499, 807, -138, 248, 555, -47, 66, 2, 240, 400, 279, 362, 248, 352, 1792, -85, 1730, 14,
  -1717, -28, -1574, 18, 1695, -9, 454, 20, 124, 440, -870, 55, 90, 420, 203, 68, 154, -10, 182, 158, 22, 99, 205, 547,
  841, 559, -618, 171, -887, 170, -628, 174, 1582, 69, 1673, 91, 1146, 45, 735, -531, 1589, -546, 968, -122, 1591, -316, 1558, -174,
  1679, -290, 1500, -162, 1056, -105, -630, 182, -622, 168, -648, 183, -718, 218, -1301, -251, -1248, -247, -587, -516, -371, -545, -587, -621,
  -456, -606, -263, -584, -647, 323, 724, -73, 1142, 223, 928, 124, 728, 112, -1110, 188, -20, 120, 1135, 221, -636, 157, 316, 48,
  -54, 362, 336, 353, -57, -160, -144, -79, -123, -371, -812, 193, -1712, -94, -1765, -438, -1779, -292, 1676, -516, 377, -469,
};
static const char DXCCPT_CODE[] = "1A 1S 3A 3B6 3B8 3B9 3C 3C0 3D2/c 3D2/r 3DA 3X 3Y/b 3Y/p 4U1I 4U1U 4W 5B 5T 5U 5V 5X 6Y 7P 8Q 9H 9J 9U 9X BS7 BV9P C2 C3 C5 C6 CE0X CE0Y CE0Z CE9 CY0 CY9 D2 D6 E3 E4 E5/n E5/s E6 EA6 EA9 FG FH FJ FK/c FO/a FO/c FO/m FP FR FS FT/g FT/j FT/t FT/w FT/x FT/z FW GD GI GJ GM GU GW H4 H40 HB0 HC8 HK0/a HK0/m HV IS J2 J5 J6 J7 J8 JD/m JD/o JW JX KG4 KH0 KH1 KH3 KH4 KH5 KH7K KH8/s KH9 KP1 KP2 KP5 OH0 OJ0 P4 PJ4 PJ5 PJ7 PY0F PY0S PY0T R1FJ S0 S7 S9 SV/a SV5 SV9 T2 T30 T31 T32 T33 T5 T7 TI9 TK TL TN TT TY UA2 UA9 V2 V3 V4 V6 V7 V8 VK0H VK0M VK9C VK9L VK9M VK9N VK9W VK9X VP2E VP2M VP2V VP5 VP6 VP6/d VP8 VP8/g VP8/h VP8/o VP8/s VP9 VQ9 VR VU4 VU7 XF4 XT XX9 YV0 Z8 ZB ZC4 ZD7 ZD8 ZD9 ZF ZK3 ZL7 ZL8 ZL9 ZS8";
static const int DXCCPT_N = 179;

static const int DXCC_N = DXCCPOLY_N + DXCCPT_N;   // 340

// Copy the idx-th DXCC prefix into out (<=7 chars). idx < DXCCPOLY_N indexes
// the polygon code list; otherwise the point code list.
static void dxccCode(int idx, char* out) {
  const char* s; int e;
  if (idx < DXCCPOLY_N) { s = DXCCPOLY_CODE; e = idx; }
  else                  { s = DXCCPT_CODE;   e = idx - DXCCPOLY_N; }
  const char* p = s;
  while (*p && e > 0) { if (*p == ' ') --e; ++p; }
  int k = 0;
  while (*p && *p != ' ' && k < 7) out[k++] = *p++;
  out[k] = 0;
}

// Great-circle distance (km) between two lat/lon points.
static double dxccGcKm(double la1, double lo1, double la2, double lo2) {
  const double d2r = 0.017453292519943295, R = 6371.0;
  double dphi = (la2 - la1) * d2r, dl = (lo2 - lo1) * d2r;
  double a = sin(dphi/2)*sin(dphi/2) +
             cos(la1*d2r)*cos(la2*d2r)*sin(dl/2)*sin(dl/2);
  if (a < 0) a = 0; if (a > 1) a = 1;
  return 2.0 * R * asin(sqrt(a));
}

// --- fast-path index tables (start offset + bbox per polygon) ---
// Precomputed per-entity start offset (int16 index) + lon/lat bbox (lon*10,
// lat*10) for fast footprint rejection. Matches DXCCPOLY/DXCCPOLY_CODE order.
static const uint16_t DXCCPOLY_START[] = {
  0, 12, 24, 36, 48, 60, 72, 84, 96, 108, 120, 142, 154, 166, 178, 190,
  202, 214, 228, 240, 252, 264, 276, 288, 300, 312, 324, 336, 348, 360, 372, 384,
  396, 408, 420, 432, 444, 456, 470, 482, 516, 528, 548, 562, 574, 586, 598, 610,
  622, 634, 646, 664, 678, 690, 706, 718, 730, 742, 754, 766, 778, 790, 802, 814,
  826, 838, 850, 868, 880, 892, 904, 916, 934, 946, 958, 970, 982, 994, 1010, 1022,
  1034, 1046, 1062, 1074, 1092, 1104, 1118, 1132, 1144, 1184, 1196, 1210, 1222, 1242, 1254, 1272,
  1298, 1310, 1322, 1334, 1348, 1360, 1372, 1396, 1408, 1420, 1432, 1446, 1458, 1470, 1484, 1496,
  1508, 1520, 1554, 1566, 1578, 1590, 1616, 1630, 1642, 1654, 1668, 1680, 1696, 1708, 1720, 1732,
  1744, 1756, 1768, 1780, 1820, 1832, 1848, 1880, 1892, 1922, 1952, 1970, 2006, 2018, 2030, 2044,
  2056, 2088, 2100, 2112, 2124, 2136, 2148, 2162, 2174, 2186, 2208, 2220, 2232, 2244, 2256, 2276,
  2288,
};
static const int16_t DXCCPOLY_LOMIN[] = {
  1765, 77, 1040, 465, 415, 187, 794, 343, 100, 310, 27, 435, -1726, 345, -162, 430,
  328, -50, -599, -604, 139, -30, 468, -128, 985, 1110, 805, 140, 1036, -620, 200, -1757,
  535, 885, 525, 508, 502, 610, 1202, 750, 320, -757, -849, -110, -690, -93, -174, -310,
  -578, -255, 60, 1200, 163, -95, -170, -105, 440, -109, 450, 270, 230, 345, 234, 705,
  685, 530, -48, 1640, -615, -1505, -540, -55, 165, 65, -805, -737, -725, -790, 1261, -818,
  -883, 980, 370, 70, -621, 1295, 880, 350, -1247, 1445, -1603, -1711, -1680, -680, 50, -720,
  57, 214, 217, -813, 353, 100, 210, 125, 170, 30, -730, -76, 70, 1410, 1255, 43,
  -695, -730, -574, 880, 136, 110, 141, 240, 250, 200, 1340, 260, -230, -918, -853, 100,
  90, -85, -90, 280, 565, 470, 221, 125, -1410, 1130, 682, -1171, 1030, 1005, 920, 610,
  950, 400, 1665, 360, 223, -870, 203, -900, 183, -734, 263, 205, 193, 192, 1665, -610,
  164,
};
static const int16_t DXCCPOLY_LOMAX[] = {
  1795, 113, 1090, 495, 455, 199, 820, 357, 250, 390, 145, 505, -1714, 415, -128, 510,
  358, 90, -591, -574, 189, 6, 488, -108, 1045, 1190, 875, 320, 1040, -604, 290, -1747,
  595, 925, 565, 516, 508, 770, 1218, 1320, 390, -670, -741, -30, -600, -67, -164, -250,
  -542, -225, 140, 1265, 193, 33, -140, -55, 460, -79, 610, 300, 280, 445, 324, 785,
  745, 630, 82, 1670, -605, -1485, -520, 17, 225, 99, -765, -713, -689, -670, 1295, -782,
  -847, 1055, 530, 185, -613, 1458, 1180, 380, -670, 1451, -1548, -1703, -1300, -650, 290, -530,
  65, 264, 287, -690, 363, 180, 315, 185, 220, 60, -200, -64, 120, 1550, 1285, 69,
  -685, -346, -544, 920, 160, 242, 239, 360, 360, 265, 1350, 440, -150, -888, -827, 150,
  140, -25, 30, 1800, 705, 870, 402, 225, -560, 1530, 898, -867, 1070, 1055, 1010, 710,
  1410, 470, 1685, 410, 273, -834, 297, -880, 233, -600, 333, 229, 207, 208, 1780, -550,
  329,
};
static const int16_t DXCCPOLY_LAMIN[] = {
  -193, 320, 95, 391, 410, 423, 61, 300, 220, -110, 43, -260, -143, -30, 130, 130,
  -175, 240, 128, 35, 437, 43, 285, 73, 10, 20, 267, -90, 12, 97, -260, -220,
  185, 265, 227, 247, 258, 240, 224, 185, -240, -530, 199, 285, -210, 372, 323, 375,
  -343, 143, 476, 60, 430, 360, 275, 516, 392, 49, 265, 460, 579, 40, 517, 400,
  370, 360, 430, -230, 141, -186, 28, 500, 460, 461, -40, 183, 179, -40, 345, 75,
  138, 70, 170, 380, 117, 310, 420, 295, 250, 131, 189, -146, 540, 177, 580, -520,
  494, 545, 413, -184, 333, 466, 600, 485, 480, 499, 600, 615, 545, -100, 380, 512,
  119, -337, 29, 215, 455, 553, 490, 90, 220, 350, 70, 360, 636, 140, 86, 10,
  -43, 45, 100, 500, 380, 420, 453, -275, 420, -385, 81, 150, 105, 155, 110, 310,
  -95, 295, -180, 330, 561, 111, 437, 132, 420, 10, -215, 409, 419, 397, -460, -265,
  -349,
};
static const int16_t DXCCPOLY_LAMAX[] = {
  -163, 370, 235, 415, 430, 433, 95, 330, 330, -10, 135, -120, -133, 40, 160, 180,
  -95, 370, 136, 65, 471, 113, 301, 97, 60, 70, 297, 50, 16, 113, -180, -204,
  245, 285, 253, 259, 264, 370, 250, 500, -120, -184, 232, 355, -130, 422, 331, 395,
  -313, 167, 545, 185, 454, 430, 291, 550, 412, 79, 385, 484, 595, 140, 553, 430,
  404, 420, 511, -200, 151, -166, 52, 586, 484, 475, 10, 197, 195, 110, 385, 95,
  158, 200, 310, 465, 125, 455, 500, 325, 490, 137, 219, -140, 700, 187, 700, -200,
  502, 561, 441, -5, 345, 486, 700, 511, 494, 513, 830, 625, 575, -30, 420, 532,
  125, 52, 53, 265, 467, 685, 544, 210, 320, 415, 80, 410, 662, 170, 112, 100,
  27, 105, 240, 730, 450, 550, 523, -165, 730, -110, 345, 325, 145, 215, 280, 370,
  60, 365, -140, 370, 577, 147, 480, 144, 460, 120, -165, 423, 433, 423, -350, -205,
  -224,
};

// Point-in-polygon for a single polygon starting at int16 offset s0 (no advance).
static bool dxccPipAt(double lon, double lat, int s0) {
  bool inside = false;
  int i = s0; double px = 0, py = 0; bool have = false;
  double x0 = 0, y0 = 0; bool first = true;
  int n = (int)(sizeof(DXCCPOLY)/sizeof(DXCCPOLY[0]));
  while (i + 1 < n) {
    int a = DXCCPOLY[i], b = DXCCPOLY[i + 1];
    if (a == 32767 && b == 32767) break;
    double cx = a / 10.0, cy = b / 10.0;
    if (first) { x0 = cx; y0 = cy; first = false; }
    if (have) {
      if (((py > lat) != (cy > lat)) &&
          (lon < (px - cx) * (lat - cy) / (py - cy) + cx)) inside = !inside;
    }
    px = cx; py = cy; have = true; i += 2;
  }
  if (have) {
    if (((py > lat) != (y0 > lat)) &&
        (lon < (px - x0) * (lat - y0) / (py - y0) + x0)) inside = !inside;
  }
  return inside;
}

void App::addFootprintDxcc(double subLat, double subLon, double altKm) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232, Re = 6371.0;
  if (altKm < 1) return;
  double coslam = Re / (Re + altKm);
  double lamDeg = acos(coslam) * R2D;
  double fpKm   = Re * acos(coslam);          // surface footprint radius (km)
  const double CLAIM_KM = 80.0;               // entity's own extent allowance
  double sinSub = sin(subLat * D2R), cosSub = cos(subLat * D2R);
  // --- polygon entities: walk a coarse footprint mesh, tag the container.
  // Each mesh point is rejected against every entity's bounding box (cheap)
  // before any ray-cast, and entities already found are skipped outright. ---
  int latLo = (int)floor(subLat - lamDeg), latHi = (int)ceil(subLat + lamDeg);
  for (int la = latLo; la <= latHi; ++la) {
    if (la < -90 || la >= 90) continue;
    double clatR = (la + 0.5) * D2R;
    double cl = cos(clatR); if (cl < 0.15) cl = 0.15;
    double lonHalf = lamDeg / cl + 2.0;
    int lonLo = (int)floor(subLon - lonHalf), lonHi = (int)ceil(subLon + lonHalf);
    double A = sin(clatR) * sinSub, B = cos(clatR) * cosSub;
    for (int lo = lonLo; lo <= lonHi; ++lo) {
      double clon = lo + 0.5;
      while (clon < -180) clon += 360; while (clon >= 180) clon -= 360;
      if (A + B * cos((clon - subLon) * D2R) < coslam) continue;
      int16_t qlo = (int16_t)lround(clon * 10.0), qla = (int16_t)lround((la + 0.5) * 10.0);
      for (int idx = 0; idx < DXCCPOLY_N; ++idx) {
        if (dxccBits[idx >> 3] & (1 << (idx & 7))) continue;          // already found
        if (qlo < DXCCPOLY_LOMIN[idx] || qlo > DXCCPOLY_LOMAX[idx] ||
            qla < DXCCPOLY_LAMIN[idx] || qla > DXCCPOLY_LAMAX[idx]) continue;  // bbox reject
        if (dxccPipAt(clon, la + 0.5, DXCCPOLY_START[idx]))
          dxccBits[idx >> 3] |= (uint8_t)(1 << (idx & 7));
      }
    }
  }
  // --- point entities: within footprint radius + claim radius of sub-point ---
  double limit = fpKm + CLAIM_KM;
  double limDeg = limit / 100.0 + 2.0;        // conservative latitude pre-reject band
  for (int k = 0; k < DXCCPT_N; ++k) {
    int gi = DXCCPOLY_N + k;
    if (dxccBits[gi >> 3] & (1 << (gi & 7))) continue;
    double ela = DXCCPT[k*2 + 1] / 10.0;
    if (fabs(ela - subLat) > limDeg) continue;
    double elo = DXCCPT[k*2] / 10.0;
    if (dxccGcKm(subLat, subLon, ela, elo) <= limit)
      dxccBits[gi >> 3] |= (uint8_t)(1 << (gi & 7));
  }
}

void App::buildDxcc(time_t a, time_t b) {
  memset(dxccBits, 0, sizeof(dxccBits));
  SatEntry* s = activeSat();
  if (!s || !timeIsSet()) { dxccN = 0; return; }
  pred.setSite(loc.obs()); pred.setSat(*s);
  int samples = (b > a) ? 1 + (int)((b - a) / 60) : 1;
  if (samples > 90) samples = 90; if (samples < 1) samples = 1;
  for (int k = 0; k < samples; ++k) {
    time_t t = (samples > 1) ? a + (time_t)((double)(b - a) * k / (samples - 1)) : a;
    LiveLook L = pred.look(t);
    addFootprintDxcc(L.subLat, L.subLon, L.satAltKm);
  }
  int cnt = 0;
  for (size_t i = 0; i < sizeof(dxccBits); ++i)
    for (uint8_t v = dxccBits[i]; v; v &= (uint8_t)(v - 1)) ++cnt;
  dxccN = cnt;
}

void App::drawDxcc() {
  if (dxccLive) {
    uint32_t ms = millis();
    if (!dxccBuiltMs || ms - dxccBuiltMs > 3000) {
      dxccBuiltMs = ms; buildDxcc(nowUtc(), nowUtc());
    }
  }
  SatEntry* s = activeSat();
  { String h = (s ? String(s->name) : String("DXCC")) +
               (dxccLive ? " now" : " pass");
    header(h); }
  canvas.setTextSize(1);
  if (dxccN == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK); canvas.setCursor(6, 56);
    canvas.print(timeIsSet() ? "No DXCC in footprint." : "Clock not set.");
    footer("` back"); return;
  }
  const int COLS = 5, ROWS = 8, PER = COLS * ROWS;
  if (dxccScroll >= dxccN) dxccScroll = 0;
  { char cnt[44];
    if (dxccN > PER) {
      int from = dxccScroll + 1, to = dxccScroll + PER; if (to > dxccN) to = dxccN;
      snprintf(cnt, sizeof(cnt), "%d workable  (%d-%d)", dxccN, from, to);
    } else {
      snprintf(cnt, sizeof(cnt), "%d workable", dxccN);
    }
    canvas.setTextColor(CL_CYAN, CL_BLACK);
    int w = (int)strlen(cnt) * 6; int x = 238 - w; if (x < 4) x = 4;
    canvas.setCursor(x, 19); canvas.print(cnt);
  }
  int seen = 0, drawn = 0;
  for (int idx = 0; idx < DXCC_N && drawn < PER; ++idx) {
    if (!(dxccBits[idx >> 3] & (1 << (idx & 7)))) continue;
    if (seen++ < dxccScroll) continue;
    char g[8]; dxccCode(idx, g);
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4 + (drawn % COLS) * 48, 31 + (drawn / COLS) * 11);
    canvas.print(g); ++drawn;
  }
  footer(dxccN > PER ? "` bk  ;/. scroll  {} page" : "` back");
}

void App::keyDxcc(char c, bool enter, bool back) {
  (void)enter;
  if (isBack(c, back)) { screen = dxccLive ? liveReturn : SCR_PASSES; lastDrawMs = 0; return; }
  const int PER = 5 * 8;
  if (isDown(c)) { if (dxccScroll + PER < dxccN) dxccScroll += 5; lastDrawMs = 0; return; }
  if (isUp(c))   { if (dxccScroll >= 5) dxccScroll -= 5; lastDrawMs = 0; return; }
  if (c == '}')  { if (dxccScroll + PER < dxccN) dxccScroll += PER; lastDrawMs = 0; return; }
  if (c == '{')  { dxccScroll -= PER; if (dxccScroll < 0) dxccScroll = 0; lastDrawMs = 0; return; }
}




// ===========================================================================
//  GP / orbital-elements source picker. Default AMSAT JSON; any CelesTrak
//  JSON-PP category (gp.php?GROUP=.. or SPECIAL=..); or a custom URL. The chosen
//  source is just written to cfg.gpUrl - the fetch/parse path is unchanged (the
//  parser already falls back AMSAT_NAME -> OBJECT_NAME for CelesTrak data).
// ===========================================================================
struct GpSrc { const char* label; const char* q; };
static const GpSrc GP_SRC[] = {
  {"AMSAT (amateur)",     "@AMSAT"},   {"Custom URL...",       "@CUSTOM"},
  {"Amateur Radio",       "GROUP=amateur"},      {"SatNOGS",            "GROUP=satnogs"},
  {"Last 30 Days",        "GROUP=last-30-days"}, {"Space Stations",     "GROUP=stations"},
  {"100 Brightest",       "GROUP=visual"},       {"Active",             "GROUP=active"},
  {"Analyst",             "GROUP=analyst"},      {"FENGYUN 1C Debris",  "GROUP=fengyun-1c-debris"},
  {"IRIDIUM 33 Debris",   "GROUP=iridium-33-debris"}, {"COSMOS 2251 Debris", "GROUP=cosmos-2251-debris"},
  {"Weather",             "GROUP=weather"},      {"Earth Resources",    "GROUP=resource"},
  {"SAR",                 "GROUP=sar"},          {"SARSAT",             "GROUP=sarsat"},
  {"Disaster Monitoring", "GROUP=dmc"},          {"TDRSS",              "GROUP=tdrss"},
  {"ARGOS",               "GROUP=argos"},        {"Planet",             "GROUP=planet"},
  {"Spire",               "GROUP=spire"},        {"Active GEO",         "GROUP=geo"},
  {"GEO Protected Zone",  "SPECIAL=gpz"},        {"GEO Prot Zone Plus", "SPECIAL=gpz-plus"},
  {"Intelsat",            "GROUP=intelsat"},     {"SES",                "GROUP=ses"},
  {"Eutelsat",            "GROUP=eutelsat"},     {"Telesat",            "GROUP=telesat"},
  {"Starlink",            "GROUP=starlink"},     {"OneWeb",             "GROUP=oneweb"},
  {"Qianfan",             "GROUP=qianfan"},      {"Hulianwang",         "GROUP=hulianwang"},
  {"Kuiper",              "GROUP=kuiper"},       {"Iridium NEXT",       "GROUP=iridium-NEXT"},
  {"Orbcomm",             "GROUP=orbcomm"},      {"Globalstar",         "GROUP=globalstar"},
  {"Experimental Comm",   "GROUP=x-comm"},       {"Other Comm",         "GROUP=other-comm"},
  {"GNSS",                "GROUP=gnss"},         {"GPS Operational",    "GROUP=gps-ops"},
  {"GLONASS Operational", "GROUP=glo-ops"},      {"Galileo",            "GROUP=galileo"},
  {"Beidou",              "GROUP=beidou"},       {"SBAS",               "GROUP=sbas"},
  {"Space/Earth Science", "GROUP=science"},      {"Geodetic",           "GROUP=geodetic"},
  {"Engineering",         "GROUP=engineering"},  {"Education",          "GROUP=education"},
  {"Military",            "GROUP=military"},     {"Radar Calibration",  "GROUP=radar"},
  {"CubeSats",            "GROUP=cubesat"},
};
static const int GP_SRC_N = (int)(sizeof(GP_SRC) / sizeof(GP_SRC[0]));

void App::drawGpSrc() {
  header("GP / elements source");
  canvas.setTextSize(1);
  const int VIS = 9;
  if (gpSrcSel < gpSrcScroll)           gpSrcScroll = gpSrcSel;
  if (gpSrcSel > gpSrcScroll + VIS - 1) gpSrcScroll = gpSrcSel - VIS + 1;
  if (gpSrcScroll < 0) gpSrcScroll = 0;
  for (int r = 0; r < VIS && gpSrcScroll + r < GP_SRC_N; ++r) {
    int i = gpSrcScroll + r, y = 18 + r * 11;
    if (i == gpSrcSel) { canvas.fillRect(0, y - 2, 240, 11, CL_SELBG);
                         canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                 canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(GP_SRC[i].label);
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (gpSrcScroll > 0)             { canvas.setCursor(232, 18);  canvas.print("^"); }
  if (gpSrcScroll + VIS < GP_SRC_N){ canvas.setCursor(232, 106); canvas.print("v"); }
  footer("; / . ENT pick  {} page  ` bk");
}

void App::keyGpSrc(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_SETTINGS; lastDrawMs = 0; return; }
  if (isUp(c))   gpSrcSel = (gpSrcSel + GP_SRC_N - 1) % GP_SRC_N;
  if (isDown(c)) gpSrcSel = (gpSrcSel + 1) % GP_SRC_N;
  if (c == '{')  gpSrcSel = (gpSrcSel >= 9) ? gpSrcSel - 9 : 0;
  if (c == '}')  gpSrcSel = (gpSrcSel + 9 < GP_SRC_N) ? gpSrcSel + 9 : GP_SRC_N - 1;
  if (enter) {
    const char* q = GP_SRC[gpSrcSel].q;
    if (!strcmp(q, "@CUSTOM")) {
      editTarget = 203; editTitle = "GP source URL"; editBuf = cfg.gpUrl;
      screen = SCR_EDIT; lastDrawMs = 0; return;
    }
    if (!strcmp(q, "@AMSAT")) {
      strncpy(cfg.gpUrl, AMSAT_GP_URL, sizeof(cfg.gpUrl) - 1);
    } else {
      // CelesTrak's GP query. Use the documented host (celestrak.org -- the .com
      // host 301-redirects and can get the IP firewalled) and the exact uppercase
      // FORMAT token from their spec. JSON (compact) is valid and smaller than
      // JSON-PRETTY; the streaming parser doesn't need pretty-printing.
      String url = String("https://celestrak.org/NORAD/elements/gp.php?") + q + "&FORMAT=JSON";
      strncpy(cfg.gpUrl, url.c_str(), sizeof(cfg.gpUrl) - 1);
    }
    cfg.gpUrl[sizeof(cfg.gpUrl) - 1] = 0;
    cfg.save();
    setStatus(String("Source: ") + GP_SRC[gpSrcSel].label);
    screen = SCR_SETTINGS; lastDrawMs = 0; return;
  }
}

void App::drawHome() {
  header("CardSat");
  static const char* items[] = { "Satellites", "Next Passes (all favs)", "Passes (sel)",
                          "Track (sel)", "World Map", "Sun / Moon", "Space Wx", "Weather", "QRZ Lookup", "Location", "Update",
                          "Settings", "Log", "Messages", "About" };
  const int N = (int)(sizeof(items) / sizeof(items[0]));
  static_assert(sizeof(items) / sizeof(items[0]) == 15,
                "Home menu item count must match keyHome's N");
  const int VIS = 9;
  if (homeSel < homeScroll)           homeScroll = homeSel;
  if (homeSel > homeScroll + VIS - 1) homeScroll = homeSel - VIS + 1;
  if (homeScroll < 0) homeScroll = 0;
  if (homeScroll > N - VIS) homeScroll = (N > VIS) ? N - VIS : 0;
  canvas.setTextSize(1);
  for (int r = 0; r < VIS && (homeScroll + r) < N; ++r) {
    int i = homeScroll + r, y = 18 + r * 11;
    if (i == homeSel) { canvas.fillRect(0, y - 2, 240, 11, CL_SELBG);
                        canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(items[i]);
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (homeScroll > 0)       { canvas.setCursor(232, 18);  canvas.print("^"); }
  if (homeScroll + VIS < N) { canvas.setCursor(232, 106); canvas.print("v"); }
  footer("; / . move  ENT");
  // Selected satellite: bottom-right of the footer row, truncated to fit and kept
  // clear of the key hint on the left.
  SatEntry* s = activeSat();
  String sel = s ? s->name : String("no sat");
  if (sel.length() > 18) sel = sel.substring(0, 17) + "~";
  canvas.setTextColor(s ? CL_CYAN : CL_GREY, CL_BLACK);
  canvas.setCursor(240 - 2 - (int)sel.length() * 6, 127);
  canvas.print(sel);
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
    if (i == schedSel) { canvas.fillRect(0, y-1, 240, 10, CL_SELBG);
                         canvas.setTextColor(CL_BLACK, CL_SELBG); }
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
  footer("ENT trk  m map  r refr  z slp  ` bk");
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
    if (vi == viewSel) { canvas.fillRect(0, y-1, 240, 10, CL_SELBG);
                         canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else                 canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%c%-21s%5lu", isFav(s.norad) ? '*' : ' ',
                  s.name, (unsigned long)s.norad);
    if (s.amsatStatus) {                       // AMSAT activity, if recently reported
      bool sel = (vi == viewSel); int cx = 233, cy = y + 3;
      uint16_t col = sel ? CL_BLACK
                   : (s.amsatStatus == 1 ? CL_GREEN
                   : (s.amsatStatus == 3 ? CL_YELLOW : CL_RED));
      if (s.amsatStatus == 1)      canvas.fillCircle(cx, cy, 3, col);          // heard = filled dot
      else if (s.amsatStatus == 3) canvas.fillRect(cx - 2, cy - 2, 5, 5, col); // telemetry = square
      else                         canvas.drawCircle(cx, cy, 3, col);          // not heard = ring
    }
  }
  bool selManual = (viewN > 0 && viewSel < viewN && db.isManualGp(db.at(view[viewSel]).norad));
  if (selManual) footer("ENT pass o orb t tx e eqx k osc f fav x del `bk");
  else           footer("ENT pass o orb s sim t tx e eqx k osc 3 glb 2 s2s d 10d i illum f fav `bk");
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
    if (i == passSel) { canvas.fillRect(0, y-1, 240, 10, CL_SELBG);
                        canvas.setTextColor(CL_BLACK, CL_SELBG); }
    else canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s  %2ldm %3.0f %s",
                  fmtMDHM(p.aos).c_str(), mins, p.maxEl, fmtHM(p.los).c_str());
  }
  footer("ENT trk d dtl n+TX g grd w st e dx x mut `bk");
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
      uint16_t col; const char* tag;
      switch (tuneMode) {
        case TM_FULL: col = CL_ORANGE; tag = "<FULL>"; break;
        case TM_DL:   col = CL_ORANGE; tag = "<DL>";   break;
        case TM_UL:   col = CL_ORANGE; tag = "<UL>";   break;
        default:      col = (trackMode == 0 ? CL_CYAN : CL_GREY);
                      tag = (trackMode == 0 ? "<TUNE>" : ""); break;
      }
      canvas.setTextColor(col, CL_BLACK);
      canvas.setCursor(4, 79);
      canvas.printf("PB %+.1fk bw%.1fk %s%s", posk, halfk,
                    t.invert ? "INV " : "", tag);
      if (cfg.tiltTune && imuReady && trackMode == 0) {
        canvas.setTextColor(CL_CYAN, CL_BLACK);
        canvas.setCursor(210, 79); canvas.print("TLT");
      }
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
    footer(imuReady ? ",/tune s x=ctr m=cal t r o p n=bcn y=tilt"
                    : ",/tune s=stp x=ctr m=cal t r o p n=bcn f=man");
  else
    footer(",/DN ;.UP s=stp x=0 m=tn t r o p n=bcn f=man");
}

// Large-font "operating" readout: only the values you need at arm's length during
// a pass -- RX, TX, az/el, and an AOS/LOS countdown. Toggled from Track with 'z';
// the radio/rotator/Doppler service keeps running (it's gated on radioOut/rotOut,
// not on which screen is showing), so this is purely an alternate view of the
// same live session. 't' (next transponder), 'r' (radio), 'o' (rotator) and 'l'
// (log) still work without leaving the big view.
void App::drawBig() {
  SatEntry* s = activeSat();
  if (!s) { canvas.setTextSize(2); canvas.setTextColor(CL_GREY, CL_BLACK);
            canvas.setCursor(6, 56); canvas.print("No satellite"); return; }

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  // Big-screen frequency string: 4 decimals (10 Hz) keeps even L-band birds inside
  // the panel at the large font, e.g. "145.9970" / "1296.5000".
  auto fmtBig = [](uint32_t hz) { return fmtMHzN(hz, 4); };

  // --- name + radio/rotator/tilt state badges (small) ---
  canvas.setTextSize(1);
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(4, 3); canvas.printf("%-.18s", s->name);
  canvas.setCursor(168, 3);
  canvas.setTextColor(radioOut ? CL_GREEN : CL_GREY, CL_BLACK); canvas.print("RAD");
  canvas.setCursor(198, 3);
  canvas.setTextColor(rotOut ? CL_GREEN : CL_GREY, CL_BLACK);   canvas.print("ROT");
  if (cfg.tiltTune && imuReady) {
    canvas.setCursor(138, 3);
    canvas.setTextColor(trackMode == 0 ? CL_CYAN : CL_GREY, CL_BLACK); canvas.print("TILT");
  }

  // --- RX / TX in the largest font that fits (size 4) ---
  uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
  if (activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
    Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
  }
  canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(2, 22); canvas.print("RX");
  canvas.setTextSize(4); canvas.setTextColor(CL_GREEN, CL_BLACK);
  canvas.setCursor(22, 18);
  canvas.print(activeTxCount > 0 ? fmtBig(rx).c_str() : "--.----");

  canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(2, 58); canvas.print("TX");
  canvas.setTextSize(4); canvas.setTextColor(CL_ORANGE, CL_BLACK);
  canvas.setCursor(22, 54);
  if (activeTxCount > 0 && ulOp) canvas.print(fmtBig(tx).c_str());
  else { canvas.setTextColor(CL_GREY, CL_BLACK); canvas.print("rx only"); }

  // --- Az / El + active Doppler tune mode (size 2) ---
  canvas.setTextSize(2);
  canvas.setTextColor(L.visible ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(4, 96);
  canvas.printf("Az%3.0f El%3.0f", L.az, L.el);
  if (timeIsSet() && L.visible && !L.sunlit) {        // in eclipse but workable
    canvas.setTextSize(1); canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(206, 90); canvas.print("ECL");
  }

  // --- bottom line: tune mode + passband position, mirroring Track ---
  canvas.setTextSize(1);
  if (activeTxCount > 0) {
    Transponder& t = activeTx[curTx];
    bool linear = t.isLinear && t.bandwidth() > 0;
    const char* mtag = (tuneMode == TM_FULL) ? "FULL"
                     : (tuneMode == TM_DL)   ? "DL"
                     : (tuneMode == TM_UL)   ? "UL"
                     : (trackMode == 0 ? "TUNE" : "CAL");
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(4, 118);
    if (linear && trackMode == 0) {
      float posk = (pbOffset - (int32_t)(t.bandwidth()/2)) / 1000.0f;
      canvas.printf("%s  PB %+.1fk%s", mtag, posk, t.invert ? " INV" : "");
    } else {
      canvas.printf("%s", mtag);
    }
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(176, 118);
    canvas.printf("TX%d/%d", curTx + 1, activeTxCount);
  }

  canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 128);
  canvas.print(imuReady ? ",/tune d=mode t r o l z=back y=tilt"
                        : ",/tune d=mode t r o l z=back");
}

void App::keyBig(char c, bool enter, bool back) {
  if (c == 'v') { toggleMemo(); return; }            // SD voice memo (record/stop)
  if (isBack(c, back) || c == 'z') { screen = SCR_TRACK; lastDrawMs = 0; return; }
  // Everything else that's valid on Track stays valid here: passband tuning
  // (,/; . and s/x), CAL trims, tune-mode cycling (m/d), transponder (t), radio
  // (r), rotator (o), tilt (y), and logging (l). Delegate to keyTrack so the
  // behaviour -- including the FULL/DL knob-driven guard -- is identical and lives
  // in one place. keyTrack won't navigate away for any of these, so we just keep
  // showing the big view.
  if (c == 'l') { beginQso(); return; }            // log keeps tracking
  bool nav = (c == 'f' || c == 'p' || c == 'g' || c == 'w' || c == 'e' ||
              c == 'c');                            // keys that leave Track
  if (!nav) {
    keyTrack(c, enter, back);
    if (screen == SCR_BIG) lastDrawMs = 0;          // stay on Big, force redraw
  }
}

// ---------------------------------------------------------------------------
//  Manual (no-radio) frequency-calculator mode. Same data and most options as
//  Track, but it never commands a radio or rotator. The operator fixes one leg
//  (the frequency they will hold on their own radio) and reads the Doppler-
//  corrected frequency they should tune the OTHER leg to. Saved calibration is
//  applied. For linear birds the fixed leg is tunable within the passband; for
//  FM the user picks which leg is fixed (typically the VHF one); downlink-only
//  sats just show the computed downlink.
// ---------------------------------------------------------------------------
void App::drawManual() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + " [MAN]") : String("Manual"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  // Az / El / range / range-rate (same as Track).
  canvas.setTextColor(L.visible ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(4, 20);
  canvas.printf("Az %5.1f  El %5.1f%s", L.az, L.el, L.visible ? " *" : "");
  { double age = gpAgeDays(*s);
    if (age >= 0) { canvas.setTextColor(ageColor(age), CL_BLACK);
                    canvas.setCursor(186, 20); canvas.printf("GP%4.1fd", age); } }
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(4, 31);
  canvas.printf("Rng %5.0fkm  Rate %+5.2f km/s", L.rangeKm, L.rangeRate);
  if (timeIsSet() && !L.sunlit) {
    canvas.setTextColor(CL_ORANGE, CL_BLACK);
    canvas.setCursor(214, 31); canvas.print("ECL");
  }

  if (activeTxCount == 0) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(4, 50); canvas.print("No transponder data.");
    footer("` back  t=tp  l p"); return;
  }

  Transponder& t = activeTx[curTx];
  bool linear = t.isLinear && t.bandwidth() > 0;
  uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
  Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
  Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);

  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(4, 44);
  canvas.printf("TX%d/%d %s%-.15s", curTx+1, activeTxCount,
                linear ? "[LIN] " : "", t.desc);

  bool haveUp = (ulOp != 0);
  // Which leg is fixed? User picks via 'u'. Downlink-only -> always downlink.
  bool fixUp = haveUp && manFixUp;

  // The FIXED leg is the frequency the operator parks on their own radio: show
  // its nominal value (operating freq + calibration, NO Doppler). The DERIVED
  // leg is what they must tune the other VFO to right now: operating freq +
  // Doppler + calibration (the rx/tx the propagator already computed).
  int64_t dnPark = (int64_t)dlOp + calDl;
  int64_t upPark = (int64_t)ulOp + calUl;

  // Row helper: label, value, and whether this is the fixed (HOLD) leg or the
  // derived (TUNE->) leg. Fixed = white/HOLD, derived = green/TUNE>.
  auto legRow = [&](int y, const char* leg, uint32_t hz, bool fixed) {
    canvas.setTextColor(fixed ? CL_WHITE : CL_GREEN, CL_BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s %s", leg, fmtMHz(hz).c_str());
    canvas.setTextColor(fixed ? CL_GREY : CL_GREEN, CL_BLACK);
    canvas.setCursor(150, y);
    canvas.print(fixed ? "HOLD" : "TUNE>");
  };

  // Downlink row: parked nominal if fixing downlink, else the derived TUNE value.
  // When the UPLINK is the fixed leg on a LINEAR bird, the operator parks TX and
  // tunes RX to hear themselves, so the downlink must cancel the *round-trip*
  // Doppler (the bird hears the fixed uplink Doppler-shifted, then re-emits a
  // downlink that shifts again on the way down). For FM the legs are independent
  // channels, so the plain per-leg rx is correct.
  uint32_t dnTune = rx;
  if (haveUp && fixUp && linear)
    dnTune = Predictor::downlinkForFixedUplink(dlOp, ulOp, t.invert,
                                               L.rangeRate, calDl, calUl);
  legRow(56, "DN", (!fixUp) ? (uint32_t)(dnPark > 0 ? dnPark : 0) : dnTune, !fixUp);
  // Uplink row (if any): parked nominal if fixing uplink, else the derived TUNE
  // value. When the DOWNLINK is the fixed leg on a LINEAR bird, the operator is
  // parking RX to hear themselves, so the uplink must cancel the *round-trip*
  // Doppler (not just its own leg) -- otherwise their signal drifts off the
  // parked downlink. For FM (non-linear) the legs are independent channels, so
  // the plain per-leg tx is correct there.
  uint32_t upTune = tx;
  if (haveUp && !fixUp && linear)
    upTune = Predictor::uplinkForFixedDownlink(dlOp, ulOp, t.invert,
                                               L.rangeRate, calDl, calUl);
  if (haveUp) legRow(67, "UP", fixUp ? (uint32_t)(upPark > 0 ? upPark : 0) : upTune, fixUp);
  else { canvas.setTextColor(CL_GREY, CL_BLACK); canvas.setCursor(4, 67);
         canvas.print("UP  (receive only)"); }

  // Passband position (linear only) — the fixed leg is what the operator tunes
  // within the passband; the derived leg follows.
  if (linear) {
    float halfk = t.bandwidth() / 2000.0f;
    float posk  = (pbOffset - (int32_t)(t.bandwidth()/2)) / 1000.0f;
    canvas.setTextColor(trackMode == 0 ? CL_CYAN : CL_GREY, CL_BLACK);
    canvas.setCursor(4, 79);
    canvas.printf("PB %+.1fk bw%.1fk %s%s", posk, halfk,
                  t.invert ? "INV " : "", trackMode == 0 ? "<TUNE>" : "");
    if (cfg.tiltTune && imuReady && trackMode == 0) {
      canvas.setTextColor(CL_CYAN, CL_BLACK);
      canvas.setCursor(210, 79); canvas.print("TLT");
    }
  } else if (haveUp) {
    // FM: indicate the fixed leg and its band.
    canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(4, 79);
    bool dnVhf = (dlOp >= 144000000UL && dlOp <= 148000000UL);
    bool upVhf = (ulOp >= 144000000UL && ulOp <= 148000000UL);
    const char* hint = fixUp ? (upVhf ? "uplink fixed (VHF)" : "uplink fixed")
                             : (dnVhf ? "downlink fixed (VHF)" : "downlink fixed");
    canvas.print(hint);
  }

  // Calibration line (active in CAL mode), same semantics as Track.
  canvas.setTextColor(trackMode == 1 ? CL_YELLOW : CL_GREY, CL_BLACK);
  canvas.setCursor(4, 90);
  canvas.printf("cal DN%+ld UP%+ld st%ld%s",
                (long)calDl, (long)calUl,
                (long)(trackMode == 0 ? tuneStep : calStep),
                trackMode == 1 ? " <CAL>" : "");

  // Mode reminder line: no radio is being driven here.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 102);
  if (haveUp) canvas.printf("Manual: hold %s, tune the other", fixUp ? "UP" : "DN");
  else        canvas.print("Manual: tune RX to DN above");

  if (linear)
    footer(trackMode == 0 ? ",/tune u=leg s=stp x=ctr m=cal t l p `bk"
                          : ",/DN ;.UP u=leg s=stp x=0 m=tn t l p `bk");
  else
    footer(haveUp ? "u=leg m=cal t=tp l p ` back" : "m=cal t=tp l p ` back");
}

// Large-font version of the Manual calculator: the DN and UP frequencies a hand
// operator parks/tunes, in big digits, with the same round-trip Doppler maths as
// drawManual. Toggled from Manual with 'z'. No radio is driven here -- it's the
// no-CAT frequency aid -- but tilt/Doppler-mode plumbing is irrelevant since the
// Manual screen has no tuneMode knob-follow; the operator simply reads the two
// numbers and tunes by hand.
void App::drawManualBig() {
  SatEntry* s = activeSat();
  if (!s) { canvas.setTextSize(2); canvas.setTextColor(CL_GREY, CL_BLACK);
            canvas.setCursor(6, 56); canvas.print("No satellite"); return; }

  auto fmtBig = [](uint32_t hz) { return fmtMHzN(hz, 4); };

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  // --- name + range-rate badge (small) ---
  canvas.setTextSize(1);
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(4, 3); canvas.printf("%-.16s", s->name);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(150, 3); canvas.printf("%+5.2f km/s", L.rangeRate);
  if (cfg.tiltTune && imuReady) {
    canvas.setCursor(122, 3);
    canvas.setTextColor(trackMode == 0 ? CL_CYAN : CL_GREY, CL_BLACK); canvas.print("TILT");
  }

  if (activeTxCount == 0) {
    canvas.setTextSize(2); canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 56); canvas.print("No transponder");
    return;
  }

  Transponder& t = activeTx[curTx];
  bool linear = t.isLinear && t.bandwidth() > 0;
  uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
  Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
  Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
  bool haveUp = (ulOp != 0);
  bool fixUp  = haveUp && manFixUp;

  int64_t dnPark = (int64_t)dlOp + calDl;
  int64_t upPark = (int64_t)ulOp + calUl;
  uint32_t dnTune = rx;
  if (haveUp && fixUp && linear)
    dnTune = Predictor::downlinkForFixedUplink(dlOp, ulOp, t.invert, L.rangeRate, calDl, calUl);
  uint32_t upTune = tx;
  if (haveUp && !fixUp && linear)
    upTune = Predictor::uplinkForFixedDownlink(dlOp, ulOp, t.invert, L.rangeRate, calDl, calUl);

  uint32_t dnShow = (!fixUp) ? (uint32_t)(dnPark > 0 ? dnPark : 0) : dnTune;
  uint32_t upShow = fixUp    ? (uint32_t)(upPark > 0 ? upPark : 0) : upTune;

  // Big leg rows: HOLD leg in white, TUNE> leg in green, with a small tag.
  auto bigRow = [&](int yLab, int yVal, const char* leg, uint32_t hz, bool fixed, bool show) {
    canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
    canvas.setCursor(2, yLab); canvas.print(leg);
    canvas.setCursor(2, yLab + 10);
    canvas.setTextColor(fixed ? CL_WHITE : CL_GREEN, CL_BLACK);
    canvas.print(fixed ? "HOLD" : "TUNE");
    canvas.setTextSize(4);
    canvas.setTextColor(fixed ? CL_WHITE : (leg[0] == 'D' ? CL_GREEN : CL_ORANGE), CL_BLACK);
    canvas.setCursor(40, yVal);
    if (show) canvas.print(fmtBig(hz).c_str());
    else { canvas.setTextColor(CL_GREY, CL_BLACK); canvas.print("rx only"); }
  };

  bigRow(20, 18, "DN", dnShow, !fixUp, true);
  bigRow(60, 58, "UP", upShow, fixUp, haveUp);

  // --- bottom line: which leg is fixed + passband + footer ---
  canvas.setTextSize(2);
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(4, 98);
  if (haveUp) canvas.print(fixUp ? "Hold UP, tune DN" : "Hold DN, tune UP");
  else        canvas.print("Receive only");

  if (linear) {
    canvas.setTextSize(1); canvas.setTextColor(CL_ORANGE, CL_BLACK);
    float posk = (pbOffset - (int32_t)(t.bandwidth()/2)) / 1000.0f;
    canvas.setCursor(4, 118);
    canvas.printf("PB %+.1fk%s", posk, t.invert ? " INV" : "");
  }

  canvas.setTextSize(1); canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(4, 128);
  canvas.print(haveUp ? ",/tune u=leg s x t z=back" : ",/tune s x t z=back");
}

void App::keyManualBig(char c, bool enter, bool back) {
  if (c == 'v') { toggleMemo(); return; }            // SD voice memo (record/stop)
  if (isBack(c, back) || c == 'z') { screen = SCR_MANUAL; lastDrawMs = 0; return; }
  // Delegate the in-place controls (u leg, m TUNE/CAL, ,/; . tune, s/x, t) to
  // keyManual; suppress the keys that would navigate to another screen.
  if (c == 'l') { beginQso(); return; }
  bool nav = (c == 'f' || c == 'p' || c == 'g' || c == 'w' || c == 'e');
  if (!nav) {
    keyManual(c, enter, back);
    if (screen == SCR_MANUALBIG) lastDrawMs = 0;
  }
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
  // Labels sit just INSIDE the disc edge so the top "N" never overlaps the
  // header bar (several callers place the disc with its top near y=20).
  canvas.setCursor(cx - 2,     cy - R + 2); canvas.print("N");
  canvas.setCursor(cx - 2,     cy + R - 8); canvas.print("S");
  canvas.setCursor(cx + R - 6, cy - 3);     canvas.print("E");
  canvas.setCursor(cx - R + 2, cy - 3);     canvas.print("W");
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

  const int cx = 66, cy = 70, R = 44;   // plot centre + outer (horizon) radius

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

// ===========================================================================
//  OSCARLOCATOR live view (SCR_OSCAR): an azimuthal-equidistant "plotting
//  board" centred either on your station (QTH mode) or on a pole (polar mode).
//  Live: it follows the satellite's sub-point in real time, drawing the
//  graticule, a coarse coastline, the satellite marker + ground footprint, and
//  (QTH mode) range rings. Modelled on the OSCARLOCATOR simulator; the same
//  footprint/great-circle math used by the world map is reused here.
// ===========================================================================

// Azimuthal-equidistant projection onto the disc. Returns false if the point
// falls outside the plotted radius (rmaxDeg). mode 0 = QTH-centred (great-circle
// bearing/range from the observer), mode 1/2 = North/South polar.
//   qth   : t = azimuth, 0=N at top, clockwise
//   polarN: t = longitude, rho = 90-lat   (north pole at centre)
//   polarS: t = longitude, rho = 90+lat   (south pole at centre)
static bool oscarProject(int mode, double qlat, double qlon,
                         double lat, double lon, double rmaxDeg,
                         int cx, int cy, int R, int& sx, int& sy) {
  const double D2R = 0.0174532925199433, R2D = 57.2957795130823;
  double rhoDeg, tdeg;
  if (mode == 0) {
    // central angle + initial bearing from (qlat,qlon) to (lat,lon)
    double p1 = qlat * D2R, p2 = lat * D2R, dl = (lon - qlon) * D2R;
    double ca = sin(p1) * sin(p2) + cos(p1) * cos(p2) * cos(dl);
    if (ca > 1) ca = 1; if (ca < -1) ca = -1;
    rhoDeg = acos(ca) * R2D;
    double y = sin(dl) * cos(p2);
    double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
    tdeg = atan2(y, x) * R2D; if (tdeg < 0) tdeg += 360.0;
  } else if (mode == 1) {                 // north polar
    rhoDeg = 90.0 - lat; tdeg = lon;
  } else {                                // south polar
    rhoDeg = 90.0 + lat; tdeg = lon;
  }
  if (rhoDeg > rmaxDeg) return false;
  double r = rhoDeg / rmaxDeg * R;
  double a = tdeg * D2R;
  if (mode == 0)      { sx = cx + (int)lround(r * sin(a)); sy = cy - (int)lround(r * cos(a)); }
  else if (mode == 1) { sx = cx + (int)lround(r * sin(a)); sy = cy + (int)lround(r * cos(a)); }
  else                { sx = cx - (int)lround(r * sin(a)); sy = cy + (int)lround(r * cos(a)); }
  return true;
}

// Sample one full orbital period of sub-points centred on "now", cached and held
// static so the ground-track arc covers the whole disc like a real OSCARLOCATOR
// (off-disc parts are clipped by the projection). Also record the AOS/LOS of the
// current/next pass so those markers can be placed on the track. Rebuilt only
// once the cached window has elapsed.
void App::buildOscarArc() {
  oscarArcN = 0; oscarArcEnd = 0; oscarArcAos = 0; oscarArcLos = 0;
  SatEntry* s = activeSat();
  if (!s || !timeIsSet() || s->meanMotion <= 0) return;
  pred.setSite(loc.obs());
  if (!pred.setSat(*s)) return;
  time_t now = nowUtc();
  double periodMin = 1440.0 / s->meanMotion;          // orbital period (minutes)
  time_t periodS = (time_t)llround(periodMin * 60.0);
  if (periodS < 60) periodS = 60;
  // Centre the track on now: half an orbit back, half forward, so the satellite
  // marker sits in the middle of the drawn track.
  time_t t0 = now - periodS / 2;
  for (int i = 0; i < OSCAR_ARC_PTS; ++i) {
    time_t t = t0 + (time_t)llround((double)periodS * i / (double)(OSCAR_ARC_PTS - 1));
    LiveLook A = pred.look(t);
    oscarArcLat[i] = (float)A.subLat;
    oscarArcLon[i] = (float)A.subLon;
  }
  oscarArcN = OSCAR_ARC_PTS;
  oscarArcEnd = now + periodS / 2;                    // rebuild after the track scrolls off

  // Find the current/next pass so AOS/LOS markers can be drawn on the track.
  PassPredict pp[3];
  int np = pred.predictPasses(now - 1800, 0.5f, pp, 3);
  for (int i = 0; i < np; ++i) if (pp[i].los > now) { oscarArcAos = pp[i].aos; oscarArcLos = pp[i].los; break; }
}

void App::drawOscar() {
  SatEntry* s = activeSat();
  const char* modeName = (oscarMode == 0) ? "QTH" : "Polar";
  header(s ? (String(s->name) + "  OSCAR " + modeName) : String("OSCARLOCATOR"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 60); canvas.print("Clock not set (GPS or Location -> c).");
    footer("m mode  ` back");
    return;
  }

  // Centre low enough that the disc (and its N/S edge labels) clear the 16 px
  // header bar: top of the disc is cy-R = 24, and the labels sit just inside it.
  const int cx = 70, cy = 76, R = 52;
  const Observer& o = loc.obs();
  const double qlat = o.lat, qlon = o.lon;

  // Resolve effective mode: QTH (0) as chosen; polar picks N/S from the sat's
  // current sub-point hemisphere so the bird stays on the visible sheet.
  pred.setSite(o); pred.setSat(*s);
  LiveLook L = pred.look(nowUtc());
  int pm = 0;
  if (oscarMode != 0) pm = (L.subLat >= 0) ? 1 : 2;

  // Plotted radius: polar shows a full hemisphere (90 deg); QTH shows out to a
  // little past the antipode-limited useful range (clamped 50..80 deg).
  double rmax = 90.0;
  if (pm == 0) { rmax = fabs(qlat) + 25.0; if (rmax < 50) rmax = 50; if (rmax > 80) rmax = 80; }

  // disc + facecolor
  canvas.fillCircle(cx, cy, R, CL_BLACK);
  // graticule: range/elevation rings
  canvas.drawCircle(cx, cy, R, CL_GREY);
  canvas.drawCircle(cx, cy, (R * 2) / 3, CL_DGREY);
  canvas.drawCircle(cx, cy, R / 3, CL_DGREY);
  canvas.drawLine(cx, cy - R, cx, cy + R, CL_DGREY);
  canvas.drawLine(cx - R, cy, cx + R, cy, CL_DGREY);
  canvas.drawPixel(cx, cy, CL_WHITE);

  // Coarse coastline, reprojected through the disc. Break the polyline whenever
  // a vertex leaves the disc or a segment would jump across it.
  {
    int px = 0, py = 0; bool pen = false;
    for (int i = 0; i + 1 < COAST_N; i += 2) {
      int lo = COAST[i], la = COAST[i + 1];
      int x, y;
      if (oscarProject(pm, qlat, qlon, (double)la, (double)lo, rmax, cx, cy, R, x, y)) {
        if (pen) {
          long dx = x - px, dy = y - py;
          if (dx * dx + dy * dy < (long)(R * R))     // skip wild cross-disc jumps
            canvas.drawLine(px, py, x, y, CL_MGREY);
        }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // Compass / pole labels. The top label sits just INSIDE the disc top edge
  // (cy-R+2) rather than above it, so it never overlaps the header bar.
  canvas.setTextColor(CL_GREY, CL_BLACK);
  if (pm == 0) {
    canvas.setCursor(cx - 2, cy - R + 2); canvas.print("N");
    canvas.setCursor(cx - 2, cy + R - 8); canvas.print("S");
    canvas.setCursor(cx + R - 6, cy - 3); canvas.print("E");
    canvas.setCursor(cx - R + 2, cy - 3); canvas.print("W");
  } else {
    canvas.setCursor(cx - 8, cy - R + 2);
    canvas.print(pm == 1 ? "N pole" : "S pole");
  }

  // QTH range ring (amber, dashed): the satellite's footprint radius at MEAN
  // altitude, centred on the station. Inside this great-circle ring the bird is
  // above the horizon and workable. Drawn dashed so it stays distinct from the
  // (same-radius) instantaneous footprint circle when the sat is near your QTH.
  // Re-projects correctly in both QTH (a circle centred on you) and polar (an
  // off-centre oval) modes.
  if (s->meanMotion > 0.0) {
    const double D2R = 0.0174532925199433, R2D = 57.2957795130823, RE = 6371.0;
    const double MU = 398600.4418;
    double nRadSec = s->meanMotion * 2.0 * M_PI / 86400.0;       // rev/day -> rad/s
    double aKm = cbrt(MU / (nRadSec * nRadSec));                 // semi-major axis
    double meanAltKm = aKm - RE; if (meanAltKm < 1) meanAltKm = 1;
    double rng = acos(RE / (RE + meanAltKm)) * R2D;              // footprint radius (deg)
    double la1 = qlat * D2R, lo1 = qlon * D2R;
    double sb = sin(rng * D2R), cb = cos(rng * D2R), s1 = sin(la1), c1 = cos(la1);
    int px = 0, py = 0; bool pen = false; int seg = 0;
    for (int a = 0; a <= 360; a += 9) {
      double az = a * D2R;
      double la2 = asin(s1 * cb + c1 * sb * cos(az));
      double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
      double la2d = la2 * R2D, lo2d = lo2 * R2D;
      while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
      int x, y;
      if (oscarProject(pm, qlat, qlon, la2d, lo2d, rmax, cx, cy, R, x, y)) {
        if (pen && (seg & 1)) {        // draw every other segment -> dashed
          long dx = x - px, dy = y - py;
          if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_ORANGE);
        }
        px = x; py = y; pen = true; seg++;
      } else { pen = false; }
    }
  }

  // Satellite footprint (green outline) at the live sub-point. Drawn before the
  // pass arc so the arc and marker sit on top (most important features).
  if (L.satAltKm > 1.0) {
    const double D2R = 0.0174532925199433, R2D = 57.2957795130823, RE = 6371.0;
    double beta = acos(RE / (RE + L.satAltKm)) * R2D;     // footprint angular radius
    double la1 = L.subLat * D2R, lo1 = L.subLon * D2R;
    double sb = sin(beta * D2R), cb = cos(beta * D2R), s1 = sin(la1), c1 = cos(la1);
    int px = 0, py = 0; bool pen = false;
    for (int a = 0; a <= 360; a += 15) {
      double az = a * D2R;
      double la2 = asin(s1 * cb + c1 * sb * cos(az));
      double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
      double la2d = la2 * R2D, lo2d = lo2 * R2D;
      while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
      int x, y;
      if (oscarProject(pm, qlat, qlon, la2d, lo2d, rmax, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_DGREEN); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // Ground-track arc (blue): the full orbital track across the disc, drawn from
  // the cached sub-points (off-disc parts clipped by the projection), like a real
  // OSCARLOCATOR. Built once and held static; rebuilt once the track scrolls off.
  // AOS/LOS of the current pass are marked along the track (green/orange); a white
  // arrowhead at the track midpoint (= now) shows the direction of travel.
  bool needArc = (oscarArcN < 2) || (oscarArcEnd != 0 && nowUtc() > oscarArcEnd);
  // If no track could be built, only retry every few seconds (avoid per-frame SGP4).
  if (needArc && oscarArcN < 2 && oscarArcTriedMs != 0 &&
      (millis() - oscarArcTriedMs) < 5000) needArc = false;
  if (needArc) { oscarArcTriedMs = millis(); buildOscarArc(); }
  if (oscarArcN >= 2) {
    int px = 0, py = 0; bool pen = false;
    int midX = 0, midY = 0, nextX = 0, nextY = 0; bool haveMid = false, haveNext = false;
    for (int i = 0; i < oscarArcN; ++i) {
      int x, y;
      if (oscarProject(pm, qlat, qlon, oscarArcLat[i], oscarArcLon[i], rmax, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_BLUE); }
        if (i == oscarArcN / 2)     { midX = x; midY = y; haveMid = true; }
        if (i == oscarArcN / 2 + 1) { nextX = x; nextY = y; haveNext = true; }
        px = x; py = y; pen = true;
      } else pen = false;
    }
    // AOS / LOS markers, placed at their time positions along the track. The
    // track spans [now - P/2, now + P/2]; map a pass time to a sample index.
    time_t now2 = nowUtc();
    double periodS2 = (s->meanMotion > 0) ? (86400.0 / s->meanMotion) : 0;
    if (periodS2 > 1) {
      time_t t0 = now2 - (time_t)llround(periodS2 / 2.0);
      auto markAt = [&](time_t tt, uint16_t col, bool fill) {
        double f = (double)(tt - t0) / periodS2;
        if (f < 0 || f > 1) return;
        int idx = (int)lround(f * (oscarArcN - 1));
        if (idx < 0) idx = 0; if (idx >= oscarArcN) idx = oscarArcN - 1;
        int x, y;
        if (oscarProject(pm, qlat, qlon, oscarArcLat[idx], oscarArcLon[idx], rmax, cx, cy, R, x, y)) {
          if (fill) canvas.fillCircle(x, y, 2, col); else canvas.drawCircle(x, y, 3, col);
        }
      };
      if (oscarArcAos != 0) markAt(oscarArcAos, CL_GREEN, false);   // AOS ring
      if (oscarArcLos != 0) markAt(oscarArcLos, CL_ORANGE, true);   // LOS dot
    }
    // travel-direction arrowhead at the track midpoint (= now)
    if (haveMid && haveNext) {
      double ddx = nextX - midX, ddy = nextY - midY, dl = sqrt(ddx*ddx + ddy*ddy);
      if (dl > 0.5) {
        ddx /= dl; ddy /= dl;
        double ex = -ddy, ey = ddx;
        int tx = midX + (int)lround(5*ddx),  ty = midY + (int)lround(5*ddy);
        int b1x = midX + (int)lround(-2*ddx + 2*ex), b1y = midY + (int)lround(-2*ddy + 2*ey);
        int b2x = midX + (int)lround(-2*ddx - 2*ex), b2y = midY + (int)lround(-2*ddy - 2*ey);
        canvas.fillTriangle(tx, ty, b1x, b1y, b2x, b2y, CL_WHITE);
      }
    }
  }

  // The satellite marker itself.
  int satx, saty;
  bool onDisc = oscarProject(pm, qlat, qlon, L.subLat, L.subLon, rmax, cx, cy, R, satx, saty);
  if (onDisc) {
    uint16_t col = L.sunlit ? CL_YELLOW : CL_CYAN;
    canvas.fillCircle(satx, saty, 3, col);
    canvas.drawCircle(satx, saty, 4, CL_BLACK);
  }

  // QTH marker (amber star-ish dot at centre in QTH mode; a small ring in polar).
  if (pm == 0) canvas.fillCircle(cx, cy, 2, CL_ORANGE);

  // ---- right-hand readout ----
  int rx = 140;
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 20);
  canvas.printf("Sub %.1f%c", fabs(L.subLat), L.subLat >= 0 ? 'N' : 'S');
  canvas.setCursor(rx, 32);
  canvas.printf("    %.1f%c", fabs(L.subLon), L.subLon >= 0 ? 'E' : 'W');
  canvas.setCursor(rx, 48);
  if (L.el > 0) { canvas.setTextColor(CL_GREEN, CL_BLACK);
                  canvas.printf("El %.0f%c", L.el, 248); }
  else          { canvas.setTextColor(CL_GREY, CL_BLACK);
                  canvas.printf("El %.0f%c", L.el, 248); }
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 60); canvas.printf("Az %.0f%c", L.az, 248);
  canvas.setCursor(rx, 72); canvas.printf("Rng %.0fkm", L.rangeKm);
  canvas.setCursor(rx, 84); canvas.printf("Alt %.0fkm", L.satAltKm);
  canvas.setTextColor(L.sunlit ? CL_YELLOW : CL_CYAN, CL_BLACK);
  canvas.setCursor(rx, 96); canvas.print(L.sunlit ? "sunlit" : "eclipse");
  if (!onDisc) { canvas.setTextColor(CL_GREY, CL_BLACK);
                 canvas.setCursor(rx, 108); canvas.print("(off chart)"); }

  footer("m mode  ` back");
}

// ===========================================================================
//  3D globe view (SCR_GLOBE): an orthographic wireframe Earth that auto-follows
//  the selected satellite. The globe rotates so the satellite's sub-point stays
//  centred; a day/night terminator and all favourites are drawn. Arrow keys nudge
//  the view; ENTER re-snaps to follow. Front hemisphere only (z>=0 culled).
// ===========================================================================

// Sub-solar point (geographic lat/lon where the Sun is overhead) at time t.
// Reuses the same low-precision solar model as the Sun az/el code: subsolar
// latitude = solar declination, subsolar longitude = RA - GMST.
static void subSolarPoint(time_t t, double& slat, double& slon) {
  const double D2R = 0.017453292519943295, R2D = 57.29577951308232;
  double d  = ((double)t - 946728000.0) / 86400.0;          // days since J2000.0
  double ecl = (23.4393 - 3.563e-7 * d) * D2R;
  double gmst = fmod(280.46061837 + 360.98564736629 * d, 360.0);
  if (gmst < 0) gmst += 360.0;
  double g = (357.529 + 0.98560028 * d) * D2R;
  double L = fmod(280.459 + 0.98564736 * d, 360.0) * D2R;
  double lon = L + (1.915 * sin(g) + 0.020 * sin(2 * g)) * D2R;
  double X = cos(lon), Y = cos(ecl) * sin(lon), Z = sin(ecl) * sin(lon);
  double ra = atan2(Y, X) * R2D, dec = atan2(Z, sqrt(X * X + Y * Y)) * R2D;
  slat = dec;
  slon = ra - gmst;                                          // degrees
  while (slon < -180) slon += 360; while (slon > 180) slon -= 360;
}

// Orthographic projection: rotate a geographic point by the view centre, then
// drop Z. Returns false if the point is on the far hemisphere (z < 0, hidden).
// Precomputed sin/cos of the view lat/lon are passed in for speed.
static bool globeProject(double lat, double lon, double svlat, double cvlat,
                         double vlon, int cx, int cy, int R, int& sx, int& sy) {
  const double D2R = 0.017453292519943295;
  double la = lat * D2R, dlo = (lon - vlon) * D2R;
  double x = cos(la) * sin(dlo);
  double y = cos(la) * cos(dlo);
  double z = sin(la);
  // Rotate about the X axis by the view latitude so the view centre (lat=vlat,
  // dlo=0) maps to the front pole (yr=0, zr=1): screen-centre, facing viewer.
  double yr = z * cvlat - y * svlat;    // vertical on screen (+ = up = north)
  double zr = y * cvlat + z * svlat;    // + = toward viewer (front hemisphere)
  if (zr < 0) return false;             // far side, hidden
  sx = cx + (int)lround(x  * R);
  sy = cy - (int)lround(yr * R);
  return true;
}

void App::drawGlobe() {
  SatEntry* s = activeSat();
  header(s ? (String(s->name) + "  Globe") : String("Globe"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }
  if (!timeIsSet()) {
    canvas.setTextColor(CL_YELLOW, CL_BLACK);
    canvas.setCursor(6, 60); canvas.print("Clock not set (GPS or Location).");
    footer("` back");
    return;
  }

  const int cx = 70, cy = 70, R = 56;
  time_t now = nowUtc();
  pred.setSite(loc.obs());
  pred.setSat(*s);
  LiveLook L = pred.look(now);

  // Auto-follow: keep the selected satellite's sub-point centred.
  if (globeFollow) { globeViewLat = L.subLat; globeViewLon = L.subLon; }
  double sv = sin(globeViewLat * 0.017453292519943295);
  double cv = cos(globeViewLat * 0.017453292519943295);

  // Globe body + limb.
  canvas.fillCircle(cx, cy, R, CL_BLACK);
  canvas.drawCircle(cx, cy, R, CL_GREY);

  // Graticule: meridians every 30 deg, parallels every 30 deg (front side only).
  for (int lon = -180; lon < 180; lon += 30) {
    int px = 0, py = 0; bool pen = false;
    for (int lat = -90; lat <= 90; lat += 10) {
      int x, y;
      if (globeProject(lat, lon, sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) canvas.drawLine(px, py, x, y, CL_DGREY);
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }
  for (int lat = -60; lat <= 60; lat += 30) {
    int px = 0, py = 0; bool pen = false;
    for (int lon = -180; lon <= 180; lon += 10) {
      int x, y;
      if (globeProject(lat, lon, sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) canvas.drawLine(px, py, x, y, CL_DGREY);
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // Coastline (front hemisphere).
  {
    int px = 0, py = 0; bool pen = false;
    for (int i = 0; i + 1 < COAST_N; i += 2) {
      int x, y;
      if (globeProject((double)COAST[i + 1], (double)COAST[i], sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_MGREY); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // Ground-track trail: the selected satellite's full-orbit ground track (the
  // same cached track the OSCARLOCATOR view uses), reprojected onto the globe and
  // clipped at the limb. Built once per orbit and held static.
  if (oscarArcN < 2 || (oscarArcEnd != 0 && now > oscarArcEnd)) {
    if (oscarArcTriedMs == 0 || (millis() - oscarArcTriedMs) >= 5000) {
      oscarArcTriedMs = millis(); buildOscarArc();
    }
  }
  if (oscarArcN >= 2) {
    int px = 0, py = 0; bool pen = false;
    for (int i = 0; i < oscarArcN; ++i) {
      int x, y;
      if (globeProject(oscarArcLat[i], oscarArcLon[i], sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_BLUE); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // Day/night terminator: the great circle 90 deg from the sub-solar point.
  {
    double slat, slon; subSolarPoint(now, slat, slon);
    const double D2R = 0.017453292519943295, R2D = 57.29577951308232;
    double la1 = slat * D2R, lo1 = slon * D2R;
    double sb = sin(90.0 * D2R), cb = cos(90.0 * D2R), s1 = sin(la1), c1 = cos(la1);
    int px = 0, py = 0; bool pen = false;
    for (int a = 0; a <= 360; a += 6) {
      double az = a * D2R;
      double la2 = asin(s1 * cb + c1 * sb * cos(az));
      double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
      double la2d = la2 * R2D, lo2d = lo2 * R2D;
      while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
      int x, y;
      if (globeProject(la2d, lo2d, sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_YELLOW); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // QTH marker.
  {
    const Observer& o = loc.obs();
    int x, y;
    if (globeProject(o.lat, o.lon, sv, cv, globeViewLon, cx, cy, R, x, y)) {
      canvas.drawLine(x - 2, y, x + 2, y, CL_WHITE);
      canvas.drawLine(x, y - 2, x, y + 2, CL_WHITE);
    }
  }

  // Second (DX) location, if a grid has been entered (g): an orange box marker
  // plus its footprint ring at the SAME altitude as the selected satellite, so the
  // overlap of the two footprints is the live mutual-visibility region for that
  // satellite. Clipped at the limb like everything else.
  if (dxGrid[0] && L.satAltKm > 1.0) {
    int dx0, dy0;
    bool dxFront = globeProject(dxLat, dxLon, sv, cv, globeViewLon, cx, cy, R, dx0, dy0);
    if (dxFront) {
      canvas.drawRect(dx0 - 2, dy0 - 2, 5, 5, CL_ORANGE);
    }
    // DX footprint (orange) at the satellite's altitude.
    const double D2R = 0.017453292519943295, R2D = 57.29577951308232, RE = 6371.0;
    double beta = acos(RE / (RE + L.satAltKm)) * R2D;
    double la1 = dxLat * D2R, lo1 = dxLon * D2R;
    double sb = sin(beta * D2R), cb = cos(beta * D2R), s1 = sin(la1), c1 = cos(la1);
    int px = 0, py = 0; bool pen = false;
    for (int a = 0; a <= 360; a += 10) {
      double az = a * D2R;
      double la2 = asin(s1 * cb + c1 * sb * cos(az));
      double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
      double la2d = la2 * R2D, lo2d = lo2 * R2D;
      while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
      int x, y;
      if (globeProject(la2d, lo2d, sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) { long ddx = x - px, ddy = y - py;
                   if (ddx * ddx + ddy * ddy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_ORANGE); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  // All favourites as dots (selected sat drawn last, larger). Non-selected use a
  // dim green dot when on the visible hemisphere.
  int selX = -1, selY = 0;
  for (int i = 0; i < favN; ++i) {
    int idx = db.indexOfNorad(favs[i]);
    if (idx < 0) continue;
    SatEntry& f = db.at(idx);
    if (!pred.setSat(f)) continue;
    LiveLook FL = pred.look(now);
    int x, y;
    if (!globeProject(FL.subLat, FL.subLon, sv, cv, globeViewLon, cx, cy, R, x, y)) continue;
    if (f.norad == s->norad) { selX = x; selY = y; }
    else canvas.fillCircle(x, y, 1, CL_DGREEN);
  }
  // restore predictor to the selected sat for the readout
  pred.setSat(*s);

  // Selected satellite's ground footprint: the small circle at central angle beta
  // around its sub-point. globeProject culls far-side points, so the footprint is
  // drawn only where it falls on the visible hemisphere (it wraps around the limb
  // naturally, exactly like the coastline).
  if (L.satAltKm > 1.0) {
    const double D2R = 0.017453292519943295, R2D = 57.29577951308232, RE = 6371.0;
    double beta = acos(RE / (RE + L.satAltKm)) * R2D;     // footprint angular radius
    double la1 = L.subLat * D2R, lo1 = L.subLon * D2R;
    double sb = sin(beta * D2R), cb = cos(beta * D2R), s1 = sin(la1), c1 = cos(la1);
    int px = 0, py = 0; bool pen = false;
    for (int a = 0; a <= 360; a += 10) {
      double az = a * D2R;
      double la2 = asin(s1 * cb + c1 * sb * cos(az));
      double lo2 = lo1 + atan2(sin(az) * sb * c1, cb - s1 * sin(la2));
      double la2d = la2 * R2D, lo2d = lo2 * R2D;
      while (lo2d < -180) lo2d += 360; while (lo2d > 180) lo2d -= 360;
      int x, y;
      if (globeProject(la2d, lo2d, sv, cv, globeViewLon, cx, cy, R, x, y)) {
        if (pen) { long dx = x - px, dy = y - py;
                   if (dx * dx + dy * dy < (long)(R * R)) canvas.drawLine(px, py, x, y, CL_DGREEN); }
        px = x; py = y; pen = true;
      } else pen = false;
    }
  }

  if (selX >= 0) {
    uint16_t col = L.sunlit ? CL_YELLOW : CL_CYAN;
    canvas.fillCircle(selX, selY, 3, col);
    canvas.drawCircle(selX, selY, 4, CL_BLACK);
  }

  // Right-hand readout.
  int rx = 140;
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 20); canvas.printf("Sub %.1f%c", fabs(L.subLat), L.subLat >= 0 ? 'N' : 'S');
  canvas.setCursor(rx, 32); canvas.printf("    %.1f%c", fabs(L.subLon), L.subLon >= 0 ? 'E' : 'W');
  canvas.setTextColor(L.el > 0 ? CL_GREEN : CL_GREY, CL_BLACK);
  canvas.setCursor(rx, 48); canvas.printf("El %.0f%c", L.el, 248);
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(rx, 60); canvas.printf("Az %.0f%c", L.az, 248);
  canvas.setCursor(rx, 72); canvas.printf("Alt %.0fkm", L.satAltKm);
  canvas.setTextColor(L.sunlit ? CL_YELLOW : CL_CYAN, CL_BLACK);
  canvas.setCursor(rx, 84); canvas.print(L.sunlit ? "sunlit" : "eclipse");
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(rx, 96); canvas.print(globeFollow ? "follow" : "free");
  if (dxGrid[0]) { canvas.setTextColor(CL_ORANGE, CL_BLACK);
                   canvas.setCursor(rx, 108); canvas.printf("DX %s", dxGrid); }
  else           { canvas.setTextColor(CL_GREY, CL_BLACK);
                   canvas.setCursor(rx, 108); canvas.printf("%d favs", favN); }

  footer("arrows turn  g DX  ENT follow  `bk");
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
  footer("e/o/a grd p gps s src c clk v=pos ENT sky");
}

void App::drawUpdate() {
  header("Update");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 24);
  canvas.print(String("k / ENT : update GP (") + gpSourceLabel() + ")");
  canvas.setCursor(6, 38); canvas.print("f       : fast (GP + fav TX)");
  canvas.setCursor(6, 52); canvas.print("a       : cache ALL transponders");
  canvas.setCursor(6, 66); canvas.print("w       : connect WiFi only");
  canvas.setTextColor(CL_CYAN, CL_BLACK);
  canvas.setCursor(6, 82); canvas.print("k adds space wx + weather;");
  canvas.setCursor(6, 92); canvas.print("f does GP+AMSAT+fav TX.");
  canvas.setTextColor(CL_WHITE, CL_BLACK);
  canvas.setCursor(6, 106);
  canvas.printf("Sats: %d   Favs: %d", db.count(), favN);
  footer("` back");
}

void App::drawSettings() {
  header(setCat < 0 ? "Settings" : SET_CAT_NAME[setCat]);
  canvas.setTextSize(1);
  const int N = 63;
  String rows[N];
  rows[0]  = String("Radio: ") + RADIOS[cfg.radioModel].name;
  rows[1]  = String("CI-V addr: ") + String(cfg.civAddr, HEX);
  rows[2]  = String("CAT baud: ") + String(cfg.civBaud);
  rows[3]  = String("Min pass el: ") + String((int)cfg.minPassEl) + " deg";
  rows[4]  = String("WiFi SSID: ") + cfg.ssid;
  rows[5]  = String("WiFi pass: ") + String(strlen(cfg.pass) ? "******" : "(none)");
  rows[6]  = String("Save & test WiFi");
  rows[50] = String("WiFi 2 SSID: ") + String(strlen(cfg.ssid2) ? cfg.ssid2 : "(none)");
  rows[51] = String("WiFi 2 pass: ") + String(strlen(cfg.pass2) ? "******" : "(none)");
  rows[52] = String("Web control: ") + (cfg.webEnable ? "on" : "off")
             + (cfg.webEnable && net.connected()
                ? String(" (http://") + WiFi.localIP().toString() + ")" : String(""));
  rows[53] = String("Web port: ") + String(cfg.webPort);
  rows[7]  = String("AOS alarm: ") + (cfg.aosAlarm ? "on" : "off");
  rows[54] = String("IR pass beacon: ") + (cfg.irBeacon ? "on" : "off");
  rows[55] = String("LoRa msg: ") + (cfg.loraEnable ? "on" : "off");
  rows[56] = String("LoRa freq: ") + String(cfg.loraFreqKHz / 1000.0, 3) + " MHz";
  rows[57] = String("LoRa SF: ") + String(cfg.loraSf);
  rows[58] = String("LoRa BW: ") + String(cfg.loraBwHz / 1000) + " kHz";
  rows[59] = String("LoRa TX pwr: ") + String(cfg.loraTxDbm) + " dBm";
  { const char* rg = (cfg.loraRegion == 1) ? "EU 70cm" : (cfg.loraRegion == 2) ? "JP 430" : "US 33cm";
    rows[60] = String("LoRa region: ") + rg; }
  rows[8]  = String("Rotator: ") + (cfg.rotEnable ? "on" : "off");
  rows[9]  = String("Rot type: ") + (cfg.rotType == ROT_PST ? "PstRotator (net)"
                     : cfg.rotType == ROT_NET ? "rotctl (net)"
                     : cfg.rotType == ROT_YAESU ? "Yaesu (direct)"
                     : cfg.rotType == ROT_EASYCOMM1 ? "Easycomm I"
                     : cfg.rotType == ROT_EASYCOMM2 ? "Easycomm II"
                     : cfg.rotType == ROT_EASYCOMM3 ? "Easycomm III"
                     : cfg.rotType == ROT_SPID ? "SPID Rot2Prog" : "GS-232");
  {
    String h = cfg.rotHost[0] ? String(cfg.rotHost) : String("(not set)");
    if (h.length() > 18) h = "..." + h.substring(h.length() - 15);
    rows[10] = String("Net host: ") + h;
  }
  rows[11] = String("Net port: ") + String(cfg.rotPort);
  rows[12] = String("Rot baud: ") + String(cfg.rotBaud);
  rows[13] = String("Rot deadband: ") + String(cfg.rotDeadband) + " deg";
  rows[14] = String("Rot park az: ") + String(cfg.rotParkAz) + " deg";
  {
    uint16_t L = cfg.rotLeadSec;
    rows[15] = String("Rot pre-point: ") + (L == 0 ? String("off")
               : (L % 60 == 0) ? String(L / 60) + " min" : String(L) + " s");
  }
  rows[16] = String("Rot Az offset: ") + String(cfg.rotAzOff) + " deg";
  rows[17] = String("Rot El offset: ") + String(cfg.rotElOff) + " deg";
  rows[18] = String("Rot az range: ") + (cfg.rotAzRange == ROT_AZ_450 ? "0..450"
                     : cfg.rotAzRange == ROT_AZ_180 ? "-180..+180" : "0..360");
  rows[19] = String("Rot el range: ") + (cfg.rotFlip ? "180 deg (flip)" : "90 deg");
  rows[20] = String("GP source: ") + gpSourceLabel();
  rows[21] = String("VFO: ") + (cfg.vfoType == VFO_MAIN_UP_SUB_DOWN
                                ? "Main Up/Sub Dn" : "Main Dn/Sub Up");
  rows[22] = String("Sat mode: ") + (cfg.satMode ? "on" : "off");
  {
    uint32_t eff = effectiveCatRateMs();
    rows[23] = String("CAT rate: ") + String(cfg.catRateMs) + " ms";
    if (eff > cfg.catRateMs) rows[23] += " (min " + String(eff) + ")";
  }
  rows[24] = String("CAT delay: ") + String(cfg.catDelayMs) + " ms";
  rows[25] = String("Screen sleep: ") + (cfg.dimSecs == 0 ? String("off")
             : (cfg.dimSecs % 60 == 0) ? String(cfg.dimSecs / 60) + " min"
                                       : String(cfg.dimSecs) + " s");
  rows[26] = String("My callsign: ") + (cfg.myCall[0] ? cfg.myCall : "(not set)");
  rows[27] = String("Backup config+favs -> SD");
  rows[28] = String("Restore config+favs");
  rows[29] = String("Reset all data (erase)");
  rows[30] = String("CAT type: ") + (cfg.catType == CAT_RIGCTL ? "rigctl (net)"
                     : cfg.catType == CAT_NET ? "Icom LAN" : "Wired CI-V");
  {
    String h = cfg.catHost[0] ? String(cfg.catHost) : String("(not set)");
    if (h.length() > 17) h = "..." + h.substring(h.length() - 14);
    rows[31] = String(cfg.catType == CAT_RIGCTL ? "rigctld host: " : "LAN host: ") + h;
  }
  rows[32] = String(cfg.catType == CAT_RIGCTL ? "rigctld port: " : "LAN port: ") + String(cfg.catPort);
  rows[33] = String("LAN user: ") + (cfg.catUser[0] ? cfg.catUser : "(not set)");
  rows[34] = String("LAN pass: ") + String(strlen(cfg.catPass) ? "******" : "(none)");
  rows[35] = String("Rotator: manual control");
  rows[36] = String("Rigctld server: ") + (cfg.rigdEnable ? "on" : "off");
  rows[37] = String("Rigctld port: ") + String(cfg.rigdPort);
  rows[38] = String("Rotctld server: ") + (cfg.rotdEnable ? "on" : "off");
  rows[39] = String("Rotctld port: ") + String(cfg.rotdPort);
  rows[40] = String("Decay solar: ") + (cfg.solarAct == SOLAR_LOW ? String("min (long life)")
                     : cfg.solarAct == SOLAR_HIGH ? String("max (short life)")
                     : cfg.solarAct == SOLAR_AUTO ? (spaceF107 > 0
                         ? String("auto (F10.7 ") + String((int)lround(spaceF107)) + ")"
                         : String("auto (no data)"))
                     : String("mean"));
  rows[41] = String("QRZ user: ") + (cfg.qrzUser[0] ? cfg.qrzUser : "(not set)");
  rows[42] = String("QRZ pass: ") + String(strlen(cfg.qrzPass) ? "******" : "(none)");
  rows[43] = String("Weather units: ") + (cfg.wxUnits == WX_IMPERIAL ? "F, mph"
                     : cfg.wxUnits == WX_METRIC_MS ? "C, m/s" : "C, km/h");
  rows[44] = String("Dopp FM band: ") + String(cfg.doppThreshFmHz) + " Hz";
  rows[45] = String("Dopp linear band: ") + String(cfg.doppThreshLinHz) + " Hz";
  rows[46] = String("Dopp lead: ") + (cfg.doppLeadMs == 0 ? String("off")
                     : String(cfg.doppLeadMs) + " ms");
  rows[47] = String("Rot az lookahead: ") + (cfg.rotAzLookSec == 0 ? String("off")                     : String(cfg.rotAzLookSec) + " s");
  rows[48] = String("Brightness: ") + String((int)((cfg.bright * 100 + 127) / 255)) + "%";
  rows[49] = String("Tilt tuning: ") + (!imuReady ? String("n/a (no IMU)")
                                                   : (cfg.tiltTune ? String("on") : String("off")));
  rows[62] = String("Run CAT self-test");
  // ---- render: the category list, or the selected category's rows ----
  if (setCat < 0) {
    for (int v = 0; v < SET_CAT_N; ++v) {
      int y = 19 + v*11;
      if (v == setSel) { canvas.fillRect(0, y-1, 240, 11, CL_SELBG);
                         canvas.setTextColor(CL_BLACK, CL_SELBG); }
      else               canvas.setTextColor(CL_WHITE, CL_BLACK);
      canvas.setCursor(4, y); canvas.print(String(SET_CAT_NAME[v]) + "  >");
    }
    footer("; / . move   ENT open   ` back");
    return;
  }
  const int len = SET_CAT_LEN[setCat];
  const int VIS = 9;
  int scroll = (setSel >= VIS) ? (setSel - VIS + 1) : 0;
  for (int v = 0; v < VIS && (scroll + v) < len; ++v) {
    int idx = scroll + v;
    int ai  = SET_CAT_ROWS[setCat][idx];
    int y = 19 + v*11;
    bool danger = (ai == 29);                     // "Reset all data (erase)"
    if (idx == setSel) { canvas.fillRect(0, y-1, 240, 11, danger ? CL_RED : CL_SELBG);
                         canvas.setTextColor(CL_BLACK, danger ? CL_RED : CL_SELBG); }
    else                 canvas.setTextColor(danger ? CL_RED : CL_WHITE, CL_BLACK);
    canvas.setCursor(4, y); canvas.print(rows[ai]);
  }
  if (SET_CAT_ROWS[setCat][setSel] == 4) footer(",/ change  ENT edit  s scan  ` back");
  else                                   footer(",/ change  ENT edit  ` back");
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
    if (i == wifiSel) { canvas.fillRect(0, y - 1, 240, 11, CL_SELBG);
                        canvas.setTextColor(CL_BLACK, CL_SELBG); }
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
