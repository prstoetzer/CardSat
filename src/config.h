#pragma once
// ===========================================================================
//  config.h  -  compile-time configuration and shared constants
// ===========================================================================
#include <Arduino.h>

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
// ARRL Logbook of the World self-authenticating upload web service. The signed
// .tq8 payload authenticates itself (no login). URL kept here so it can be
// updated without touching code if ARRL moves the endpoint again.
#define LOTW_UPLOAD_URL    "https://lotw.arrl.org/lotw/upload"
// hams.at upcoming satellite activations (Atom feed of scheduled rove/activations).
#define HAMSAT_FEED_URL    "https://hams.at/feeds/upcoming_alerts"
#define FILE_HAMSAT  "/CardSat/hamsat.dat"   // cached parsed activations (binary, survives reboot)
// "Cache all" runs in small per-sat batches across reboots: a fresh socket pool
// and WiFi association each boot keeps the LWIP pool from exhausting on this
// link. This marker file holds the next satellite index to cache; its presence
// means a resume is pending and setup() auto-continues. Deleted when done.
#define FILE_TX_RESUME     "/CardSat/tx_resume.txt"
#define FILE_CL_RESUME     "/CardSat/cl_resume.txt"  // Cloudlog upload-after-reboot marker
#define TX_CACHE_BATCH     12                       // sats cached per boot

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

// Built-in IR LED (Cardputer & Cardputer-Adv): GPIO 44, the same pin M5's IR
// examples use. CardSat uses it as a silent pass-alert *beacon*: each pass-alert
// event emits a distinct number of 38 kHz IR bursts so external user-built
// hardware (an IR receiver/demodulator like a TSOP38238 feeding a microcontroller)
// can tell the events apart and trigger whatever the user designs. Off by default.
static constexpr int      IR_LED_PIN     = 44;
static constexpr uint32_t IR_CARRIER_HZ  = 38000;   // standard IR receiver carrier
static constexpr uint16_t IR_BURST_MS    = 60;      // each flash: carrier-on duration
static constexpr uint16_t IR_GAP_MS      = 140;     // off-time between flashes in a group
// Distinct flash counts per pass-alert event (so a receiver can disambiguate):
static constexpr uint8_t  IR_N_T60       = 1;       // 60 s to AOS
static constexpr uint8_t  IR_N_T30       = 2;       // 30 s to AOS
static constexpr uint8_t  IR_N_T10       = 3;       // 10 s to AOS
static constexpr uint8_t  IR_N_AOS       = 4;       // AOS (pass start)
static constexpr uint8_t  IR_N_TCA       = 5;       // TCA (peak elevation)
static constexpr uint8_t  IR_N_LOS       = 6;       // LOS (pass end)

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
static constexpr const char* FW_VERSION = "0.9.37";
// Auto-refresh GP at boot when even the freshest cached element set is older.
static constexpr double  GP_STALE_DAYS = 7.0;
// Display backlight level used for normal (awake) operation.
static constexpr uint8_t SCREEN_BRIGHT = 180;
// Most-recent QSO log entries loaded into RAM for the on-device view/edit list.
static constexpr int     LOG_VIEW_MAX  = 60;

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

// --- Yaesu direct rotator (ROT_YAESU) -------------------------------------
// Closed-loop control of a Yaesu az/el controller's external jack via I2C
// modules on the SAME Wire1 bus as the GS-232 bridge: an ADS1115 reads the two
// position pots (AIN0=az, AIN1=el, through dividers) and a PCF8574 drives four
// opto/relay direction lines. See ROTOR_INTERFACE.md.
// *** UNTESTED hardware. Build and connect at your own risk; the author accepts
//     no liability for any damage to equipment. ***
static constexpr uint8_t  YAESU_ADC_ADDR = 0x48;   // ADS1115 (ADDR->GND)
static constexpr uint8_t  YAESU_OUT_ADDR = 0x20;   // PCF8574 (A2..A0->GND)
static constexpr uint8_t  YAESU_BIT_CW   = 0;      // PCF8574 bit -> azimuth CW  (Right)
static constexpr uint8_t  YAESU_BIT_CCW  = 1;      //             -> azimuth CCW (Left)
static constexpr uint8_t  YAESU_BIT_UP   = 2;      //             -> elevation Up
static constexpr uint8_t  YAESU_BIT_DOWN = 3;      //             -> elevation Down
static constexpr bool     YAESU_OUT_ACTIVE_LOW = true; // relay/opto modules: 0 = ON
static constexpr uint16_t YAESU_SVC_MS    = 100;   // closed-loop update period (~10 Hz)
static constexpr uint16_t YAESU_STALL_MS  = 4000;  // driving without progress -> all-stop
static constexpr int32_t  YAESU_STALL_CNT = 25;    // ADC counts counted as "progress"
static constexpr uint8_t  YAESU_ADC_DR    = 0x5;   // ADS1115 data-rate 101 = 250 SPS
static constexpr uint16_t YAESU_ADC_MS    = 6;     // single-shot settle for 250 SPS

// ---------------------------------------------------------------------------
//  Limits (kept modest - no PSRAM on the StampS3A)
// ---------------------------------------------------------------------------
static constexpr int   MAX_SATS        = 150;  // sats held in RAM from GP data
static constexpr int   MAX_TX_PER_SAT  = 64;   // transmitters held for active sat (e.g. ISS has ~49 on SatNOGS)
static constexpr int   PASS_LIST_LEN   = 12;   // passes shown per satellite
static constexpr int   SCHED_MAX       = 24;   // favorites tracked in the schedule
static constexpr int   PD_SAMPLES      = 100;  // samples in the pass-detail curve
static constexpr int   POLAR_PTS       = 48;   // samples in a polar ground-track arc
static constexpr int   MUTUAL_MAX      = 24;   // co-visibility windows listed
static constexpr int   MUTUAL_PASS_SCAN= 64;   // of my passes scanned for mutual windows
static constexpr int   MUTUAL_HORIZON_DAYS = 10; // search co-visibility this many days out
static constexpr int   VIS_DAYS        = 10;   // InstantTrack-style overview horizon (days)
static constexpr int   ORB_OUTLOOK_DAYS = 7;   // orbital-analysis pass-outlook window (days)
static constexpr int   VIS_PASS_MAX    = 128;  // passes cached for the 10-day overview
                                               // (busy LEO ~10-12/day x 10 d; was 64,
                                               // which truncated the last day-rows)
static constexpr int   ILLUM_DAYS      = 60;   // illumination raster columns (days)
static constexpr int   ILLUM_ROWS      = 80;   // illumination raster rows (orbit phase samples)

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define DATA_DIR     "/CardSat"               // all data/config lives in this folder
#define AUDIO_DIR    "/CardSat/audio"         // voice memos (SD card only)
// Voice-memo capture (SD-card-only feature; PDM mic via M5Unified into a WAV).
static constexpr uint32_t MEMO_SAMPLE_HZ  = 16000;  // 16 kHz mono
static constexpr uint32_t MEMO_MAX_SECS   = 30;     // hard cap per memo
static constexpr uint32_t MEMO_MIN_FREE_KB = 512;   // refuse if SD has less free
static constexpr int      MEMO_AC_GAIN     = 8;     // gain on DC-blocked AC signal (tunable)
static constexpr int      MEMO_LIST_MAX    = 64;    // max memos shown in the browser
static constexpr size_t   MEMO_PLAY_SAMPLES = 1024; // playback block size (samples)
#define FILE_GP      "/CardSat/gp.json"       // cached GP/OMM download (JSON array)
#define FILE_CFG     "/CardSat/config.json"
#define FILE_TXCACHE "/CardSat/tx_%lu.json"   // %lu = norad id
#define FILE_CALIB   "/CardSat/calib.txt"     // per-sat calibration: "norad dl ul" lines
#define FILE_TONES   "/CardSat/tones.txt"     // per-sat CTCSS override: "norad tenths" lines
#define FILE_NOTES   "/CardSat/notes.txt"     // per-sat operating notes: "norad<TAB>text" lines
#define FILE_FAVS    "/CardSat/favs.txt"      // favorite NORAD ids, one per line
#define FILE_MGP     "/CardSat/mgp.json"      // manually-entered GP sats (one OMM object/line)
#define FILE_MTX     "/CardSat/mtx_%lu.json"  // manual transponders per norad (text lines)
#define FILE_CFG_BAK  "/CardSat/config.bak"    // backup copy of config.json
#define FILE_FAVS_BAK "/CardSat/favs.bak"      // backup copy of favs.txt
#define FILE_AMSTAT   "/CardSat/amstat.json"   // cached AMSAT OSCAR status summary
#define AMSAT_STATUS_URL  "https://www.amsat.org/status/api/v1/summary.php?hours="
#define AMSAT_STATUS_HOURS 72                    // "recently" window for status reports
#define FILE_SPACEWX  "/CardSat/spacewx.txt"    // cached space weather: "f107 ap epoch"
#define FILE_SPACEWX_TMP "/CardSat/spacewx.tmp"  // scratch for streamed NOAA JSON (low heap)
#define FILE_DL_TMP      "/CardSat/dl.tmp"        // shared scratch for streamed downloads (one at a time)
// NOAA SWPC daily F10.7 cm solar-radio-flux observations (JSON array of records).
// Best-effort: drives the orbital-decay density scale when "Decay solar = auto".
#define SPACEWX_F107_URL  "https://services.swpc.noaa.gov/json/f107_cm_flux.json"
#define SPACEWX_KP_URL    "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json"
// Open-Meteo terrestrial weather (current + multi-day forecast) for the operating
// site. Free, no key, non-commercial. https://open-meteo.com  Cached for offline use.
#define WEATHER_API_BASE  "https://api.open-meteo.com/v1/forecast"
#define FILE_WEATHER      "/CardSat/weather.txt"   // cached parsed weather
#define FILE_WEATHER_TMP  "/CardSat/weather.tmp"   // scratch for streamed JSON (low heap)
#define WX_FORECAST_DAYS  4                          // today + 3 days shown
#define FILE_LOG     "/CardSat/qso_log.csv"     // QSO log (CSV, notes is last field)
#define FILE_ADIF    "/CardSat/qso_log.adi"     // ADIF export (generated on demand)
#define FILE_LOTW    "/CardSat/lotw_sats.csv"   // LoTW SAT_NAME map ("SAT_NAME,AMSAT_NAME")
