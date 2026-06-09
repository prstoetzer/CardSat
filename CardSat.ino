// ===========================================================================
//  CardSat.ino  -  M5Stack Cardputer ADV satellite tracker + multi-radio CAT
//
//  SINGLE-FILE Arduino IDE build (modular PlatformIO sources concatenated).
//
//  ---- REQUIRED LIBRARIES (Library Manager unless noted) ----
//    M5Cardputer (pulls M5Unified+M5GFX) | ArduinoJson v7 | TinyGPSPlus
//    Sgp4  <- Hopperpop: https://github.com/Hopperpop/Sgp4-Library (.ZIP)
//    WiFi/WiFiClientSecure/HTTPClient/LittleFS/HardwareSerial: ESP32 core.
//
//  ---- BOARD SETTINGS (Arduino IDE Tools menu) ----
//    Board "ESP32S3 Dev Module" or "M5StampS3" | Flash 8MB
//    Partition Scheme "Huge APP (3MB No OTA/1MB SPIFFS)"  <-- REQUIRED
//    PSRAM Disabled | USB CDC On Boot Enabled
//
//  ---- SUPPORTED RADIOS (3 CAT families, pick in Settings) ----
//    Icom CI-V    : IC-820/821/910/970/9100/9700 (binary, addressed bus)
//    Yaesu CAT    : FT-847, FT-736R (5-byte binary, 8N2)
//    Kenwood CAT  : TS-790, TS-2000 (ASCII ';'-terminated, RS-232)
//    Protocols follow the Hamlib backends (icom, yaesu/ft847+ft736,
//    kenwood/ts2000+ts790). See civ/yaesu/kenwood.cpp for the encoders.
//
//  ---- I/O ----
//    CAT  : UART1 on G1/G2 @3.3 V. The interface HARDWARE differs per family:
//           Icom    = single-wire 5 V CI-V level interface (G1/G2 + GND);
//           Yaesu   = serial CAT (verify TTL vs RS-232 per the CAT manual);
//           Kenwood = RS-232 (DB-9) via a MAX3232-class level shifter.
//           The ESP32-S3 pins are NOT 5 V tolerant -- never wire CAT direct.
//           Every CAT frame is traced to Serial @115200 (decoded). Toggle via
//           CIV_DEBUG / YAESU_DEBUG / KW_DEBUG in the respective .cpp.
//    Freq read-back (radio-knob One True Rule tuning): Icom yes; Kenwood yes;
//           Yaesu FT-847/FT-736R no (use the device TUNE keys instead).
//    BAND PAIR: on the Yaesu/Kenwood sat rigs, CAT cannot switch bands -- the
//           operator selects the uplink/downlink bands and the rig's own sat/
//           full-duplex mode by hand; CardSat Doppler-tunes within that. (Same
//           as SatPC32.) On Icom, CardSat drives MAIN/SUB and forces sat OFF.
//    GPS  : runtime-selectable on the Location screen ('s'): Grove (G1/G2,
//           shares CAT pins), Cap LoRa868 (G15/G13 @9600), or Cap LoRa-1262
//           (G15/G13 @115200). Runs on UART2 so it doesn't fight CAT.
//    Spkr : built-in speaker drives the AOS alarm (M5Cardputer.Speaker).
//
//  Doppler: full correction of BOTH legs to hold a CONSTANT FREQUENCY AT THE
//    SATELLITE (KB5MU "One True Rule"). Tune the passband with the device keys
//    (TUNE) or, on 'd', with the radio's own knob -- let go and nothing drifts.
//  Linear-transponder modes: USB downlink + LSB uplink (inverting-SSB), or
//    USB up + USB down if either leg is HF (< 30 MHz). FM birds: FM both legs.
//
//  Next Passes: unified schedule across ALL favorites (soonest AOS first),
//    AOS alarm (countdown beeps + screen flash, toggle in Settings), TLE
//    element-set age shown/colored, and "z" to deep-sleep until the next AOS.
//  Sun/eclipse: Polar screen shows Sun az/el + a Sun glyph and whether the
//    satellite is SUNLIT or in ECLIPSE; Track shows an "ECL" flag in shadow.
//  Pass detail: on Passes, ;/. pick a pass and "d" plots its elevation curve
//    (yellow = sunlit, blue = eclipse) with AOS/LOS/az, max el, and sunlit %.
//
//  Keys: Satellites - f=favorite v=favorites-only n=new TLE.
//        Next Passes - ENT track, r refresh, z deep-sleep until AOS.
//        Passes - ;/. select, d=detail plot, n=add transponder, t/ENT track.
//        Location - p=gps s=gps-source c=set UTC.
//        Track - m=TUNE/CAL, d=radio-knob tuning (One True Rule), t=cycle TX,
//                r=radio on/off, o=rotator on/off, p=polar, ENTER=save cal.
//        Update - k=keps, a=cache ALL transponders (offline). DEL=backspace.
//        Settings - radio model/addr/baud; AOS alarm; "Reset all data" (ERASE).
//  Convention: "Sub"=downlink/RX, "Main"=uplink/TX on every backend.
// ===========================================================================

#include <Arduino.h>
#include <M5Cardputer.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <HardwareSerial.h>
#include <Wire.h>
#include <TinyGPSPlus.h>
#include <Sgp4.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include <SPI.h>
#include <SD.h>
#include <esp_sleep.h>
#include <time.h>
#include <sys/time.h>
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
//  Orbital data is GP (General Perturbations / OMM) element sets in JSON, from
//  AMSAT's distribution. Each record carries the SGP4 mean elements in named
//  fields (no fixed-width catalog number), with an added AMSAT_NAME for the
//  friendly satellite name. This replaces the legacy TLE text format, which is
//  being retired as the 5-digit NORAD catalog field runs out.
//
//  The GP URL is user-configurable in Settings; AMSAT_GP_URL is the default.
//  SatNOGS provides JSON for transponder/transmitter frequencies.
// ---------------------------------------------------------------------------
#define AMSAT_GP_URL       "https://newark192.amsat.org/gpdata/current/daily-bulletin.json"

// SatNOGS DB REST API (transponder frequencies)
#define SATNOGS_TX_URL     "https://db.satnogs.org/api/transmitters/?format=json&satellite__norad_cat_id="

// ---------------------------------------------------------------------------
//  Serial / UART wiring
// ---------------------------------------------------------------------------
//  The Cardputer ADV exposes a 2x7 header. Free GPIOs broken out include
//  G1, G2 (and G13/G15 etc). We use Serial1 on these for *either*:
//     - a NMEA GPS (e.g. the LoRa+GPS cap / external GPS module), or
//     - the Icom CI-V bus through a TTL<->CI-V level interface.
//  Pick pins that match your wiring; defaults below are a common choice.
//
//  CI-V is driven over a 3.3 V hardware UART (TTL serial) on the header pins.
//  Use a 3.3 V-safe CI-V level interface between these pins and the radio's
//  REMOTE jack (the ESP32-S3 GPIOs are not 5 V tolerant). Set the CI-V address
//  and baud (to match the radio's menu) in Settings.
// ---------------------------------------------------------------------------
// GPS source is selectable at runtime (Settings -> Location screen, 's').
// Per-source UART/pins/baud live in GPS_PROFILES[] in app.cpp. All sources use
// UART2, so CI-V keeps UART1 (G1/G2) to itself.
//   GROVE 9600 / GROVE 115200 : Cardputer Grove HY2.0-4P on G1/G2 -- SAME pins
//             as the default CI-V, so don't run Grove GPS and CI-V together.
//   CAP868 / CAP1262 : Cap LoRa868 / LoRa-1262 GNSS (AT6668, G15 RX / G13 TX,
//             115200 8N1). Both caps share identical GPS settings.
enum GpsSource : uint8_t {
  GPS_SRC_GROVE_9600 = 0,
  GPS_SRC_GROVE_115K,
  GPS_SRC_CAP868,
  GPS_SRC_CAP1262,
  GPS_SRC_COUNT
};

static constexpr int   CIV_UART_NUM   = 1;     // CI-V owns UART1 on G1/G2
static constexpr int   CIV_RX_PIN     = 1;     // G1
static constexpr int   CIV_TX_PIN     = 2;     // G2

// microSD (SPI). Used only as a storage fallback when no internal LittleFS/
// SPIFFS partition is available -- e.g. when CardSat is launched from the
// bmorcelli Launcher without a SPIFFS region attached (a card is normally
// present then, since the launcher boots from it). Standard Cardputer pins.
static constexpr int   SD_SCK_PIN     = 40;
static constexpr int   SD_MISO_PIN    = 39;
static constexpr int   SD_MOSI_PIN    = 14;
static constexpr int   SD_CS_PIN      = 12;
static constexpr uint32_t SD_FREQ_HZ  = 25000000;   // SD SPI clock (matches M5 reference init)

// Soft guard for the CAT update rate: an estimate of the bytes moved per Doppler
// update (band select + set-freq + set-mode per leg, plus echo/read-back and
// margin). The effective rate is floored at the time this many bytes take at the
// configured CAT baud, so a too-low CAT-rate setting can't outrun the link.
// 8N1 => 10 bits/byte, hence (bytes * 10000) / baud milliseconds.
static constexpr uint32_t CAT_BYTES_PER_UPDATE = 80;

// Firmware version (single source of truth; shown on the About screen).
static constexpr const char* FW_VERSION = "0.9.6c";
// Auto-refresh GP at boot when even the freshest cached element set is older.
static constexpr double  GP_STALE_DAYS = 7.0;
// Display backlight level used for normal (awake) operation.
static constexpr uint8_t SCREEN_BRIGHT = 180;
// Most-recent QSO log entries loaded into RAM for the on-device view/edit list.
static constexpr int     LOG_VIEW_MAX  = 120;

// ---------------------------------------------------------------------------
//  Antenna rotator: GS-232 over an I2C->UART bridge (SC16IS750/752)
// ---------------------------------------------------------------------------
//  All three ESP32-S3 UARTs are spoken for (USB-CDC, CI-V on UART1, GPS on
//  UART2), so the rotator's serial link is created by an I2C->UART bridge. The
//  bridge runs on a SECOND I2C controller (Wire1) so it never touches the
//  keyboard/IMU bus. Chain: Wire1 -> SC16IS750 -> MAX3232 -> DB-9 -> GS-232.
//
//  Pins confirmed from the M5Stack Cap LoRa-1262 pinmap: the Cardputer-ADV
//  expansion I2C bus is G8 = SDA, G9 = SCL. That is the bus the cap exposes on
//  its HY2.0-4P Grove Port.A, so a Grove SC16IS750 bridge plugs straight in. It
//  is shared with the cap's PI4IOE5V6408 IO expander (~0x43/0x44, used only for
//  the LoRa RF switch, which CardSat doesn't drive), so keep ROT_I2C_ADDR clear
//  of those. These pins don't collide with CI-V (G1/G2), the GPS UART (G13/G15),
//  the LoRa SPI (G3/G4/G5/G6/G14/G39/G40), or the SD card (G14/G39/G40 + CS G12).
static constexpr int      ROT_I2C_SDA  = 8;           // G8  (Cap LoRa Port.A SDA)
static constexpr int      ROT_I2C_SCL  = 9;           // G9  (Cap LoRa Port.A SCL)
static constexpr uint8_t  ROT_I2C_ADDR = 0x4D;        // SC16IS750 (A0/A1 strap)
static constexpr uint32_t ROT_XTAL_HZ  = 14745600UL;  // bridge crystal (breakout)
static constexpr uint32_t ROT_I2C_HZ   = 400000UL;    // Wire1 clock

// ---------------------------------------------------------------------------
//  Limits (kept modest - no PSRAM on the StampS3A)
// ---------------------------------------------------------------------------
static constexpr int   MAX_SATS        = 220;  // sats held in RAM from GP data
static constexpr int   MAX_TX_PER_SAT  = 64;   // transmitters held for active sat (e.g. ISS has ~49 on SatNOGS)
static constexpr int   PASS_LIST_LEN   = 12;   // passes shown per satellite
static constexpr int   SCHED_MAX       = 24;   // favorites tracked in the schedule
static constexpr int   PD_SAMPLES      = 100;  // samples in the pass-detail curve
static constexpr int   POLAR_PTS       = 48;   // samples in a polar ground-track arc
static constexpr int   MUTUAL_MAX      = 12;   // co-visibility windows listed
static constexpr int   MUTUAL_PASS_SCAN= 16;   // of my passes scanned for mutual windows

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define DATA_DIR     "/CardSat"               // all data/config lives in this folder
#define FILE_GP      "/CardSat/gp.json"       // cached GP/OMM download (JSON array)
#define FILE_CFG     "/CardSat/config.json"
#define FILE_TXCACHE "/CardSat/tx_%lu.json"   // %lu = norad id
#define FILE_CALIB   "/CardSat/calib.txt"     // per-sat calibration: "norad dl ul" lines
#define FILE_TONES   "/CardSat/tones.txt"     // per-sat CTCSS override: "norad tenths" lines
#define FILE_FAVS    "/CardSat/favs.txt"      // favorite NORAD ids, one per line
#define FILE_MGP     "/CardSat/mgp.json"      // manually-entered GP sats (one OMM object/line)
#define FILE_MTX     "/CardSat/mtx_%lu.json"  // manual transponders per norad (text lines)
#define FILE_CFG_BAK  "/CardSat/config.bak"    // backup copy of config.json
#define FILE_FAVS_BAK "/CardSat/favs.bak"      // backup copy of favs.txt
#define FILE_LOG     "/CardSat/qso_log.csv"     // QSO log (CSV, notes is last field)
#define FILE_ADIF    "/CardSat/qso_log.adi"     // ADIF export (generated on demand)
#define FILE_LOTW    "/CardSat/lotw_sats.csv"   // LoTW SAT_NAME map ("SAT_NAME,AMSAT_NAME")


// =========================================================================
//  storage.h
// =========================================================================

// ===========================================================================
//  storage.h -- filesystem abstraction (internal LittleFS, SD-card fallback)
// ===========================================================================
//  CardSat keeps everything in a "/CardSat" folder on the microSD card by
//  default -- config, cached elements, favorites, calibration, transponders.
//  If no SD card is present it falls back to the internal LittleFS partition
//  (same "/CardSat" folder) so the firmware still works standalone.
//
//  All persistence goes through Store::fs(), which points at whichever
//  filesystem mounted. Call Store::begin() once at startup before any file I/O.

namespace Store {
  bool   begin();            // mount LittleFS (format on fail), else SD card
  fs::FS& fs();              // the active filesystem (LittleFS or SD)
  bool   ready();            // true if some filesystem mounted
  bool   onSD();             // true if we fell back to the SD card
  bool   formatInternal();   // wipe internal LittleFS (factory reset); never the SD
}


// =========================================================================
//  radio_profiles.h
// =========================================================================

// ===========================================================================
//  radio_profiles.h  -  per-radio protocol + capability table
// ===========================================================================
//
//  CardSat speaks three CAT dialects, one per manufacturer family:
//    PROTO_CIV     Icom CI-V          (binary, FE FE framing, BCD, addressed)
//    PROTO_YAESU   Yaesu CAT          (5-byte binary: 4 data + opcode, BCD)
//    PROTO_KENWOOD Kenwood/Elecraft   (ASCII text commands, ';'-terminated)
//
//  Protocol details are taken from the Hamlib backends (icom, yaesu/ft847.c,
//  yaesu/ft736.c, kenwood/ts2000.c, kenwood/ts790.c) and the radios' CAT
//  manuals. See civ.cpp / yaesu.cpp / kenwood.cpp for the wire-level encoders.
//
//  Icom CI-V addresses (verified against the standard Icom address table):
//      IC-820 = 0x42   IC-910 = 0x60   IC-9100 = 0x7C
//      IC-821 = 0x4C   IC-970 = 0x2E   IC-9700 = 0xA2
//
//  MAIN/SUB band select (Icom only): CI-V cmd 0x07, D0 = MAIN, D1 = SUB,
//  verified from the IC-821H manual command table and shared across the family.
//
//  Frequency read-back (canReadFreq) enables the "radio knob" One True Rule
//  tuning mode:
//    * Icom (all six)     : CI-V 0x03 reads the operating frequency.
//    * Yaesu FT-847       : "read freq & mode" (opcode 0x03, patched to 0x13 for
//                           SAT-RX) returns 4 BCD bytes + mode. Works only on
//                           firmware-updated units (early ones can't read). true.
//    * Yaesu FT-736R      : CAT cannot report frequency at all (only squelch /
//                           S-meter); Hamlib caches the last set value. false.
//    * Kenwood TS-790/2000: ASCII "FA;" reads the frequency. true.
//
//  IMPORTANT shared limitation of the older sat rigs (FT-736R, TS-790, and the
//  Yaesu/Kenwood pairs generally): CAT cannot switch the BAND PAIR. The operator
//  selects the uplink/downlink bands (and engages the rig's own satellite / full-
//  duplex mode) manually on the radio; CardSat only Doppler-tunes within them.
//  This is exactly how SatPC32 drives these radios.
// ===========================================================================

enum RigProtocol : uint8_t { PROTO_CIV, PROTO_YAESU, PROTO_KENWOOD };

enum RadioModel : uint8_t {
  RIG_IC820 = 0,
  RIG_IC821,
  RIG_IC910,
  RIG_IC970,
  RIG_IC9100,
  RIG_IC9700,
  RIG_FT847,
  RIG_FT736R,
  RIG_TS790,
  RIG_TS2000,
  RIG_COUNT
};

struct RadioProfile {
  const char* name;
  RigProtocol proto;
  uint8_t     civAddr;       // CI-V address (Icom only; 0 otherwise)
  uint32_t    defaultBaud;   // typical default CAT baud
  uint8_t     selMain[3];    // CI-V MAIN band-select bytes (Icom only)
  uint8_t     selSub[3];     // CI-V SUB  band-select bytes (Icom only)
  uint8_t     selLen;        // valid bytes in selMain/selSub (0 = n/a)
  bool        selVerified;   // CI-V select sequence documented (Icom only)
  bool        hasSatMode;    // radio has a dedicated full-duplex / sat mode
  bool        canReadFreq;   // frequency read-back implemented for this rig
  bool        hasTone;       // CAT can set the TX CTCSS (PL) encoder tone
};

// Order MUST match RadioModel.
static const RadioProfile RADIOS[RIG_COUNT] = {
  // name       proto         addr   baud    selMain        selSub         len verf satM read tone
  { "IC-820",   PROTO_CIV,    0x42,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true, false },
  { "IC-821",   PROTO_CIV,    0x4C,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true, false },
  { "IC-910",   PROTO_CIV,    0x60,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true, true  },
  { "IC-970",   PROTO_CIV,    0x2E,  9600,  {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, false,true, false },
  { "IC-9100",  PROTO_CIV,    0x7C,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, true, true  },
  { "IC-9700",  PROTO_CIV,    0xA2,  19200, {0x07,0xD0,0}, {0x07,0xD1,0},  2,  true, true, true, true  },
  // Yaesu: 5-byte CAT. baud is the radio's CAT menu setting. No CI-V select.
  { "FT-847",   PROTO_YAESU,  0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, true, true  },
  { "FT-736R",  PROTO_YAESU,  0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, false,false },
  // Kenwood: ASCII CAT over RS-232 (needs a MAX3232-class level interface).
  { "TS-790",   PROTO_KENWOOD,0x00,  4800,  {0,0,0},       {0,0,0},        0,  true, true, true, false },
  { "TS-2000",  PROTO_KENWOOD,0x00,  57600, {0,0,0},       {0,0,0},        0,  true, true, true, true  },
};


// =========================================================================
//  rig.h
// =========================================================================

// ===========================================================================
//  rig.h  -  abstract transceiver interface (one backend per CAT family)
// ===========================================================================
//
//  The whole application talks to the radio through this narrow interface, so
//  SGP4 prediction, the One True Rule Doppler loop, calibration and the UI are
//  all protocol-agnostic. Concrete backends:
//      CivRig     (civ.cpp)     Icom CI-V          IC-820/821/910/970/9100/9700
//      YaesuRig   (yaesu.cpp)   Yaesu 5-byte CAT   FT-847, FT-736R
//      KenwoodRig (kenwood.cpp) Kenwood ASCII CAT  TS-790, TS-2000
//
//  Convention used everywhere in the app (kept regardless of how a given rig
//  labels its VFOs): "Sub" = downlink / RX, "Main" = uplink / TX.
// ===========================================================================

// Protocol-neutral operating modes; each backend maps these to its own codes.
enum RigMode : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

class Rig {
public:
  virtual ~Rig() {}

  // Open the CAT serial port. The backend already knows its own model/params.
  virtual void begin(uint32_t baud, int uartNum, int rxPin, int txPin) = 0;
  virtual bool ready() const = 0;

  // Inter-command pacing: pause this many ms after each CAT frame (CAT Delay),
  // so a slow radio keeps up. Overwritten from the CAT Delay setting at engage.
  void setCmdDelay(uint16_t ms) { cmdDelayMs = ms; }
protected:
  uint16_t cmdDelayMs = 70;
public:

  // Independent downlink (Sub/RX) and uplink (Main/TX) control.
  virtual bool setMainFreq(uint32_t hz) = 0;   // uplink (TX)
  virtual bool setSubFreq (uint32_t hz) = 0;   // downlink (RX)
  virtual bool setMainMode(RigMode m)   = 0;
  virtual bool setSubMode (RigMode m)   = 0;

  // Read the downlink (Sub/RX) frequency. Returns false if unsupported.
  virtual bool readSubFreq(uint32_t& hzOut) = 0;
  virtual bool readMainFreq(uint32_t& hzOut) = 0;

  // Toggle the rig's own satellite mode. Icom: actively forced OFF (we drive
  // MAIN/SUB ourselves). Yaesu/Kenwood: no-op -- their full-duplex/sat mode is
  // set up by the operator and must NOT be disturbed.
  virtual bool enableSatMode(bool on) = 0;

  // Leave band access on the downlink so the operator's dial stays on RX
  // (meaningful for Icom; no-op elsewhere).
  virtual void selectSubBand() = 0;
  virtual void selectMainBand() = 0;

  // Set the transmit CTCSS (PL) tone encoder. Used for FM satellites whose
  // uplink requires a subaudible tone (SO-50, AO-91, ISS, PO-101...). The tone
  // is applied to the uplink (Main/TX). on=false disables it. Backends that
  // can't drive CTCSS over CAT return false (the default).
  virtual bool setCtcss(bool on, float toneHz) { (void)on; (void)toneHz; return false; }

  // Capabilities / identity (read from the model's RadioProfile).
  virtual bool canReadFreq() const = 0;
  virtual bool hasSatMode()  const = 0;
  virtual bool hasTone()     const { return false; }   // CAT CTCSS supported
  virtual bool selVerified() const = 0;
  virtual const char* name() const = 0;

  // CI-V address (Icom only; harmless no-ops on other backends).
  virtual void    setAddress(uint8_t) {}
  virtual uint8_t address() const { return 0; }

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a RigMode.
  static RigMode modeFromString(const String& s);
};

// Index (0..38) of the nearest standard CTCSS tone to hz, or -1 if hz<=0 or no
// tone is within tolerance. The 39-tone EIA list is shared by the Yaesu code
// table and the Kenwood tone numbers; Icom encodes the frequency directly.
int  ctcssToneIndex(float hz);
// The standard tone (in Hz) at a given index, or 0 if out of range.
float ctcssToneHz(int index);

// Construct the backend for a model. Caller owns the returned pointer.
Rig* makeRig(RadioModel model);


// =========================================================================
//  civ.h
// =========================================================================

// ===========================================================================
//  civ.h  -  Icom CI-V backend (CivRig : Rig)
// ===========================================================================

// CI-V operating modes (data byte for command 0x06)
enum CivMode : uint8_t {
  CIV_LSB = 0x00, CIV_USB = 0x01, CIV_AM = 0x02, CIV_CW = 0x03,
  CIV_RTTY = 0x04, CIV_FM = 0x05, CIV_CWR = 0x07, CIV_RTTYR = 0x08
};

class CivRig : public Rig {
public:
  explicit CivRig(RadioModel m) : _model(m), _addr(RADIOS[m].civAddr) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }

  bool setMainFreq(uint32_t hz) override;        // uplink (TX) on MAIN
  bool setSubFreq (uint32_t hz) override;        // downlink (RX) on SUB
  bool setMainMode(RigMode m)   override;
  bool setSubMode (RigMode m)   override;
  bool readSubFreq(uint32_t& hzOut) override;
  bool readMainFreq(uint32_t& hzOut) override;
  bool enableSatMode(bool on)   override;
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override { selectSub(); }
  void selectMainBand()         override { selectMain(); }

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

  void    setAddress(uint8_t a) override { _addr = a; }
  uint8_t address() const       override { return _addr; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;
  uint8_t    _addr;

  void   selectMain();
  void   selectSub();
  bool   sendFrame(const uint8_t* payload, size_t len);
  bool   setFreqCiv(bool sub, uint32_t hz);
  bool   setModeCiv(bool sub, CivMode m, uint8_t filter = 0x01);
  bool   readFreqCiv(bool sub, uint32_t& hzOut);
  static CivMode toCiv(RigMode m);
  static void freqToBcd(uint32_t hz, uint8_t out[5]);
  bool   drainEcho(uint32_t timeoutMs = 60);  // CI-V is a shared bus: read back
};


// =========================================================================
//  yaesu.h
// =========================================================================

// ===========================================================================
//  yaesu.h  -  Yaesu 5-byte CAT backend (YaesuRig : Rig)  FT-847, FT-736R
// ===========================================================================
//
//  Wire format (per Hamlib rigs/yaesu/ft847.c and the FT-847 CAT manual):
//    every command is exactly 5 bytes -- four parameter bytes P1..P4 followed
//    by the OPCODE byte. Frequencies are big-endian BCD at 10 Hz resolution
//    (8 digits in 4 bytes). Satellite operation targets two VFOs by patching
//    the opcode: MAIN = 0x0-, SAT-RX (downlink) = opcode|0x10, SAT-TX (uplink)
//    = opcode|0x20. CAT must be switched on (00 00 00 00 00) before use.
//    Serial is 8 data bits, no parity, 2 stop bits (8N2).
//
//  FT-847 read-back: "read freq & mode" is opcode 0x03, patched to 0x13 for the
//  SAT-RX (downlink) VFO; the radio replies with 4 big-endian BCD bytes (10 Hz)
//  plus a mode byte. This works only on firmware-updated FT-847s -- early units
//  have no read capability and stay silent (we time out gracefully).
//
//  FT-736R note: it shares the 5-byte framing and the 8-digit/10 Hz BCD (which
//  is why 1240 MHz wraps), but it has NO frequency read-back (Hamlib caches the
//  last set value) and its native opcodes differ from the FT-847. The proven
//  community path is to drive it through an FT-847-emulating CAT interface
//  (KA6BFB / HS-736USB), which is what this backend assumes. To talk to a bare
//  FT-736R, confirm its opcodes against the FT-736R CAT manual / Hamlib ft736.c.
// ===========================================================================

class YaesuRig : public Rig {
public:
  explicit YaesuRig(RadioModel m)
    : _model(m), _postMs(m == RIG_FT736R ? 60 : 10) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }

  bool setMainFreq(uint32_t hz) override { return setFreq(0x21, hz); } // SAT TX
  bool setSubFreq (uint32_t hz) override { _lastSubHz = hz;            // SAT RX
                                           return setFreq(0x11, hz); }
  bool setMainMode(RigMode m)   override { return setMode(0x27, m); }  // SAT TX
  bool setSubMode (RigMode m)   override { return setMode(0x17, m); }  // SAT RX
  bool readSubFreq(uint32_t& hzOut) override;          // FT-847 only (0x13)
  bool readMainFreq(uint32_t& hzOut) override { (void)hzOut; return false; }
  bool enableSatMode(bool)      override { return false; }             // operator-set
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override {}                            // n/a
  void selectMainBand()         override {}                            // n/a

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;
  uint16_t   _postMs;          // inter-command delay (FT-736R needs more)
  uint32_t   _lastSubHz = 0;   // last downlink we commanded (wrong-VFO guard)

  bool   send(const uint8_t cmd[5]);
  bool   setFreq(uint8_t opcode, uint32_t hz);
  bool   setMode(uint8_t opcode, RigMode m);
  static uint8_t modeCode(RigMode m);
  static void    freqToBcd(uint32_t hz, uint8_t out[4]);
  static uint32_t bcdToFreq(const uint8_t in[4]);   // big-endian BCD * 10 Hz
};


// =========================================================================
//  kenwood.h
// =========================================================================

// ===========================================================================
//  kenwood.h  -  Kenwood ASCII CAT backend (KenwoodRig : Rig)  TS-790, TS-2000
// ===========================================================================
//
//  Wire format (per Hamlib kenwood/ and the TS-2000 CAT manual): two-letter
//  ASCII commands with optional parameters, terminated by ';'. Frequencies are
//  an 11-digit Hz field. Reads echo the same command back with data.
//      FA<11 digits>;   set VFO A frequency      FA;  -> FA<11 digits>;  (read)
//      FB<11 digits>;   set VFO B frequency
//      MD<n>;           set mode  (1 LSB 2 USB 3 CW 4 FM 5 AM 6 FSK 7 CWR)
//      IF;              read transceiver status
//  Serial is 8N1 over RS-232 levels, so a MAX3232-class interface is required
//  between the radio's DB-9 and the 3.3 V UART (NOT the Icom CI-V circuit).
//
//  Sat mapping: this backend puts the downlink (RX) on VFO A (FA) and the
//  uplink (TX) on VFO B (FB). The rig's own satellite / split mode and the
//  uplink/downlink BANDS are selected by the operator on the radio (CAT can't
//  switch bands on these rigs); CardSat Doppler-tunes within that setup. On the
//  TS-2000 in particular, verify the VFO-A/B vs main/sub-band behaviour for your
//  firmware. The TS-790 supports a subset of these commands.
// ===========================================================================

class KenwoodRig : public Rig {
public:
  explicit KenwoodRig(RadioModel m) : _model(m) {}

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _stream != nullptr; }

  bool setMainFreq(uint32_t hz) override { return setVfoFreq("FB", hz); } // uplink/TX
  bool setSubFreq (uint32_t hz) override { return setVfoFreq("FA", hz); } // downlink/RX
  bool setMainMode(RigMode m)   override { return setModeKw(m); }
  bool setSubMode (RigMode m)   override { return setModeKw(m); }
  bool readSubFreq(uint32_t& hzOut) override;
  bool readMainFreq(uint32_t& hzOut) override { (void)hzOut; return false; }
  bool enableSatMode(bool)      override { return false; } // operator-set on radio
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override {}
  void selectMainBand()         override {}

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return RADIOS[_model].name; }

private:
  Stream*    _stream = nullptr;
  RadioModel _model;

  bool   sendCmd(const String& cmd);
  bool   setVfoFreq(const char* vfo, uint32_t hz);
  bool   setModeKw(RigMode m);
  static char modeDigit(RigMode m);
};


// =========================================================================
//  rotator.h
// =========================================================================

// ===========================================================================
//  rotator.h  -  az/el antenna rotator interface + GS-232 backend
// ===========================================================================
//
//  Mirrors the Rig abstraction: the app points the rotator through a narrow
//  interface and the backend handles the wire protocol. The only backend so
//  far is GS-232 (Yaesu's de-facto standard, also emulated by SPID, K3NG,
//  RadioArtisan, ERC, etc.), reached through an SC16IS750/752 I2C->UART bridge
//  because all three ESP32-S3 hardware UARTs are already in use.
//
//  GS-232 (per the Yaesu GS-232A/B manuals and Hamlib rotators/gs232a,gs232b):
//      "Waaa eee\r"  point to azimuth aaa (000-360/450) + elevation eee (000-180)
//      "C2\r"        read position -> "+0aaa+0eee" (GS-232A) or
//                                     "AZ=aaaEL=eee" (GS-232B); we parse both
//      "S\r"         all stop
//  Serial is 8N1, no handshake, commonly 9600 baud (the controller's setting).
//
//  Convention: degrees; azimuth 0-360 (0-450 with overlap), elevation 0-90
//  (0-180 in flip mode). "Sub"/"Main" do not apply here.
// ===========================================================================

class Rotator {
public:
  virtual ~Rotator() {}
  virtual void begin() = 0;
  virtual bool ready() const = 0;
  virtual bool point(float az, float el) = 0;     // command absolute position
  virtual bool readPos(float& az, float& el) = 0; // false if no/!valid reply
  virtual void stop() = 0;
  virtual const char* name() const = 0;
};

// GS-232A/B rotator via an SC16IS750/752 I2C->UART bridge on Wire1.
class Gs232Rotator : public Rotator {
public:
  Gs232Rotator(uint8_t i2cAddr, uint32_t baud) : _addr(i2cAddr), _baud(baud) {}
  void begin() override;
  bool ready() const override { return _ok; }
  bool point(float az, float el) override;
  bool readPos(float& az, float& el) override;
  void stop() override;
  const char* name() const override { return "GS-232"; }

private:
  uint8_t  _addr;
  uint32_t _baud;
  bool     _ok = false;

  // SC16IS750 I2C-UART bridge register access (Wire1).
  void    wreg(uint8_t reg, uint8_t val);
  uint8_t rreg(uint8_t reg);
  bool    bridgeInit();
  // Byte-level UART through the bridge.
  void    putc_(char c);
  void    puts_(const char* s);
  int     getc_();                 // -1 if no byte ready
  void    flushIn();
};

// Build the configured rotator backend (GS-232 at ROT_I2C_ADDR). Caller owns it.
Rotator* makeRotator(uint32_t baud);


// =========================================================================
//  satdb.h
// =========================================================================

// ===========================================================================
//  satdb.h  -  in-memory satellite catalog (slim) + transponder parsing
// ===========================================================================
//  Orbital data is GP (General Perturbations / OMM) element sets, sourced from
//  AMSAT's JSON distribution. The legacy TLE *text* format is being retired as
//  the 5-digit NORAD catalog field runs out; GP/OMM carries the same SGP4 mean
//  elements in named fields with no width limit. We store the elements here and
//  reconstruct a TLE line-pair on demand only to feed the SGP4 propagator (the
//  Hopperpop library ingests elements via twoline2rv); see gpToTle().
//
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
  float    toneHz   = 0.0f;  // required FM uplink CTCSS/PL tone (0 = none)

  // Downlink passband width in Hz (0 for single-channel / FM).
  uint32_t bandwidth() const {
    return (downlinkHigh > downlink) ? (downlinkHigh - downlink) : 0;
  }
};

// One satellite's GP mean elements (the SGP4 inputs) plus identity.
struct SatEntry {
  char     name[26];          // AMSAT_NAME
  uint32_t norad = 0;         // NORAD_CAT_ID (identity / display)
  char     intlDes[12] = {0}; // OBJECT_ID, e.g. "1974-089B"
  double   epochUnix = 0;     // EPOCH as Unix UTC seconds (fractional)
  double   incl = 0;          // INCLINATION       (deg)
  double   ecc = 0;           // ECCENTRICITY      (dimensionless)
  double   raan = 0;          // RA_OF_ASC_NODE    (deg)
  double   argp = 0;          // ARG_OF_PERICENTER (deg)
  double   ma = 0;            // MEAN_ANOMALY      (deg)
  double   meanMotion = 0;    // MEAN_MOTION       (rev/day)
  double   bstar = 0;         // BSTAR             (1/earth radii)
  double   ndot = 0;          // MEAN_MOTION_DOT   (rev/day^2, = ndot/2)
  double   nddot = 0;         // MEAN_MOTION_DDOT  (rev/day^3, = nddot/6)
  uint32_t revAtEpoch = 0;    // REV_AT_EPOCH
  uint16_t elsetNum = 0;      // ELEMENT_SET_NO
  bool     txLoaded = false;  // have we fetched transponders this session?
};

class SatDb {
public:
  bool begin();                  // mount LittleFS
  int  count() const { return _n; }
  SatEntry& at(int i) { return _sats[i]; }
  int  indexOfNorad(uint32_t norad) const;

  // Parse AMSAT's GP JSON (array of OMM objects) into the catalog.
  int  loadGpFromJson(const String& json);    // replace DB
  int  appendGpFromJson(const String& json);  // append (dedup/replace by norad)
  bool addGp(const SatEntry& s);               // one manual sat (+persist NDJSON)
  bool loadManualGpFile();                     // merge FILE_MGP into the DB
  bool loadGpFromFs();                         // reload cached GP JSON at boot
  int  loadGpFromFile(const char* path);       // stream-parse a GP file (low RAM)
  bool saveGpJson(const String& json);         // cache the downloaded blob

  // Reconstruct a TLE line-pair from a satellite's GP elements (69 chars each,
  // checksummed). Only used to initialise the SGP4 propagator. Returns false
  // on a malformed entry.
  static bool gpToTle(const SatEntry& s, char l1[72], char l2[72]);

  // Parse an OMM EPOCH string ("YYYY-MM-DD HH:MM:SS.ffffff") to Unix UTC
  // seconds (fractional). TZ-independent. Exposed for manual entry.
  static double gpEpochToUnix(const char* s);

  // Parse a SatNOGS /api/transmitters/ JSON array into out[0..maxN-1].
  // Returns number of (active) transponders parsed.
  static int parseTransmittersJson(const String& json,
                                   Transponder* out, int maxN);

  // Per-satellite transponder cache on LittleFS.
  static bool saveTxCache(uint32_t norad, const String& json);
  static int  loadTxCache(uint32_t norad, Transponder* out, int maxN);

  // Required FM uplink CTCSS (PL) tone in Hz for well-known FM satellites
  // (SatNOGS carries no structured tone field), or 0 if none/unknown.
  static float knownCtcssHz(uint32_t norad);

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
  // Maidenhead -> lat/lon (square centre) without mutating any Location.
  static bool gridToLatLon(const String& grid, double& lat, double& lon);

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
//  net.h  -  WiFi + HTTPS downloads (AMSAT GP, SatNOGS transponders)
// ===========================================================================

// One access point returned by a WiFi scan.
struct WifiAp {
  char    ssid[33];
  int8_t  rssi;     // signal strength, dBm
  bool    enc;      // true = secured (needs a password)
};

class Net {
public:
  bool connect(const String& ssid, const String& pass, uint32_t timeoutMs = 15000);
  int  scanWifi(WifiAp* out, int maxAps);   // scan nearby APs (blocking); count or -1
  bool connected();
  void syncTimeNtp();                       // sets system clock via NTP (UTC)

  // GET a URL over HTTPS into `out`. Returns false on HTTP/transport error.
  bool httpsGet(const String& url, String& out, size_t maxBytes = 200000);

  // GET a URL over HTTPS straight into a LittleFS file (no large RAM buffer).
  // Essential for the GP file: a ~75 KB body can't be held as one contiguous
  // String on the fragmented no-PSRAM heap (String growth silently truncates).
  bool httpsGetToFile(const String& url, const char* path,
                      size_t maxBytes = 400000, size_t* written = nullptr);

  // Convenience wrappers.
  bool fetchGp(const String& url, String& out);    // AMSAT GP/OMM JSON array
  bool fetchGpToFile(const String& url, const char* path);  // GP -> cache file
  bool fetchSatnogsTransmitters(uint32_t norad, String& out);

  // Diagnostics from the most recent httpsGet (for on-screen / serial errors).
  int    lastCode = 0;     // HTTP status (>0) or HTTPClient error (<0)
  String lastErr  = "";    // short human-readable reason
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
  bool   sunlit = true;      // satellite illuminated (not in Earth's shadow)
  double sunAz = 0, sunEl = 0;   // Sun position from the observer (degrees)
};

// One co-visibility (mutual) window: both my station and a remote (DX) station
// see the satellite above their horizons at the same time.
struct MutualWindow {
  time_t start = 0, end = 0; // unix UTC bounds of co-visibility
  float  myMaxEl = 0;        // peak elevation here during the window
  float  dxMaxEl = 0;        // peak elevation at the remote site during the window
};

class Predictor {
public:
  void setSite(const Observer& o);
  // Point the propagator at a satellite (renders its GP elements for SGP4).
  bool setSat(SatEntry& s);

  // Compute az/el/range/range-rate at unix time `t` (UTC seconds).
  LiveLook look(time_t t);

  // Range rate (km/s, +ve receding) at a FRACTIONAL unix time, taken from the
  // SGP4 velocity vector (the method Gpredict/sgp4sdp4 use) rather than by
  // differencing slant range. Exact and not quantised to whole seconds.
  double rangeRateAt(double unixSec);

  // Lightweight: just az/el (degrees) for the current site at time t.
  bool azelAt(time_t t, double& az, double& el);

  // Topocentric elevation (deg) of a satellite at sub-point (satLat/Lon, altKm)
  // as seen from an arbitrary observer. Used for the remote leg of a mutual
  // (co-visibility) window without a second propagator.
  static double elevationFromSubpoint(double obsLatDeg, double obsLonDeg,
                                      double obsAltM,
                                      double satLatDeg, double satLonDeg,
                                      double satAltKm);

  // Find co-visibility windows where this site and `dx` both see the satellite
  // above `minEl` at once, searching forward from `from`. Returns count (<=maxN).
  int mutualWindows(time_t from, const Observer& dx, float minEl,
                    MutualWindow* out, int maxN);

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
  double _epochUnix = 0;        // element-set epoch (Unix UTC s), for tsince
  char   _name[26], _l1[72], _l2[72];
};


// =========================================================================
//  settings.h
// =========================================================================

// ===========================================================================
//  settings.h  -  persistent user configuration (LittleFS JSON)
// ===========================================================================

// Which physical VFO (Main/Sub) carries the uplink vs the downlink.
enum VfoType : uint8_t {
  VFO_MAIN_UP_SUB_DOWN = 0,   // uplink on MAIN, downlink on SUB (default)
  VFO_MAIN_DOWN_SUB_UP = 1,   // uplink on SUB,  downlink on MAIN
};

struct Settings {
  // WiFi
  char     ssid[33] = "";
  char     pass[65] = "";
  // Orbital data source (GP/OMM JSON). Editable in Settings.
  char     gpUrl[160] = AMSAT_GP_URL;
  char     myCall[14] = "";   // operator's own callsign (stored uppercase)
  // Location
  double   lat = 0.0, lon = 0.0, altM = 0.0;
  bool     useGps = false;
  uint8_t  gpsSource = GPS_SRC_CAP1262;  // GpsSource: Grove / Cap868 / Cap1262
  // Radio
  uint8_t  radioModel = RIG_IC9700;
  uint8_t  civAddr    = 0xA2;   // 0 => use model default
  uint32_t civBaud    = 19200;
  // (CI-V is TTL serial only.) VFO roles + whether to command the rig's own
  // satellite mode when engaging radio control.
  uint8_t  vfoType    = VFO_MAIN_UP_SUB_DOWN;
  bool     satMode    = false;
  uint32_t catRateMs  = 500;   // CAT/Doppler update period (ms), adjustable in 10 ms steps
  uint16_t catDelayMs = 70;    // pause after each CAT command before the next (ms)
  // Tracking
  float    minPassEl  = 5.0f;
  bool     aosAlarm   = true;   // beep + flash before a favorite's AOS
  // Display / power
  uint16_t dimSecs    = 120;    // blank the backlight after this idle time (s); 0 = never
  // Calibration (persisted oscillator offsets, Hz)
  int32_t  calDlHz = 0;
  int32_t  calUlHz = 0;
  // Rotator (GS-232 az/el over an I2C->UART bridge)
  bool     rotEnable   = false;
  uint32_t rotBaud     = 9600;   // GS-232 serial (commonly 9600)
  int16_t  rotAzOff    = 0;      // deg added to commanded azimuth (alignment)
  int16_t  rotElOff    = 0;      // deg added to commanded elevation
  uint8_t  rotDeadband = 3;      // deg; suppress smaller moves (anti-chatter)
  uint16_t rotParkAz   = 0;      // park azimuth on LOS / when disabled
  uint8_t  rotParkEl   = 0;      // park elevation
  bool     rotFlip     = false;  // flip mode (450 az + 0-180 el) for overhead passes

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
  SCR_HOME = 0, SCR_SATLIST, SCR_SCHEDULE, SCR_PASSES, SCR_PASSDETAIL,
  SCR_TRACK, SCR_POLAR, SCR_LOCATION, SCR_UPDATE, SCR_SETTINGS, SCR_EDIT,
  SCR_PASSPOLAR, SCR_MUTUAL, SCR_WIFISCAN, SCR_ABOUT, SCR_LOG, SCR_LOGENTRY,
  SCR_LOGLIST
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
  uint32_t   aosFlashUntil = 0;        // screen-flash overlay end (millis)
  char       aosFlashName[26] = {0};
  int      curTx = 0;             // selected transponder index for active sat
  Transponder activeTx[MAX_TX_PER_SAT];
  int      activeTxCount = 0;     // transponders loaded for the active sat
  int      setSel = 0;            // settings menu cursor

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
  TuneMode tuneMode = TM_HOLD;    // Doppler tune mode (cycle with 'd' on Track)
                                  // holds a constant frequency AT THE SATELLITE
  uint32_t lastRxSet = 0;         // last downlink Hz we wrote (detect knob moves)
  uint32_t lastDoppMs = 0;
  float    toneApplied = -2.0f;   // CTCSS tone currently set on the rig (Hz);
                                  // 0 = off, -2 = unknown/never (force re-apply)
  bool     rotOut = false;       // are we pointing the rotator?
  float    lastAzCmd = -999.0f;  // last az/el we commanded (deadband)
  float    lastElCmd = -999.0f;
  bool     rotParked = false;
  uint32_t lastRotMs = 0;
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

  // status line
  String   status;
  uint32_t statusUntil = 0;

  // ---- helpers ----
  void applyRadioFromCfg();
  void applyRotatorFromCfg();
  void applyTransponderModes(const Transponder& t);  // per-leg SSB/FM mode policy
  // Route logical downlink/uplink to the physical MAIN/SUB VFOs per cfg.vfoType.
  bool dlOnSub() const { return cfg.vfoType == VFO_MAIN_UP_SUB_DOWN; }
  bool rigSetDownlinkFreq(uint32_t hz) { return dlOnSub() ? rig->setSubFreq(hz)  : rig->setMainFreq(hz); }
  bool rigSetUplinkFreq  (uint32_t hz) { return dlOnSub() ? rig->setMainFreq(hz) : rig->setSubFreq(hz); }
  void rigSetDownlinkMode(RigMode m)   { if (dlOnSub()) rig->setSubMode(m);  else rig->setMainMode(m); }
  void rigSetUplinkMode  (RigMode m)   { if (dlOnSub()) rig->setMainMode(m); else rig->setSubMode(m); }
  bool rigReadDownlinkFreq(uint32_t& h){ return dlOnSub() ? rig->readSubFreq(h) : rig->readMainFreq(h); }
  void rigSelectDownlink()             { if (dlOnSub()) rig->selectSubBand();  else rig->selectMainBand(); }
  void rigSelectUplink()               { if (dlOnSub()) rig->selectMainBand(); else rig->selectSubBand(); }
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
  void doUpdateGp();
  void doCacheAllTransponders();           // fetch+cache every sat's TX (offline prep)
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
  void keyPolar(char c, bool enter, bool back);
  void keyPassPolar(char c, bool enter, bool back);
  void keyMutual(char c, bool enter, bool back);
  void keyLocation(char c, bool enter, bool back);
  void keyUpdate(char c, bool enter, bool back);
  void keySettings(char c, bool enter, bool back);
  void startWifiScan();
  void keyWifiScan(char c, bool enter, bool back);
  void keyAbout(char c, bool enter, bool back);
  void keyLog(char c, bool enter, bool back);
  void keyLogEntry(char c, bool enter, bool back);
  void beginQso();                // snapshot auto fields, open the entry screen
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


// =========================================================================
//  storage.cpp
// =========================================================================

// ===========================================================================
//  storage.cpp
// ===========================================================================

namespace Store {

static fs::FS* g_fs    = &LittleFS;   // default; updated by begin()
static bool    g_ready = false;
static bool    g_sd    = false;

bool begin() {
  g_ready = false; g_sd = false; g_fs = &LittleFS;

  // 1) Prefer the microSD card. Use the default SPI bus (FSPI) with the
  //    Cardputer SD pins and a 25 MHz clock -- matching M5's reference init.
  //    (An earlier build used a separate HSPI instance, which collides with the
  //    display's SPI peripheral and makes SD.begin() fail even with a card in.)
  //    The card must be formatted FAT32 (not exFAT).
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  bool sdMounted = SD.begin(SD_CS_PIN, SPI, SD_FREQ_HZ);
  if (!sdMounted) sdMounted = SD.begin(SD_CS_PIN, SPI, 1000000);   // retry slower
  if (sdMounted) {
    g_fs = &SD; g_ready = true; g_sd = true;
    if (!SD.exists(DATA_DIR)) SD.mkdir(DATA_DIR);
    Serial.println("[fs] using microSD card for storage (" DATA_DIR ")");
    return true;
  }

  // 2) No SD card -> fall back to internal LittleFS (same DATA_DIR). begin(true)
  //    formats the SPIFFS partition if it mounts dirty; only works if such a
  //    partition exists in the flashed table.
  Serial.println("[fs] no SD card - falling back to internal LittleFS");
  if (LittleFS.begin(true)) {
    g_fs = &LittleFS; g_ready = true;
    if (!LittleFS.exists(DATA_DIR)) LittleFS.mkdir(DATA_DIR);
    Serial.printf("[fs] LittleFS mounted (%u/%u bytes used)\n",
                  (unsigned)LittleFS.usedBytes(), (unsigned)LittleFS.totalBytes());
    return true;
  }

  Serial.println("[fs] NO filesystem available (SD and LittleFS both failed)");
  g_fs = &LittleFS;           // leave a valid object; opens will fail gracefully
  return false;
}

fs::FS& fs()  { return *g_fs; }
bool ready()  { return g_ready; }
bool onSD()   { return g_sd; }

bool formatInternal() {
  if (g_sd) {
    // Don't format the user's SD card on a factory reset -- just delete our own
    // top-level files (per-NORAD transponder caches are harmless and refresh).
    const char* names[] = { FILE_CFG, FILE_GP, FILE_MGP, FILE_FAVS, FILE_CALIB, FILE_TONES };
    for (const char* n : names) if (SD.exists(n)) SD.remove(n);
    return true;
  }
  return LittleFS.format();
}

} // namespace Store


// =========================================================================
//  rig.cpp
// =========================================================================

// ===========================================================================
//  rig.cpp  -  Rig factory + shared helpers
// ===========================================================================

RigMode Rig::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return RM_FM;
  if (u.indexOf("USB") >= 0) return RM_USB;
  if (u.indexOf("LSB") >= 0) return RM_LSB;
  if (u.indexOf("CW")  >= 0) return RM_CW;
  if (u.indexOf("AM")  >= 0) return RM_AM;
  if (u.indexOf("FSK") >= 0 || u.indexOf("RTTY") >= 0 ||
      u.indexOf("DATA") >= 0 || u.indexOf("DIG") >= 0) return RM_DATA;
  // Linear transponders are most often operated USB up / USB down.
  return RM_USB;
}

Rig* makeRig(RadioModel model) {
  switch (RADIOS[model].proto) {
    case PROTO_YAESU:   return new YaesuRig(model);
    case PROTO_KENWOOD: return new KenwoodRig(model);
    case PROTO_CIV:
    default:            return new CivRig(model);
  }
}

// Standard 39 EIA CTCSS tones in tenths of Hz, ascending. This exact order is
// shared with Hamlib's ft847_ctcss_list[] and the Kenwood tone list, so the
// index doubles as the Kenwood tone number (index+1) and the row into the
// FT-847 CAT code table. Icom encodes the frequency in BCD instead.
static const uint16_t CTCSS_TENTHS[39] = {
  670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
  948, 974, 1000,1035,1072,1109,1148,1188,1230,1273,
  1318,1365,1413,1462,1514,1567,1622,1679,1738,1799,
  1862,1928,2035,2107,2181,2257,2336,2418,2503
};

int ctcssToneIndex(float hz) {
  if (hz <= 0) return -1;
  int target = (int)lroundf(hz * 10.0f);   // tenths of Hz
  int best = -1, bestErr = 9999;
  for (int i = 0; i < 39; ++i) {
    int e = abs((int)CTCSS_TENTHS[i] - target);
    if (e < bestErr) { bestErr = e; best = i; }
  }
  // Reject if the nearest standard tone is more than ~1 Hz away (bad input).
  return (bestErr <= 10) ? best : -1;
}

float ctcssToneHz(int index) {
  if (index < 0 || index >= 39) return 0.0f;
  return CTCSS_TENTHS[index] / 10.0f;
}


// =========================================================================
//  civ.cpp
// =========================================================================

// ===========================================================================
//  civ.cpp  -  Icom CI-V backend
// ===========================================================================

// Set to 0 to silence the CI-V trace on the serial monitor (115200 baud).
#define CIV_DEBUG 1

#if CIV_DEBUG
// Decode the command byte of a single CI-V frame for the human-readable tail.
static void civDecode(const uint8_t* b, size_t n) {
  if (n < 5) return;
  uint8_t cmd = b[4];
  switch (cmd) {
    case 0x03:
      if (n >= 11) { uint32_t hz = 0;
        for (int k = 9; k >= 5; --k) hz = hz*100 + (b[k]>>4)*10 + (b[k]&0x0F);
        Serial.printf("  read-freq -> %lu Hz", (unsigned long)hz); }
      else Serial.print("  read-freq req");
      break;
    case 0x05:
      if (n >= 10) { uint32_t hz = 0;
        for (int k = 9; k >= 5; --k) hz = hz*100 + (b[k]>>4)*10 + (b[k]&0x0F);
        Serial.printf("  set-freq %lu Hz", (unsigned long)hz); }
      break;
    case 0x06: { const char* m = "?";
      if (n > 5) switch (b[5]) { case 0:m="LSB";break; case 1:m="USB";break;
        case 2:m="AM";break; case 3:m="CW";break; case 5:m="FM";break;
        case 7:m="CW-R";break; }
      Serial.printf("  set-mode %s", m); } break;
    case 0x07: Serial.print("  sel-band ");
      if (n > 5) { if (b[5]==0xD0) Serial.print("MAIN");
                   else if (b[5]==0xD1) Serial.print("SUB");
                   else if (b[5]==0xB0) Serial.print("swap");
                   else Serial.printf("%02X", b[5]); } break;
    case 0x16: Serial.printf("  sat-mode %s", (n > 6 && b[6]) ? "ON" : "OFF"); break;
    case 0xFB: Serial.print("  ACK"); break;
    case 0xFA: Serial.print("  NAK"); break;
  }
}
static void civLog(const char* dir, const uint8_t* b, size_t n) {
  Serial.printf("[CI-V %s]", dir);
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  civDecode(b, n);
  Serial.println();
}
static void civLogRaw(const char* dir, const uint8_t* b, size_t n) {
  Serial.printf("[CI-V %s]", dir);
  if (!n) Serial.print(" (none)");
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  Serial.println();
}
#else
static inline void civLog(const char*, const uint8_t*, size_t) {}
static inline void civLogRaw(const char*, const uint8_t*, size_t) {}
#endif

void CivRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;   // construct once, reuse on re-begin
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N1, rxPin, txPin);
  _stream = hs;
}

CivMode CivRig::toCiv(RigMode m) {
  switch (m) {
    case RM_LSB: return CIV_LSB;
    case RM_USB: return CIV_USB;
    case RM_CW:  return CIV_CW;
    case RM_FM:  return CIV_FM;
    case RM_AM:  return CIV_AM;
    case RM_DATA:return CIV_RTTY;
    default:     return CIV_USB;
  }
}

// --- BCD: 1 Hz resolution, 5 bytes, least-significant pair first ----------
void CivRig::freqToBcd(uint32_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; ++i) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}

bool CivRig::sendFrame(const uint8_t* payload, size_t len) {
  if (!_stream) return false;
  uint8_t buf[20];
  size_t n = 0;
  buf[n++] = 0xFE; buf[n++] = 0xFE;
  buf[n++] = _addr;       // to radio
  buf[n++] = 0xE0;        // from controller
  for (size_t i = 0; i < len && n < sizeof(buf) - 1; ++i) buf[n++] = payload[i];
  buf[n++] = 0xFD;        // end of message
  civLog("TX", buf, n);   // trace the command to the serial monitor
  _stream->write(buf, n);
  _stream->flush();
  drainEcho();            // swallow our own echo + radio's OK/NG (0xFB/0xFA)
  if (cmdDelayMs) delay(cmdDelayMs);   // CAT Delay: pause before the next command
  return true;
}

bool CivRig::drainEcho(uint32_t timeoutMs) {
  if (!_stream) return false;
  uint32_t t0 = millis();
  int fd = 0;                          // 1 = our echo seen, 2 = radio reply seen
#if CIV_DEBUG
  uint8_t rx[40]; size_t rn = 0;
#endif
  while (millis() - t0 < timeoutMs) {
    while (_stream->available()) {
      uint8_t b = (uint8_t)_stream->read();
#if CIV_DEBUG
      if (rn < sizeof(rx)) rx[rn++] = b;
#endif
      if (b == 0xFD) fd++;
      t0 = millis();
    }
    if (fd >= 2) break;                       // echo + ACK/NAK both arrived
    if (fd >= 1 && millis() - t0 > 25) break;  // echo seen, radio not replying
    delay(1);
  }
#if CIV_DEBUG
  // Report only the radio's reply (ACK/NAK), not the echo of our own frame.
  for (size_t i = 0; i + 5 < rn; ++i) {
    if (rx[i]==0xFE && rx[i+1]==0xFE && rx[i+2]==0xE0 && rx[i+3]==_addr) {
      if (rx[i+4]==0xFB) { Serial.println("[CI-V RX] radio ACK (FB)"); break; }
      if (rx[i+4]==0xFA) { Serial.println("[CI-V RX] radio NAK (FA)"); break; }
    }
  }
#endif
  return fd >= 1;
}

void CivRig::selectMain() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selMain, p.selLen);
}
void CivRig::selectSub() {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendFrame(p.selSub, p.selLen);
}

bool CivRig::setFreqCiv(bool sub, uint32_t hz) {
  sub ? selectSub() : selectMain();
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendFrame(pl, 6);
}
bool CivRig::setModeCiv(bool sub, CivMode m, uint8_t filter) {
  sub ? selectSub() : selectMain();
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendFrame(pl, 3);
}

bool CivRig::setMainFreq(uint32_t hz) { return setFreqCiv(false, hz); }
bool CivRig::setSubFreq (uint32_t hz) { return setFreqCiv(true,  hz); }
bool CivRig::setMainMode(RigMode m)   { return setModeCiv(false, toCiv(m)); }
bool CivRig::setSubMode (RigMode m)   { return setModeCiv(true,  toCiv(m)); }

bool CivRig::enableSatMode(bool on) {
  if (!RADIOS[_model].hasSatMode) return false;
  // IC-9700/9100: command 0x16, sub 0x5A, data 0x01/0x00.
  uint8_t pl[3] = { 0x16, 0x5A, (uint8_t)(on ? 0x01 : 0x00) };
  return sendFrame(pl, 3);
}

// Transmit CTCSS (PL) tone for an FM uplink. The tone lives on the uplink, so
// we select MAIN first. Repeater-tone frequency: cmd 0x1B sub 0x00 + 2 BCD
// bytes of the tone in tenths of Hz (e.g. 67.0 -> 0670 -> 0x06 0x70). Repeater-
// tone (encoder) on/off: cmd 0x16 sub 0x42 data 0x01/0x00. Verified against the
// Icom CI-V reference (IC-9700/910H/9100 family).
bool CivRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  // The caller selects the uplink band first (MAIN or SUB, per VFO Type).
  if (on && toneHz > 0) {
    int t = (int)lroundf(toneHz * 10.0f);         // tenths of Hz, 4 BCD digits
    uint8_t b1 = (uint8_t)((((t / 1000) % 10) << 4) | ((t / 100) % 10));
    uint8_t b2 = (uint8_t)((((t / 10)   % 10) << 4) | (t % 10));
    uint8_t freq[4] = { 0x1B, 0x00, b1, b2 };
    sendFrame(freq, 4);
    uint8_t enc[3]  = { 0x16, 0x42, 0x01 };
    return sendFrame(enc, 3);
  }
  uint8_t off[3] = { 0x16, 0x42, 0x00 };
  return sendFrame(off, 3);
}

bool CivRig::readSubFreq (uint32_t& hzOut) { return readFreqCiv(true,  hzOut); }
bool CivRig::readMainFreq(uint32_t& hzOut) { return readFreqCiv(false, hzOut); }

// Read the operating frequency of the SUB (sub=true) or MAIN (sub=false) band.
bool CivRig::readFreqCiv(bool sub, uint32_t& hzOut) {
  if (!_stream) return false;
  if (!RADIOS[_model].canReadFreq) return false;   // set-only rig
  sub ? selectSub() : selectMain();                 // 07 D1/D0 (drains its echo)
  while (_stream->available()) _stream->read();     // clear anything stale
  // Send read-operating-frequency request (cmd 0x03) WITHOUT draining: we want
  // the radio's reply, which on a single-wire CI-V bus arrives after our echo.
  uint8_t f[6] = { 0xFE, 0xFE, _addr, 0xE0, 0x03, 0xFD };
  civLog("TX", f, 6);
  _stream->write(f, 6);
  _stream->flush();
  // Collect the response bytes (echo + reply) for a short window.
  uint8_t buf[48]; size_t bn = 0; uint32_t t0 = millis();
  while (millis() - t0 < 150) {
    while (_stream->available() && bn < sizeof(buf)) {
      buf[bn++] = (uint8_t)_stream->read(); t0 = millis();
    }
    delay(1);
  }
  civLogRaw("RX", buf, bn);
  // Find the reply frame addressed to the controller:
  //   FE FE E0 <addr> 03 b0 b1 b2 b3 b4 FD   (b0 = least-significant BCD pair)
  for (size_t i = 0; i + 10 < bn; ++i) {
    if (buf[i] == 0xFE && buf[i+1] == 0xFE && buf[i+2] == 0xE0 &&
        buf[i+3] == _addr && buf[i+4] == 0x03 && buf[i+10] == 0xFD) {
      uint32_t hz = 0;
      for (int k = 9; k >= 5; --k)
        hz = hz * 100 + (buf[i+k] >> 4) * 10 + (buf[i+k] & 0x0F);
      hzOut = hz;
#if CIV_DEBUG
      Serial.printf("[CI-V] %s freq read: %lu Hz\n", sub ? "SUB" : "MAIN", (unsigned long)hz);
#endif
      return true;
    }
  }
#if CIV_DEBUG
  Serial.printf("[CI-V] %s freq read: no valid reply\n", sub ? "SUB" : "MAIN");
#endif
  return false;
}


// =========================================================================
//  yaesu.cpp
// =========================================================================

// ===========================================================================
//  yaesu.cpp  -  Yaesu 5-byte CAT backend
// ===========================================================================

// Set to 0 to silence the CAT trace on the serial monitor (115200 baud).
#define YAESU_DEBUG 1

#if YAESU_DEBUG
static void yaLog(const char* tag, const uint8_t* b, size_t n) {
  Serial.printf("[CAT %s]", tag);
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", b[i]);
  if (n == 5) {
    uint8_t op = b[4];
    const char* vfo = (op & 0x20) ? "TX" : (op & 0x10) ? "RX" : "MAIN";
    switch (op & 0x0F) {
      case 0x01: { uint32_t f = 0;             // set-freq (10 Hz BCD)
        for (int i = 0; i < 4; ++i) f = f*100 + (b[i]>>4)*10 + (b[i]&0x0F);
        Serial.printf("  set-freq %s %lu Hz", vfo, (unsigned long)f*10); } break;
      case 0x07: Serial.printf("  set-mode %s 0x%02X", vfo, b[0]); break;
      case 0x03: Serial.printf("  read-freq req %s", vfo); break;
      case 0x00: Serial.print(b[4] == 0x80 ? "  CAT OFF" : "  CAT ON"); break;
    }
  }
  Serial.println();
}
#else
static inline void yaLog(const char*, const uint8_t*, size_t) {}
#endif

void YaesuRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N2, rxPin, txPin);   // Yaesu CAT = 8N2
  _stream = hs;
  const uint8_t catOn[5] = { 0x00, 0x00, 0x00, 0x00, 0x00 };  // enable CAT
  send(catOn);
}

uint8_t YaesuRig::modeCode(RigMode m) {
  switch (m) {                       // FT-847 operating-mode codes
    case RM_LSB: return 0x00;
    case RM_USB: return 0x01;
    case RM_CW:  return 0x02;
    case RM_AM:  return 0x04;
    case RM_FM:  return 0x08;
    case RM_DATA:return 0x0A;
    default:     return 0x01;
  }
}

// Big-endian BCD, 10 Hz resolution, 4 bytes (8 digits of hz/10).
void YaesuRig::freqToBcd(uint32_t hz, uint8_t out[4]) {
  uint32_t f = hz / 10;                          // 10 Hz units
  out[0] = (uint8_t)((((f/10000000)%10)<<4) | ((f/1000000)%10));
  out[1] = (uint8_t)((((f/100000)%10)<<4)   | ((f/10000)%10));
  out[2] = (uint8_t)((((f/1000)%10)<<4)     | ((f/100)%10));
  out[3] = (uint8_t)((((f/10)%10)<<4)       | (f%10));
}

bool YaesuRig::send(const uint8_t cmd[5]) {
  if (!_stream) return false;
  yaLog("TX", cmd, 5);
  _stream->write(cmd, 5);
  _stream->flush();
  delay(_postMs);              // Yaesu CAT dislikes back-to-back fast writes
  // Yaesu set commands are not acknowledged; drain any stray bytes.
  while (_stream->available()) _stream->read();
  return true;
}

bool YaesuRig::setFreq(uint8_t opcode, uint32_t hz) {
  uint8_t cmd[5];
  freqToBcd(hz, cmd);          // P1..P4 = BCD frequency
  cmd[4] = opcode;             // 0x11 = SAT RX, 0x21 = SAT TX
  return send(cmd);
}

bool YaesuRig::setMode(uint8_t opcode, RigMode m) {
  uint8_t cmd[5] = { modeCode(m), 0x00, 0x00, 0x00, opcode };  // 0x17 RX / 0x27 TX
  return send(cmd);
}

// Inverse of freqToBcd: 4 big-endian BCD bytes (8 digits) of 10 Hz units -> Hz.
uint32_t YaesuRig::bcdToFreq(const uint8_t in[4]) {
  uint32_t f = 0;
  for (int i = 0; i < 4; ++i) f = f * 100 + (in[i] >> 4) * 10 + (in[i] & 0x0F);
  return f * 10;
}

// Read the SAT-RX (downlink) frequency. FT-847 "read freq & mode" is opcode
// 0x03, patched to 0x13 for the SAT-RX VFO; the reply is 5 bytes: 4 big-endian
// BCD frequency bytes (10 Hz units) + 1 mode byte. Only firmware-updated FT-847s
// can read at all (early units stay silent -> we time out and return false). The
// FT-736R has no read path (canReadFreq is false for it).
bool YaesuRig::readSubFreq(uint32_t& hzOut) {
  if (!_stream || !RADIOS[_model].canReadFreq) return false;
  while (_stream->available()) _stream->read();                 // flush stale bytes
  const uint8_t cmd[5] = { 0x00, 0x00, 0x00, 0x00, 0x13 };      // read SAT-RX
  yaLog("TX", cmd, 5);
  _stream->write(cmd, 5);
  _stream->flush();
  uint8_t r[5]; size_t n = 0; uint32_t t0 = millis();
  while (millis() - t0 < 200 && n < 5) {
    if (_stream->available()) { r[n++] = (uint8_t)_stream->read(); t0 = millis(); }
    else delay(1);
  }
#if YAESU_DEBUG
  Serial.print("[CAT RX]");
  for (size_t i = 0; i < n; ++i) Serial.printf(" %02X", r[i]);
  if (n < 5) Serial.print("  (no/short reply -- early FT-847 firmware can't read)");
  Serial.println();
#endif
  if (n < 5) return false;                                       // timeout / no read
  uint32_t hz = bcdToFreq(r);                                    // r[4] = mode (unused)
  // Plausibility + wrong-VFO guard. In satellite mode the FT-847 sometimes
  // returns the SAT-TX (uplink) VFO instead of SAT-RX (Hamlib #1286, "freqs
  // alternate"). A real knob move within a transponder passband is well under
  // 1 MHz, while the uplink is a whole band away -- so reject reads that jump
  // > 1 MHz from the downlink we last commanded and hold the passband steady.
  if (hz < 1000000UL || hz > 1300000000UL) return false;
  if (_lastSubHz) {
    uint32_t d = hz > _lastSubHz ? hz - _lastSubHz : _lastSubHz - hz;
    if (d > 1000000UL) {
#if YAESU_DEBUG
      Serial.printf("[CAT] read %lu Hz rejected (>1 MHz from downlink %lu -- wrong VFO?)\n",
                    (unsigned long)hz, (unsigned long)_lastSubHz);
#endif
      return false;
    }
  }
  hzOut = hz;
#if YAESU_DEBUG
  Serial.printf("[CAT] SAT-RX (downlink) read: %lu Hz\n", (unsigned long)hz);
#endif
  return true;
}

// FT-847 CTCSS CAT codes, in the same order as the shared 39-tone list
// (see ctcssToneIndex). The satellite uplink is the SAT-TX VFO, so the tone
// frequency uses opcode 0x2B and the encoder is enabled with {0x4A..0x2A}
// (off = {0x8A..0x2A}). Codes + sequences verified against Hamlib ft847.c.
static const uint8_t FT847_CTCSS_CODE[39] = {
  0x3F,0x39,0x1F,0x3E,0x0F,0x3D,0x1E,0x3C,0x0E,0x3B,
  0x1D,0x3A,0x0D,0x1C,0x0C,0x1B,0x0B,0x1A,0x0A,0x19,
  0x09,0x18,0x08,0x17,0x07,0x16,0x06,0x15,0x05,0x14,
  0x04,0x13,0x03,0x12,0x02,0x11,0x01,0x10,0x00
};

bool YaesuRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  if (!on || toneHz <= 0) {
    const uint8_t off[5] = { 0x8A, 0x00, 0x00, 0x00, 0x2A };  // CTCSS/DCS off, sat tx
    return send(off);
  }
  int i = ctcssToneIndex(toneHz);
  if (i < 0) return false;
  const uint8_t freq[5] = { FT847_CTCSS_CODE[i], 0x00, 0x00, 0x00, 0x2B }; // freq, sat tx
  send(freq);
  const uint8_t enc[5]  = { 0x4A, 0x00, 0x00, 0x00, 0x2A };   // CTCSS enc on, sat tx
  return send(enc);
}


// =========================================================================
//  kenwood.cpp
// =========================================================================

// ===========================================================================
//  kenwood.cpp  -  Kenwood ASCII CAT backend
// ===========================================================================

// Set to 0 to silence the CAT trace on the serial monitor (115200 baud).
#define KW_DEBUG 1

#if KW_DEBUG
static void kwLog(const char* tag, const String& s) {
  String t = s; t.replace("\r", "");   // show the command without the CR
  Serial.printf("[CAT %s] %s\n", tag, t.c_str());
}
#else
static inline void kwLog(const char*, const String&) {}
#endif

void KenwoodRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  static HardwareSerial* hs = nullptr;
  if (!hs) hs = new HardwareSerial(uartNum);
  hs->begin(baud, SERIAL_8N1, rxPin, txPin);   // Kenwood CAT = 8N1
  _stream = hs;
}

char KenwoodRig::modeDigit(RigMode m) {
  switch (m) {                 // Kenwood mode codes
    case RM_LSB: return '1';
    case RM_USB: return '2';
    case RM_CW:  return '3';
    case RM_FM:  return '4';
    case RM_AM:  return '5';
    case RM_DATA:return '6';   // FSK
    default:     return '2';
  }
}

bool KenwoodRig::sendCmd(const String& cmd) {
  if (!_stream) return false;
  kwLog("TX", cmd);
  _stream->print(cmd);
  _stream->flush();
  return true;
}

bool KenwoodRig::setVfoFreq(const char* vfo, uint32_t hz) {
  char buf[20];
  snprintf(buf, sizeof(buf), "%s%011lu;", vfo, (unsigned long)hz);
  return sendCmd(buf);
}

bool KenwoodRig::setModeKw(RigMode m) {
  char buf[6];
  snprintf(buf, sizeof(buf), "MD%c;", modeDigit(m));
  return sendCmd(buf);
}

bool KenwoodRig::readSubFreq(uint32_t& hzOut) {
  if (!_stream || !RADIOS[_model].canReadFreq) return false;
  while (_stream->available()) _stream->read();    // clear stale bytes
  sendCmd("FA;");                                  // query VFO A (downlink)
  // Collect the reply up to the ';' terminator within a short window.
  String rx; uint32_t t0 = millis();
  while (millis() - t0 < 250) {
    while (_stream->available()) {
      char c = (char)_stream->read();
      rx += c; t0 = millis();
      if (c == ';') break;
    }
    if (rx.endsWith(";")) break;
    delay(1);
  }
  kwLog("RX", rx);
  int i = rx.indexOf("FA");
  if (i >= 0 && (int)rx.length() >= i + 13) {       // "FA" + 11 digits
    uint32_t hz = 0; bool ok = false;
    for (int k = i + 2; k < i + 13; ++k) {
      char c = rx[k];
      if (c < '0' || c > '9') { ok = false; break; }
      hz = hz * 10 + (c - '0'); ok = true;
    }
    if (ok) {
      hzOut = hz;
#if KW_DEBUG
      Serial.printf("[CAT] VFO-A (downlink) read: %lu Hz\n", (unsigned long)hz);
#endif
      return true;
    }
  }
#if KW_DEBUG
  Serial.println("[CAT] VFO-A read: no valid reply");
#endif
  return false;
}

// Transmit CTCSS (PL) tone for an FM uplink. TS-2000: TNnn sets the tone
// (encode) number -- 1-based into the same 39-tone list as ctcssToneIndex --
// and TO1/TO0 turns the TONE (encode) function on/off. The rig applies it to
// the current TX (uplink) band. Per Hamlib kenwood TN variant + the TS-2000
// CAT list; least bench-verified of the three families, so watch the trace.
bool KenwoodRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  if (!on || toneHz <= 0) return sendCmd("TO0;");      // TONE function off
  int i = ctcssToneIndex(toneHz);
  if (i < 0) return false;
  char buf[8];
  snprintf(buf, sizeof(buf), "TN%02d;", i + 1);        // tone number (1-based)
  sendCmd(buf);
  return sendCmd("TO1;");                              // TONE (encode) on
}


// =========================================================================
//  rotator.cpp
// =========================================================================

// ===========================================================================
//  rotator.cpp  -  GS-232 rotator over an SC16IS750/752 I2C->UART bridge
// ===========================================================================

// Set to 0 to silence the rotator trace on the serial monitor (115200 baud).
#define ROT_DEBUG 1

// --- SC16IS750/760 register map (16C550-compatible) ------------------------
//  The I2C sub-address byte is (reg << 3) | (channel << 1); channel 0 here.
static constexpr uint8_t SC_RHR   = 0x00;   // read  (FIFO out)
static constexpr uint8_t SC_THR   = 0x00;   // write (FIFO in)
static constexpr uint8_t SC_FCR   = 0x02;   // FIFO control (write)
static constexpr uint8_t SC_LCR   = 0x03;   // line control
static constexpr uint8_t SC_LSR   = 0x05;   // line status
static constexpr uint8_t SC_TXLVL = 0x08;   // TX FIFO free spaces
static constexpr uint8_t SC_RXLVL = 0x09;   // RX FIFO bytes available
static constexpr uint8_t SC_SPR   = 0x07;   // scratchpad (presence test)
static constexpr uint8_t SC_IOCTL = 0x0E;   // I/O control (bit3 = soft reset)
static constexpr uint8_t SC_DLL   = 0x00;   // divisor low  (when LCR[7]=1)
static constexpr uint8_t SC_DLH   = 0x01;   // divisor high (when LCR[7]=1)

void Gs232Rotator::wreg(uint8_t reg, uint8_t val) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));        // channel 0
  Wire1.write(val);
  Wire1.endTransmission();
}
uint8_t Gs232Rotator::rreg(uint8_t reg) {
  Wire1.beginTransmission(_addr);
  Wire1.write((uint8_t)(reg << 3));
  Wire1.endTransmission();
  Wire1.requestFrom((int)_addr, 1);
  return Wire1.available() ? (uint8_t)Wire1.read() : 0x00;
}

bool Gs232Rotator::bridgeInit() {
  wreg(SC_IOCTL, 0x08);                    // software reset (self-clearing)
  delay(5);
  uint32_t div = ROT_XTAL_HZ / (16UL * _baud);
  wreg(SC_LCR, 0x80);                      // enable divisor latch
  wreg(SC_DLL, (uint8_t)(div & 0xFF));
  wreg(SC_DLH, (uint8_t)((div >> 8) & 0xFF));
  wreg(SC_LCR, 0x03);                      // 8 data bits, no parity, 1 stop; latch off
  wreg(SC_FCR, 0x07);                      // enable FIFO + reset RX/TX FIFO
  // Presence test: the scratchpad register should hold whatever we write.
  wreg(SC_SPR, 0x5A);
  bool ok = (rreg(SC_SPR) == 0x5A);
#if ROT_DEBUG
  Serial.printf("[ROT] SC16IS750 @0x%02X %s (baud %lu, div %lu)\n",
                _addr, ok ? "ready" : "NOT FOUND", (unsigned long)_baud,
                (unsigned long)div);
#endif
  return ok;
}

void Gs232Rotator::begin() {
  Wire1.begin(ROT_I2C_SDA, ROT_I2C_SCL, ROT_I2C_HZ);
  _ok = bridgeInit();
}

void Gs232Rotator::putc_(char c) {
  uint32_t t0 = millis();
  while (!(rreg(SC_LSR) & 0x20)) {         // wait for THR empty
    if (millis() - t0 > 50) break;         // don't hang if the bridge stalls
  }
  wreg(SC_THR, (uint8_t)c);
}
void Gs232Rotator::puts_(const char* s) {
  while (*s) putc_(*s++);
}
int Gs232Rotator::getc_() {
  if (rreg(SC_RXLVL) == 0) return -1;
  return (int)rreg(SC_RHR);
}
void Gs232Rotator::flushIn() {
  while (rreg(SC_RXLVL)) rreg(SC_RHR);
}

bool Gs232Rotator::point(float az, float el) {
  if (!_ok) return false;
  if (az < 0)   az += 360.0f;
  if (az > 450) az = 450;
  if (el < 0)   el = 0;
  if (el > 180) el = 180;
  char cmd[16];
  snprintf(cmd, sizeof(cmd), "W%03d %03d\r", (int)lroundf(az), (int)lroundf(el));
#if ROT_DEBUG
  Serial.printf("[ROT TX] W%03d %03d\n", (int)lroundf(az), (int)lroundf(el));
#endif
  puts_(cmd);
  return true;
}

void Gs232Rotator::stop() {
  if (!_ok) return;
#if ROT_DEBUG
  Serial.println("[ROT TX] S (stop)");
#endif
  puts_("S\r");
}

// Read position. Accepts both GS-232A ("+0aaa+0eee") and GS-232B
// ("AZ=aaaEL=eee", with or without a separating space) reply formats.
bool Gs232Rotator::readPos(float& az, float& el) {
  if (!_ok) return false;
  flushIn();
  puts_("C2\r");
  String r;
  uint32_t t0 = millis();
  while (millis() - t0 < 500) {
    int c = getc_();
    if (c < 0) { delay(2); continue; }
    if (c == '\r' || c == '\n') { if (r.length()) break; else continue; }
    r += (char)c; t0 = millis();
    if (r.length() > 24) break;
  }
#if ROT_DEBUG
  Serial.printf("[ROT RX] %s\n", r.length() ? r.c_str() : "(no reply)");
#endif
  int ia = r.indexOf("AZ=");
  if (ia >= 0) {                           // GS-232B form
    int ie = r.indexOf("EL=");
    if (ie < 0) return false;
    az = (float)atoi(r.c_str() + ia + 3);
    el = (float)atoi(r.c_str() + ie + 3);
    return true;
  }
  int p1 = r.indexOf('+');                 // GS-232A form "+0aaa+0eee"
  if (p1 >= 0) {
    int p2 = r.indexOf('+', p1 + 1);
    if (p2 < 0) return false;
    az = (float)atoi(r.c_str() + p1 + 1);
    el = (float)atoi(r.c_str() + p2 + 1);
    return true;
  }
  return false;
}

Rotator* makeRotator(uint32_t baud) {
  return new Gs232Rotator(ROT_I2C_ADDR, baud);
}


// =========================================================================
//  satdb.cpp
// =========================================================================

// ===========================================================================
//  satdb.cpp
// ===========================================================================

bool SatDb::begin() {
  return Store::begin();
}

int SatDb::indexOfNorad(uint32_t norad) const {
  for (int i = 0; i < _n; ++i) if (_sats[i].norad == norad) return i;
  return -1;
}

static void rstrip(char* s) {
  int n = strlen(s);
  while (n > 0 && (s[n-1] == '\r' || s[n-1] == ' ' || s[n-1] == '\n')) s[--n] = 0;
}

// ---- EPOCH "YYYY-MM-DD HH:MM:SS.ffffff" -> Unix UTC seconds (fractional) ----
// Civil-from-days (Howard Hinnant) so it never depends on the process TZ.
double SatDb::gpEpochToUnix(const char* s) {
  if (!s) return 0.0;
  // OMM/GP JSON gives the epoch in ISO 8601 with a 'T' between date and time
  // (e.g. "2024-01-15T12:34:56.789012"); some sources use a space instead.
  // Normalize 'T'/'t' to a space so the time-of-day is always parsed -- missing
  // it silently zeroed HH:MM:SS and shifted pass times by up to ~12 hours.
  char b[40]; strncpy(b, s, sizeof(b) - 1); b[sizeof(b) - 1] = 0;
  for (char* p = b; *p; ++p) if (*p == 'T' || *p == 't') *p = ' ';
  int Y = 0, Mo = 1, D = 1, h = 0, mi = 0; double se = 0.0;
  if (sscanf(b, "%d-%d-%d %d:%d:%lf", &Y, &Mo, &D, &h, &mi, &se) < 3) return 0.0;
  int y = Y - (Mo <= 2);
  long era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = (unsigned)(y - era * 400);
  unsigned doy = (153 * (Mo + (Mo > 2 ? -3 : 9)) + 2) / 5 + D - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  long days = era * 146097 + (long)doe - 719468;
  return (double)days * 86400.0 + h * 3600 + mi * 60 + se;
}

// ---- Unix UTC seconds -> EPOCH string (for persisting manual entries) ------
static String unixToGpEpoch(double u) {
  time_t ip = (time_t)floor(u);
  double frac = u - (double)ip;
  struct tm tmv; gmtime_r(&ip, &tmv);
  char buf[40];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%09.6f",
           tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
           tmv.tm_hour, tmv.tm_min, (double)tmv.tm_sec + frac);
  return String(buf);
}

// ===========================================================================
//  GP/OMM parsing
// ===========================================================================
// AMSAT sends the element values as JSON *strings* (e.g. "101.9903"); a few
// fields (ELEMENT_SET_NO) are numbers. Read either form without relying on the
// JSON library's string->number coercion.
static double jnum(JsonObjectConst o, const char* key) {
  JsonVariantConst v = o[key];
  if (v.is<const char*>()) {
    const char* s = v.as<const char*>();
    return s ? strtod(s, nullptr) : 0.0;
  }
  return v.as<double>();
}

static bool parseGpObject(JsonObjectConst o, SatEntry& s) {
  const char* nm = o["AMSAT_NAME"] | (const char*)(o["OBJECT_NAME"] | "");
  if (!nm || !nm[0]) return false;
  strncpy(s.name, nm, sizeof(s.name) - 1); s.name[sizeof(s.name)-1] = 0; rstrip(s.name);
  const char* idd = o["OBJECT_ID"] | "";
  strncpy(s.intlDes, idd, sizeof(s.intlDes) - 1); s.intlDes[sizeof(s.intlDes)-1] = 0;

  s.norad       = (uint32_t)jnum(o, "NORAD_CAT_ID");
  const char* ep = o["EPOCH"] | "";
  s.epochUnix   = SatDb::gpEpochToUnix(ep);
  s.incl        = jnum(o, "INCLINATION");
  s.ecc         = jnum(o, "ECCENTRICITY");
  s.raan        = jnum(o, "RA_OF_ASC_NODE");
  s.argp        = jnum(o, "ARG_OF_PERICENTER");
  s.ma          = jnum(o, "MEAN_ANOMALY");
  s.meanMotion  = jnum(o, "MEAN_MOTION");
  s.bstar       = jnum(o, "BSTAR");
  s.ndot        = jnum(o, "MEAN_MOTION_DOT");
  s.nddot       = jnum(o, "MEAN_MOTION_DDOT");
  s.revAtEpoch  = (uint32_t)jnum(o, "REV_AT_EPOCH");
  s.elsetNum    = (uint16_t)jnum(o, "ELEMENT_SET_NO");
  s.txLoaded    = false;
  // A valid element set needs a non-zero epoch and mean motion.
  return s.norad != 0 && s.epochUnix > 0 && s.meanMotion > 0;
}

int SatDb::loadGpFromJson(const String& json) {
  _n = 0;
  return appendGpFromJson(json);
}

// Extract the raw value of "key" from a flat JSON object in [o, o+len). Copies
// the unquoted string value (or a bare token like 999.0 / null) into out. The
// trailing-quote check means "MEAN_MOTION" won't match "MEAN_MOTION_DOT". This
// uses no heap, unlike a per-object ArduinoJson document -- which matters
// because the GP array is parsed while a ~75 KB download buffer is resident and
// repeated document alloc/free fragments the no-PSRAM heap (it would quietly
// fail partway and drop the rest of the satellites).
static bool gpFindValue(const char* o, size_t len, const char* key,
                        char* out, size_t outsz) {
  out[0] = 0;
  size_t klen = strlen(key);
  const char* end = o + len;
  const char* hit = nullptr;
  for (const char* p = o; p + klen + 2 <= end; ++p) {
    if (*p == '"' && memcmp(p + 1, key, klen) == 0 && p[1 + klen] == '"') {
      hit = p + klen + 2; break;            // just past the key's closing quote
    }
  }
  if (!hit) return false;
  while (hit < end && (*hit == ' ' || *hit == '\t' || *hit == ':')) ++hit;
  if (hit >= end) return false;
  size_t n = 0;
  if (*hit == '"') {                        // quoted string value
    ++hit;
    while (hit < end && *hit != '"' && n + 1 < outsz) {
      if (*hit == '\\' && hit + 1 < end) ++hit;
      out[n++] = *hit++;
    }
  } else {                                  // bare token (number / null / true)
    while (hit < end && *hit != ',' && *hit != '}' &&
           *hit != ' ' && *hit != '\n' && *hit != '\r' && *hit != '\t' &&
           n + 1 < outsz)
      out[n++] = *hit++;
  }
  out[n] = 0;
  return true;
}

// Parse one flat OMM object (raw text, bounded) into a SatEntry. Same validity
// rule as parseGpObject but allocation-free, for the bulk GP-array path.
static bool parseGpObjectRaw(const char* o, size_t len, SatEntry& s) {
  char v[48];
  if (!gpFindValue(o, len, "AMSAT_NAME", v, sizeof(v)) || !v[0]) {
    if (!gpFindValue(o, len, "OBJECT_NAME", v, sizeof(v)) || !v[0]) return false;
  }
  strncpy(s.name, v, sizeof(s.name) - 1); s.name[sizeof(s.name)-1] = 0; rstrip(s.name);
  gpFindValue(o, len, "OBJECT_ID", v, sizeof(v));
  strncpy(s.intlDes, v, sizeof(s.intlDes) - 1); s.intlDes[sizeof(s.intlDes)-1] = 0;

  gpFindValue(o, len, "NORAD_CAT_ID",      v, sizeof(v)); s.norad      = (uint32_t)strtoul(v, nullptr, 10);
  gpFindValue(o, len, "EPOCH",             v, sizeof(v)); s.epochUnix  = SatDb::gpEpochToUnix(v);
  gpFindValue(o, len, "INCLINATION",       v, sizeof(v)); s.incl       = strtod(v, nullptr);
  gpFindValue(o, len, "ECCENTRICITY",      v, sizeof(v)); s.ecc        = strtod(v, nullptr);
  gpFindValue(o, len, "RA_OF_ASC_NODE",    v, sizeof(v)); s.raan       = strtod(v, nullptr);
  gpFindValue(o, len, "ARG_OF_PERICENTER", v, sizeof(v)); s.argp       = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_ANOMALY",      v, sizeof(v)); s.ma         = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION",       v, sizeof(v)); s.meanMotion = strtod(v, nullptr);
  gpFindValue(o, len, "BSTAR",             v, sizeof(v)); s.bstar      = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION_DOT",   v, sizeof(v)); s.ndot       = strtod(v, nullptr);
  gpFindValue(o, len, "MEAN_MOTION_DDOT",  v, sizeof(v)); s.nddot      = strtod(v, nullptr);
  gpFindValue(o, len, "REV_AT_EPOCH",      v, sizeof(v)); s.revAtEpoch = (uint32_t)strtoul(v, nullptr, 10);
  gpFindValue(o, len, "ELEMENT_SET_NO",    v, sizeof(v)); s.elsetNum   = (uint16_t)strtoul(v, nullptr, 10);
  s.txLoaded = false;
  return s.norad != 0 && s.epochUnix > 0 && s.meanMotion > 0;
}

int SatDb::appendGpFromJson(const String& json) {
  // Parse one OMM object at a time, allocation-free (see parseGpObjectRaw).
  // Walking object-by-object also tolerates a truncated download tail.
  const char* arr = strchr(json.c_str(), '[');
  if (!arr) return _n;
  const char* s = arr + 1;
  while (*s && _n < MAX_SATS) {
    while (*s && *s != '{' && *s != ']') ++s;     // skip whitespace/commas
    if (*s != '{') break;                          // ']' or end of input
    const char* objStart = s;
    int depth = 0; bool inStr = false, esc = false;
    const char* q = s;
    for (; *q; ++q) {                              // find the matching '}'
      char c = *q;
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"')  inStr = false;
      } else if (c == '"') inStr = true;
      else if (c == '{')   ++depth;
      else if (c == '}') { if (--depth == 0) { ++q; break; } }
    }
    if (depth != 0) break;                         // truncated / malformed tail
    size_t len = (size_t)(q - objStart);

    SatEntry tmp;                                  // zero-allocation field parse
    if (parseGpObjectRaw(objStart, len, tmp)) {
      int idx = indexOfNorad(tmp.norad);           // replace if it already exists
      if (idx < 0) { idx = _n; _n++; }
      _sats[idx] = tmp;
    }
    s = q;                                         // continue after this object
  }
  return _n;
}

bool SatDb::addGp(const SatEntry& s) {
  if (s.norad == 0 || s.meanMotion <= 0) return false;
  int idx = indexOfNorad(s.norad);
  if (idx < 0) { if (_n >= MAX_SATS) return false; idx = _n++; }
  _sats[idx] = s;

  // Persist as one compact OMM object per line (NDJSON), kept separate so an
  // AMSAT refresh that rewrites FILE_GP doesn't wipe hand-entered satellites.
  JsonDocument d;
  d["AMSAT_NAME"]        = s.name;
  d["OBJECT_ID"]         = s.intlDes;
  d["NORAD_CAT_ID"]      = s.norad;
  d["EPOCH"]             = unixToGpEpoch(s.epochUnix);
  d["INCLINATION"]       = s.incl;
  d["ECCENTRICITY"]      = s.ecc;
  d["RA_OF_ASC_NODE"]    = s.raan;
  d["ARG_OF_PERICENTER"] = s.argp;
  d["MEAN_ANOMALY"]      = s.ma;
  d["MEAN_MOTION"]       = s.meanMotion;
  d["BSTAR"]             = s.bstar;
  d["MEAN_MOTION_DOT"]   = s.ndot;
  d["MEAN_MOTION_DDOT"]  = s.nddot;
  d["REV_AT_EPOCH"]      = s.revAtEpoch;
  d["ELEMENT_SET_NO"]    = s.elsetNum;
  File f = Store::fs().open(FILE_MGP, "a");
  if (f) { serializeJson(d, f); f.print("\n"); f.close(); }
  return true;
}

bool SatDb::loadManualGpFile() {
  File f = Store::fs().open(FILE_MGP, "r");
  if (!f) return false;
  bool any = false;
  while (f.available() && _n < MAX_SATS) {
    String line = f.readStringUntil('\n');
    line.trim();
    if (line.length() == 0) continue;
    JsonDocument d;
    if (deserializeJson(d, line)) continue;
    SatEntry tmp;
    if (!parseGpObject(d.as<JsonObjectConst>(), tmp)) continue;
    int idx = indexOfNorad(tmp.norad);
    if (idx < 0) { idx = _n; _n++; }
    _sats[idx] = tmp; any = true;
  }
  f.close();
  return any;
}

bool SatDb::saveGpJson(const String& json) {
  File f = Store::fs().open(FILE_GP, "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

bool SatDb::loadGpFromFs() {
  return loadGpFromFile(FILE_GP) > 0;
}

// Stream-parse a GP/OMM JSON array from a file, one object at a time, using a
// small fixed buffer. Never loads the whole file into RAM, so it works for the
// full ~75 KB amateur list on the no-PSRAM heap (where a single contiguous
// String would fail). Object state carries across read-buffer boundaries.
int SatDb::loadGpFromFile(const char* path) {
  _n = 0;
  File f = Store::fs().open(path, "r");
  if (!f) return 0;

  static const size_t OBJ_MAX = 1200;     // largest OMM object is ~800 bytes
  static char obj[OBJ_MAX];               // static: keep it off the stack
  uint8_t rd[256];
  size_t oi = 0;
  int  depth = 0;
  bool inStr = false, esc = false, collecting = false, started = false;

  int avail;
  while ((avail = f.read(rd, sizeof(rd))) > 0 && _n < MAX_SATS) {
    for (int i = 0; i < avail && _n < MAX_SATS; ++i) {
      char c = (char)rd[i];
      if (!started) { if (c == '[') started = true; continue; }
      if (!collecting) {                  // between objects: wait for '{'
        if (c == '{') { collecting = true; depth = 1; inStr = false; esc = false;
                        oi = 0; obj[oi++] = c; }
        continue;
      }
      bool overflow = (oi >= OBJ_MAX - 1);
      if (!overflow) obj[oi++] = c;
      if (inStr) {
        if (esc) esc = false;
        else if (c == '\\') esc = true;
        else if (c == '"')  inStr = false;
      } else if (c == '"') inStr = true;
      else if (c == '{')   ++depth;
      else if (c == '}') {
        if (--depth == 0) {               // object complete
          collecting = false;
          if (!overflow) {                // only parse if captured whole
            obj[oi] = 0;
            SatEntry tmp;
            if (parseGpObjectRaw(obj, oi, tmp)) {
              int idx = indexOfNorad(tmp.norad);
              if (idx < 0) { idx = _n; _n++; }
              _sats[idx] = tmp;
            }
          }
          oi = 0;
        }
      }
    }
  }
  f.close();
  return _n;
}

// ===========================================================================
//  GP elements -> TLE line-pair (only to initialise the SGP4 propagator)
// ===========================================================================
//  Field layout follows the canonical NORAD two-line spec. This is host-tested
//  by round-tripping the elements back through spec column offsets and by
//  checksum verification; SGP4 results are identical to the original element
//  set because TLE is just an alternate encoding of the same mean elements.

// Assumed-decimal exponential field (8 chars), e.g. " 71831-4", " 00000-0".
static void encExp(double v, char out[10]) {
  char s = (v < 0) ? '-' : ' ';
  double a = fabs(v);
  int e = 0;
  if (a != 0.0) {
    while (a >= 1.0) { a /= 10.0; e++; }
    while (a < 0.1)  { a *= 10.0; e--; }
  }
  long mant = llround(a * 1e5);
  if (mant >= 100000) { mant = 10000; e++; }
  if (e > 9)  e = 9;  if (e < -9) e = -9;
  snprintf(out, 10, "%c%05ld%c%01d", s, mant, (e < 0 ? '-' : '+'), (int)labs(e));
}

// First-derivative field (10 chars): sign + ".XXXXXXXX".
static void encNdot(double v, char out[12]) {
  char s = (v < 0) ? '-' : ' ';
  long m = llround(fabs(v) * 1e8);
  if (m > 99999999L) m = 99999999L;
  snprintf(out, 12, "%c.%08ld", s, m);
}

// Catalog number: 5 digits, or Alpha-5 for 100000-339999 (TLE's stopgap).
static void encCatalog(uint32_t n, char out[6]) {
  if (n <= 99999u) { snprintf(out, 6, "%05lu", (unsigned long)n); return; }
  static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZ";   // skips I and O
  int hi = (int)(n / 10000), lo = (int)(n % 10000);
  if (hi >= 10 && hi <= 33) snprintf(out, 6, "%c%04d", A[hi - 10], lo);
  else snprintf(out, 6, "%05lu", (unsigned long)(n % 100000u));
}

static int tleChecksum(const char* line) {
  int s = 0;
  for (int i = 0; i < 68 && line[i]; i++) {
    char c = line[i];
    if (c >= '0' && c <= '9') s += c - '0';
    else if (c == '-')        s += 1;
  }
  return s % 10;
}

static void putAt(char* line, int col1, const char* s) {   // col1 is 1-indexed
  int i = col1 - 1;
  for (int k = 0; s[k]; k++) line[i + k] = s[k];
}

bool SatDb::gpToTle(const SatEntry& s, char l1[72], char l2[72]) {
  if (s.meanMotion <= 0 || s.epochUnix <= 0) return false;
  memset(l1, ' ', 69); l1[69] = 0;
  memset(l2, ' ', 69); l2[69] = 0;

  char cat[6]; encCatalog(s.norad, cat);

  // International designator OBJECT_ID "YYYY-NNNP[PP]" -> "YYNNNPPP".
  char intl[9] = "        ";
  if (s.intlDes[0] && strlen(s.intlDes) >= 8 && s.intlDes[4] == '-') {
    intl[0] = s.intlDes[2]; intl[1] = s.intlDes[3];
    intl[2] = s.intlDes[5]; intl[3] = s.intlDes[6]; intl[4] = s.intlDes[7];
    int k = 5;
    for (size_t j = 8; j < strlen(s.intlDes) && k < 8; ++j) intl[k++] = s.intlDes[j];
  }

  // Epoch -> YYDDD.DDDDDDDD.
  time_t ip = (time_t)floor(s.epochUnix);
  double frac = s.epochUnix - (double)ip;
  struct tm tmv; gmtime_r(&ip, &tmv);
  double day = (tmv.tm_yday + 1)
             + (tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec + frac) / 86400.0;
  char epoch[16];
  snprintf(epoch, sizeof(epoch), "%02d%012.8f", tmv.tm_year % 100, day);

  char nd[12];  encNdot(s.ndot, nd);
  char ndd[10]; encExp(s.nddot, ndd);
  char bs[10];  encExp(s.bstar, bs);

  // --- line 1 ---
  l1[0] = '1'; putAt(l1, 3, cat); l1[7] = 'U';
  putAt(l1, 10, intl);
  putAt(l1, 19, epoch);
  putAt(l1, 34, nd);
  putAt(l1, 45, ndd);
  putAt(l1, 54, bs);
  l1[62] = '0';                                   // ephemeris type
  char es[6]; snprintf(es, sizeof(es), "%4u", (unsigned)(s.elsetNum % 10000));
  putAt(l1, 65, es);
  l1[68] = '0' + tleChecksum(l1);

  // --- line 2 ---
  char buf[16];
  l2[0] = '2'; putAt(l2, 3, cat);
  snprintf(buf, sizeof(buf), "%8.4f", s.incl); putAt(l2, 9,  buf);
  snprintf(buf, sizeof(buf), "%8.4f", s.raan); putAt(l2, 18, buf);
  long e7 = llround(s.ecc * 1e7); if (e7 < 0) e7 = 0; if (e7 > 9999999L) e7 = 9999999L;
  snprintf(buf, sizeof(buf), "%07ld", e7);     putAt(l2, 27, buf);
  snprintf(buf, sizeof(buf), "%8.4f", s.argp); putAt(l2, 35, buf);
  snprintf(buf, sizeof(buf), "%8.4f", s.ma);   putAt(l2, 44, buf);
  snprintf(buf, sizeof(buf), "%11.8f", s.meanMotion); putAt(l2, 53, buf);
  snprintf(buf, sizeof(buf), "%5lu", (unsigned long)(s.revAtEpoch % 100000u));
  putAt(l2, 64, buf);
  l2[68] = '0' + tleChecksum(l2);
  return true;
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
  File f = Store::fs().open(txPath(norad), "w");
  if (!f) return false;
  f.print(json); f.close();
  return true;
}

int SatDb::loadTxCache(uint32_t norad, Transponder* out, int maxN) {
  File f = Store::fs().open(txPath(norad), "r");
  if (!f) return 0;
  String j = f.readString(); f.close();
  return parseTransmittersJson(j, out, maxN);
}

// Required FM-uplink CTCSS (PL) tones for the common FM birds. SatNOGS has no
// structured tone field, so these are built in by NORAD id. Operating tones
// only (e.g. SO-50's 74.4 Hz arming burst is a separate manual action; its
// working uplink tone is 67.0 Hz). Extend as new FM satellites appear.
float SatDb::knownCtcssHz(uint32_t norad) {
  switch (norad) {
    case 25544: return 67.0f;   // ISS (FM cross-band repeater)
    case 27607: return 67.0f;   // SO-50  (SaudiSat-1C)
    case 43017: return 67.0f;   // AO-91  (RadFxSat / Fox-1B)
    case 43137: return 67.0f;   // AO-92  (Fox-1D)
    case 43678: return 141.3f;  // PO-101 (Diwata-2)
    default:    return 0.0f;
  }
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
  // Fix is "held" only while the last position is fresh; if the receiver goes
  // quiet or drops lock, age() climbs past the timeout and we report fix lost.
  static const uint32_t GPS_FIX_TIMEOUT_MS = 5000;
  _hasFix = gps.location.isValid() && gps.location.age() < GPS_FIX_TIMEOUT_MS;
  return updated;
}

void Location::setManual(double lat, double lon, double altM) {
  _obs.lat = lat; _obs.lon = lon; _obs.altM = altM;
  _obs.valid = true; _obs.fromGps = false;
}

// Maidenhead grid -> lat/lon (centre of the square). Accepts 4 or 6 chars.
bool Location::gridToLatLon(const String& gridIn, double& latOut, double& lonOut) {
  String g = gridIn; g.trim(); g.toUpperCase();
  if (g.length() < 4) return false;
  if (g[0] < 'A' || g[0] > 'R' || g[1] < 'A' || g[1] > 'R') return false;
  if (g[2] < '0' || g[2] > '9' || g[3] < '0' || g[3] > '9') return false;
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
  latOut = lat; lonOut = lon;
  return true;
}
bool Location::setFromGrid(const String& gridIn) {
  double lat, lon;
  if (!gridToLatLon(gridIn, lat, lon)) return false;
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

int Net::scanWifi(WifiAp* out, int maxAps) {
  if (!out || maxAps <= 0) return 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();                        // drop any association for a clean scan
  delay(50);
  int n = WiFi.scanNetworks();              // blocking, a few seconds
  if (n < 0) { WiFi.scanDelete(); return -1; }
  int count = 0;
  for (int i = 0; i < n; ++i) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;          // skip hidden / blank SSIDs
    int8_t r = (int8_t)WiFi.RSSI(i);
    bool   e = (WiFi.encryptionType(i) != WIFI_AUTH_OPEN);
    int found = -1;                         // de-dup by SSID, keep the strongest
    for (int j = 0; j < count; ++j) if (s == out[j].ssid) { found = j; break; }
    if (found >= 0) {
      if (r > out[found].rssi) { out[found].rssi = r; out[found].enc = e; }
      continue;
    }
    if (count >= maxAps) continue;
    strncpy(out[count].ssid, s.c_str(), sizeof(out[count].ssid) - 1);
    out[count].ssid[sizeof(out[count].ssid) - 1] = 0;
    out[count].rssi = r;
    out[count].enc  = e;
    count++;
  }
  WiFi.scanDelete();                        // free the scan result buffer
  for (int i = 1; i < count; ++i) {         // insertion sort, strongest first
    WifiAp key = out[i]; int j = i - 1;
    while (j >= 0 && out[j].rssi < key.rssi) { out[j + 1] = out[j]; --j; }
    out[j + 1] = key;
  }
  return count;
}

void Net::syncTimeNtp() {
  // UTC (no offset, no DST). Pool servers.
  configTime(0, 0, "pool.ntp.org", "time.nist.gov");
  struct tm ti;
  for (int i = 0; i < 40 && !getLocalTime(&ti, 250); ++i) { /* wait */ }
}

bool Net::httpsGet(const String& url, String& out, size_t maxBytes) {
  lastCode = 0; lastErr = "";
  if (!connected()) { lastErr = "no WiFi"; return false; }

  Serial.printf("[net] GET %s\n", url.c_str());
  Serial.printf("[net] heap before TLS: %u, IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(), WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI());

  WiFiClientSecure client;
  // Certificate validation disabled for simplicity (public GP data). For a
  // security-sensitive deployment, pin the CA root instead of setInsecure().
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);  // AMSAT may 301/302
  http.useHTTP10(true);   // avoid chunked-encoding edge cases for static files

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
  if (code != HTTP_CODE_OK) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] GET failed: code=%d (%s)\n", code, lastErr.c_str());
    http.end();
    return false;
  }

  int len = http.getSize();          // -1 if server didn't send Content-Length
  out = "";
  if (len > 0) out.reserve(min((size_t)len + 16, maxBytes));  // avoid realloc churn

  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t total = 0;
  uint32_t lastRx = millis();
  // Read until the declared length arrives, or (no Content-Length) until the
  // stream has been idle long enough to be sure the transfer is done. We must
  // NOT stop the instant connected()/available() momentarily go false: with TLS
  // the socket can report no data between records mid-stream, which previously
  // truncated large bodies (the GP file) to whatever had arrived so far -- the
  // amount varying run to run with network timing.
  while (total < maxBytes) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (r <= 0) break;
      out.concat((const char*)buf, r);
      total += r;
      lastRx = millis();
      if (len > 0 && total >= (size_t)len) break;     // whole body received
    } else {
      if (len > 0 && total >= (size_t)len) break;
      // Peer closed with nothing buffered: allow a short grace for any final
      // TLS-buffered bytes, then finish. Otherwise wait, up to a hard timeout.
      if (!http.connected() && !stream->available() && millis() - lastRx > 500)
        break;
      if (millis() - lastRx > 10000) break;           // idle/stall timeout
      delay(5);
    }
  }
  http.end();

  Serial.printf("[net] received %u bytes (declared %d), heap now %u\n",
                (unsigned)total, len, (unsigned)ESP.getFreeHeap());
  if (out.length() == 0) { lastErr = "empty body"; return false; }
  return true;
}

bool Net::fetchGp(const String& url, String& out) {
  // GP/OMM JSON can be a few hundred KB for the full amateur list; cap higher
  // than the old TLE text. MAX_SATS still bounds what we actually store.
  return httpsGet(url, out, 400000);
}

bool Net::fetchGpToFile(const String& url, const char* path) {
  return httpsGetToFile(url, path, 400000, nullptr);
}

bool Net::httpsGetToFile(const String& url, const char* path,
                         size_t maxBytes, size_t* written) {
  lastCode = 0; lastErr = "";
  if (written) *written = 0;
  if (!connected()) { lastErr = "no WiFi"; return false; }

  Serial.printf("[net] GET %s -> %s\n", url.c_str(), path);
  Serial.printf("[net] heap before TLS: %u, IP %s, RSSI %d\n",
                (unsigned)ESP.getFreeHeap(), WiFi.localIP().toString().c_str(),
                (int)WiFi.RSSI());

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(15000);

  HTTPClient http;
  http.setUserAgent("CardSat-Cardputer/1.0");
  http.setConnectTimeout(15000);
  http.setTimeout(15000);
  http.setFollowRedirects(HTTPC_FORCE_FOLLOW_REDIRECTS);
  http.useHTTP10(true);

  if (!http.begin(client, url)) { lastErr = "begin failed"; return false; }
  http.addHeader("Accept", "*/*");

  int code = http.GET();
  lastCode = code;
  if (code != HTTP_CODE_OK) {
    lastErr = (code > 0) ? ("HTTP " + String(code))
                         : HTTPClient::errorToString(code);
    Serial.printf("[net] GET failed: code=%d (%s)\n", code, lastErr.c_str());
    http.end();
    return false;
  }

  File f = Store::fs().open(path, "w");
  if (!f) {
    lastErr = Store::ready() ? "fs open failed" : "no filesystem (SPIFFS/SD)";
    http.end(); return false;
  }

  int len = http.getSize();          // -1 if server didn't send Content-Length
  WiFiClient* stream = http.getStreamPtr();
  uint8_t buf[512];
  size_t total = 0;
  uint32_t lastRx = millis();
  bool writeErr = false;
  // Stream straight to flash: each chunk is written and freed, so no large
  // contiguous RAM buffer is ever needed (which is what truncated the String
  // version). Terminate on declared length / idle timeout, not on a transient
  // connected()/available() lull.
  while (total < maxBytes) {
    size_t avail = stream->available();
    if (avail) {
      int r = stream->readBytes(buf, min(avail, sizeof(buf)));
      if (r <= 0) break;
      if (f.write(buf, r) != (size_t)r) { writeErr = true; break; }  // flash full?
      total += r;
      lastRx = millis();
      if (len > 0 && total >= (size_t)len) break;
    } else {
      if (len > 0 && total >= (size_t)len) break;
      if (!http.connected() && !stream->available() && millis() - lastRx > 500)
        break;
      if (millis() - lastRx > 10000) break;
      delay(5);
    }
  }
  f.close();
  http.end();
  if (written) *written = total;

  Serial.printf("[net] streamed %u bytes to %s (declared %d), heap now %u\n",
                (unsigned)total, path, len, (unsigned)ESP.getFreeHeap());
  if (writeErr)    { lastErr = "fs write failed"; return false; }
  if (total == 0)  { lastErr = "empty body"; return false; }
  return true;
}

bool Net::fetchSatnogsTransmitters(uint32_t norad, String& out) {
  String url = String(SATNOGS_TX_URL) + String((unsigned long)norad);
  // Busy birds return large transmitter lists (the ISS has ~49); allow ample room
  // so the JSON body isn't truncated mid-object, which would fail the parse and
  // leave the satellite with no transponders.
  return httpsGet(url, out, 200000);
}


// =========================================================================
//  predict.cpp
// =========================================================================

// ===========================================================================
//  predict.cpp
// ===========================================================================

static const double DEG = M_PI / 180.0;
static const double RE_KM = 6378.135;          // WGS72 equatorial radius (matches the TLE element set)

// Geocentric unit vector to the Sun in equatorial inertial coords (ECI).
// Low-precision almanac, good to ~0.01 deg -- ample for shadow / az-el.
static void sunEciUnit(double jd, double& x, double& y, double& z) {
  double n   = jd - 2451545.0;
  double L   = fmod(280.460 + 0.9856474 * n, 360.0);   // mean longitude
  double g   = fmod(357.528 + 0.9856003 * n, 360.0) * DEG;
  double lam = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG;  // ecliptic lon
  double eps = (23.439 - 0.0000004 * n) * DEG;          // obliquity
  x = cos(lam);
  y = cos(eps) * sin(lam);
  z = sin(eps) * sin(lam);
}

// Greenwich mean sidereal time (radians) for a given Julian date.
static double gmstRad(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  double g = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
             + 0.000387933 * T * T - T * T * T / 38710000.0;
  g = fmod(g, 360.0); if (g < 0) g += 360.0;
  return g * DEG;
}

void Predictor::setSite(const Observer& o) {
  _o = o;
  _sat.site(o.lat, o.lon, o.altM);
}

bool Predictor::setSat(SatEntry& s) {
  strncpy(_name, s.name, sizeof(_name)-1); _name[sizeof(_name)-1]=0;
  // The SGP4 library ingests elements through twoline2rv, so render the stored
  // GP mean elements into a TLE line-pair (SGP4 is encoding-agnostic).
  if (!SatDb::gpToTle(s, _l1, _l2)) { _haveSat = false; return false; }
  _sat.init(_name, _l1, _l2);
  _haveSat = (_sat.satrec.error == 0);
  _epochUnix = s.epochUnix;          // for fractional-time range rate (rangeRateAt)
  return _haveSat;
}

// Range rate from the SGP4 velocity vector at a fractional instant -- the
// method Gpredict uses (sgp4sdp4 converts ECI position+velocity straight to
// observer-centred range rate). Far cleaner near TCA than differencing slant
// range, and evaluated at the exact time rather than the nearest whole second.
// This Hopperpop build uses the older Vallado propagator signature
// sgp4(whichconst, satrec, tsince_min, r[3], v[3]); pass WGS72 (the constant set
// the elements are fit to) -> TEME position (km) and velocity (km/s).
double Predictor::rangeRateAt(double unixSec) {
  if (!_haveSat) return 0.0;

  // Propagate to the exact instant. tsince is MINUTES since the element epoch;
  // measure it from the stored Unix epoch so we don't depend on satrec's epoch
  // field layout.
  double tsince = (unixSec - _epochUnix) / 60.0;
  double r[3] = {0, 0, 0}, v[3] = {0, 0, 0};
  sgp4(wgs72, _sat.satrec, tsince, r, v);       // TEME position/velocity (WGS72)

  // Observer in the same TEME frame: geodetic -> ECEF -> rotate by GMST.
  double jd  = unixSec / 86400.0 + 2440587.5;
  double th  = gmstRad(jd);
  double ct = cos(th), st = sin(th);
  double phi = _o.lat * DEG, lam = _o.lon * DEG, hKm = _o.altM / 1000.0;
  double e2  = 6.694318e-3;                     // WGS72 first eccentricity^2 (f = 1/298.26)
  double sphi = sin(phi), cphi = cos(phi);
  double N   = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double xe = (N + hKm) * cphi * cos(lam);      // ECEF
  double ye = (N + hKm) * cphi * sin(lam);
  double ze = (N * (1.0 - e2) + hKm) * sphi;
  double ox = xe * ct - ye * st;                // ECEF -> TEME  (Rz(+theta))
  double oy = xe * st + ye * ct;
  double oz = ze;

  // Observer velocity in TEME from Earth rotation: omega_earth x r_obs.
  const double we = 7.2921150e-5;               // rad/s (sidereal)
  double ovx = -we * oy, ovy = we * ox, ovz = 0.0;

  // Range rate = (r_rel . v_rel) / |r_rel|, +ve when the range is increasing.
  double rx = r[0] - ox,  ry = r[1] - oy,  rz = r[2] - oz;
  double vx = v[0] - ovx, vy = v[1] - ovy, vz = v[2] - ovz;
  double rmag = sqrt(rx * rx + ry * ry + rz * rz);
  if (rmag <= 0.0) return 0.0;
  return (rx * vx + ry * vy + rz * vz) / rmag;
}

LiveLook Predictor::look(time_t t) {
  LiveLook L;
  if (!_haveSat) return L;

  // Current sample (az/el/range/sub-point) from the propagator.
  _sat.findsat((unsigned long)t);
  L.az       = _sat.satAz;
  L.el       = _sat.satEl;
  L.rangeKm  = _sat.satDist;
  L.subLat   = _sat.satLat;
  L.subLon   = _sat.satLon;
  L.satAltKm = _sat.satAlt;
  L.visible  = (_sat.satEl > 0.0);

  // Range rate from the SGP4 velocity vector (exact; no finite-difference
  // truncation), at this same instant -- see rangeRateAt().
  L.rangeRate = rangeRateAt((double)t);

  // ---- Sun geometry: satellite illumination + Sun look-angle --------------
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);    // Sun unit vector (ECI)
  double th = gmstRad(jd);
  double ct = cos(th), st = sin(th);

  // Satellite ECEF position from its geodetic sub-point (lat/lon/alt).
  double phi = L.subLat * DEG, lam = L.subLon * DEG, h = L.satAltKm;
  double e2 = 6.694318e-3;                           // WGS72 first ecc^2 (f = 1/298.26)
  double sphi = sin(phi), cphi = cos(phi);
  double Nlat = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double rx = (Nlat + h) * cphi * cos(lam);
  double ry = (Nlat + h) * cphi * sin(lam);
  double rz = (Nlat * (1.0 - e2) + h) * sphi;

  // Sun unit vector rotated ECI -> ECEF (Rz(-theta)).
  double ux =  sx * ct + sy * st;
  double uy = -sx * st + sy * ct;
  double uz =  sz;

  // Cylindrical-shadow test: in eclipse if on the anti-solar side and the
  // perpendicular distance to the Earth-Sun axis is less than Earth's radius.
  double proj = rx * ux + ry * uy + rz * uz;         // km along Sun direction
  double rmag2 = rx * rx + ry * ry + rz * rz;
  double perp  = sqrt(fmax(0.0, rmag2 - proj * proj));
  L.sunlit = !(proj < 0.0 && perp < RE_KM);

  // Sun az/el for the observer (topocentric ENU; solar parallax negligible).
  double olat = _o.lat * DEG;
  double ost = sin(th + _o.lon * DEG), oct = cos(th + _o.lon * DEG);
  double slat = sin(olat), clat = cos(olat);
  // East, North, Up (ECI) dotted with Sun unit vector:
  double eComp = (-ost) * sx + (oct) * sy;
  double nComp = (-slat * oct) * sx + (-slat * ost) * sy + (clat) * sz;
  double uComp = ( clat * oct) * sx + ( clat * ost) * sy + (slat) * sz;
  L.sunEl = atan2(uComp, sqrt(eComp * eComp + nComp * nComp)) / DEG;
  double az = atan2(eComp, nComp) / DEG; if (az < 0) az += 360.0;
  L.sunAz = az;
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

bool Predictor::azelAt(time_t t, double& az, double& el) {
  if (!_haveSat) { az = el = 0; return false; }
  _sat.findsat((unsigned long)t);
  az = _sat.satAz;
  el = _sat.satEl;
  return true;
}

double Predictor::elevationFromSubpoint(double obsLatDeg, double obsLonDeg,
                                        double obsAltM,
                                        double satLatDeg, double satLonDeg,
                                        double satAltKm) {
  const double D = M_PI / 180.0, RE = 6378.135, e2 = 6.694318e-3;
  auto ecef = [&](double latD, double lonD, double hKm,
                  double& x, double& y, double& z) {
    double phi = latD * D, lam = lonD * D, s = sin(phi), c = cos(phi);
    double N = RE / sqrt(1.0 - e2 * s * s);
    x = (N + hKm) * c * cos(lam);
    y = (N + hKm) * c * sin(lam);
    z = (N * (1.0 - e2) + hKm) * s;
  };
  double ox, oy, oz, sx, sy, sz;
  ecef(obsLatDeg, obsLonDeg, obsAltM / 1000.0, ox, oy, oz);
  ecef(satLatDeg, satLonDeg, satAltKm,          sx, sy, sz);
  double dx = sx - ox, dy = sy - oy, dz = sz - oz;
  double dn = sqrt(dx * dx + dy * dy + dz * dz);
  if (dn <= 0) return -90.0;
  double phi = obsLatDeg * D, lam = obsLonDeg * D;          // ellipsoidal up
  double ux = cos(phi) * cos(lam), uy = cos(phi) * sin(lam), uz = sin(phi);
  return asin((dx * ux + dy * uy + dz * uz) / dn) / D;
}

int Predictor::mutualWindows(time_t from, const Observer& dx, float minEl,
                             MutualWindow* out, int maxN) {
  if (!_haveSat) return 0;
  // My passes down to the horizon mask bound the search (a mutual window can
  // only occur while I can see the bird), so scan inside each of my passes.
  PassPredict mine[MUTUAL_PASS_SCAN];
  int np = predictPasses(from, minEl, mine, MUTUAL_PASS_SCAN);

  const time_t dt = 10;                 // scan step (s)
  int n = 0;
  for (int p = 0; p < np && n < maxN; ++p) {
    bool inWin = false;
    MutualWindow w;
    for (time_t t = mine[p].aos; t <= mine[p].los; t += dt) {
      _sat.findsat((unsigned long)t);
      double myEl = _sat.satEl;
      double dxEl = elevationFromSubpoint(dx.lat, dx.lon, dx.altM,
                                          _sat.satLat, _sat.satLon, _sat.satAlt);
      bool both = (myEl >= minEl && dxEl >= minEl);
      if (both) {
        if (!inWin) { inWin = true; w = MutualWindow();
                      w.start = t; w.end = t;
                      w.myMaxEl = (float)myEl; w.dxMaxEl = (float)dxEl; }
        else { w.end = t;
               if (myEl > w.myMaxEl) w.myMaxEl = (float)myEl;
               if (dxEl > w.dxMaxEl) w.dxMaxEl = (float)dxEl; }
      } else if (inWin) {
        out[n++] = w; inWin = false;
        if (n >= maxN) return n;
      }
    }
    if (inWin && n < maxN) out[n++] = w;   // window open at pass end
  }
  return n;
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
  File f = Store::fs().open(FILE_CFG, "r");
  if (!f) return false;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return false; }
  f.close();

  strncpy(ssid, d["ssid"] | "", sizeof(ssid)-1);
  strncpy(pass, d["pass"] | "", sizeof(pass)-1);
  strncpy(gpUrl, d["gpurl"] | AMSAT_GP_URL, sizeof(gpUrl)-1); gpUrl[sizeof(gpUrl)-1]=0;
  strncpy(myCall, d["mycall"] | "", sizeof(myCall)-1); myCall[sizeof(myCall)-1]=0;
  lat        = d["lat"] | 0.0;
  lon        = d["lon"] | 0.0;
  altM       = d["alt"] | 0.0;
  useGps     = d["gps"] | false;
  gpsSource  = d["gpssrc"] | (uint8_t)GPS_SRC_CAP1262;
  radioModel = d["rig"] | (uint8_t)RIG_IC9700;
  civAddr    = d["addr"]| (uint8_t)0xA2;
  civBaud    = d["baud"]| 19200u;
  vfoType    = d["vfotype"] | (uint8_t)VFO_MAIN_UP_SUB_DOWN;
  satMode    = d["satmode"] | false;
  if (vfoType > VFO_MAIN_DOWN_SUB_UP) vfoType = VFO_MAIN_UP_SUB_DOWN;
  catRateMs  = d["catms"] | 500u;
  if (catRateMs < 10) catRateMs = 10;
  catDelayMs = d["catdly"] | (uint16_t)70;
  if (catDelayMs > 200) catDelayMs = 200;
  minPassEl  = d["minel"] | 5.0f;
  aosAlarm   = d["aosalarm"] | true;
  dimSecs    = d["dimsecs"] | (uint16_t)120;
  calDlHz    = d["caldl"] | 0;
  calUlHz    = d["calul"] | 0;
  rotEnable  = d["roten"]  | false;
  rotBaud    = d["rotbaud"]| 9600u;
  rotAzOff   = d["rotaz"]  | (int16_t)0;
  rotElOff   = d["rotel"]  | (int16_t)0;
  rotDeadband= d["rotdb"]  | (uint8_t)3;
  rotParkAz  = d["rotpaz"] | (uint16_t)0;
  rotParkEl  = d["rotpel"] | (uint8_t)0;
  rotFlip    = d["rotflip"]| false;
  if (radioModel >= RIG_COUNT) radioModel = RIG_IC9700;
  return true;
}

bool Settings::save() {
  JsonDocument d;
  d["ssid"] = ssid;  d["pass"] = pass;
  d["gpurl"] = gpUrl;
  d["mycall"] = myCall;
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["gpssrc"] = gpsSource;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["vfotype"] = vfoType; d["satmode"] = satMode; d["catms"] = catRateMs;
  d["catdly"] = catDelayMs;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  d["aosalarm"] = aosAlarm;
  d["dimsecs"] = dimSecs;
  d["roten"]=rotEnable; d["rotbaud"]=rotBaud; d["rotaz"]=rotAzOff;
  d["rotel"]=rotElOff; d["rotdb"]=rotDeadband; d["rotpaz"]=rotParkAz;
  d["rotpel"]=rotParkEl; d["rotflip"]=rotFlip;
  File f = Store::fs().open(FILE_CFG, "w");
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
  auto m5cfg = M5.config();
  M5Cardputer.begin(m5cfg, true);   // true => init keyboard
  Serial.begin(115200);             // diagnostics on the USB serial monitor
  M5Cardputer.Display.setRotation(1);
  canvas.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
  canvas.setTextWrap(false);
  M5Cardputer.Display.setBrightness(SCREEN_BRIGHT);

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

  // If a WiFi network is configured, connect at boot and set the clock over NTP.
  // Best-effort: a failure is non-fatal (GPS or a cached/manual clock still work).
  if (cfg.ssid[0]) {
    setStatus("WiFi..."); draw();
    if (net.connect(cfg.ssid, cfg.pass)) {
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
  loadFavs();
  buildSatView();

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
  if (timeIsSet() && favN) buildSchedule();

  // If we woke from the deep-sleep-until-pass timer, jump to the schedule so
  // the imminent pass is front and centre (the AOS alarm will sound shortly).
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) {
    if (favN && timeIsSet()) { buildSchedule(); schedSel = 0; screen = SCR_SCHEDULE; }
  }

  lastInputMs = millis();
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
  rig->setCmdDelay(cfg.catDelayMs);                  // CAT Delay: inter-command pause
  // The rig's satellite mode is no longer forced here -- it is commanded per the
  // Sat Mode setting when radio control is engaged (see keyTrack, 'r' key).
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
    rigSetDownlinkMode(RM_USB);                               // downlink: USB
    if (t.uplink) rigSetUplinkMode(hf ? RM_USB : RM_LSB);     // uplink: LSB (USB if HF)
  } else {
    RigMode m = Rig::modeFromString(t.mode);
    rigSetDownlinkMode(m);
    if (t.uplink) rigSetUplinkMode(m);
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
  tuneMode = TM_HOLD; lastRxSet = 0;          // start each channel holding both legs
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
    lastInputMs = millis();
    if (screenAsleep) {                       // first key just wakes the display
      M5Cardputer.Display.setBrightness(SCREEN_BRIGHT);
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
        bool otr   = (tuneMode == TM_FULL || tuneMode == TM_DL) &&
                     t.isLinear && rig->canReadFreq();
        bool drvDL = (tuneMode != TM_UL);   // downlink driven in FULL/DL/HOLD
        bool drvUL = (tuneMode != TM_DL);   // uplink driven in FULL/UL/HOLD
        if (otr) {
          // ---- One True Rule (KB5MU): hold a constant frequency AT THE
          // SATELLITE while the operator tunes the rig's knob. Read the
          // downlink the operator is on, back out Doppler to find their chosen
          // spot in the passband, then Doppler-correct BOTH legs around that
          // fixed satellite frequency. Let go of the knob and nothing drifts.
          uint32_t rxNow;
          if (rigReadDownlinkFreq(rxNow)) {
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
          if (drvDL && t.downlink) rigSetDownlinkFreq(rx);    // hold sat downlink
          if (drvUL && t.uplink) { rigSetUplinkFreq(tx);            // hold sat uplink
                                   rigSelectDownlink(); }     // dial back on RX
          lastRxSet = rx;
        } else {
          Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
          Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
          if (drvDL && t.downlink) rigSetDownlinkFreq(rx);    // downlink (RX)
          if (drvUL && t.uplink) { rigSetUplinkFreq(tx); }          // uplink (TX)
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
  // Redraw cadence is still screen-dependent (the radio/rotator service above is
  // not): refresh the live screens periodically; static ones redraw on keypress.
  if (screen == SCR_TRACK || screen == SCR_POLAR) {
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
    M5Cardputer.Display.setBrightness(SCREEN_BRIGHT);
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
    case SCR_ABOUT:    keyAbout(c, enter, back); break;
    case SCR_LOG:      keyLog(c, enter, back); break;
    case SCR_LOGENTRY: keyLogEntry(c, enter, back); break;
    case SCR_LOGLIST:  keyLogList(c, enter, back); break;
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
  const int N = 9;
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
      case 7: screen = SCR_ABOUT; break;
      case 8: logMenuSel = 0; screen = SCR_LOG; break;
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

  if (c == 'd') {                                    // cycle the Doppler tune mode
    if (!linear) setStatus("Tune modes: linear birds only");
    else {
      bool canRead = rig && rig->canReadFreq();
      do { tuneMode = (TuneMode)((tuneMode + 1) & 3); }   // FULL/DL need knob read
      while (!canRead && (tuneMode == TM_FULL || tuneMode == TM_DL));
      lastRxSet = 0;                                  // re-sync to the knob
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
      // Command the rig's satellite mode per the Sat Mode setting (a no-op on
      // rigs without one). Which physical VFO carries up/downlink follows the
      // VFO Type setting (see the rigSet*/rigSelect* helpers).
      if (rig) rig->enableSatMode(cfg.satMode);
      if (activeTxCount > 0) applyTransponderModes(activeTx[curTx]);
      lastDoppMs = 0;
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
  if (c == 'l') { beginQso(); return; }              // log a QSO (radio keeps tracking)
  if (c == 'p') { polarPathValid = false; screen = SCR_POLAR;
                  lastDrawMs = 0; return; }                   // live polar
  if (enter) {  // persist calibration for THIS satellite (per-sat store)
    SatEntry* s = activeSat();
    if (s) { saveCalForSat(s->norad);
             setStatus("Cal saved: " + String(s->name)); }
  }
}

void App::keyPolar(char c, bool enter, bool back) {
  if (c == 'l') { beginQso(); return; }          // log a QSO (radio keeps tracking)
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
  const int N = 24;
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
      case 15: cfg.vfoType = (cfg.vfoType == VFO_MAIN_UP_SUB_DOWN)
                             ? VFO_MAIN_DOWN_SUB_UP : VFO_MAIN_UP_SUB_DOWN;
               cfg.save(); break;
      case 16: cfg.satMode = !cfg.satMode; cfg.save(); break;
      case 17: { long v = (long)cfg.catRateMs + dir*10; if (v < 10) v = 10;
                 cfg.catRateMs = (uint32_t)v; cfg.save(); } break;
      case 18: { long v = (long)cfg.catDelayMs + dir*2; if (v < 0) v = 0;
                 if (v > 200) v = 200; cfg.catDelayMs = (uint16_t)v; cfg.save();
                 if (rig) rig->setCmdDelay(cfg.catDelayMs); } break;
      case 19: { const uint16_t opts[] = {0,30,60,120,300}; int idx=0;
                 for (int i=0;i<5;i++) if (opts[i]==cfg.dimSecs) idx=i;
                 idx = (idx + dir + 5) % 5; cfg.dimSecs = opts[idx];
                 cfg.save(); lastInputMs = millis(); } break;
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
      case 20: editTarget = 204; editTitle = "My callsign";
               editBuf = cfg.myCall; screen = SCR_EDIT; break;
      case 21: {
        bool ok = copyFile(FILE_CFG, FILE_CFG_BAK) && copyFile(FILE_FAVS, FILE_FAVS_BAK);
        setStatus(ok ? "Backed up to SD" : "Backup failed");
      } break;
      case 22: {
        if (!Store::fs().exists(FILE_CFG_BAK)) { setStatus("No backup found"); break; }
        bool ok = copyFile(FILE_CFG_BAK, FILE_CFG);
        copyFile(FILE_FAVS_BAK, FILE_FAVS);
        if (ok) { cfg.load(); calDl = cfg.calDlHz; calUl = cfg.calUlHz;
                  loadFavs(); applyRadioFromCfg(); applyRotatorFromCfg();
                  buildSatView(); if (timeIsSet() && favN) buildSchedule();
                  setStatus("Restored from SD"); }
        else setStatus("Restore failed");
      } break;
      case 23: editTarget = 400; editTitle = "Type ERASE to wipe all";
               editBuf = ""; screen = SCR_EDIT; break;
      default: adj(+1); break;
    }
  }
}

static Screen editHome(int t) {
  if (t == 600) return SCR_LOG;         // LoTW SAT_NAME prompt (abort export)
  if (t >= 500) return SCR_LOGENTRY;    // QSO log field edit
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
      case 204: strncpy(cfg.myCall, editBuf.c_str(), sizeof(cfg.myCall)-1);
                cfg.myCall[sizeof(cfg.myCall)-1] = 0; break;

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
  if (c >= 32 && c < 127) {
    if (editTarget == 103 || editTarget == 204 || editTarget == 330 ||
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
void App::drawAbout() {
  header("About");
  canvas.setTextSize(1);
  int y = 22; const int dy = 11;
  auto line = [&](const String& s){
    canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(s); y += dy;
  };
  line(String("CardSat v") + FW_VERSION);
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

// Snapshot the auto fields (time, sat, freqs, mode, my grid) at the instant the
// operator hits 'l', then open the entry screen. Radio control keeps running.
void App::beginQso() {
  logReturn = screen;
  memset(&qso, 0, sizeof(qso));
  strncpy(qso.rstS, "59", sizeof(qso.rstS) - 1);
  strncpy(qso.rstR, "59", sizeof(qso.rstR) - 1);
  qso.utc = timeIsSet() ? (uint32_t)nowUtc() : 0;
  String mg = loc.toGrid(loc.obs().lat, loc.obs().lon);
  strncpy(qso.myGrid, mg.c_str(), sizeof(qso.myGrid) - 1);
  strncpy(qso.myCall, cfg.myCall, sizeof(qso.myCall) - 1);
  SatEntry* s = activeSat();
  if (s) {
    strncpy(qso.sat, s->name, sizeof(qso.sat) - 1);
    if (activeTxCount > 0) {
      Transponder& t = activeTx[curTx];
      strncpy(qso.mode, t.isLinear ? "SSB" : (t.mode[0] ? t.mode : "FM"),
              sizeof(qso.mode) - 1);
      if (timeIsSet()) {
        pred.setSite(loc.obs()); pred.setSat(*s);
        LiveLook L = pred.look(nowUtc());
        struct timeval tv; gettimeofday(&tv, nullptr);
        L.rangeRate = pred.rangeRateAt((double)tv.tv_sec + tv.tv_usec * 1e-6);
        uint32_t dlOp, ulOp, rx, tx;
        Predictor::passbandFreqs(t, pbOffset, dlOp, ulOp);
        Predictor::dopplerFreqs(dlOp, ulOp, L.rangeRate, calDl, calUl, rx, tx);
        qso.dlHz = rx; qso.ulHz = tx;
      }
    }
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
  const char* items[] = { "New QSO entry", "View / edit log", "Export to ADIF" };
  for (int i = 0; i < 3; ++i) {
    int y = 26 + i*14;
    if (i == logMenuSel) { canvas.fillRect(0, y-2, 240, 13, CL_GREEN);
                           canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                   canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y); canvas.print(items[i]);
  }
  canvas.setTextColor(CL_GREY, CL_BLACK);
  canvas.setCursor(6, 26 + 3*14 + 6);
  canvas.printf("%d logged  (qso_log.csv)", qsoCount());
  footer("; / . move  ENT  ` back");
}

void App::keyLog(char c, bool enter, bool back) {
  if (isBack(c, back)) { screen = SCR_HOME; return; }
  const int N = 3;
  if (isUp(c))   logMenuSel = (logMenuSel + N - 1) % N;
  if (isDown(c)) logMenuSel = (logMenuSel + 1) % N;
  if (enter) {
    if (logMenuSel == 0) beginQso();
    else if (logMenuSel == 1) { loadLog(); screen = SCR_LOGLIST; }
    else beginAdifExport();
  }
}

void App::drawLogEntry() {
  header(logEditIdx >= 0 ? "Edit QSO" : "Log QSO");
  canvas.setTextSize(1);
  canvas.setTextColor(CL_GREY, CL_BLACK);
  char tb[24];
  if (qso.utc) { time_t tt = (time_t)qso.utc; struct tm* g = gmtime(&tt);
                 strftime(tb, sizeof(tb), "%m-%d %H:%M:%SZ", g); }
  else strcpy(tb, "(no clock)");
  canvas.setCursor(4, 20); canvas.printf("%s  %.10s", tb, qso.sat);
  canvas.setCursor(4, 30); canvas.printf("DL %.3f  UL %.3f", qso.dlHz/1e6, qso.ulHz/1e6);
  canvas.setCursor(4, 40); canvas.printf("MyGrid %s  MyCall %s",
                                         qso.myGrid, qso.myCall[0] ? qso.myCall : "-");
  const char* labels[] = { "Call", "Mode", "RST S", "RST R", "Grid", "Notes" };
  const char* vals[]   = { qso.call, qso.mode, qso.rstS, qso.rstR, qso.grid, qso.notes };
  for (int i = 0; i < 6; ++i) {
    int y = 52 + i*11;
    bool sel = (i == logSel);
    if (sel) { canvas.fillRect(0, y-1, 240, 11, CL_BLUE);
               canvas.setTextColor(CL_WHITE, CL_BLUE); }
    else        canvas.setTextColor(CL_CYAN, CL_BLACK);
    canvas.setCursor(4, y); canvas.printf("%-6s %.28s", labels[i], vals[i]);
  }
  footer(logEditIdx >= 0 ? "ENT edit  s save  x del  ` back"
                         : "ENT edit  s save  ` cancel");
}

void App::keyLogEntry(char c, bool enter, bool back) {
  if (c != 'x') logDelArm = false;             // any other key disarms delete
  if (isBack(c, back)) { logEditIdx = -1; screen = logReturn; lastDrawMs = 0; return; }
  if (isUp(c))   logSel = (logSel + 6 - 1) % 6;
  if (isDown(c)) logSel = (logSel + 1) % 6;
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
    if (logSel == 1) {                          // Mode: toggle SSB<->CW on linear
      if (strcmp(qso.mode, "FM") != 0) {
        strncpy(qso.mode, strcmp(qso.mode, "CW") == 0 ? "SSB" : "CW",
                sizeof(qso.mode) - 1);
        qso.mode[sizeof(qso.mode) - 1] = 0;
      } else setStatus("FM mode is fixed");
      return;
    }
    switch (logSel) {
      case 0: editTarget = 500; editTitle = "Callsign";   editBuf = qso.call; break;
      case 2: editTarget = 501; editTitle = "RST sent";   editBuf = qso.rstS; break;
      case 3: editTarget = 502; editTitle = "RST rcvd";   editBuf = qso.rstR; break;
      case 4: editTarget = 503; editTitle = "Their grid"; editBuf = qso.grid; break;
      case 5: editTarget = 504; editTitle = "Notes";      editBuf = qso.notes; break;
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
    case SCR_ABOUT:    drawAbout(); break;
    case SCR_LOG:      drawLog(); break;
    case SCR_LOGENTRY: drawLogEntry(); break;
    case SCR_LOGLIST:  drawLogList(); break;
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
                          "Track (sel)", "Location", "Update GP/Freq", "Settings",
                          "About / diagnostics", "Log" };
  canvas.setTextSize(1);
  for (int i = 0; i < 9; ++i) {
    int y = 18 + i*11;
    if (i == homeSel) { canvas.fillRect(0, y-2, 240, 11, CL_GREEN);
                        canvas.setTextColor(CL_BLACK, CL_GREEN); }
    else                canvas.setTextColor(CL_WHITE, CL_BLACK);
    canvas.setCursor(6, y);
    canvas.print(items[i]);
  }
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
  const int N = 24;
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
  rows[15] = String("VFO: ") + (cfg.vfoType == VFO_MAIN_UP_SUB_DOWN
                                ? "Main Up/Sub Dn" : "Main Dn/Sub Up");
  rows[16] = String("Sat mode: ") + (cfg.satMode ? "on" : "off");
  {
    uint32_t eff = effectiveCatRateMs();
    rows[17] = String("CAT rate: ") + String(cfg.catRateMs) + " ms";
    if (eff > cfg.catRateMs) rows[17] += " (min " + String(eff) + ")";
  }
  rows[18] = String("CAT delay: ") + String(cfg.catDelayMs) + " ms";
  rows[19] = String("Screen sleep: ") + (cfg.dimSecs == 0 ? String("off")
             : (cfg.dimSecs % 60 == 0) ? String(cfg.dimSecs / 60) + " min"
                                       : String(cfg.dimSecs) + " s");
  rows[20] = String("My callsign: ") + (cfg.myCall[0] ? cfg.myCall : "(not set)");
  rows[21] = String("Backup config+favs -> SD");
  rows[22] = String("Restore config+favs");
  rows[23] = String("Reset all data (erase)");
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
