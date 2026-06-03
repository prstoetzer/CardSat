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
static constexpr int   MAX_TX_PER_SAT  = 32;   // transmitters held for active sat
static constexpr int   PASS_LIST_LEN   = 12;   // passes shown per satellite
static constexpr int   SCHED_MAX       = 24;   // favorites tracked in the schedule
static constexpr int   PD_SAMPLES      = 100;  // samples in the pass-detail curve
static constexpr int   POLAR_PTS       = 48;   // samples in a polar ground-track arc
static constexpr int   MUTUAL_MAX      = 12;   // co-visibility windows listed
static constexpr int   MUTUAL_PASS_SCAN= 16;   // of my passes scanned for mutual windows

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define FILE_GP      "/gp.json"     // cached GP/OMM download (JSON array)
#define FILE_CFG     "/config.json"
#define FILE_TXCACHE "/tx_%lu.json"   // %lu = norad id
#define FILE_CALIB   "/calib.txt"     // per-sat calibration: "norad dl ul" lines
#define FILE_TONES   "/tones.txt"     // per-sat CTCSS override: "norad tenths" lines
#define FILE_FAVS    "/favs.txt"      // favorite NORAD ids, one per line
#define FILE_MGP     "/mgp.json"     // manually-entered GP sats (one OMM object/line)
#define FILE_MTX     "/mtx_%lu.json"  // manual transponders per norad (text lines)
