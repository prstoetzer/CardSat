// =============================================================================
//  CardSatDualRig - a rigctld server for the M5StickS3 that bridges CardSat (or
//  any Hamlib NET-rigctl client) to TWO half-duplex / receive-only radios.
//
//  A linear-transponder satellite QSO needs uplink and downlink tuned together.
//  A full-duplex rig does both at once, so CardSat drives it directly. HALF-duplex
//  and RECEIVE-ONLY radios (IC-705/905/7100, FT-817/991A, IC-R10/R20/R30,
//  TH-D74/D75, ...) can't, so a proper station uses two of them - one downlink,
//  one uplink. This firmware is the glue: CardSat sees ONE rigctld server and
//  steers two logical VFOs; the Stick hosts the radios on USB and speaks each
//  one's native CAT. All the dual-radio / dual-USB complexity lives on the Stick.
//
//  Control paths (either or both):
//    * Wi-Fi / TCP  - CardSat connects to the Stick's rigctld TCP port.
//    * Grove UART   - a Grove cable between the Cardputer and the Stick carries the
//                     same rigctld text protocol, no Wi-Fi needed (see GROVE below).
//
//  Configuration is entirely at RUNTIME (no compile-time flags):
//    * Boot with no saved config, or hold Button A at boot, to enter CONFIG mode:
//      the Stick raises a Wi-Fi AP + captive portal, and runs USB host so plugged
//      radios enumerate and can be assigned to the uplink/downlink legs from the
//      web page. A plain HTTP API (same endpoints) lets a script configure it
//      without the web UI. Config is saved to NVS and survives reboots.
//    * In RUN mode a long-press of Button A returns to CONFIG mode.
//
//  Board: M5StickS3 (ESP32-S3-PICO-1-N8R8, 8 MB flash, 8 MB PSRAM, native USB OTG).
//  Libraries: M5Unified, EspUsbHost (>= 2.3.2).
//
//  Build (arduino-cli) - verified recipe. The extra defines MUST go in
//  compiler.cpp.extra_flags (which appends); build.extra_flags would wipe the
//  core's CORE_DEBUG_LEVEL and reintroduce an EspUsbHost 'TAG' error:
//    arduino-cli compile \
//      --fqbn "esp32:esp32:esp32s3:USBMode=hwcdc,CDCOnBoot=default,FlashSize=8M,\
//PartitionScheme=default_8MB,PSRAM=enabled,DebugLevel=error" \
//      --build-property "compiler.cpp.extra_flags=-DESP_USB_HOST_MAX_DEVICES=4 -DCORE_DEBUG_LEVEL=1" \
//      CardSatDualRig
//  In the IDE: board "ESP32S3 Dev Module", USB Mode = "Hardware CDC and JTAG",
//  Flash 8 MB, PSRAM enabled, Partition "8M with spiffs", Core Debug Level "Error".
// =============================================================================

#ifndef ESP_USB_HOST_MAX_DEVICES
#define ESP_USB_HOST_MAX_DEVICES 4      // hub + 2 radios (+headroom)
#endif

#include <M5Unified.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Preferences.h>
#include <EspUsbHost.h>
#include "catradio_types.h"

// Forward declarations for helpers defined later (used by the rigctld \csdr_*
// config escape, which lives in handleRigctlLine earlier in the file).
static bool   applyConfigKV(const String& key, const String& val);
static String statusJson();
static String devicesJson();
static String modelOptionsJson();
static void   saveConfig();
static void   applyLegConfig();

// ------------------------------------------------------------------- constants
static const uint32_t CONFIG_MAGIC = 0x43534452;   // "CSDR"
static const char*    AP_PREFIX    = "CardSatDualRig";
static const char*    AP_PASSWORD  = "cardsat123";  // >= 8 chars; change if you like
static const IPAddress AP_IP(192, 168, 4, 1);
static const uint8_t  BTN_HOLD_MS  = 60;            // debounce loops for a "long press"

// StickS3 Grove port is on GPIO9 / GPIO10. Cardputer TX -> Stick RX (G9),
// Cardputer RX <- Stick TX (G10). Both are 3.3 V, so no level shifter. If your
// wiring is reversed, swap these two.
static const int GROVE_RX_PIN = 9;
static const int GROVE_TX_PIN = 10;

// =============================================================================
//  Radio catalogue
// =============================================================================
// Default CAT baud / CI-V address / capability for every supported radio. All of
// these are just DEFAULTS - the per-leg config can override baud and CI-V address
// at runtime, so a radio with a non-standard address still works.
static const RadioProfile RADIO_TABLE[] = {
  //  model          name          family          baud   civAddr rxOnly
  { RIG_IC705,     "IC-705",     CAT_CIV,        19200, 0xA4,  false },
  { RIG_IC905,     "IC-905",     CAT_CIV,        19200, 0xAC,  false },
  { RIG_IC7100,    "IC-7100",    CAT_CIV,        19200, 0x88,  false },
  { RIG_IC7000,    "IC-7000",    CAT_CIV,        19200, 0x70,  false },
  { RIG_IC706MK2G, "IC-706MKIIG",CAT_CIV,         9600, 0x58,  false },
  { RIG_IC275,     "IC-275",     CAT_CIV,         9600, 0x10,  false },
  { RIG_IC475,     "IC-475",     CAT_CIV,         9600, 0x14,  false },
  // Icom receivers (all RX-only). Wideband all-mode sets cover VHF/UHF SSB/CW.
  { RIG_ICR10,     "IC-R10",     CAT_CIV,         9600, 0x52,  true  },
  { RIG_ICR20,     "IC-R20",     CAT_CIV,         9600, 0x6C,  true  },
  { RIG_ICR30,     "IC-R30",     CAT_CIV,         9600, 0x9C,  true  },
  { RIG_ICR7000,   "IC-R7000",   CAT_CIV,         1200, 0x08,  true  },
  { RIG_ICR7100,   "IC-R7100",   CAT_CIV,         9600, 0x34,  true  },
  { RIG_ICR8500,   "IC-R8500",   CAT_CIV,         9600, 0x4A,  true  },
  { RIG_ICR8600,   "IC-R8600",   CAT_CIV,        19200, 0x96,  true  },
  { RIG_ICR9000,   "IC-R9000",   CAT_CIV,         1200, 0x2A,  true  },
  { RIG_ICR9500,   "IC-R9500",   CAT_CIV,        19200, 0x72,  true  },
  { RIG_FT817,     "FT-817",     CAT_YAESU_BIN,   9600, 0x00,  false },
  { RIG_FT818,     "FT-818",     CAT_YAESU_BIN,   9600, 0x00,  false },
  { RIG_FT857,     "FT-857",     CAT_YAESU_BIN,   9600, 0x00,  false },
  { RIG_FT897,     "FT-897",     CAT_YAESU_BIN,   9600, 0x00,  false },
  { RIG_FT100,     "FT-100",     CAT_YAESU_BIN,   9600, 0x00,  false },
  // Yaesu VR-5000 wideband all-mode receiver. Uses the Yaesu 5-byte CAT family;
  // its opcodes are close to the FT-817's but VERIFY on hardware (see README).
  { RIG_VR5000,    "VR-5000",    CAT_YAESU_BIN,   9600, 0x00,  true  },
  { RIG_FT991,     "FT-991",     CAT_YAESU_TXT,  38400, 0x00,  false },
  { RIG_FT991A,    "FT-991A",    CAT_YAESU_TXT,  38400, 0x00,  false },
  { RIG_FTX1,      "FTX-1",      CAT_YAESU_TXT,  38400, 0x00,  false },
  // Kenwood handhelds: all-mode receiver on Band B; RX only here.
  { RIG_THD74,     "TH-D74",     CAT_KENWOOD_HT, 9600,  0x00,  true  },
  { RIG_THD75,     "TH-D75",     CAT_KENWOOD_HT, 9600,  0x00,  true  },
};
static const size_t RADIO_COUNT = sizeof(RADIO_TABLE) / sizeof(RADIO_TABLE[0]);

static const RadioProfile& profileOf(uint8_t model) {
  for (size_t i = 0; i < RADIO_COUNT; i++) if (RADIO_TABLE[i].model == model) return RADIO_TABLE[i];
  return RADIO_TABLE[0];
}

// ------------------------------------------------------------- mode name tables
static RigMode modeFromToken(const String& tok) {
  String t = tok; t.trim(); t.toUpperCase();
  if (t == "LSB")  return MODE_LSB;
  if (t == "USB")  return MODE_USB;
  if (t == "CW")   return MODE_CW;
  if (t == "CWR")  return MODE_CWR;
  if (t == "FM" || t == "FMN" || t == "WFM") return MODE_FM;
  if (t == "AM")   return MODE_AM;
  if (t == "PKTUSB" || t == "PKTLSB" || t == "PKTFM" || t == "DATA" || t == "USB-D" || t == "RTTY")
    return MODE_DATA;
  return MODE_USB;
}
static const char* modeToken(RigMode m) {
  switch (m) {
    case MODE_LSB: return "LSB"; case MODE_USB: return "USB";
    case MODE_CW:  return "CW";  case MODE_CWR: return "CWR";
    case MODE_FM:  return "FM";  case MODE_AM:  return "AM";
    case MODE_DATA:return "PKTUSB";
  }
  return "USB";
}
static long modePassband(RigMode m) {
  switch (m) { case MODE_FM: return 15000; case MODE_AM: return 6000;
               case MODE_CW: case MODE_CWR: return 500; default: return 2400; }
}

// =============================================================================
//  Config persistence (NVS via Preferences)
// =============================================================================
static Preferences gPrefs;
static AppConfig   gCfg;

static void loadConfig() {
  gPrefs.begin("csdr", true);                       // read-only
  size_t n = gPrefs.getBytesLength("cfg");
  if (n == sizeof(AppConfig)) {
    gPrefs.getBytes("cfg", &gCfg, sizeof(AppConfig));
    if (gCfg.magic != CONFIG_MAGIC) gCfg = AppConfig();   // corrupt -> defaults
  } else {
    gCfg = AppConfig();                             // first boot -> defaults (invalid)
  }
  gPrefs.end();
}
static void saveConfig() {
  gCfg.magic = CONFIG_MAGIC;
  gPrefs.begin("csdr", false);                      // read-write
  gPrefs.putBytes("cfg", &gCfg, sizeof(AppConfig));
  gPrefs.end();
}
static bool configValid() { return gCfg.magic == CONFIG_MAGIC && gCfg.downlink.model != 0xFF; }

// =============================================================================
//  USB transport + device registry
// =============================================================================
static EspUsbHost gUsb;

// Devices seen during enumeration (for the config portal to display/assign).
static const size_t MAX_SEEN = 6;
static SeenDevice gSeen[MAX_SEEN];

static void registerSeen(const EspUsbHostDeviceInfo& info) {
  // update-or-insert by address
  SeenDevice* slot = nullptr;
  for (auto& s : gSeen) if (s.used && s.address == info.address) { slot = &s; break; }
  if (!slot) for (auto& s : gSeen) if (!s.used) { slot = &s; break; }
  if (!slot) return;
  slot->used = true; slot->address = info.address;
  slot->vid = info.vid; slot->pid = info.pid; slot->isHub = info.isHub;
  strncpy(slot->product, info.product ? info.product : "", sizeof(slot->product) - 1);
  slot->product[sizeof(slot->product) - 1] = 0;
  strncpy(slot->serial, info.serial ? info.serial : "", sizeof(slot->serial) - 1);
  slot->serial[sizeof(slot->serial) - 1] = 0;
}
static void unregisterSeen(uint8_t address) {
  for (auto& s : gSeen) if (s.used && s.address == address) s.used = false;
}

// Per-radio RX ring: the USB host task delivers CAT bytes asynchronously; the
// rigctld loop drains them synchronously when it expects a reply.

// A radio bound to a USB device. Holds the effective profile (with per-leg baud /
// CI-V address overrides applied), the RX ring, and last-known values.

RadioPort gDown;   // VFOA - downlink / RX  (extern-declared in catradio_types.h)
RadioPort gUp;     // VFOB - uplink / TX

static RadioPort* portForAddr(uint8_t a) {
  if (gDown.bound && gDown.addr == a) return &gDown;
  if (gUp.bound   && gUp.addr   == a) return &gUp;
  return nullptr;
}

static bool gTrace = true;
static void traceCat(const char* tag, const RadioPort& p, const uint8_t* d, size_t n) {
  if (!gTrace) return;
  Serial.printf("[%s %s] ", p.legName, tag);
  for (size_t i = 0; i < n; i++) Serial.printf("%02X ", d[i]);
  Serial.println();
}

static bool catSend(RadioPort& p, const uint8_t* d, size_t n) {
  if (!p.bound || !p.online) return false;
  traceCat("TX", p, d, n);
  return gUsb.sendSerial(d, n, p.addr);
}
static bool catSendStr(RadioPort& p, const String& s) {
  return catSend(p, (const uint8_t*)s.c_str(), s.length());
}
static size_t catDrain(RadioPort& p, uint8_t* out, size_t maxOut,
                       uint32_t timeoutMs, int stopByte) {
  size_t n = 0; uint32_t t0 = millis();
  while ((millis() - t0) < timeoutMs && n < maxOut) {
    int c = p.rx.pop();
    if (c < 0) { delay(1); continue; }
    out[n++] = (uint8_t)c;
    if (stopByte >= 0 && c == stopByte) break;
  }
  if (n) traceCat("RX", p, out, n);
  return n;
}

// =============================================================================
//  CAT dialect: Icom CI-V  (transceivers + IC-R receivers)
// =============================================================================
static void civPackFreq(uint64_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; i++) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}
static uint64_t civUnpackFreq(const uint8_t* b) {
  uint64_t hz = 0;
  for (int i = 4; i >= 0; i--) hz = hz * 100 + (b[i] >> 4) * 10 + (b[i] & 0x0F);
  return hz;
}
static uint8_t civModeByte(RigMode m) {
  switch (m) { case MODE_LSB: return 0x00; case MODE_USB: return 0x01;
               case MODE_AM: return 0x02; case MODE_CW: return 0x03;
               case MODE_FM: return 0x05; case MODE_CWR: return 0x07;
               case MODE_DATA: return 0x01; default: return 0x01; }
}
static bool civSetFreq(RadioPort& p, uint64_t hz) {
  uint8_t f[5]; civPackFreq(hz, f);
  uint8_t fr[11] = { 0xFE,0xFE, p.civAddr, 0xE0, 0x05, f[0],f[1],f[2],f[3],f[4], 0xFD };
  return catSend(p, fr, sizeof(fr));
}
static bool civSetMode(RadioPort& p, RigMode m) {
  uint8_t fr[8] = { 0xFE,0xFE, p.civAddr, 0xE0, 0x06, civModeByte(m), 0x01, 0xFD };
  return catSend(p, fr, sizeof(fr));
}
static bool civReadFreq(RadioPort& p, uint64_t& hz) {
  p.rx.clear();
  uint8_t q[6] = { 0xFE,0xFE, p.civAddr, 0xE0, 0x03, 0xFD };
  if (!catSend(p, q, sizeof(q))) return false;
  uint8_t buf[64];
  size_t n = catDrain(p, buf, sizeof(buf), 180, 0xFD);
  for (size_t i = 0; i + 11 <= n; i++) {
    if (buf[i]==0xFE && buf[i+1]==0xFE && buf[i+2]==0xE0 &&
        buf[i+3]==p.civAddr && buf[i+4]==0x03 && buf[i+10]==0xFD) {
      hz = civUnpackFreq(&buf[i+5]);
      return hz > 0;
    }
  }
  return false;
}

// =============================================================================
//  CAT dialect: Yaesu "old" 5-byte binary (FT-817/818/857/897/FT-100)
// =============================================================================
static void yBinPackFreq(uint64_t hz, uint8_t out[4]) {
  uint32_t f = (uint32_t)(hz / 10);
  out[0] = (uint8_t)((((f/10000000)%10)<<4) | ((f/1000000)%10));
  out[1] = (uint8_t)((((f/100000)%10)<<4)   | ((f/10000)%10));
  out[2] = (uint8_t)((((f/1000)%10)<<4)     | ((f/100)%10));
  out[3] = (uint8_t)((((f/10)%10)<<4)       | (f%10));
}
static uint64_t yBinUnpackFreq(const uint8_t* b) {
  uint64_t f = 0;
  for (int i = 0; i < 4; i++) f = f * 100 + (b[i] >> 4) * 10 + (b[i] & 0x0F);
  return f * 10ULL;
}
static uint8_t yBinModeByte(RigMode m) {
  switch (m) { case MODE_LSB: return 0x00; case MODE_USB: return 0x01;
               case MODE_CW: return 0x02; case MODE_CWR: return 0x03;
               case MODE_AM: return 0x04; case MODE_FM: return 0x08;
               case MODE_DATA: return 0x0A; default: return 0x01; }
}
static bool yBinSetFreq(RadioPort& p, uint64_t hz) {
  uint8_t f[4]; yBinPackFreq(hz, f);
  uint8_t cmd[5] = { f[0], f[1], f[2], f[3], 0x01 };
  return catSend(p, cmd, sizeof(cmd));
}
static bool yBinSetMode(RadioPort& p, RigMode m) {
  uint8_t cmd[5] = { yBinModeByte(m), 0x00, 0x00, 0x00, 0x07 };
  return catSend(p, cmd, sizeof(cmd));
}
static bool yBinReadFreq(RadioPort& p, uint64_t& hz) {
  p.rx.clear();
  uint8_t cmd[5] = { 0x00, 0x00, 0x00, 0x00, 0x03 };
  if (!catSend(p, cmd, sizeof(cmd))) return false;
  uint8_t buf[8];
  size_t n = catDrain(p, buf, sizeof(buf), 180, -1);
  if (n < 5) return false;
  hz = yBinUnpackFreq(buf);
  return hz > 0;
}

// =============================================================================
//  CAT dialect: Yaesu "new" ASCII (FT-991/991A/FTX-1)
// =============================================================================
static char yTxtModeDigit(RigMode m) {
  switch (m) { case MODE_LSB: return '1'; case MODE_USB: return '2';
               case MODE_CW: return '3'; case MODE_FM: return '4';
               case MODE_AM: return '5'; case MODE_CWR: return '7';
               case MODE_DATA: return 'C'; default: return '2'; }
}
static bool yTxtSetFreq(RadioPort& p, uint64_t hz) {
  char b[20]; snprintf(b, sizeof(b), "FA%09llu;", (unsigned long long)hz);
  return catSendStr(p, b);
}
static bool yTxtSetMode(RadioPort& p, RigMode m) {
  char b[8]; snprintf(b, sizeof(b), "MD0%c;", yTxtModeDigit(m));
  return catSendStr(p, b);
}
static bool yTxtReadFreq(RadioPort& p, uint64_t& hz) {
  p.rx.clear();
  if (!catSendStr(p, "FA;")) return false;
  uint8_t buf[24];
  size_t n = catDrain(p, buf, sizeof(buf), 180, ';');
  for (size_t i = 0; i + 12 <= n; i++) {
    if (buf[i]=='F' && buf[i+1]=='A') {
      uint64_t v = 0; bool ok = true;
      for (int k = 2; k < 11; k++) { char c = buf[i+k]; if (c<'0'||c>'9'){ok=false;break;} v = v*10 + (c-'0'); }
      if (ok) { hz = v; return hz > 0; }
    }
  }
  return false;
}

// =============================================================================
//  CAT dialect: Kenwood TH-D74 / TH-D75 handheld  (all-mode receiver, RX only)
//  Set:  FQ<band>,<10-digit Hz><CR>     Read: FQ<band><CR> -> FQ<band>,<Hz><CR>
//  Mode: MD<band>,<n><CR>   (n: 0=FM 1=DV 2=AM 3=LSB 4=USB 5=CW 6=NFM ...)
//
//  IMPORTANT: the DSP all-mode receiver (SSB/CW/AM) lives on *Band B* only -- Band A
//  offers just FM/DV. For satellite downlink (SSB/CW) we therefore drive Band B.
//  Band digit: 0 = Band A, 1 = Band B. Mode values verified against the CHIRP
//  TH-D74 driver order [FM,DV,AM,LSB,USB,CW,NFM]. These are receive radios; PTT is
//  handled manually by the operator, so no TX/RX keying is sent.
// =============================================================================
static const char KWHT_BAND = '1';        // Band B = the all-mode (SSB/CW/AM) receiver
static char kwHtModeDigit(RigMode m) {
  switch (m) { case MODE_FM: return '0'; case MODE_AM: return '2';
               case MODE_LSB: return '3'; case MODE_USB: return '4';
               case MODE_CW: return '5';  case MODE_DATA: return '1'; /* DV */
               default: return '4'; }
}
static bool kwHtSetFreq(RadioPort& p, uint64_t hz) {
  char b[24]; snprintf(b, sizeof(b), "FQ%c,%010llu\r", KWHT_BAND, (unsigned long long)hz);
  return catSendStr(p, b);
}
static bool kwHtSetMode(RadioPort& p, RigMode m) {
  char b[12]; snprintf(b, sizeof(b), "MD%c,%c\r", KWHT_BAND, kwHtModeDigit(m));
  return catSendStr(p, b);
}
static bool kwHtReadFreq(RadioPort& p, uint64_t& hz) {
  p.rx.clear();
  char q[8]; snprintf(q, sizeof(q), "FQ%c\r", KWHT_BAND);
  if (!catSendStr(p, q)) return false;
  uint8_t buf[40];
  size_t n = catDrain(p, buf, sizeof(buf), 200, '\r');
  // Expect "FQ<band>,<10 digits>"
  for (size_t i = 0; i + 4 <= n; i++) {
    if (buf[i]=='F' && buf[i+1]=='Q') {
      // find the comma, then read digits
      size_t j = i + 2;
      while (j < n && buf[j] != ',') j++;
      j++;                                    // skip comma
      uint64_t v = 0; int digits = 0;
      while (j < n && buf[j] >= '0' && buf[j] <= '9') { v = v*10 + (buf[j]-'0'); j++; digits++; }
      if (digits >= 6) { hz = v; return hz > 0; }
    }
  }
  return false;
}

// =============================================================================
//  Dialect dispatch
// =============================================================================
static bool radioSetFreq(RadioPort& p, uint64_t hz) {
  if (!p.bound || !p.online) return false;
  bool ok = false;
  switch (p.prof->family) {
    case CAT_CIV:        ok = civSetFreq(p, hz);  break;
    case CAT_YAESU_BIN:  ok = yBinSetFreq(p, hz); break;
    case CAT_YAESU_TXT:  ok = yTxtSetFreq(p, hz); break;
    case CAT_KENWOOD_HT: ok = kwHtSetFreq(p, hz); break;
  }
  if (ok) p.lastFreq = hz;
  return ok;
}
static bool radioSetMode(RadioPort& p, RigMode m) {
  if (!p.bound || !p.online) return false;
  bool ok = false;
  switch (p.prof->family) {
    case CAT_CIV:        ok = civSetMode(p, m);  break;
    case CAT_YAESU_BIN:  ok = yBinSetMode(p, m); break;
    case CAT_YAESU_TXT:  ok = yTxtSetMode(p, m); break;
    case CAT_KENWOOD_HT: ok = kwHtSetMode(p, m); break;
  }
  if (ok) p.lastMode = m;
  return ok;
}
// PTT is handled MANUALLY by the operator (front-panel / footswitch), so the Stick
// never keys a radio over CAT. We only track the state a client may set/read via
// rigctld, and never send a transmit command down the wire.
static bool radioSetPtt(RadioPort& p, bool tx) {
  if (!p.assigned()) return false;
  if (p.prof->rxOnly && tx) return false;    // can't "transmit" a receiver, even nominally
  p.ptt = tx;                                // state only - no CAT emitted
  return true;
}
static uint64_t radioReadFreq(RadioPort& p) {
  if (p.bound && p.online) {
    uint64_t hz = 0; bool ok = false;
    switch (p.prof->family) {
      case CAT_CIV:        ok = civReadFreq(p, hz);  break;
      case CAT_YAESU_BIN:  ok = yBinReadFreq(p, hz); break;
      case CAT_YAESU_TXT:  ok = yTxtReadFreq(p, hz); break;
      case CAT_KENWOOD_HT: ok = kwHtReadFreq(p, hz); break;
    }
    if (ok) { p.lastFreq = hz; return hz; }
  }
  return p.lastFreq;
}

// =============================================================================
//  Leg configuration + USB device -> leg binding
// =============================================================================
// Resolve each leg's effective profile/baud/CI-V address from the saved config.
static void applyLegConfig() {
  gDown.leg = &gCfg.downlink; gDown.legName = "DN";
  gUp.leg   = &gCfg.uplink;   gUp.legName   = "UP";
  for (RadioPort* p : { &gDown, &gUp }) {
    if (!p->assigned()) { p->prof = &RADIO_TABLE[0]; continue; }
    p->prof    = &profileOf(p->leg->model);
    p->civAddr = p->leg->civAddr ? p->leg->civAddr : p->prof->civAddr;
    p->baud    = p->leg->baud    ? p->leg->baud    : p->prof->baud;
  }
}

// Try to bind a newly-enumerated device to a leg. Preference order:
//   1) a leg whose configured usbSerial matches this device's serial (robust pin)
//   2) otherwise the first still-unbound, assigned leg (by enumeration order)
static void bindDevice(const EspUsbHostDeviceInfo& info) {
  if (info.isHub) return;
  const char* ser = info.serial ? info.serial : "";

  auto tryPin = [&](RadioPort& p) -> bool {
    if (!p.assigned() || p.bound) return false;
    if (p.leg->usbSerial[0] && ser[0] && strcmp(p.leg->usbSerial, ser) == 0) {
      p.bound = true; p.addr = info.address;
      gUsb.setSerialBaudRate(p.baud, p.addr);
      Serial.printf("[USB] pinned %s (serial %s) -> %s\n", p.prof->name, ser, p.legName);
      return true;
    }
    return false;
  };
  if (tryPin(gDown) || tryPin(gUp)) return;

  // Fall back to enumeration order for legs without a serial pin.
  auto tryOrder = [&](RadioPort& p) -> bool {
    if (!p.assigned() || p.bound || p.leg->usbSerial[0]) return false;
    p.bound = true; p.addr = info.address;
    gUsb.setSerialBaudRate(p.baud, p.addr);
    Serial.printf("[USB] bound %s (addr %u) -> %s\n", p.prof->name, info.address, p.legName);
    return true;
  };
  if (tryOrder(gDown)) return;
  tryOrder(gUp);
}

static void onUsbConnected(const EspUsbHostDeviceInfo& info) {
  Serial.printf("[USB] up: addr %u VID %04X PID %04X hub=%d '%s' ser '%s'\n",
                info.address, info.vid, info.pid, info.isHub,
                info.product ? info.product : "", info.serial ? info.serial : "");
  registerSeen(info);
  bindDevice(info);
}
static void onUsbDisconnected(const EspUsbHostDeviceInfo& info) {
  unregisterSeen(info.address);
  RadioPort* p = portForAddr(info.address);
  if (p) { p->bound = false; p->online = false; p->addr = ESP_USB_HOST_ANY_ADDRESS;
           Serial.printf("[USB] %s radio removed\n", p->legName); }
}
static void onUsbSerialData(const EspUsbHostSerialData& d) {
  RadioPort* p = portForAddr(d.address);
  if (p) p->rx.push(d.data, d.length);
}
static void pollRadioOnline() {
  if (gDown.bound) gDown.online = gUsb.serialReady(gDown.addr);
  if (gUp.bound)   gUp.online   = gUsb.serialReady(gUp.addr);
}

// =============================================================================
//  rigctld protocol - shared by every transport (TCP + Grove UART)
// =============================================================================
// Each transport has a small session (its own selected VFO + line buffer), so two
// clients never fight over VFO state. Responses go to the session's Stream.

static void sessSelectVfo(RigctlSession& s, const String& a) {
  String t = a; t.toUpperCase();
  if (t.indexOf("VFOB") >= 0 || t.indexOf("SUB") >= 0 || t.indexOf("TX") >= 0) s.vfo = 1;
  else if (t.indexOf("VFOA") >= 0 || t.indexOf("MAIN") >= 0 || t.indexOf("RX") >= 0) s.vfo = 0;
}

static void handleRigctlLine(RigctlSession& s, const String& lineIn) {
  String line = lineIn; line.trim();
  if (!line.length()) return;
  Stream& io = *s.io;
  auto rprt = [&](int code) { io.printf("RPRT %d\n", code); };

  if (line.startsWith("\\chk_vfo"))       { io.print("CHKVFO 0\n"); return; }
  if (line.startsWith("\\dump_state")) {
    io.print("0\n0\n2\n"
      "150000.000000 1500000000.000000 0x1ff -1 -1 0x10000003 0x3\n0 0 0 0 0 0 0\n"
      "150000.000000 1500000000.000000 0x1ff -1 -1 0x10000003 0x3\n0 0 0 0 0 0 0\n"
      "0 0\n0 0\n0\n0\n0\n0\n0x0\n0x0\n0x0\n0x0\n0x0\n0x0\n");
    return;
  }
  if (line.startsWith("\\get_powerstat")) { io.print("1\n"); return; }

  // --- CardSatDualRig vendor config escape (over ANY transport, incl. Grove) ---
  // Lets CardSat configure the Stick over the same link, no phone/portal needed.
  //   \csdr_get                 -> one line of JSON status (config + USB devices)
  //   \csdr_devices             -> one line of JSON: enumerated USB devices
  //   \csdr_set k=v k=v ...      -> apply config keys (same names as the HTTP API),
  //                                plus optional save=1 / reboot=1 ; -> "RPRT 0"
  //   \csdr_save                -> persist current config to NVS ; -> "RPRT 0"
  if (line.startsWith("\\csdr_get"))     { io.print(statusJson());  io.print("\n"); return; }
  if (line.startsWith("\\csdr_devices")) { io.print(devicesJson()); io.print("\n"); return; }
  if (line.startsWith("\\csdr_models"))  { io.print(modelOptionsJson()); io.print("\n"); return; }
  if (line.startsWith("\\csdr_save"))    { saveConfig(); io.print("RPRT 0\n"); return; }
  if (line.startsWith("\\csdr_set")) {
    String rest = line.substring(9); rest.trim();
    bool doSave = false, doReboot = false;
    int i = 0;
    while (i < (int)rest.length()) {
      int sp = rest.indexOf(' ', i);
      String tok = (sp < 0) ? rest.substring(i) : rest.substring(i, sp);
      i = (sp < 0) ? rest.length() : sp + 1;
      tok.trim(); if (!tok.length()) continue;
      int eq = tok.indexOf('=');
      if (eq < 0) continue;
      String k = tok.substring(0, eq), v = tok.substring(eq + 1);
      if      (k == "save")   doSave   = v.toInt() != 0;
      else if (k == "reboot") doReboot = v.toInt() != 0;
      else applyConfigKV(k, v);
    }
    applyLegConfig();
    if (doSave) saveConfig();
    io.print("RPRT 0\n");
    if (doReboot) { delay(200); ESP.restart(); }
    return;
  }

  char c = line.charAt(0);
  String arg = (line.length() > 1) ? line.substring(1) : "";
  arg.trim();

  switch (c) {
    case 'F': { uint64_t hz = strtoull(arg.c_str(), nullptr, 10);
                rprt(radioSetFreq(s.cur(), hz) ? 0 : -1); } break;
    case 'f': io.printf("%llu\n", (unsigned long long)radioReadFreq(s.cur())); break;
    case 'I': { uint64_t hz = strtoull(arg.c_str(), nullptr, 10);
                rprt(radioSetFreq(gUp, hz) ? 0 : -1); } break;
    case 'i': io.printf("%llu\n", (unsigned long long)radioReadFreq(gUp)); break;

    case 'M': { String tok = arg; int sp = tok.indexOf(' '); if (sp>=0) tok = tok.substring(0,sp);
                rprt(radioSetMode(s.cur(), modeFromToken(tok)) ? 0 : -1); } break;
    case 'm': { RigMode md = s.cur().lastMode; io.printf("%s\n%ld\n", modeToken(md), modePassband(md)); } break;
    case 'X': { String tok = arg; int sp = tok.indexOf(' '); if (sp>=0) tok = tok.substring(0,sp);
                rprt(radioSetMode(gUp, modeFromToken(tok)) ? 0 : -1); } break;
    case 'x': io.printf("%s\n%ld\n", modeToken(gUp.lastMode), modePassband(gUp.lastMode)); break;

    case 'V': sessSelectVfo(s, arg); rprt(0); break;
    case 'v': io.printf("%s\n", s.vfoTok()); break;

    case 'S': { int sp = arg.indexOf(' '); if (sp>=0) sessSelectVfo(s, arg.substring(sp+1)); rprt(0); } break;
    case 's': io.print("1\nVFOB\n"); break;

    case 'T': { bool tx = (arg.toInt() != 0); rprt(radioSetPtt(gUp, tx) ? 0 : -1); } break;
    case 't': io.printf("%d\n", gUp.ptt ? 1 : 0); break;

    case 'q': case 'Q': s.wantClose = true; break;   // TCP transport closes; Grove ignores
    case '_': io.print("CardSatDualRig\n"); break;
    default:  rprt(-11); break;
  }
}

// Pump bytes from a Stream into a session, dispatching each complete line.
static void pumpSession(RigctlSession& s) {
  if (!s.io) return;
  int guard = 0;
  while (s.io->available() && guard++ < 512) {
    char ch = (char)s.io->read();
    if (ch == '\r') continue;
    if (ch == '\n') { handleRigctlLine(s, s.line); s.line = ""; }
    else if (s.line.length() < 128) s.line += ch;
  }
}

// --- TCP transport ---
static WiFiServer   gTcp(4532);
static WiFiClient   gTcpClient;
static RigctlSession gTcpSess;

static void serviceTcp() {
  if (!gCfg.wifiOn) return;
  if (!gTcpClient || !gTcpClient.connected()) {
    WiFiClient nc = gTcp.available();
    if (nc) { gTcpClient = nc; gTcpClient.setNoDelay(true);
              gTcpSess.io = &gTcpClient; gTcpSess.line = ""; }
  }
  if (gTcpClient && gTcpClient.connected()) {
    gTcpSess.io = &gTcpClient; pumpSession(gTcpSess);
    if (gTcpSess.wantClose) { gTcpClient.stop(); gTcpSess.wantClose = false; }
  }
}

// --- Grove UART transport (rigctld over the Grove cable to the Cardputer) ---
static HardwareSerial gGrove(1);
static RigctlSession  gGroveSess;

static void serviceGrove() {
  gGroveSess.io = &gGrove;
  pumpSession(gGroveSess);
}

// =============================================================================
//  Config mode: Wi-Fi AP + captive portal + HTTP API
// =============================================================================
static WebServer gWeb(80);
static DNSServer gDns;
static bool      gConfigMode = false;
static String    gApSsid;

// Build the model <option> list (shared by the web form and GET /api/models).
static String modelOptionsJson() {
  String j = "[";
  for (size_t i = 0; i < RADIO_COUNT; i++) {
    if (i) j += ",";
    j += "{\"id\":"; j += RADIO_TABLE[i].model;
    j += ",\"name\":\""; j += RADIO_TABLE[i].name;
    j += "\",\"rxOnly\":"; j += RADIO_TABLE[i].rxOnly ? "true" : "false";
    j += ",\"civ\":"; j += RADIO_TABLE[i].civAddr; j += "}";
  }
  j += "]";
  return j;
}
static String devicesJson() {
  String j = "[";
  bool first = true;
  for (auto& d : gSeen) {
    if (!d.used || d.isHub) continue;
    if (!first) j += ","; first = false;
    j += "{\"addr\":"; j += d.address;
    char vp[16]; snprintf(vp, sizeof(vp), "%04X:%04X", d.vid, d.pid);
    j += ",\"vidpid\":\""; j += vp;
    j += "\",\"product\":\""; j += d.product;
    j += "\",\"serial\":\""; j += d.serial; j += "\"}";
  }
  j += "]";
  return j;
}
static String legJson(const LegConfig& L) {
  String j = "{";
  j += "\"model\":"; j += (L.model == 0xFF ? -1 : (int)L.model);
  j += ",\"civ\":"; j += L.civAddr;
  j += ",\"baud\":"; j += L.baud;
  j += ",\"serial\":\""; j += L.usbSerial; j += "\"}";
  return j;
}
static String statusJson() {
  String j = "{";
  j += "\"mode\":\""; j += (gConfigMode ? "config" : "run"); j += "\",";
  j += "\"ap\":\""; j += gApSsid; j += "\",";
  j += "\"sta_ip\":\""; j += (WiFi.status()==WL_CONNECTED ? WiFi.localIP().toString() : String("")); j += "\",";
  j += "\"tcpPort\":"; j += gCfg.tcpPort; j += ",";
  j += "\"wifiOn\":"; j += gCfg.wifiOn ? "true":"false"; j += ",";
  j += "\"groveBaud\":"; j += gCfg.groveBaud; j += ",";
  j += "\"downlink\":"; j += legJson(gCfg.downlink); j += ",";
  j += "\"uplink\":"; j += legJson(gCfg.uplink); j += ",";
  j += "\"devices\":"; j += devicesJson();
  j += "}";
  return j;
}

// Apply an incoming argument set (from the web form OR a bare HTTP request) to a
// leg. Prefix is "dl" or "ul". Only provided args change; missing args are kept.
static void applyLegArgs(LegConfig& L, const String& pfx) {
  if (gWeb.hasArg(pfx + "_model")) {
    int m = gWeb.arg(pfx + "_model").toInt();
    L.model = (m < 0 || m >= RIG_MODEL_COUNT) ? 0xFF : (uint8_t)m;
  }
  if (gWeb.hasArg(pfx + "_civ"))  L.civAddr = (uint8_t)strtol(gWeb.arg(pfx + "_civ").c_str(), nullptr, 16);
  if (gWeb.hasArg(pfx + "_baud")) L.baud    = (uint32_t)gWeb.arg(pfx + "_baud").toInt();
  if (gWeb.hasArg(pfx + "_serial")) {
    strncpy(L.usbSerial, gWeb.arg(pfx + "_serial").c_str(), sizeof(L.usbSerial)-1);
    L.usbSerial[sizeof(L.usbSerial)-1] = 0;
  }
}

// -------- transport-neutral config applier (shared by HTTP + the Grove/TCP escape)
// Apply a single key=value pair to the live config. Returns true if the key was
// recognized. This is the one place config keys are defined; both the HTTP handler
// and the rigctl \csdr_set escape funnel through it.
static bool applyConfigKV(const String& key, const String& val) {
  auto legKV = [&](LegConfig& L, const char* suffix) -> bool {
    if (key.endsWith(suffix)) {
      String s = suffix;
      if (s == "_model")  { int m = val.toInt(); L.model = (m < 0 || m >= RIG_MODEL_COUNT) ? 0xFF : (uint8_t)m; return true; }
      if (s == "_civ")    { L.civAddr = (uint8_t)strtol(val.c_str(), nullptr, 16); return true; }
      if (s == "_baud")   { L.baud = (uint32_t)val.toInt(); return true; }
      if (s == "_serial") { strncpy(L.usbSerial, val.c_str(), sizeof(L.usbSerial)-1); L.usbSerial[sizeof(L.usbSerial)-1]=0; return true; }
    }
    return false;
  };
  if (key == "ssid")      { strncpy(gCfg.ssid, val.c_str(), sizeof(gCfg.ssid)-1); gCfg.ssid[sizeof(gCfg.ssid)-1]=0; return true; }
  if (key == "pass")      { strncpy(gCfg.pass, val.c_str(), sizeof(gCfg.pass)-1); gCfg.pass[sizeof(gCfg.pass)-1]=0; return true; }
  if (key == "tcpport")   { gCfg.tcpPort = (uint16_t)val.toInt(); return true; }
  if (key == "wifi")      { gCfg.wifiOn = val.toInt() != 0; return true; }
  if (key == "grovebaud") { gCfg.groveBaud = (uint32_t)val.toInt(); return true; }
  if (key.startsWith("dl")) return legKV(gCfg.downlink, key.substring(2).c_str());
  if (key.startsWith("ul")) return legKV(gCfg.uplink,   key.substring(2).c_str());
  return false;
}

// POST/GET /api/config - set any subset of the configuration. Works as an HTML
// form target AND as a scriptable endpoint (all params optional):
//   ssid, pass, tcpport, wifi(0|1), grovebaud,
//   dl_model, dl_civ(hex), dl_baud, dl_serial,  ul_model, ul_civ, ul_baud, ul_serial,
//   save(1) to persist, reboot(1) to restart into run mode.
static void handleApiConfig() {
  if (gWeb.hasArg("ssid")) { strncpy(gCfg.ssid, gWeb.arg("ssid").c_str(), sizeof(gCfg.ssid)-1); gCfg.ssid[sizeof(gCfg.ssid)-1]=0; }
  if (gWeb.hasArg("pass")) { strncpy(gCfg.pass, gWeb.arg("pass").c_str(), sizeof(gCfg.pass)-1); gCfg.pass[sizeof(gCfg.pass)-1]=0; }
  if (gWeb.hasArg("tcpport"))   gCfg.tcpPort   = (uint16_t)gWeb.arg("tcpport").toInt();
  if (gWeb.hasArg("wifi"))      gCfg.wifiOn    = gWeb.arg("wifi").toInt() != 0;
  if (gWeb.hasArg("grovebaud")) gCfg.groveBaud = (uint32_t)gWeb.arg("grovebaud").toInt();
  applyLegArgs(gCfg.downlink, "dl");
  applyLegArgs(gCfg.uplink,   "ul");

  bool doSave   = gWeb.hasArg("save")   && gWeb.arg("save").toInt()   != 0;
  bool doReboot = gWeb.hasArg("reboot") && gWeb.arg("reboot").toInt() != 0;
  if (doSave) saveConfig();
  applyLegConfig();                      // re-resolve effective profiles live

  gWeb.send(200, "application/json", statusJson());
  if (doReboot) { delay(300); ESP.restart(); }
}

// The captive-portal page: a single self-contained HTML/JS file (no external
// resources, so it works with no internet). It polls /api/status for the live
// device list and posts back to /api/config.
static const char PORTAL_HTML[] PROGMEM = R"HTML(
<!doctype html><html><head><meta name=viewport content="width=device-width,initial-scale=1">
<title>CardSat Dual-Rig</title><style>
body{font-family:system-ui,sans-serif;margin:0;background:#101418;color:#e8eef2}
.wrap{max-width:640px;margin:0 auto;padding:16px}
h1{font-size:20px;color:#4fd0ff}h2{font-size:15px;color:#9fb3c8;margin:18px 0 6px}
label{display:block;font-size:13px;margin:8px 0 2px;color:#c5d2de}
input,select{width:100%;box-sizing:border-box;padding:8px;border-radius:6px;border:1px solid #2c3742;background:#182029;color:#e8eef2}
.row{display:flex;gap:12px}.row>div{flex:1}
.card{background:#161d25;border:1px solid #232d38;border-radius:10px;padding:12px;margin:10px 0}
button{margin-top:14px;padding:10px 16px;border:0;border-radius:8px;background:#1f8fff;color:#fff;font-size:15px;cursor:pointer}
.dev{font-size:12px;color:#8aa0b3;background:#0d141b;border:1px solid #202a34;border-radius:6px;padding:6px 8px;margin:4px 0}
.rx{color:#ffb454}.small{font-size:12px;color:#8aa0b3}
</style></head><body><div class=wrap>
<h1>CardSat Dual-Rig setup</h1>
<div class=small id=status>loading...</div>
<h2>Plugged-in radios (USB)</h2><div id=devs class=small>scanning...</div>
<form id=f>
<h2>Home Wi-Fi (for run mode)</h2>
<div class=row><div><label>SSID</label><input name=ssid id=ssid></div>
<div><label>Password</label><input name=pass id=pass type=password></div></div>
<div class=row><div><label>rigctld TCP port</label><input name=tcpport id=tcpport value=4532></div>
<div><label>Wi-Fi in run mode</label><select name=wifi id=wifi><option value=1>On (TCP)</option><option value=0>Off (Grove only)</option></select></div></div>
<label>Grove UART baud (must match CardSat)</label><input name=grovebaud id=grovebaud value=115200>

<div class=card><h2>Downlink radio (VFOA / RX)</h2>
<label>Model</label><select name=dl_model id=dl_model></select>
<div class=row><div><label>CI-V addr (hex, blank=default)</label><input name=dl_civ id=dl_civ></div>
<div><label>CAT baud (blank=default)</label><input name=dl_baud id=dl_baud></div></div>
<label>Pin to USB device</label><select name=dl_serial id=dl_serial></select></div>

<div class=card><h2>Uplink radio (VFOB / TX)</h2>
<label>Model</label><select name=ul_model id=ul_model></select>
<div class=row><div><label>CI-V addr (hex, blank=default)</label><input name=ul_civ id=ul_civ></div>
<div><label>CAT baud (blank=default)</label><input name=ul_baud id=ul_baud></div></div>
<label>Pin to USB device</label><select name=ul_serial id=ul_serial></select></div>

<button type=button onclick=save(0)>Apply (no reboot)</button>
<button type=button onclick=save(1)>Save &amp; run</button>
</form></div>
<script>
let models=[];
function opt(v,t){let o=document.createElement('option');o.value=v;o.textContent=t;return o}
async function refresh(){
 let s=await (await fetch('/api/status')).json();
 document.getElementById('status').textContent='mode: '+s.mode+(s.sta_ip?('  IP '+s.sta_ip):'')+'  AP '+s.ap;
 let d=document.getElementById('devs');d.innerHTML='';
 let serSels=[document.getElementById('dl_serial'),document.getElementById('ul_serial')];
 serSels.forEach(sel=>{sel.innerHTML='';sel.appendChild(opt('','(any, by plug order)'))});
 if(!s.devices.length)d.textContent='none yet - plug the radios into the hub';
 s.devices.forEach(x=>{
   let e=document.createElement('div');e.className='dev';
   e.textContent='addr '+x.addr+'  '+x.vidpid+'  '+(x.product||'')+(x.serial?('  ser:'+x.serial):'  (no serial)');
   d.appendChild(e);
   if(x.serial)serSels.forEach(sel=>sel.appendChild(opt(x.serial,(x.product||x.vidpid)+' ['+x.serial+']')));
 });
 // fill scalar fields once
 if(!window._init){
   ssid.value=''; tcpport.value=s.tcpPort; grovebaud.value=s.groveBaud; wifi.value=s.wifiOn?1:0;
   window._init=true;
 }
 setLeg('dl',s.downlink);setLeg('ul',s.uplink);
}
function setLeg(p,L){
 let m=document.getElementById(p+'_model');
 if(L.model>=0)m.value=L.model;
 if(document.activeElement.id!=p+'_civ')document.getElementById(p+'_civ').value=L.civ?L.civ.toString(16):'';
 if(document.activeElement.id!=p+'_baud')document.getElementById(p+'_baud').value=L.baud||'';
 if(L.serial)document.getElementById(p+'_serial').value=L.serial;
}
async function loadModels(){
 models=await (await fetch('/api/models')).json();
 ['dl_model','ul_model'].forEach(id=>{let sel=document.getElementById(id);sel.innerHTML='';
   sel.appendChild(opt(-1,'(unassigned)'));
   models.forEach(m=>sel.appendChild(opt(m.id,m.name+(m.rxOnly?' (RX only)':''))));});
}
async function save(reboot){
 let fd=new URLSearchParams(new FormData(document.getElementById('f')));
 fd.set('save',reboot?1:1);if(reboot)fd.set('reboot',1);
 let r=await fetch('/api/config',{method:'POST',body:fd});
 let s=await r.json();alert(reboot?'Saved. Rebooting into run mode.':'Applied.');
}
loadModels().then(refresh);setInterval(refresh,2000);
</script></body></html>
)HTML";

static void handleRoot()     { gWeb.send_P(200, "text/html", PORTAL_HTML); }
static void handleModels()   { gWeb.send(200, "application/json", modelOptionsJson()); }
static void handleStatus()   { gWeb.send(200, "application/json", statusJson()); }
static void handleDevices()  { gWeb.send(200, "application/json", devicesJson()); }
// Captive-portal redirect: send every unknown host to the config page.
static void handleCaptive()  { gWeb.sendHeader("Location", String("http://") + AP_IP.toString()); gWeb.send(302, "text/plain", ""); }

static void startWebRoutes() {
  gWeb.on("/", handleRoot);
  gWeb.on("/api/status", handleStatus);
  gWeb.on("/api/models", handleModels);
  gWeb.on("/api/devices", handleDevices);
  gWeb.on("/api/config", handleApiConfig);        // GET or POST
  gWeb.on("/api/reboot", [](){ gWeb.send(200,"text/plain","rebooting"); delay(200); ESP.restart(); });
  // Common captive-portal probe URLs -> redirect to the portal.
  gWeb.on("/generate_204", handleCaptive);
  gWeb.on("/hotspot-detect.html", handleCaptive);
  gWeb.on("/connecttest.txt", handleCaptive);
  gWeb.on("/ncsi.txt", handleCaptive);
  gWeb.onNotFound(handleCaptive);
  gWeb.begin();
}

// =============================================================================
//  Mode entry / exit + display + setup/loop
// =============================================================================
static void enterConfigMode() {
  gConfigMode = true;
  if (gTcpClient) gTcpClient.stop();
  gTcp.stop();
  WiFi.mode(WIFI_AP);
  uint8_t mac[6]; WiFi.macAddress(mac);
  char ssid[40]; snprintf(ssid, sizeof(ssid), "%s-%02X%02X", AP_PREFIX, mac[4], mac[5]);
  gApSsid = ssid;
  WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255,255,255,0));
  WiFi.softAP(ssid, AP_PASSWORD);
  gDns.start(53, "*", AP_IP);             // captive portal: all DNS -> us
  startWebRoutes();
  Serial.printf("[CFG] AP '%s' pass '%s'  http://%s\n", ssid, AP_PASSWORD, AP_IP.toString().c_str());
}

static void enterRunMode() {
  gConfigMode = false;
  applyLegConfig();
  if (gCfg.wifiOn && gCfg.ssid[0]) {
    WiFi.mode(WIFI_STA);
    WiFi.begin(gCfg.ssid, gCfg.pass);
    gTcp = WiFiServer(gCfg.tcpPort);
    gTcp.begin(); gTcp.setNoDelay(true);
    Serial.printf("[RUN] joining '%s', rigctld TCP :%u\n", gCfg.ssid, gCfg.tcpPort);
  } else {
    WiFi.mode(WIFI_OFF);
    Serial.println("[RUN] headless (Grove-only), Wi-Fi off");
  }
}

// ------------------------------------------------------------------ display
static uint32_t gLastDraw = 0;
static void legLine(int y, RadioPort& p) {
  auto& lcd = M5.Display;
  uint16_t col = !p.assigned() ? TFT_DARKGREY : (p.online ? TFT_GREEN : (p.bound ? TFT_ORANGE : TFT_RED));
  lcd.setTextColor(col, TFT_BLACK);
  lcd.setCursor(2, y);
  const char* nm = p.assigned() ? p.prof->name : "(none)";
  lcd.printf("%s %-11s %s", p.legName, nm, p.online ? "ok" : (p.bound ? "wait" : (p.assigned()?"--":"")));
  lcd.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  lcd.setCursor(2, y + 10);
  lcd.printf("  %10.4f %s%s", p.lastFreq/1e6, modeToken(p.lastMode),
             (&p == &gUp && p.ptt) ? " TX" : "");
}
static void drawConfig() {
  auto& lcd = M5.Display; lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_YELLOW, TFT_BLACK); lcd.setTextSize(1);
  lcd.setCursor(2, 2);  lcd.print("CONFIG MODE");
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(2, 16); lcd.printf("AP: %s", gApSsid.c_str());
  lcd.setCursor(2, 26); lcd.printf("pass: %s", AP_PASSWORD);
  lcd.setCursor(2, 36); lcd.printf("http://%s", AP_IP.toString().c_str());
  int nd = 0; for (auto& d : gSeen) if (d.used && !d.isHub) nd++;
  lcd.setTextColor(TFT_CYAN, TFT_BLACK);
  lcd.setCursor(2, 52); lcd.printf("USB radios seen: %d", nd);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setCursor(2, 70); lcd.print("Join AP, open the page,");
  lcd.setCursor(2, 80); lcd.print("assign radios, Save & run.");
}
static void drawRun() {
  auto& lcd = M5.Display; lcd.fillScreen(TFT_BLACK);
  lcd.setTextColor(TFT_CYAN, TFT_BLACK); lcd.setTextSize(1);
  lcd.setCursor(2, 2); lcd.print("CardSat Dual-Rig");
  lcd.setTextColor(TFT_WHITE, TFT_BLACK);
  lcd.setCursor(2, 14);
  if (gCfg.wifiOn) {
    if (WiFi.status()==WL_CONNECTED) lcd.printf("IP %s :%u", WiFi.localIP().toString().c_str(), gCfg.tcpPort);
    else lcd.print("Wi-Fi: connecting...");
  } else lcd.print("Grove-only (no Wi-Fi)");
  lcd.setCursor(2, 24);
  bool cli = (gTcpClient && gTcpClient.connected());
  lcd.printf("TCP %s  Grove @%lu", cli ? "CLIENT" : (gCfg.wifiOn?"listen":"off"), (unsigned long)gCfg.groveBaud);
  legLine(40, gDown);
  legLine(64, gUp);
  lcd.setTextColor(TFT_DARKGREY, TFT_BLACK);
  lcd.setCursor(2, 92); lcd.print("hold BtnA: reconfigure");
}

// ------------------------------------------------------------------ buttons
static bool btnAHeldAtBoot() {
  // sample Button A briefly at startup
  for (int i = 0; i < 8; i++) { M5.update(); if (M5.BtnA.isPressed()) { delay(20); } else return false; delay(20); }
  return M5.BtnA.isPressed();
}

void setup() {
  auto cfg = M5.config();
  // SAFETY (see the note below): M5Unified's cfg.output_power defaults to TRUE,
  // which makes M5.begin() drive EXT_5V as an OUTPUT. Clear it BEFORE begin() so
  // the Stick never even momentarily sources 5 V on the Grove line during init.
  cfg.output_power = false;
  M5.begin(cfg);

  // ---- SAFETY: force the Grove / EXT_5V rail to INPUT (never source 5 V) -----
  // Intended power topology when tethered to a Cardputer over Grove:
  //   Cardputer Grove = 5V OUTPUT (set by its physical slide switch)  --->  feeds
  //   Stick     Grove = 5V INPUT  (this call)                          <---  5 V in
  // If BOTH ends drove 5 V, the two supplies would fight on one wire -- the
  // short-circuit / hardware-damage case M5Stack explicitly warns about. Forcing
  // INPUT here guarantees the Stick only ever *receives* 5 V on Grove, so it is
  // safe no matter how the Cardputer switch is set. NOTE: M5Unified's default is
  // actually OUTPUT (cfg.output_power = true), so this is a REAL correction, not
  // just belt-and-braces -- we also cleared cfg.output_power above.
  // setExtOutput(false) = EXT_5V INPUT ; setExtOutput(true) = EXT_5V OUTPUT.
  // NEVER call setExtOutput(true) while a Cardputer (or any source) feeds Grove 5 V.
  M5.Power.setExtOutput(false);

  M5.Display.setRotation(1);
  M5.Display.fillScreen(TFT_BLACK);
  Serial.begin(115200);
  Serial.println("\nCardSatDualRig starting");

  loadConfig();
  applyLegConfig();

  // USB host (used in BOTH modes so radios enumerate during config).
  gUsb.onDeviceConnected(onUsbConnected);
  gUsb.onDeviceDisconnected(onUsbDisconnected);
  gUsb.onSerialData(onUsbSerialData);
  EspUsbHostConfig ucfg;
  if (!gUsb.begin(ucfg)) Serial.println("[USB] host begin FAILED");

  // Grove UART transport is always available in run mode.
  gGrove.begin(gCfg.groveBaud, SERIAL_8N1, GROVE_RX_PIN, GROVE_TX_PIN);

  // Enter config mode if there's no valid config, or Button A is held at boot.
  if (!configValid() || btnAHeldAtBoot()) enterConfigMode();
  else                                    enterRunMode();
}

void loop() {
  M5.update();

  if (gConfigMode) {
    gDns.processNextRequest();
    gWeb.handleClient();
    // (radios keep enumerating via the USB host task; gSeen updates live)
    if (millis() - gLastDraw > 400) { gLastDraw = millis(); drawConfig(); }
    // In config mode, a long press of Button A reboots (into run mode if saved).
    if (M5.BtnA.pressedFor(1500)) { delay(150); ESP.restart(); }
    return;
  }

  // --- run mode ---
  static uint32_t lastWifiTry = 0;
  if (gCfg.wifiOn && WiFi.status() != WL_CONNECTED && millis() - lastWifiTry > 4000) {
    lastWifiTry = millis(); WiFi.begin(gCfg.ssid, gCfg.pass);
  }
  pollRadioOnline();
  serviceTcp();
  serviceGrove();

  // Long-press Button A -> return to config mode (no reboot needed).
  if (M5.BtnA.pressedFor(1500)) enterConfigMode();

  if (millis() - gLastDraw > 300) { gLastDraw = millis(); drawRun(); }
}
