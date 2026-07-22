#pragma once
// ===========================================================================
//  config.h  -  compile-time configuration and shared constants
// ===========================================================================
#include <Arduino.h>

// ---- Console capture (see consolelog.h) -----------------------------------
// arduino-esp32 defines `Serial` as a MACRO aliasing the real device --
// `#define Serial HWCDCSerial` (HardwareSerial.h:413, this board's config:
// ARDUINO_USB_MODE=1 + ARDUINO_USB_CDC_ON_BOOT=1). Redefining it here points
// every Serial.print/printf/println in CardSat at a tee that forwards to the
// hardware AND (when the Settings toggle is on) captures the text to
// /CardSat/Logs/console.log. ~181 call sites, none of them edited.
//
// This MUST come after <Arduino.h> (which defines the macro we are replacing)
// and before any code that uses Serial. Every module includes config.h first,
// which is exactly why it lives here rather than in consolelog.h.
//
// Left alone when console capture is compiled out, so a build without it is
// byte-for-byte the stock Serial.
// The class must be DEFINED here, not forward-declared: `Serial.print(...)` needs
// a complete type at every call site, and a forward declaration gives
// "'CardSatSerialTee' has incomplete type" at all ~181 of them. So the tee is
// declared inline here and its behaviour lives in consolelog.cpp.
#ifndef CARDSAT_NO_CONSOLE_LOG
namespace ConsoleLog {
  // Byte sink: consolelog.cpp decides whether to buffer it. Kept out of line so
  // this header stays free of logstore/Store dependencies -- config.h is included
  // by everything, including modules that must not drag in the filesystem.
  void teeByte(uint8_t c);
  void teeBytes(const uint8_t* b, size_t n);

  class Tee : public Print {
   public:
    size_t write(uint8_t c) override { HWCDCSerial.write(c); teeByte(c); return 1; }
    size_t write(const uint8_t* b, size_t n) override {
      const size_t w = HWCDCSerial.write(b, n); teeBytes(b, n); return w;
    }
    // Stream/HWCDC pass-throughs: code that treats Serial as more than a Print
    // (Serial.begin, `if (Serial)`, Serial.read) must keep working unchanged.
    int  available()            { return HWCDCSerial.available(); }
    int  read()                 { return HWCDCSerial.read(); }
    int  peek()                 { return HWCDCSerial.peek(); }
    void flush()                { HWCDCSerial.flush(); }
    void begin(unsigned long b) { HWCDCSerial.begin(b); }
    void end()                  { HWCDCSerial.end(); }
    void setDebugOutput(bool e) { HWCDCSerial.setDebugOutput(e); }
    operator bool()             { return (bool)HWCDCSerial; }
  };
}
extern ConsoleLog::Tee CardSatSerialTee;
#undef Serial
#define Serial CardSatSerialTee
#endif

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
// Max QSOs per LoTW upload batch. Chosen so the compressed .tq8 body stays well under
// the ESP32 lwip TCP_SND_BUF send ceiling (~5744 B; ~650 B/QSO => 6 QSOs ~= 4.1 KB).
// Larger uploads are split into multiple size-bounded batches, one per reboot.
#define LOTW_BATCH_QSOS    6
// Max QSOs per Cloudlog upload batch. Cloudlog's ADIF body is ~275 B/QSO, so 15 QSOs
// (~4.1 KB) stays under the SAFE_UPLOAD_BODY / TCP_SND_BUF ceiling with margin.
#define CL_BATCH_QSOS      15
// Safe request-body size for a single POST on this platform, below the TCP_SND_BUF
// send ceiling (with margin for multipart headers/tail). POSTs larger than this stall
// mid-body on-device and must be batched. Applies to LoTW and Cloudlog uploads.
#define SAFE_UPLOAD_BODY   5000
// hams.at upcoming satellite activations (Atom feed of scheduled rove/activations).
#define HAMSAT_FEED_URL    "https://hams.at/feeds/upcoming_alerts"
#define FILE_HAMSAT  "/CardSat/hamsat.dat"   // cached parsed activations (binary, survives reboot)
#define FILE_USERSKED "/CardSat/usersked.dat" // user-entered manual activations/skeds (binary, survives feed refresh)
// "Cache all" runs in small per-sat batches across reboots: a fresh socket pool
// and WiFi association each boot keeps the LWIP pool from exhausting on this
// link. This marker file holds the next satellite index to cache; its presence
// means a resume is pending and setup() auto-continues. Deleted when done.
#define FILE_TX_RESUME     "/CardSat/tx_resume.txt"
#define FILE_CL_RESUME     "/CardSat/cl_resume.txt"  // Cloudlog upload-after-reboot marker
#define FILE_LOTW_RESUME   "/CardSat/lotw_resume.txt" // LoTW upload-after-reboot marker
#define TX_CACHE_BATCH     12                       // sats cached per boot (reboot-per-batch mode)
// Cooldown between full network update cycles. The LWIP TCP PCB pool is small (16)
// and a closed socket sits in TIME_WAIT for ~2xMSL (~120s) before the PCB frees; an
// update cycle opens ~6 sockets, so re-running it back-to-back (e.g. hammering the
// button when a fetch fails) can pile TIME_WAIT PCBs faster than they drain and wedge
// every further connect with "connection refused". A normal update is run once or
// twice, so gating repeats to this window lets the pool drain instead of saturating.
#define NET_COOLDOWN_MS    90000UL                   // min gap between update cycles (~TIME_WAIT window)

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

// --- Grove rotator (ROT_XPORT_GROVE) --------------------------------------
// The SAME two pins and the SAME UART as wired CI-V, deliberately: the Cardputer
// has one Grove port. Whoever gets it is a settings-level decision, and the app
// enforces that only one claimant is ever configured (App::rotTransportConflict
// blocks Grove rotator against wired CI-V and against the Grove GPS).
static constexpr int   ROT_GROVE_UART_NUM = 1;   // UART1, same as CI-V
static constexpr int   ROT_GROVE_RX_PIN   = 1;   // G1
static constexpr int   ROT_GROVE_TX_PIN   = 2;   // G2

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
static constexpr const char* FW_VERSION = "0.9.63";
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

// Frequency type (0.9.62). Widened from uint32_t (ceiling ~4.294 GHz) to 64-bit so
// microwave satellite transponders -- QO-100's 10489.75 MHz downlink, 5.7/10/24 GHz --
// are representable, stored, and displayed. The CAT wire encoders still take whatever
// their protocol needs (CI-V 5-BCD-byte = 10 digits; Yaesu IF is sub-GHz), converting at
// the boundary. See docs/design/HIGH_FREQ_SCOPE.md.
typedef uint64_t freq_t;

// ---- USB-host CAT (CAT_USB) ------------------------------------------------
// ON by default since 0.9.59 -- like LoRa, ship every feature. It was opt-in
// while unproven; 0.9.58 bench-proved it on an IC-821 + FTDI adapter (engage /
// disengage / re-engage / Doppler tracking over many cycles). Defined here
// rather than in usbserial.h because settings.h needs it for CAT_TYPE_N and
// does not include usbserial.h.
//
// The default build therefore REQUIRES tanakamasayuki/EspUsbHost, v2.3.1 or
// later (Library Manager / lib_deps). v2.3.1 fixed the peripheral_map compile
// error upstream (the assignment is now inside an ESP32-P4 target guard), so no
// library patch is needed; only stale copies <= 2.3.0 still need the one-line
// edit in docs/BUILD_AND_FLASH.md.
//
// TO BUILD WITHOUT USB CAT (no EspUsbHost needed; Settings cannot reach
// "USB serial"; otherwise byte-for-byte the same firmware):
//   * Arduino IDE  -- change the 1 below to 0.
//   * PlatformIO   -- add -DCARDSAT_HAS_USBCAT=0 under build_flags and comment
//                     out the EspUsbHost line under lib_deps.
// There is deliberately NO per-file ESP_USB_HOST_MAX_DEVICES define: the slot
// array is a member of the host object, sized in the LIBRARY's header, so only a
// global -D can change it consistently for the library's own translation unit
// too -- a per-file define diverges the layouts and corrupts the firmware (the
// 0.9.58-wip enable-USB-CAT freeze; see usbserial.cpp). Both build paths supply
// that global -D as ESP_USB_HOST_MAX_DEVICES=4: the Arduino IDE via build_opt.h
// (which the esp32 core applies to every translation unit, libraries included),
// PlatformIO via build_flags in platformio.ini. The host object is heap-
// allocated only while USB is engaged, so the slots cost nothing at rest.
#ifndef CARDSAT_HAS_USBCAT
#define CARDSAT_HAS_USBCAT 1
#endif
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
static constexpr int   VIS_DAY_MAX     = 32;   // passes in ONE day-window (scratch; a LEO does ~16/day)

// ---- Shared scratch arena (RAM Tier 1) -----------------------------------------
// One 1.5 KB buffer replaces five function-static scratch buffers that can never
// be live at the same time (all run to completion on loopTask; none calls another):
//   buildVisList day[VIS_DAY_MAX]  1,280 B    printTicket tp[16]        1,216 B
//   printSatCard tp[16]            1,216 B    SatDb::scanGpFile obj[]   1,200 B
//   takeScreenshot line[]+row[]    1,536 B
// drawOrbit's 816 B scratch stays a plain static ON PURPOSE: buildVisList paints
// a status line mid-build via draw(), and the draw dispatch can reach drawOrbit --
// a real nesting the arena must not serve. The RAII Lease releases on every return
// path; if the arena is unexpectedly busy (a nesting this comment missed), the
// Lease falls back to a one-shot heap block so the feature still works.
static constexpr size_t SCRATCH_BYTES = 1536;
namespace Scratch {
  inline uint8_t     buf[SCRATCH_BYTES] __attribute__((aligned(8)));
  inline const char* owner = nullptr;   // who holds the arena right now (debug)
  struct Lease {
    uint8_t* p    = nullptr;
    bool     heap = false;              // fallback path: block came from malloc()
    Lease(const char* who, size_t need) {
      if (owner == nullptr && need <= SCRATCH_BYTES) { owner = who; p = buf; }
      else { p = (uint8_t*)malloc(need); heap = (p != nullptr); }
    }
    ~Lease() { if (heap) free(p); else if (p) owner = nullptr; }
    Lease(const Lease&) = delete;
    Lease& operator=(const Lease&) = delete;
  };
}
                                               // (busy LEO ~10-12/day x 10 d; was 64,
                                               // which truncated the last day-rows)
static constexpr int   ILLUM_DAYS      = 60;   // illumination raster columns (days)
static constexpr int   ILLUM_ROWS      = 80;   // illumination raster rows (orbit phase samples)

// ---------------------------------------------------------------------------
//  Files on LittleFS
// ---------------------------------------------------------------------------
#define DATA_DIR     "/CardSat"               // all data/config lives in this folder
#define AUDIO_DIR    "/CardSat/audio"         // voice memos (SD card only)
#define CALENDAR_DIR "/CardSat/Calendars"     // exported .ics calendar files (0.9.62)
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
#define FILE_TOOLDEF "/CardSat/tooldef.txt"   // per-form-tool saved field values (one line per tool id)
#define FILE_NOTES   "/CardSat/notes.txt"     // per-sat operating notes: "norad<TAB>text" lines
#define FILE_FAVS    "/CardSat/favs.txt"      // favorite NORAD ids, one per line
#define FILE_MGP     "/CardSat/mgp.json"      // manually-entered GP sats (one OMM object/line)
#define FILE_CTX     "/CardSat/ctx.json"      // CelesTrak-sourced extra favorites (one OMM object/line);
                                              // refreshed from CelesTrak on GP updates, unlike FILE_MGP
#define FILE_CTX_TS  "/CardSat/ctx.ts"        // CelesTrak courtesy-limit state (timestamps + last query hash)
#define CTX_MAX      25                       // max CelesTrak extras tracked (matches the per-update
                                              // refresh cap; also bounds every ctx file walk)
#define FILE_MTX     "/CardSat/mtx_%lu.json"  // manual transponders per norad (text lines)
#define FILE_CFG_BAK  "/CardSat/config.bak"    // backup copy of config.json
#define FILE_FAVS_BAK "/CardSat/favs.bak"      // backup copy of favs.txt
#define FILE_AMSTAT   "/CardSat/amstat.json"   // cached AMSAT OSCAR status summary
#define FILE_AMSCAT   "/CardSat/amscat.json"   // cached AMSAT status-API catalog (name map)
// ---- Logs -----------------------------------------------------------------
// One directory so the web portal can list it, and so a user can clear every log
// without touching caches or config. Works identically on SD and on bare flash.
#define LOG_DIR      "/CardSat/Logs"
// Per-file cap; each log rotates ONCE to .1, so the hard ceiling is 2x this.
// Flash is a few hundred KB shared with the GP cache, config and tx caches -- an
// uncapped log there breaks the things CardSat needs to work. The SD card has
// room to be useful.
#define LOG_MAX_BYTES_FLASH  (16u * 1024u)   // 32 KB worst case per log on flash
#define LOG_MAX_BYTES_SD     (256u * 1024u)  // 512 KB worst case per log on SD

// (FILE_USBCAT_LOG removed: every writer now goes through Logstore, which owns
// rotation and the size cap. A bare path invites an uncapped open() that fills a
// flash-only Cardputer's filesystem -- the log path lives in logstore.cpp now.)
#define AMSAT_STATUS_URL  "https://www.amsat.org/status/api/v1/summary.php?hours="
#define AMSAT_REPORTS_URL "https://www.amsat.org/status/api/v1/reports.php?name="
#define AMSAT_CATALOG_URL "https://www.amsat.org/status/api/v1/catalog.php"
#define AMSAT_REPORT_POST "https://www.amsat.org/status/api/v1/reports.php"
// AMSAT status window is now a user setting (cfg.amsatWindowH, default 24 h); see settings.h.
#define FILE_SPACEWX  "/CardSat/spacewx.txt"    // cached space weather: "f107 ap epoch"
#define FILE_SPACEWX_TMP "/CardSat/spacewx.tmp"  // scratch for streamed NOAA JSON (low heap)
#define FILE_DL_TMP      "/CardSat/dl.tmp"        // shared scratch for streamed downloads (one at a time)
// NOAA SWPC daily F10.7 cm solar-radio-flux observations (JSON array of records).
// Best-effort: drives the orbital-decay density scale when "Decay solar = auto".
#define SPACEWX_F107_URL  "https://services.swpc.noaa.gov/json/f107_cm_flux.json"
#define SPACEWX_KP_URL    "https://services.swpc.noaa.gov/products/noaa-planetary-k-index.json"
// GOES primary X-ray flare summary (object JSON: newest flare with class letter +
// peak flux). Drives the flare / HF-blackout indicator.
#define SPACEWX_XRAY_URL  "https://services.swpc.noaa.gov/json/goes/primary/xray-flares-latest.json"
// Real-time solar wind Bz + speed from the SWPC dashboard summary products.
// The legacy /products/solar-wind/*.json family was RETIRED by SCN 26-21 (bench
// 404s, 2026-07); the summary files were kept: ~100 B objects, key:value.
#define SPACEWX_MAG_URL   "https://services.swpc.noaa.gov/products/summary/solar-wind-mag-field.json"
#define SPACEWX_PLASMA_URL "https://services.swpc.noaa.gov/products/summary/solar-wind-speed.json"
// Daily observed sunspot number: SWPC 30-day text table (~2.5 KB). The JSON
// sunspot_report.json is 170+ KB -- too large for the no-PSRAM heap.
#define SPACEWX_SSN_URL   "https://services.swpc.noaa.gov/text/daily-solar-indices.txt"
// SWPC 3-day geomagnetic forecast (max predicted Kp per day). Text product.
#define SPACEWX_FCAST_URL "https://services.swpc.noaa.gov/text/3-day-forecast.txt"
// Open-Meteo terrestrial weather (current + multi-day forecast) for the operating
// site. Free, no key, non-commercial. https://open-meteo.com  Cached for offline use.
#define WEATHER_API_BASE  "https://api.open-meteo.com/v1/forecast"
// Open-Meteo elevation API: accepts comma-separated latitude/longitude lists and
// returns an "elevation":[...] array (metres). Used by the terrain path profiler.
#define ELEVATION_API_BASE "https://api.open-meteo.com/v1/elevation"
#define FILE_WEATHER      "/CardSat/weather.txt"   // cached parsed weather
#define FILE_WEATHER_TMP  "/CardSat/weather.tmp"   // scratch for streamed JSON (low heap)
#define WX_FORECAST_DAYS  4                          // today + 3 days shown
#define FILE_LOG     "/CardSat/qso_log.csv"     // QSO log (CSV, notes is last field)
#define FILE_CLOUDLOG_TMP "/CardSat/cloudlog.tmp" // scratch: JSON upload body, streamed (low heap)
#define FILE_ADIF    "/CardSat/qso_log.adi"     // ADIF export (generated on demand)
#define FILE_LOTW    "/CardSat/lotw_sats.csv"   // LoTW SAT_NAME map ("SAT_NAME,AMSAT_NAME")
