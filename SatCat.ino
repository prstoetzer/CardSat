// ===========================================================================
//  SatCat.ino  -  M5Stack Cardputer ADV satellite tracker + CI-V Doppler
//
//  SINGLE-FILE Arduino IDE build. This sketch is the concatenation, in
//  dependency order, of the modular PlatformIO sources (config, radio
//  profiles, CI-V, satellite DB, location/GPS, networking, SGP4 prediction,
//  settings, and the app/UI state machine). The PlatformIO project remains
//  the reference layout; this file is generated for Arduino IDE users.
//
//  ---- REQUIRED LIBRARIES (install via Library Manager unless noted) ----
//    * M5Cardputer        (pulls in M5Unified + M5GFX)
//    * ArduinoJson        (v7.x)
//    * TinyGPSPlus
//    * Sgp4              <- Hopperpop SGP4, install from
//                            https://github.com/Hopperpop/Sgp4-Library
//                            (Sketch > Include Library > Add .ZIP Library)
//    WiFi / WiFiClientSecure / HTTPClient / LittleFS / HardwareSerial come
//    with the ESP32 Arduino core.
//
//  ---- BOARD SETTINGS ----
//    Board:      "M5StampS3" (or M5Stack > StampS3) — ESP32-S3, 8 MB flash
//    USB CDC On Boot: Enabled (for the serial monitor / USB-CDC CI-V option)
//    Partition:  any 8 MB scheme with a filesystem (e.g. "8M Flash (3MB
//                APP/1.5MB FATFS)" or a default 8 MB app+SPIFFS/LittleFS)
//    PSRAM:      Disabled (the StampS3A has none)
//
//  See README.md for wiring, the dual-UART GPS/CI-V caveat, the unverified
//  MAIN/SUB commands on older rigs, and passband-tracking usage.
// ===========================================================================

#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <TinyGPSPlus.h>
#include <Sgp4.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <time.h>
#include <math.h>


// =========================================================================
//  config.h
// =========================================================================
// ===========================================================================
//  config.h  -  compile-time configuration and shared constants
// ===========================================================================

// ---- Speed of light (m/s) used for Doppler ----
static constexpr double C_LIGHT = 299792458.0;

// ---------------------------------------------------------------------------
//  Data sources
// ---------------------------------------------------------------------------
//  AMSAT distributes Keplerian elements as *TLE text* (not JSON). Two files:
//    - daily-bulletin.txt : human bulletin (name + 2 lines, with header/footer)
//    - dailytle.txt       : "bare" 3-line stanzas, no header  <-- we use this
//  SatNOGS provides JSON for transponder/transmitter frequencies, and also
//  offers TLEs as JSON (/api/tle/) which can be used as a fallback.
//
//  If you prefer a JSON keps source, set USE_SATNOGS_TLE = true and the app
//  will pull TLEs from SatNOGS DB as JSON instead of AMSAT text.
// ---------------------------------------------------------------------------
#define AMSAT_TLE_URL      "https://www.amsat.org/tle/dailytle.txt"
#define AMSAT_BULLETIN_URL "https://www.amsat.org/tle/daily-bulletin.txt"

// SatNOGS DB REST API
#define SATNOGS_TX_URL     "https://db.satnogs.org/api/transmitters/?format=json&satellite__norad_cat_id="
#define SATNOGS_TLE_URL    "https://db.satnogs.org/api/tle/?format=json&norad_cat_id="

// ---------------------------------------------------------------------------
//  Serial / UART wiring
// ---------------------------------------------------------------------------
//  The Cardputer ADV exposes a 2x7 header. Free GPIOs broken out include
//  G1, G2 (and G13/G15 etc). We use Serial1 on these for *either*:
//     - a NMEA GPS (e.g. the LoRa+GPS cap / external GPS module), or
//     - the Icom CI-V bus through a TTL<->CI-V level interface.
//  Pick pins that match your wiring; defaults below are a common choice.
//
//  If you control the radio over USB-CDC instead (IC-9700/9100/7610 native
//  USB), set CIV_USE_USB_CDC = true and the app talks to the radio on the
//  USB Serial port (the same one used for the PC console will be shared, so
//  prefer the hardware UART when possible).
// ---------------------------------------------------------------------------
static constexpr int   GPS_UART_NUM   = 1;
static constexpr int   GPS_RX_PIN     = 1;     // <- ESP32-S3 GPIO from header (G1)
static constexpr int   GPS_TX_PIN     = 2;     // <- (G2)
static constexpr uint32_t GPS_BAUD    = 9600;

static constexpr int   CIV_UART_NUM   = 1;     // shares UART1 by default; if you
                                               // run GPS + CI-V simultaneously,
                                               // move CI-V to UART2 and set pins.
static constexpr int   CIV_RX_PIN     = 1;
static constexpr int   CIV_TX_PIN     = 2;
static constexpr bool  CIV_USE_USB_CDC = false;

// ---------------------------------------------------------------------------
//  Limits (kept modest - no PSRAM on the StampS3A)
// ---------------------------------------------------------------------------
static constexpr int   MAX_SATS        = 220;  // sats held in RAM from TLE file
static constexpr int   MAX_TX_PER_SAT  = 8;    // transmitters cached per sat
static constexpr int   PASS_LIST_LEN   = 12;   // passes shown per satellite

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define FILE_TLE     "/tle.txt"
#define FILE_CFG     "/config.json"
#define FILE_TXCACHE "/tx_%lu.json"   // %lu = norad id

// =========================================================================
//  radio_profiles.h
// =========================================================================
// ===========================================================================
//  radio_profiles.h  -  per-radio CI-V address + capability table
// ===========================================================================
//
//  Default CI-V addresses (verified against the standard Icom CI-V address
//  table):
//      IC-820  = 0x42      IC-910  = 0x60      IC-9100 = 0x7C
//      IC-821  = 0x4C      IC-970  = 0x2E      IC-9700 = 0xA2
//
//  MAIN/SUB band selection
//  -----------------------
//  Full-duplex satellite radios keep the downlink on the MAIN receiver and the
//  uplink on the SUB receiver. To drive both independently we must select the
//  band before sending a "set frequency" (0x05) command.
//
//   * IC-9700 / IC-9100 : documented to use CI-V cmd 0x07 with sub-code
//       0xD0 = select MAIN band, 0xD1 = select SUB band.  (Verified from the
//       IC-9700 CI-V reference guide.)
//   * IC-910            : also accepts 0x07 0xD0/0xD1 in practice.
//   * IC-820 / 821 / 970: these pre-date the 0xD0/0xD1 sub-codes. Their
//       behaviour is less uniform; the values below are a *best-effort* default
//       and are flagged unverified. If your radio does not track correctly,
//       consult its CI-V appendix and adjust selMain[]/selSub[] for that row.
//
//  Everything radio-specific lives in this one table so it is easy to tune.
// ===========================================================================

enum RadioModel : uint8_t {
  RIG_IC820 = 0,
  RIG_IC821,
  RIG_IC910,
  RIG_IC970,
  RIG_IC9100,
  RIG_IC9700,
  RIG_COUNT
};

struct RadioProfile {
  const char* name;
  uint8_t     civAddr;       // default CI-V address
  uint32_t    defaultBaud;   // typical default CI-V baud
  // Band-select command sequences (sent after 0xFE 0xFE addr 0xE0):
  uint8_t     selMain[3];    // bytes, length in selLen
  uint8_t     selSub[3];
  uint8_t     selLen;        // number of valid bytes in selMain/selSub
  bool        selVerified;   // true if the select sequence is documented
  bool        hasSatMode;    // radio has a dedicated satellite mode
};

// Order MUST match RadioModel enum.
static const RadioProfile RADIOS[RIG_COUNT] = {
  // name        addr   baud    selMain        selSub         len verified satMode
  { "IC-820",   0x42,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  false,   false },
  { "IC-821",   0x4C,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  false,   false },
  { "IC-910",   0x60,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true,    false },
  { "IC-970",   0x2E,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  false,   false },
  { "IC-9100",  0x7C,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true,    true  },
  { "IC-9700",  0xA2,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true,    true  },
};

// =========================================================================
//  civ.h
// =========================================================================
// ===========================================================================
//  civ.h  -  Icom CI-V controller (frame building + MAIN/SUB freq/mode set)
// ===========================================================================

// CI-V operating modes (data byte for command 0x06)
enum CivMode : uint8_t {
  CIV_LSB = 0x00, CIV_USB = 0x01, CIV_AM = 0x02, CIV_CW = 0x03,
  CIV_RTTY = 0x04, CIV_FM = 0x05, CIV_CWR = 0x07, CIV_RTTYR = 0x08
};

class CivRadio {
public:
  // begin() opens the chosen serial port. On ESP32-S3 the hardware UART can be
  // remapped to arbitrary pins. If useUsbCdc is true, the native USB CDC port
  // is used instead (for radios with built-in USB CI-V).
  void begin(RadioModel model, uint32_t baud,
             int uartNum, int rxPin, int txPin, bool useUsbCdc);

  void setModel(RadioModel m);
  void setAddress(uint8_t addr) { _addr = addr; }
  uint8_t address() const       { return _addr; }
  const RadioProfile& profile() const { return RADIOS[_model]; }

  // Independent MAIN (downlink/RX) and SUB (uplink/TX) control.
  bool setMainFreq(uint32_t hz);
  bool setSubFreq (uint32_t hz);
  bool setMainMode(CivMode m, uint8_t filter = 0x01);
  bool setSubMode (CivMode m, uint8_t filter = 0x01);

  // Convenience: push both legs of a Doppler update at once.
  bool updateDoppler(uint32_t rxHz, uint32_t txHz);

  // Try to put a sat-capable radio into satellite mode (no-op if unsupported).
  bool enableSatMode(bool on);

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a CivMode.
  static CivMode modeFromString(const String& s);

  bool ready() const { return _stream != nullptr; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model  = RIG_IC9700;
  uint8_t    _addr   = 0xA2;

  void   selectMain();
  void   selectSub();
  bool   sendFrame(const uint8_t* payload, size_t len);
  static void freqToBcd(uint32_t hz, uint8_t out[5]);
  bool   drainEcho(uint32_t timeoutMs = 60);  // CI-V is a shared bus: read back
};

// =========================================================================
//  satdb.h
// =========================================================================
// ===========================================================================
//  satdb.h  -  in-memory satellite catalog (slim) + transponder parsing
// ===========================================================================
//  RAM note: the StampS3A has ~512 KB internal SRAM and no PSRAM, so we keep
//  SatEntry small (no embedded transponder array). Transponders are parsed on
//  demand into a caller-supplied buffer for the *active* satellite only.
// ===========================================================================

struct Transponder {
  char     desc[40];
  uint32_t downlink     = 0; // Hz (downlink_low;  0 if none)
  uint32_t downlinkHigh = 0; // Hz (downlink_high; 0 if single-channel)
  uint32_t uplink       = 0; // Hz (uplink_low;    0 if none / beacon)
  uint32_t uplinkHigh   = 0; // Hz (uplink_high;   0 if single-channel)
  char     mode[12] = {0};   // e.g. "FM", "USB", "DATA"
  bool     invert   = false; // inverting linear transponder
  bool     isLinear = false; // true => has a tunable passband (do passband tracking)

  // Downlink passband width in Hz (0 for single-channel / FM).
  uint32_t bandwidth() const {
    return (downlinkHigh > downlink) ? (downlinkHigh - downlink) : 0;
  }
};

struct SatEntry {
  char     name[26];
  uint32_t norad = 0;
  char     line1[72];
  char     line2[72];
  bool     txLoaded = false;     // have we fetched transponders this session?
};

class SatDb {
public:
  bool begin();                  // mount LittleFS
  int  count() const { return _n; }
  SatEntry& at(int i) { return _sats[i]; }
  int  indexOfNorad(uint32_t norad) const;

  // Parse a bare 3-line AMSAT TLE text blob into the catalog (replaces it).
  int  loadTleFromText(const String& blob);
  bool loadTleFromFs();
  bool saveTleText(const String& blob);

  // Parse a SatNOGS /api/transmitters/ JSON array into out[0..maxN-1].
  // Returns number of (active) transponders parsed.
  static int parseTransmittersJson(const String& json,
                                   Transponder* out, int maxN);

  // Per-satellite transponder cache on LittleFS.
  static bool saveTxCache(uint32_t norad, const String& json);
  static int  loadTxCache(uint32_t norad, Transponder* out, int maxN);

private:
  SatEntry _sats[MAX_SATS];
  int      _n = 0;
};

// =========================================================================
//  location.h
// =========================================================================
// ===========================================================================
//  location.h  -  observer location (manual entry, grid square, or GPS)
// ===========================================================================

struct Observer {
  double lat = 0.0;     // degrees +N
  double lon = 0.0;     // degrees +E
  double altM = 0.0;    // metres
  bool   valid = false;
  bool   fromGps = false;
};

class Location {
public:
  // Start a NMEA GPS on the given UART/pins (LoRa+GPS cap or external module).
  void beginGps(int uartNum, int rxPin, int txPin, uint32_t baud);
  // Feed bytes from the GPS; call frequently from loop(). Updates obs if a fix
  // is available. Returns true when a new fix was just parsed.
  bool pollGps();

  void setManual(double lat, double lon, double altM);
  bool setFromGrid(const String& grid);    // Maidenhead -> lat/lon (centre)

  const Observer& obs() const { return _obs; }
  bool gpsHasFix() const { return _hasFix; }
  int  gpsSats()  const { return _sats; }

  static String toGrid(double lat, double lon);  // 6-char Maidenhead

private:
  Observer _obs;
  bool     _hasFix = false;
  int      _sats   = 0;
  bool     _gpsOn  = false;
};

// =========================================================================
//  net.h
// =========================================================================
// ===========================================================================
//  net.h  -  WiFi + HTTPS downloads (AMSAT keps, SatNOGS transponders)
// ===========================================================================

class Net {
public:
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
  bool connected();
  void syncTimeNtp();                       // sets system clock via NTP (UTC)

  // GET a URL over HTTPS into `out`. Returns false on HTTP/transport error.
  bool httpsGet(const String& url, String& out, size_t maxBytes = 200000);

  // Convenience wrappers.
  bool fetchAmsatTle(String& out);                  // bare 3-line text
  bool fetchSatnogsTransmitters(uint32_t norad, String& out);
};

// =========================================================================
//  predict.h
// =========================================================================
// ===========================================================================
//  predict.h  -  SGP4 wrapper: live look-angles, Doppler range-rate, passes
// ===========================================================================

struct PassPredict {
  time_t aos = 0;        // unix UTC of acquisition of signal
  time_t los = 0;        // unix UTC of loss of signal
  time_t tca = 0;        // unix UTC of time of closest approach (max elev)
  float  maxEl = 0;      // degrees
  float  azAos = 0;
  float  azLos = 0;
};

struct LiveLook {
  double az = 0, el = 0;     // degrees
  double rangeKm = 0;        // slant range
  double rangeRate = 0;      // km/s, +ve = receding
  double subLat = 0, subLon = 0, satAltKm = 0;
  bool   visible = false;    // el > 0
};

class Predictor {
public:
  void setSite(const Observer& o);
  // Point the propagator at a satellite (copies TLE lines).
  bool setSat(SatEntry& s);

  // Compute az/el/range/range-rate at unix time `t` (UTC seconds).
  LiveLook look(time_t t);

  // Doppler-corrected radio frequencies for the current geometry.
  //   rxHz: tune the receiver here to hear a downlink of dlNominal
  //   txHz: transmit here so the satellite receives ulNominal
  static void dopplerFreqs(uint32_t dlNominal, uint32_t ulNominal,
                           double rangeRateKmS,
                           int32_t calDlHz, int32_t calUlHz,
                           uint32_t& rxHz, uint32_t& txHz);

  // Linear-transponder passband tracking. Given a tuning offset measured in Hz
  // up from the downlink passband bottom, return the *operating* downlink and
  // uplink centre frequencies (before Doppler). For an inverting transponder
  // the uplink moves opposite to the downlink; for non-inverting it tracks it.
  // Single-channel transponders ignore the offset (dlOp=downlink, ulOp=uplink).
  static void passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                            uint32_t& dlOp, uint32_t& ulOp);

  // Fill up to `maxN` upcoming passes starting from `from` (unix UTC).
  int  predictPasses(time_t from, float minEl, PassPredict* out, int maxN);

  static time_t jdToUnix(double jd);

private:
  Sgp4   _sat;
  Observer _o;
  bool   _haveSat = false;
  char   _name[26], _l1[72], _l2[72];
};

// =========================================================================
//  settings.h
// =========================================================================
// ===========================================================================
//  settings.h  -  persistent user configuration (LittleFS JSON)
// ===========================================================================

struct Settings {
  // WiFi
  char     ssid[33] = "";
  char     pass[65] = "";
  // Location
  double   lat = 0.0, lon = 0.0, altM = 0.0;
  bool     useGps = false;
  // Radio
  uint8_t  radioModel = RIG_IC9700;
  uint8_t  civAddr    = 0xA2;   // 0 => use model default
  uint32_t civBaud    = 19200;
  bool     civUseUsb  = false;
  bool     useSatMode = true;   // try to enable radio sat mode on entry
  // Tracking
  float    minPassEl  = 5.0f;
  // Calibration (persisted oscillator offsets, Hz)
  int32_t  calDlHz = 0;
  int32_t  calUlHz = 0;

  bool load();
  bool save();
};

// =========================================================================
//  app.h
// =========================================================================
// ===========================================================================
//  app.h  -  top-level application: UI state machine + Doppler control loop
// ===========================================================================

enum Screen : uint8_t {
  SCR_HOME = 0, SCR_SATLIST, SCR_PASSES, SCR_TRACK,
  SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT
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
  PassPredict passes[PASS_LIST_LEN];
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
  void setStatus(const String& s, uint32_t ms = 2500);
  time_t nowUtc();
  SatEntry* activeSat();
  bool ensureTransponders(SatEntry& s);   // load (cache or net)
  void onTransponderChanged();             // recenter passband + pick default mode
  void doUpdateKeps();

  // ---- input ----
  void handleKey(char c, bool enter, bool back);

  // ---- per-screen render ----
  void draw();
  void drawHome();
  void drawSatList();
  void drawPasses();
  void drawTrack();
  void drawLocation();
  void drawUpdate();
  void drawSettings();
  void drawEdit();

  // ---- per-screen input ----
  void keyHome(char c, bool enter, bool back);
  void keySatList(char c, bool enter, bool back);
  void keyPasses(char c, bool enter, bool back);
  void keyTrack(char c, bool enter, bool back);
  void keyLocation(char c, bool enter, bool back);
  void keyUpdate(char c, bool enter, bool back);
  void keySettings(char c, bool enter, bool back);
  void keyEdit(char c, bool enter, bool back);

  // ---- small draw utilities ----
  void header(const String& t);
  void footer(const String& t);
};

// =========================================================================
//  civ.cpp
// =========================================================================
// ===========================================================================
//  civ.cpp
// ===========================================================================

static HardwareSerial CivSerial(1);   // default; re-init in begin()

void CivRadio::begin(RadioModel model, uint32_t baud,
                     int uartNum, int rxPin, int txPin, bool useUsbCdc) {
  setModel(model);
  if (useUsbCdc) {
    Serial.begin(baud);          // native USB CDC
    _stream = &Serial;
  } else {
    // (Re)construct on the requested UART number.
    static HardwareSerial* hs = nullptr;
    hs = new HardwareSerial(uartNum);
    hs->begin(baud, SERIAL_8N1, rxPin, txPin);
    _stream = hs;
  }
}

void CivRadio::setModel(RadioModel m) {
  _model = m;
  _addr  = RADIOS[m].civAddr;
}

// --- BCD: 1 Hz resolution, 5 bytes, least-significant pair first ----------
void CivRadio::freqToBcd(uint32_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; ++i) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}

bool CivRadio::sendFrame(const uint8_t* payload, size_t len) {
  if (!_stream) return false;
  uint8_t buf[20];
  size_t n = 0;
  buf[n++] = 0xFE; buf[n++] = 0xFE;
  buf[n++] = _addr;       // to radio
  buf[n++] = 0xE0;        // from controller
  for (size_t i = 0; i < len && n < sizeof(buf) - 1; ++i) buf[n++] = payload[i];
  buf[n++] = 0xFD;        // end of message
  _stream->write(buf, n);
  _stream->flush();
  drainEcho();            // swallow our own echo + radio's OK/NG (0xFB/0xFA)
  return true;
}

bool CivRadio::drainEcho(uint32_t timeoutMs) {
  if (!_stream) return false;
  uint32_t t0 = millis();
  bool sawEnd = false;
  while (millis() - t0 < timeoutMs) {
    while (_stream->available()) {
      uint8_t b = (uint8_t)_stream->read();
      if (b == 0xFD) sawEnd = true;    // end-of-frame
      t0 = millis();
    }
    if (sawEnd) break;
    delay(1);
  }
  return sawEnd;
}

void CivRadio::selectMain() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selMain, p.selLen);
}
void CivRadio::selectSub() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selSub, p.selLen);
}

bool CivRadio::setMainFreq(uint32_t hz) {
  selectMain();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRadio::setSubFreq(uint32_t hz) {
  selectSub();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRadio::setMainMode(CivMode m, uint8_t filter) {
  selectMain();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}
bool CivRadio::setSubMode(CivMode m, uint8_t filter) {
  selectSub();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}

bool CivRadio::updateDoppler(uint32_t rxHz, uint32_t txHz) {
  bool ok = true;
  ok &= setMainFreq(rxHz);   // downlink on MAIN/RX
  delay(8);
  ok &= setSubFreq(txHz);    // uplink on SUB/TX
  return ok;
}

bool CivRadio::enableSatMode(bool on) {
  if (!RADIOS[_model].hasSatMode) return false;
  // IC-9700/9100: command 0x16, sub 0x5A, data 0x01/0x00.
  uint8_t pl[3] = { 0x16, 0x5A, (uint8_t)(on ? 0x01 : 0x00) };
  return sendFrame(pl, 3);
}

CivMode CivRadio::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return CIV_FM;
  if (u.indexOf("USB") >= 0) return CIV_USB;
  if (u.indexOf("LSB") >= 0) return CIV_LSB;
  if (u.indexOf("CW")  >= 0) return CIV_CW;
  if (u.indexOf("AM")  >= 0) return CIV_AM;
  // Linear transponders are most often operated USB up / USB down.
  return CIV_USB;
}

// =========================================================================
//  satdb.cpp
// =========================================================================
// ===========================================================================
//  satdb.cpp
// ===========================================================================

bool SatDb::begin() {
  return LittleFS.begin(true);
}

int SatDb::indexOfNorad(uint32_t norad) const {
  for (int i = 0; i < _n; ++i) if (_sats[i].norad == norad) return i;
  return -1;
}

static void rstrip(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\n')) s[--n] = 0;
}

// Parse bare 3-line stanzas:  NAME \n  1 ... \n  2 ... \n
int SatDb::loadTleFromText(const String& blob) {
  _n = 0;
  int i = 0, len = blob.length();
  String name, l1, l2;
  auto nextLine = [&](String& out) -> bool {
    if (i >= len) return false;
    int nl = blob.indexOf('\n', i);
    if (nl < 0) nl = len;
    out = blob.substring(i, nl);
    out.trim();
    i = nl + 1;
    return true;
  };
  while (_n < MAX_SATS) {
    if (!nextLine(name)) break;
    if (name.length() == 0) continue;
    if (name.startsWith("1 ") || name.startsWith("2 ")) continue; // resync
    if (!nextLine(l1)) break;
    if (!nextLine(l2)) break;
    if (!l1.startsWith("1 ") || !l2.startsWith("2 ")) continue;

    SatEntry& s = _sats[_n];
    strncpy(s.name, name.c_str(), sizeof(s.name) - 1); s.name[sizeof(s.name)-1]=0;
    rstrip(s.name);
    strncpy(s.line1, l1.c_str(), sizeof(s.line1) - 1); s.line1[sizeof(s.line1)-1]=0;
    strncpy(s.line2, l2.c_str(), sizeof(s.line2) - 1); s.line2[sizeof(s.line2)-1]=0;
    s.norad = (uint32_t) atol(l1.substring(2, 7).c_str());
    s.txLoaded = false;
    _n++;
  }
  return _n;
}

bool SatDb::saveTleText(const String& blob) {
  File f = LittleFS.open(FILE_TLE, "w");
  if (!f) return false;
  f.print(blob); f.close();
  return true;
}

bool SatDb::loadTleFromFs() {
  File f = LittleFS.open(FILE_TLE, "r");
  if (!f) return false;
  String blob = f.readString(); f.close();
  return loadTleFromText(blob) > 0;
}

// --- SatNOGS transmitters JSON -------------------------------------------
int SatDb::parseTransmittersJson(const String& json, Transponder* out, int maxN) {
  JsonDocument filter;
  JsonObject fe = filter.add<JsonObject>();
  fe["description"]   = true;
  fe["uplink_low"]    = true;
  fe["uplink_high"]   = true;
  fe["downlink_low"]  = true;
  fe["downlink_high"] = true;
  fe["mode"]          = true;
  fe["invert"]        = true;
  fe["type"]          = true;
  fe["status"]        = true;
  fe["alive"]         = true;

  JsonDocument doc;
  if (deserializeJson(doc, json, DeserializationOption::Filter(filter))) return 0;

  int n = 0;
  for (JsonObject o : doc.as<JsonArray>()) {
    if (n >= maxN) break;
    const char* st = o["status"] | "";
    bool alive = o["alive"] | true;
    if (!alive || (st[0] && strcmp(st, "active") != 0)) continue; // active only

    Transponder& t = out[n];
    const char* d = o["description"] | "";
    strncpy(t.desc, d, sizeof(t.desc)-1); t.desc[sizeof(t.desc)-1]=0;
    const char* m = o["mode"] | "";
    strncpy(t.mode, m, sizeof(t.mode)-1); t.mode[sizeof(t.mode)-1]=0;
    t.downlink     = o["downlink_low"]   | 0u;
    t.downlinkHigh = o["downlink_high"]  | 0u;
    t.uplink       = o["uplink_low"]      | 0u;
    t.uplinkHigh   = o["uplink_high"]     | 0u;
    t.invert       = o["invert"]          | false;

    // Linear (tunable-passband) transponder: a real downlink passband plus an
    // uplink. SatNOGS marks these type=="Transponder", but we also require a
    // positive downlink width so single-channel "Transponder" rows don't count.
    const char* ty = o["type"] | "";
    bool typeLinear = (strcmp(ty, "Transponder") == 0);
    t.isLinear = (t.uplink != 0) && (t.downlinkHigh > t.downlink) &&
                 (typeLinear || (t.downlinkHigh - t.downlink) >= 5000u);
    n++;
  }
  return n;
}

static String txPath(uint32_t norad) {
  char buf[32]; snprintf(buf, sizeof(buf), FILE_TXCACHE, (unsigned long)norad);
  return String(buf);
}

bool SatDb::saveTxCache(uint32_t norad, const String& json) {
  File f = LittleFS.open(txPath(norad), "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

int SatDb::loadTxCache(uint32_t norad, Transponder* out, int maxN) {
  File f = LittleFS.open(txPath(norad), "r");
  if (!f) return 0;
  String j = f.readString(); f.close();
  return parseTransmittersJson(j, out, maxN);
}

// =========================================================================
//  location.cpp
// =========================================================================
// ===========================================================================
//  location.cpp
// ===========================================================================

static TinyGPSPlus    gps;
static HardwareSerial* gpsSerial = nullptr;

void Location::beginGps(int uartNum, int rxPin, int txPin, uint32_t baud) {
  gpsSerial = new HardwareSerial(uartNum);
  gpsSerial->begin(baud, SERIAL_8N1, rxPin, txPin);
  _gpsOn = true;
}

bool Location::pollGps() {
  if (!_gpsOn || !gpsSerial) return false;
  bool updated = false;
  while (gpsSerial->available()) {
    if (gps.encode(gpsSerial->read())) {
      if (gps.location.isValid()) {
        _obs.lat = gps.location.lat();
        _obs.lon = gps.location.lng();
        if (gps.altitude.isValid()) _obs.altM = gps.altitude.meters();
        _obs.valid = true;
        _obs.fromGps = true;
        _hasFix = true;
        updated = true;
      }
      if (gps.satellites.isValid()) _sats = gps.satellites.value();
      // Opportunistically set the clock from GPS time if NTP wasn't available.
      if (gps.date.isValid() && gps.time.isValid() && gps.date.year() > 2020) {
        struct tm t = {};
        t.tm_year = gps.date.year() - 1900;
        t.tm_mon  = gps.date.month() - 1;
        t.tm_mday = gps.date.day();
        t.tm_hour = gps.time.hour();
        t.tm_min  = gps.time.minute();
        t.tm_sec  = gps.time.second();
        time_t epoch = mktime(&t);     // GPS gives UTC; TZ is UTC (set in main)
        struct timeval tv = { epoch, 0 };
        settimeofday(&tv, nullptr);
      }
    }
  }
  return updated;
}

void Location::setManual(double lat, double lon, double altM) {
  _obs.lat = lat; _obs.lon = lon; _obs.altM = altM;
  _obs.valid = true; _obs.fromGps = false;
}

// Maidenhead grid -> lat/lon (centre of the square). Accepts 4 or 6 chars.
bool Location::setFromGrid(const String& gridIn) {
  String g = gridIn; g.trim(); g.toUpperCase();
  if (g.length() < 4) return false;
  double lon = (g[0] - 'A') * 20.0 - 180.0;
  double lat = (g[1] - 'A') * 10.0 - 90.0;
  lon += (g[2] - '0') * 2.0;
  lat += (g[3] - '0') * 1.0;
  if (g.length() >= 6) {
    lon += (g[4] - 'A') * (2.0 / 24.0) + (1.0 / 24.0);
    lat += (g[5] - 'A') * (1.0 / 24.0) + (0.5 / 24.0);
  } else {
    lon += 1.0; lat += 0.5;   // centre of the 2x1 deg square
  }
  setManual(lat, lon, 0.0);
  return true;
}

String Location::toGrid(double lat, double lon) {
  lon += 180.0; lat += 90.0;
  char g[7];
  g[0] = 'A' + (int)(lon / 20.0);
  g[1] = 'A' + (int)(lat / 10.0);
  g[2] = '0' + (int)(fmod(lon, 20.0) / 2.0);
  g[3] = '0' + (int)(fmod(lat, 10.0) / 1.0);
  g[4] = 'A' + (int)(fmod(lon, 2.0) / (2.0 / 24.0));
  g[5] = 'A' + (int)(fmod(lat, 1.0) / (1.0 / 24.0));
  g[6] = 0;
  return String(g);
}

// =========================================================================
//  net.cpp
// =========================================================================
// ===========================================================================
//  net.cpp
// ===========================================================================

bool Net::connect(const String& ssid, const String& pass, uint32_t timeoutMs) {
  if (ssid.length() == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < timeoutMs) delay(150);
  return WiFi.status() == WL_CONNECTED;
}

bool Net::connected() { return WiFi.status() == WL_CONNECTED; }

void Net::syncTimeNtp() {
  // UTC (no offset, no DST). Pool servers.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  for (int i = 0; i < 40 && !getLocalTime(&ti, 250); ++i) { /* wait */ }
}

bool Net::httpsGet(const String& url, String& out, size_t maxBytes) {
  if (!connected()) return false;
  WiFiClientSecure client;
  // NOTE: certificate validation is disabled for simplicity. For a security-
  // sensitive deployment, pin the CA root instead of setInsecure().
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setUserAgent("SatCat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  if (!http.begin(client, url)) return false;
  http.addHeader("Accept", "*/*");
  int code = http.GET();
  if (code != HTTP_CODE_OK) { http.end(); return false; }

  out = "";
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t total = 0;
  while (http.connected() && total < maxBytes) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min(avail, sizeof(buf)));
      out.concat((const char*)buf, r);
      total += r;
    } else {
      if (!stream->connected()) break;
      delay(2);
    }
  }
  http.end();
  return out.length() > 0;
}

bool Net::fetchAmsatTle(String& out) {
  return httpsGet(AMSAT_TLE_URL, out, 240000);
}

bool Net::fetchSatnogsTransmitters(uint32_t norad, String& out) {
  String url = String(SATNOGS_TX_URL) + String((unsigned long)norad);
  return httpsGet(url, out, 60000);
}

// =========================================================================
//  predict.cpp
// =========================================================================
// ===========================================================================
//  predict.cpp
// ===========================================================================

void Predictor::setSite(const Observer& o) {
  _o = o;
  _sat.site(o.lat, o.lon, o.altM);
}

bool Predictor::setSat(SatEntry& s) {
  strncpy(_name, s.name,  sizeof(_name)-1); _name[sizeof(_name)-1]=0;
  strncpy(_l1,   s.line1, sizeof(_l1)-1);   _l1[sizeof(_l1)-1]=0;
  strncpy(_l2,   s.line2, sizeof(_l2)-1);   _l2[sizeof(_l2)-1]=0;
  _sat.init(_name, _l1, _l2);
  _haveSat = (_sat.satrec.error == 0);
  return _haveSat;
}

LiveLook Predictor::look(time_t t) {
  LiveLook L;
  if (!_haveSat) return L;

  // Range rate via central finite difference of slant range (2 s baseline).
  _sat.findsat((unsigned long)(t - 1));
  double d0 = _sat.satDist;
  _sat.findsat((unsigned long)(t + 1));
  double d1 = _sat.satDist;
  L.rangeRate = (d1 - d0) / 2.0;          // km/s

  // Current sample.
  _sat.findsat((unsigned long)t);
  L.az       = _sat.satAz;
  L.el       = _sat.satEl;
  L.rangeKm  = _sat.satDist;
  L.subLat   = _sat.satLat;
  L.subLon   = _sat.satLon;
  L.satAltKm = _sat.satAlt;
  L.visible  = (_sat.satEl > 0.0);
  return L;
}

void Predictor::dopplerFreqs(uint32_t dlNominal, uint32_t ulNominal,
                             double rangeRateKmS,
                             int32_t calDlHz, int32_t calUlHz,
                             uint32_t& rxHz, uint32_t& txHz) {
  double rr = rangeRateKmS * 1000.0;       // m/s, +ve receding
  double beta = rr / C_LIGHT;

  // Downlink: observer receives dl*(1 - beta) -> tune RX there.
  double rx = (double)dlNominal * (1.0 - beta) + (double)calDlHz;
  // Uplink: transmit so the satellite hears ul nominal -> ul/(1 - beta).
  double tx = (ulNominal ? ((double)ulNominal / (1.0 - beta)) : 0.0);
  if (ulNominal) tx += (double)calUlHz;

  rxHz = (uint32_t)llround(rx);
  txHz = (uint32_t)llround(tx);
}

void Predictor::passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                              uint32_t& dlOp, uint32_t& ulOp) {
  // No tunable downlink passband -> single channel; ignore the offset.
  uint32_t dlBw = t.bandwidth();
  if (!t.isLinear || dlBw == 0) {
    dlOp = t.downlink;
    ulOp = t.uplink;
    return;
  }

  // Clamp the tuning offset into [0, downlink bandwidth].
  int32_t off = pbOffsetHz;
  if (off < 0) off = 0;
  if ((uint32_t)off > dlBw) off = (int32_t)dlBw;

  dlOp = t.downlink + (uint32_t)off;

  if (t.uplink == 0) { ulOp = 0; return; }

  // Assume equal up/down passband width when the uplink top edge is missing.
  uint32_t ulBw = (t.uplinkHigh > t.uplink) ? (t.uplinkHigh - t.uplink) : dlBw;
  if (t.invert) {
    // Inverting: bottom of uplink maps to top of downlink. As the downlink
    // tunes up by `off`, the uplink tunes down by the same amount.
    ulOp = t.uplink + ulBw - (uint32_t)off;
  } else {
    ulOp = t.uplink + (uint32_t)off;
  }
}

time_t Predictor::jdToUnix(double jd) {
  return (time_t)llround((jd - 2440587.5) * 86400.0);
}

int Predictor::predictPasses(time_t from, float minEl, PassPredict* out, int maxN) {
  if (!_haveSat) return 0;
  passinfo overpass;
  _sat.initpredpoint((unsigned long)from, (double)minEl);

  int found = 0;
  for (int i = 0; i < maxN; ++i) {
    // search up to ~ a number of iterations for the next pass
    bool ok = _sat.nextpass(&overpass, 200);
    if (!ok) break;
    PassPredict& p = out[found];
    p.aos   = jdToUnix(overpass.jdstart);
    p.los   = jdToUnix(overpass.jdstop);
    p.tca   = jdToUnix(overpass.jdmax);
    p.maxEl = (float)overpass.maxelevation;
    p.azAos = (float)overpass.azstart;
    p.azLos = (float)overpass.azstop;
    found++;
  }
  return found;
}

// =========================================================================
//  settings.cpp
// =========================================================================
// ===========================================================================
//  settings.cpp
// ===========================================================================

bool Settings::load() {
  File f = LittleFS.open(FILE_CFG, "r");
  if (!f) return false;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return false; }
  f.close();

  strncpy(ssid, d["ssid"] | "", sizeof(ssid)-1);
  strncpy(pass, d["pass"] | "", sizeof(pass)-1);
  lat        = d["lat"] | 0.0;
  lon        = d["lon"] | 0.0;
  altM       = d["alt"] | 0.0;
  useGps     = d["gps"] | false;
  radioModel = d["rig"] | (uint8_t)RIG_IC9700;
  civAddr    = d["addr"]| (uint8_t)0xA2;
  civBaud    = d["baud"]| 19200u;
  civUseUsb  = d["usb"] | false;
  useSatMode = d["sat"] | true;
  minPassEl  = d["minel"] | 5.0f;
  calDlHz    = d["caldl"] | 0;
  calUlHz    = d["calul"] | 0;
  if (radioModel >= RIG_COUNT) radioModel = RIG_IC9700;
  return true;
}

bool Settings::save() {
  JsonDocument d;
  d["ssid"] = ssid;  d["pass"] = pass;
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["usb"]  = civUseUsb;  d["sat"]  = useSatMode;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  File f = LittleFS.open(FILE_CFG, "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  return true;
}

// =========================================================================
//  app.cpp
// =========================================================================
// ===========================================================================
//  app.cpp  -  UI state machine, rendering, and real-time Doppler control
// ===========================================================================

// 16-bit 565 colours
static const uint16_t BLACK=0x0000, WHITE=0xFFFF, GREEN=0x07E0, RED=0xF800,
                      YELLOW=0xFFE0, CYAN=0x07FF, ORANGE=0xFD20, GREY=0x7BEF,
                      BLUE=0x041F, DGREEN=0x0320;

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

// ===========================================================================
//  Setup
// ===========================================================================
void App::setup() {
  auto m5cfg = M5.config();
  M5Cardputer.begin(m5cfg, true);   // true => init keyboard
  M5Cardputer.Display.setRotation(1);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextWrap(false);

  setenv("TZ", "UTC0", 1); tzset();   // work entirely in UTC

  db.begin();
  if (!cfg.load()) { cfg.save(); }     // first boot: write defaults
  calDl = cfg.calDlHz; calUl = cfg.calUlHz;

  applyRadioFromCfg();

  // Location
  if (cfg.useGps) loc.beginGps(GPS_UART_NUM, GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
  if (cfg.lat != 0.0 || cfg.lon != 0.0) loc.setManual(cfg.lat, cfg.lon, cfg.altM);
  pred.setSite(loc.obs());

  // Try cached TLEs so the unit is useful offline at boot.
  if (db.loadTleFromFs()) setStatus("Loaded cached keps: " + String(db.count()));
  else setStatus("No keps yet. Use Update.");

  draw();
}

void App::applyRadioFromCfg() {
  RadioModel m = (RadioModel)cfg.radioModel;
  uint32_t baud = cfg.civBaud ? cfg.civBaud : RADIOS[m].defaultBaud;
  if (CIV_USE_USB_CDC || cfg.civUseUsb)
    rig.begin(m, baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN, true);
  else
    rig.begin(m, baud, CIV_UART_NUM, CIV_RX_PIN, CIV_TX_PIN, false);
  if (cfg.civAddr) rig.setAddress(cfg.civAddr);
  else             rig.setAddress(RADIOS[m].civAddr);
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
  if (activeTxCount > 0) { s.txLoaded = true; return true; }
  // 2) try network
  if (net.connected()) {
    String j;
    if (net.fetchSatnogsTransmitters(s.norad, j)) {
      SatDb::saveTxCache(s.norad, j);
      activeTxCount = SatDb::parseTransmittersJson(j, activeTx, MAX_TX_PER_SAT);
      s.txLoaded = (activeTxCount > 0);
      return s.txLoaded;
    }
  }
  return false;
}

// Recenter the passband tuning to mid-band and choose a sensible default
// track-screen mode for the currently selected transponder.
void App::onTransponderChanged() {
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
//  Keplerian update (download AMSAT TLEs)
// ===========================================================================
void App::doUpdateKeps() {
  setStatus("WiFi..."); draw();
  if (!net.connected() && !net.connect(cfg.ssid, cfg.pass)) {
    setStatus("WiFi failed"); return;
  }
  net.syncTimeNtp();
  setStatus("Downloading keps..."); draw();
  String blob;
  if (!net.fetchAmsatTle(blob)) { setStatus("Keps DL failed"); return; }
  db.saveTleText(blob);
  int n = db.loadTleFromText(blob);
  setStatus("Keps OK: " + String(n) + " sats");
}

// ===========================================================================
//  Main loop
// ===========================================================================
void App::loop() {
  M5Cardputer.update();
  if (cfg.useGps) {
    if (loc.pollGps()) { pred.setSite(loc.obs()); }
  }

  // Keyboard
  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    auto ks = M5Cardputer.Keyboard.keysState();
    char c = ks.word.empty() ? 0 : ks.word.front();
    handleKey(c, ks.enter, ks.del);
    draw();
  }

  // Real-time Doppler service (only on the tracking screen)
  uint32_t ms = millis();
  if (screen == SCR_TRACK) {
    if (radioOut && rig.ready() && ms - lastDoppMs > 500) {
      lastDoppMs = ms;
      SatEntry* s = activeSat();
      if (s && activeTxCount > 0 && timeIsSet()) {
        LiveLook L = pred.look(nowUtc());
        Transponder& t = activeTx[curTx];
        uint32_t dlOp, ulOp, rx, tx;
        Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
        Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
        if (t.downlink) rig.setMainFreq(rx);
        if (t.uplink)   { delay(8); rig.setSubFreq(tx); }
      }
    }
    if (ms - lastDrawMs > 500) { lastDrawMs = ms; draw(); }
  } else if (screen == SCR_PASSES || screen == SCR_HOME) {
    if (ms - lastDrawMs > 1000) { lastDrawMs = ms; draw(); }  // live clock
  }
}

// ===========================================================================
//  Input dispatch
// ===========================================================================
void App::handleKey(char c, bool enter, bool back) {
  switch (screen) {
    case SCR_HOME:     keyHome(c, enter, back); break;
    case SCR_SATLIST:  keySatList(c, enter, back); break;
    case SCR_PASSES:   keyPasses(c, enter, back); break;
    case SCR_TRACK:    keyTrack(c, enter, back); break;
    case SCR_LOCATION: keyLocation(c, enter, back); break;
    case SCR_UPDATE:   keyUpdate(c, enter, back); break;
    case SCR_SETTINGS: keySettings(c, enter, back); break;
    case SCR_EDIT:     keyEdit(c, enter, back); break;
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
  const int N = 6;
  if (isUp(c))   homeSel = (homeSel + N - 1) % N;
  if (isDown(c)) homeSel = (homeSel + 1) % N;
  if (enter) {
    switch (homeSel) {
      case 0: screen = SCR_SATLIST; break;
      case 1: // passes for selected sat
      case 2: { // track selected sat
        SatEntry* s = activeSat();
        if (!s) { setStatus("No sats. Update first."); break; }
        pred.setSite(loc.obs());
        pred.setSat(*s);
        if (homeSel == 1) {
          passN = timeIsSet()
                ? pred.predictPasses(nowUtc(), cfg.minPassEl, passes, PASS_LIST_LEN)
                : 0;
          screen = SCR_PASSES;
        } else {
          ensureTransponders(*s);
          onTransponderChanged();
          radioOut = false;
          screen = SCR_TRACK;
        }
      } break;
      case 3: screen = SCR_LOCATION; break;
      case 4: screen = SCR_UPDATE; break;
      case 5: setSel = 0; screen = SCR_SETTINGS; break;
    }
  }
}

void App::keySatList(char c, bool enter, bool back) {
  int n = db.count();
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (n == 0) return;
  if (isUp(c))   { if (satSel > 0) satSel--; }
  if (isDown(c)) { if (satSel < n-1) satSel++; }
  if (c == '{')  { satSel = max(0, satSel - 10); }     // page up   (shift-[)
  if (c == '}')  { satSel = min(n-1, satSel + 10); }   // page down (shift-])
  // keep selection within a scrolling window
  if (satSel < satScroll) satScroll = satSel;
  if (satSel > satScroll + 9) satScroll = satSel - 9;
  if (enter) {
    SatEntry* s = activeSat();
    pred.setSite(loc.obs());
    pred.setSat(*s);
    ensureTransponders(*s);
    onTransponderChanged();
    passN = timeIsSet()
          ? pred.predictPasses(nowUtc(), cfg.minPassEl, passes, PASS_LIST_LEN)
          : 0;
    screen = SCR_PASSES;
  }
}

void App::keyPasses(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_SATLIST; return; }
  if (c == 'r') {  // recompute
    if (timeIsSet()) passN = pred.predictPasses(nowUtc(), cfg.minPassEl,
                                                passes, PASS_LIST_LEN);
  }
  if (enter || c == 't') {     // enter tracking
    radioOut = false;
    lastDoppMs = 0;
    screen = SCR_TRACK;
  }
}

void App::keyTrack(char c, bool enter, bool back) {
  if (isBack(c, back)) {
    if (radioOut) { radioOut = false; }     // stop sending on first back
    else screen = SCR_PASSES;
    return;
  }

  bool linear = (activeTxCount > 0) && activeTx[curTx].isLinear &&
                activeTx[curTx].bandwidth() > 0;

  if (c == 'm') {                                    // toggle TUNE / CAL
    if (linear) trackMode ^= 1;
    else { trackMode = 1; setStatus("Not linear: CAL only"); }
  }

  if (trackMode == 0 && linear) {
    // ---- TUNE mode: move within the transponder passband ----
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
  }
  if (c == 'r') {                                    // toggle radio output
    radioOut = !radioOut;
    if (radioOut) {
      if (cfg.useSatMode) rig.enableSatMode(true);
      if (activeTxCount > 0) {
        Transponder& t = activeTx[curTx];
        rig.setMainMode(CivRadio::modeFromString(t.mode));
        if (t.uplink) rig.setSubMode(CivRadio::modeFromString(t.mode));
      }
      lastDoppMs = 0;
      setStatus("Radio ON");
    } else setStatus("Radio OFF");
  }
  if (enter) {  // persist calibration (passband offset is per-pass, not saved)
    cfg.calDlHz = calDl; cfg.calUlHz = calUl; cfg.save();
    setStatus("Calibration saved");
  }
}

void App::keyLocation(char c, bool enter, bool back) {
  if (isBack(c, back)) { pred.setSite(loc.obs()); screen = SCR_HOME; return; }
  if (c == 'p') {                       // toggle GPS use
    cfg.useGps = !cfg.useGps; cfg.save();
    if (cfg.useGps) loc.beginGps(GPS_UART_NUM, GPS_RX_PIN, GPS_TX_PIN, GPS_BAUD);
    setStatus(cfg.useGps ? "GPS enabled" : "GPS off");
  }
  if (c == 'e') { editTarget = 100; editTitle = "Latitude (deg)";
                  editBuf = String(cfg.lat, 5); screen = SCR_EDIT; }
  if (c == 'o') { editTarget = 101; editTitle = "Longitude (deg)";
                  editBuf = String(cfg.lon, 5); screen = SCR_EDIT; }
  if (c == 'a') { editTarget = 102; editTitle = "Altitude (m)";
                  editBuf = String(cfg.altM, 1); screen = SCR_EDIT; }
  if (c == 'g') { editTarget = 103; editTitle = "Grid (Maidenhead)";
                  editBuf = ""; screen = SCR_EDIT; }
}

void App::keyUpdate(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  if (c == 'k' || enter) { doUpdateKeps(); }
  if (c == 'w') {
    setStatus(net.connect(cfg.ssid, cfg.pass) ? "WiFi connected" : "WiFi failed");
  }
}

void App::keySettings(char c, bool enter, bool back) {
  const int N = 9;
  if (isBack(c, back)) { applyRadioFromCfg(); screen = SCR_HOME; return; }
  if (isUp(c))   setSel = (setSel + N - 1) % N;
  if (isDown(c)) setSel = (setSel + 1) % N;

  auto adj = [&](int dir){
    switch (setSel) {
      case 0: { int m = (cfg.radioModel + dir + RIG_COUNT) % RIG_COUNT;
                cfg.radioModel = m; cfg.civAddr = RADIOS[m].civAddr;
                cfg.civBaud = RADIOS[m].defaultBaud; cfg.save();
                applyRadioFromCfg(); } break;
      case 2: { uint32_t bs[] = {1200,4800,9600,19200,38400,115200};
                int idx=2; for (int i=0;i<6;i++) if (bs[i]==cfg.civBaud) idx=i;
                idx = (idx + dir + 6) % 6; cfg.civBaud = bs[idx];
                cfg.save(); applyRadioFromCfg(); } break;
      case 3: cfg.civUseUsb = !cfg.civUseUsb; cfg.save(); applyRadioFromCfg(); break;
      case 4: cfg.useSatMode = !cfg.useSatMode; cfg.save(); break;
      case 5: cfg.minPassEl = constrain(cfg.minPassEl + dir, 0, 30); cfg.save(); break;
    }
  };
  if (isLeft(c))  adj(-1);
  if (isRight(c)) adj(+1);
  if (enter) {
    switch (setSel) {
      case 1: editTarget = 200; editTitle = "CI-V addr (hex)";
              editBuf = String(cfg.civAddr, HEX); screen = SCR_EDIT; break;
      case 6: editTarget = 201; editTitle = "WiFi SSID";
              editBuf = cfg.ssid; screen = SCR_EDIT; break;
      case 7: editTarget = 202; editTitle = "WiFi password";
              editBuf = cfg.pass; screen = SCR_EDIT; break;
      case 8: setStatus(net.connect(cfg.ssid, cfg.pass) ? "WiFi OK" : "WiFi FAIL");
              break;
      default: adj(+1); break;
    }
  }
}

void App::keyEdit(char c, bool enter, bool back) {
  if (c == '`') { screen = (editTarget >= 200) ? SCR_SETTINGS
                         : (editTarget >= 100 ? SCR_LOCATION : SCR_HOME); return; }
  if (back) { if (editBuf.length()) editBuf.remove(editBuf.length()-1); return; }
  if (enter) {
    switch (editTarget) {
      case 100: cfg.lat = editBuf.toFloat(); break;
      case 101: cfg.lon = editBuf.toFloat(); break;
      case 102: cfg.altM = editBuf.toFloat(); break;
      case 103: loc.setFromGrid(editBuf);
                cfg.lat = loc.obs().lat; cfg.lon = loc.obs().lon; break;
      case 200: cfg.civAddr = (uint8_t)strtol(editBuf.c_str(), nullptr, 16); break;
      case 201: strncpy(cfg.ssid, editBuf.c_str(), sizeof(cfg.ssid)-1); break;
      case 202: strncpy(cfg.pass, editBuf.c_str(), sizeof(cfg.pass)-1); break;
    }
    if (editTarget < 200 || editTarget == 200) {
      loc.setManual(cfg.lat, cfg.lon, cfg.altM);
      pred.setSite(loc.obs());
    }
    cfg.save();
    applyRadioFromCfg();
    screen = (editTarget >= 200) ? SCR_SETTINGS
           : (editTarget >= 100 ? SCR_LOCATION : SCR_HOME);
    setStatus("Saved");
    return;
  }
  if (c >= 32 && c < 127) editBuf += c;
}

// ===========================================================================
//  Rendering
// ===========================================================================
void App::header(const String& t) {
  canvas.fillRect(0, 0, 240, 16, BLUE);
  canvas.setTextColor(WHITE, BLUE);
  canvas.setTextSize(2);
  canvas.setCursor(3, 1);
  canvas.print(t);
  canvas.setTextSize(1);
  if (timeIsSet()) {
    String clk = fmtClock(nowUtc()) + "Z";
    canvas.setCursor(240 - clk.length()*6 - 3, 4);
    canvas.print(clk);
  }
}
void App::footer(const String& t) {
  canvas.setTextColor(GREY, BLACK);
  canvas.setTextSize(1);
  canvas.setCursor(2, 127);
  canvas.print(t);
}

void App::draw() {
  canvas.fillScreen(BLACK);
  switch (screen) {
    case SCR_HOME:     drawHome(); break;
    case SCR_SATLIST:  drawSatList(); break;
    case SCR_PASSES:   drawPasses(); break;
    case SCR_TRACK:    drawTrack(); break;
    case SCR_LOCATION: drawLocation(); break;
    case SCR_UPDATE:   drawUpdate(); break;
    case SCR_SETTINGS: drawSettings(); break;
    case SCR_EDIT:     drawEdit(); break;
  }
  // transient status
  if (status.length() && millis() < statusUntil) {
    canvas.fillRect(0, 114, 240, 11, DGREEN);
    canvas.setTextColor(WHITE, DGREEN);
    canvas.setTextSize(1);
    canvas.setCursor(2, 115);
    canvas.print(status);
  }
  canvas.pushSprite(0, 0);
}

void App::drawHome() {
  header("SatCat");
  const char* items[] = { "Satellites", "Passes (sel)", "Track (sel)",
                          "Location", "Update Keps/Freq", "Settings" };
  canvas.setTextSize(1);
  for (int i = 0; i < 6; ++i) {
    int y = 22 + i*13;
    if (i == homeSel) { canvas.fillRect(0, y-2, 240, 12, GREEN);
                        canvas.setTextColor(BLACK, GREEN); }
    else                canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(6, y);
    canvas.print(items[i]);
  }
  SatEntry* s = activeSat();
  canvas.setTextColor(CYAN, BLACK);
  canvas.setCursor(6, 102);
  canvas.print(s ? String("Sel: ") + s->name : String("Sel: none"));
  footer("; / . move   ENT select");
}

void App::drawSatList() {
  header("Satellites");
  canvas.setTextSize(1);
  int n = db.count();
  if (n == 0) {
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(6, 40); canvas.print("No TLEs. Run Update.");
    footer("` back");
    return;
  }
  for (int row = 0; row < 10 && (satScroll+row) < n; ++row) {
    int idx = satScroll + row;
    int y = 20 + row*10;
    if (idx == satSel) { canvas.fillRect(0, y-1, 240, 10, GREEN);
                         canvas.setTextColor(BLACK, GREEN); }
    else                 canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%-22s %5lu", db.at(idx).name, (unsigned long)db.at(idx).norad);
  }
  footer("ENT passes  {/} page  ` back");
}

void App::drawPasses() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Passes"));
  canvas.setTextSize(1);
  if (!timeIsSet()) {
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(6, 40); canvas.print("Clock not set.");
    canvas.setCursor(6, 52); canvas.print("Run Update (NTP) or GPS.");
    footer("` back  r recompute");
    return;
  }
  if (!loc.obs().valid) {
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(6, 40); canvas.print("Set your location first.");
    footer("` back");
    return;
  }
  canvas.setTextColor(GREY, BLACK);
  canvas.setCursor(4, 18); canvas.print("AOS (UTC)     Max  El  LOS");
  if (passN == 0) {
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(6, 40); canvas.print("No passes >= min elev.");
  }
  for (int i = 0; i < passN && i < 9; ++i) {
    int y = 30 + i*10;
    PassPredict& p = passes[i];
    long mins = (p.los - p.aos) / 60;
    canvas.setTextColor(i == 0 ? GREEN : WHITE, BLACK);
    canvas.setCursor(4, y);
    canvas.printf("%s  %2ldm %3.0f %s",
                  fmtMDHM(p.aos).c_str(), mins, p.maxEl, fmtHM(p.los).c_str());
  }
  footer("ENT track  r recompute  ` back");
}

void App::drawTrack() {
  SatEntry* s = activeSat();
  header(s ? String(s->name) : String("Track"));
  canvas.setTextSize(1);
  if (!s) { footer("` back"); return; }

  LiveLook L = timeIsSet() ? pred.look(nowUtc()) : LiveLook();

  // Az / El / range / range-rate
  canvas.setTextColor(L.visible ? GREEN : GREY, BLACK);
  canvas.setCursor(4, 20);
  canvas.printf("Az %5.1f  El %5.1f%s", L.az, L.el, L.visible ? " *" : "");
  canvas.setTextColor(WHITE, BLACK);
  canvas.setCursor(4, 31);
  canvas.printf("Rng %5.0fkm  Rate %+5.2f km/s", L.rangeKm, L.rangeRate);

  // Transponder + Doppler
  if (activeTxCount == 0) {
    canvas.setTextColor(YELLOW, BLACK);
    canvas.setCursor(4, 48); canvas.print("No transponder data.");
    canvas.setCursor(4, 59); canvas.print("Connect WiFi + reopen sat.");
  } else {
    Transponder& t = activeTx[curTx];
    bool linear = t.isLinear && t.bandwidth() > 0;
    uint32_t dlOp = 0, ulOp = 0, rx = 0, tx = 0;
    Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
    Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);

    canvas.setTextColor(CYAN, BLACK);
    canvas.setCursor(4, 44);
    canvas.printf("TX%d/%d %s%-.16s", curTx+1, activeTxCount,
                  linear ? "[LIN] " : "", t.desc);

    // DN/UP show the operating (passband) frequency; RX/TX are Doppler-tuned.
    canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(4, 56);
    canvas.printf("DN %s", fmtMHz(dlOp).c_str());
    canvas.setTextColor(GREEN, BLACK);
    canvas.setCursor(120, 56);
    canvas.printf("RX %s", fmtMHz(rx).c_str());

    canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(4, 67);
    if (ulOp) canvas.printf("UP %s", fmtMHz(ulOp).c_str());
    else      canvas.print("UP  (rx only)");
    if (ulOp) {
      canvas.setTextColor(ORANGE, BLACK);
      canvas.setCursor(120, 67);
      canvas.printf("TX %s", fmtMHz(tx).c_str());
    }

    // Passband position (linear only).
    if (linear) {
      float halfk = t.bandwidth() / 2000.0f;
      float posk  = (pbOffset - (int32_t)(t.bandwidth()/2)) / 1000.0f;
      canvas.setTextColor(trackMode == 0 ? CYAN : GREY, BLACK);
      canvas.setCursor(4, 79);
      canvas.printf("PB %+.1fk bw%.1fk %s%s", posk, halfk,
                    t.invert ? "INV " : "", trackMode == 0 ? "<TUNE>" : "");
    }

    // Calibration line (active in CAL mode).
    canvas.setTextColor(trackMode == 1 ? YELLOW : GREY, BLACK);
    canvas.setCursor(4, 90);
    canvas.printf("cal DN%+ld UP%+ld st%ld%s",
                  (long)calDl, (long)calUl,
                  (long)(trackMode == 0 ? tuneStep : calStep),
                  trackMode == 1 ? " <CAL>" : "");
  }

  // Radio status line
  canvas.setCursor(4, 102);
  if (!rig.ready()) { canvas.setTextColor(GREY, BLACK); canvas.print("Radio: n/a"); }
  else {
    canvas.setTextColor(radioOut ? GREEN : GREY, BLACK);
    canvas.printf("Radio %s [%s %02X]", radioOut ? "ON " : "off",
                  rig.profile().name, rig.address());
  }
  if (!rig.profile().selVerified) {
    canvas.setTextColor(ORANGE, BLACK);
    canvas.setCursor(4, 113);
    canvas.print("! verify MAIN/SUB for this rig");
  }
  if (trackMode == 0)
    footer(",/=tune s=step x=ctr m=cal t=tp r=rf");
  else
    footer(",/=DN ;.=UP s=stp x=0 m=tune t=tp r=rf");
}

void App::drawLocation() {
  header("Location");
  canvas.setTextSize(1);
  const Observer& o = loc.obs();
  canvas.setTextColor(WHITE, BLACK);
  canvas.setCursor(6, 22); canvas.printf("Lat: %.5f", o.lat);
  canvas.setCursor(6, 34); canvas.printf("Lon: %.5f", o.lon);
  canvas.setCursor(6, 46); canvas.printf("Alt: %.0f m", o.altM);
  canvas.setCursor(6, 58); canvas.printf("Grid: %s",
       (o.valid ? Location::toGrid(o.lat, o.lon).c_str() : "----"));
  canvas.setTextColor(cfg.useGps ? GREEN : GREY, BLACK);
  canvas.setCursor(6, 74);
  canvas.printf("GPS: %s  fix:%s sats:%d", cfg.useGps ? "on" : "off",
                loc.gpsHasFix() ? "Y" : "N", loc.gpsSats());
  footer("e lat  o lon  a alt  g grid  p gps  ` back");
}

void App::drawUpdate() {
  header("Update");
  canvas.setTextSize(1);
  canvas.setTextColor(WHITE, BLACK);
  canvas.setCursor(6, 24); canvas.print("k / ENT : download keps (AMSAT)");
  canvas.setCursor(6, 38); canvas.print("w       : connect WiFi only");
  canvas.setCursor(6, 56);
  canvas.printf("Sats in memory: %d", db.count());
  canvas.setTextColor(GREY, BLACK);
  canvas.setCursor(6, 72); canvas.print("Transponders load per-sat from");
  canvas.setCursor(6, 84); canvas.print("SatNOGS when a sat is opened.");
  footer("` back");
}

void App::drawSettings() {
  header("Settings");
  canvas.setTextSize(1);
  String rows[9];
  rows[0] = String("Radio: ") + RADIOS[cfg.radioModel].name;
  rows[1] = String("CI-V addr: ") + String(cfg.civAddr, HEX);
  rows[2] = String("CI-V baud: ") + String(cfg.civBaud);
  rows[3] = String("CI-V via USB: ") + (cfg.civUseUsb ? "yes" : "no");
  rows[4] = String("Try sat mode: ") + (cfg.useSatMode ? "yes" : "no");
  rows[5] = String("Min pass el: ") + String((int)cfg.minPassEl) + " deg";
  rows[6] = String("WiFi SSID: ") + cfg.ssid;
  rows[7] = String("WiFi pass: ") + String(strlen(cfg.pass) ? "******" : "(none)");
  rows[8] = String("Save & test WiFi");
  for (int i = 0; i < 9; ++i) {
    int y = 19 + i*11;
    if (i == setSel) { canvas.fillRect(0, y-1, 240, 11, GREEN);
                       canvas.setTextColor(BLACK, GREEN); }
    else               canvas.setTextColor(WHITE, BLACK);
    canvas.setCursor(4, y); canvas.print(rows[i]);
  }
  footer(",/ change  ENT edit  ` back");
}

void App::drawEdit() {
  header("Edit");
  canvas.setTextSize(1);
  canvas.setTextColor(CYAN, BLACK);
  canvas.setCursor(6, 30); canvas.print(editTitle);
  canvas.drawRect(6, 46, 228, 18, WHITE);
  canvas.setTextColor(WHITE, BLACK);
  canvas.setCursor(10, 51); canvas.print(editBuf + "_");
  footer("type  DEL bksp  ENT ok  ` cancel");
}

// =========================================================================
//  main (entry point)
// =========================================================================
// ===========================================================================
//  main.cpp  -  entry point for the Cardputer ADV satellite tracker
//
//  All hardware bring-up (M5Cardputer.begin, display, keyboard, LittleFS,
//  config load, radio + GPS init) happens inside App::setup(). This file is
//  intentionally a thin shell so the whole program lives in the App class.
// ===========================================================================

static App app;

void setup() {
  app.setup();
}

void loop() {
  app.loop();
}
