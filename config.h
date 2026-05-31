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
//  CI-V is driven over a 3.3 V hardware UART (TTL serial) on the header pins.
//  Use a 3.3 V-safe CI-V level interface between these pins and the radio's
//  REMOTE jack (the ESP32-S3 GPIOs are not 5 V tolerant). Set the CI-V address
//  and baud (to match the radio's menu) in Settings.
// ---------------------------------------------------------------------------
// GPS source is selectable at runtime (Settings -> Location screen, 's').
// Per-source UART/pins/baud live in GPS_PROFILES[] in app.cpp. All sources use
// UART2, so CI-V keeps UART1 (G1/G2) to itself.
//   GROVE   : Cardputer Grove HY2.0-4P on G1/G2 -- SAME pins as default CI-V,
//             so don't use Grove GPS and CI-V on G1/G2 at the same time.
//   CAP868  : Cap LoRa868 GNSS   (G15 RX / G13 TX, AT6668, 9600 baud)
//   CAP1262 : Cap LoRa-1262 GNSS (G15 RX / G13 TX, AT6668, 115200 baud)
enum GpsSource : uint8_t {
  GPS_SRC_GROVE = 0,
  GPS_SRC_CAP868,
  GPS_SRC_CAP1262,
  GPS_SRC_COUNT
};

static constexpr int   CIV_UART_NUM   = 1;     // CI-V owns UART1 on G1/G2
static constexpr int   CIV_RX_PIN     = 1;     // G1
static constexpr int   CIV_TX_PIN     = 2;     // G2

// ---------------------------------------------------------------------------
//  Limits (kept modest - no PSRAM on the StampS3A)
// ---------------------------------------------------------------------------
static constexpr int   MAX_SATS        = 220;  // sats held in RAM from TLE file
static constexpr int   MAX_TX_PER_SAT  = 8;    // transmitters cached per sat
static constexpr int   PASS_LIST_LEN   = 12;   // passes shown per satellite
static constexpr int   SCHED_MAX       = 24;   // favorites tracked in the schedule

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define FILE_TLE     "/tle.txt"
#define FILE_CFG     "/config.json"
#define FILE_TXCACHE "/tx_%lu.json"   // %lu = norad id
#define FILE_CALIB   "/calib.txt"     // per-sat calibration: "norad dl ul" lines
#define FILE_FAVS    "/favs.txt"      // favorite NORAD ids, one per line
#define FILE_MTLE    "/mtle.txt"      // manually-entered TLEs (3-line stanzas)
#define FILE_MTX     "/mtx_%lu.json"  // manual transponders per norad (text lines)
