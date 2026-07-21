// CardSatDualRig - shared type definitions (in a header so Arduino's
// auto-prototype generator sees them before the .ino's functions).
#pragma once
#include <Arduino.h>

// CAT families. Four dialects cover every supported radio:
//   CIV        - Icom binary CI-V (5-byte BCD frequency), per-radio bus address
//   YAESU_BIN  - "old" Yaesu 5-byte binary CAT (4-byte BCD @10 Hz + opcode)
//   YAESU_TXT  - "new" Yaesu ASCII CAT (FA/MD/TX ';'-terminated, like Kenwood HF)
//   KENWOOD_HT - Kenwood TH-D74/D75 handheld CAT (FQ<band>,<10 digit Hz> + CR)
enum CatFamily : uint8_t { CAT_CIV, CAT_YAESU_BIN, CAT_YAESU_TXT, CAT_KENWOOD_HT };

enum RadioModel : uint8_t {
  // --- Icom CI-V transceivers ---
  RIG_IC705, RIG_IC905, RIG_IC7100, RIG_IC7000, RIG_IC706MK2G, RIG_IC275, RIG_IC475,
  // --- Icom CI-V receivers (RX only) ---
  RIG_ICR10, RIG_ICR20, RIG_ICR30,
  RIG_ICR7000, RIG_ICR7100, RIG_ICR8500, RIG_ICR8600, RIG_ICR9000, RIG_ICR9500,
  // --- Yaesu old binary ---
  RIG_FT817, RIG_FT818, RIG_FT857, RIG_FT897, RIG_FT100,
  // --- Yaesu receiver (old-binary CAT family) ---
  RIG_VR5000,
  // --- Yaesu new ASCII ---
  RIG_FT991, RIG_FT991A, RIG_FTX1,
  // --- Kenwood handhelds (all-mode receiver, RX only) ---
  RIG_THD74, RIG_THD75,
  RIG_MODEL_COUNT
};

struct RadioProfile {
  RadioModel  model;
  const char* name;
  CatFamily   family;
  uint32_t    baud;     // default CAT baud; for native-USB rigs the CDC line rate is set anyway
  uint8_t     civAddr;  // default CI-V bus address (CIV family only; 0 otherwise)
  bool        rxOnly;   // true = receive-only (never keyed; warn if assigned to uplink)
};

// Neutral mode enum shared by the server and the CAT encoders.
enum RigMode : uint8_t { MODE_LSB, MODE_USB, MODE_CW, MODE_CWR, MODE_FM, MODE_AM, MODE_DATA };

// ------------------------------------------------------------ persisted config
// Everything the user sets at runtime (captive portal or HTTP), stored in NVS so
// it survives reboots. No compile-time radio/Wi-Fi flags.
struct LegConfig {
  uint8_t  model    = 0xFF;   // RadioModel; 0xFF = unassigned
  uint8_t  civAddr  = 0;      // 0 = use the model's table default
  uint32_t baud     = 0;      // 0 = use the model's table default
  char     usbSerial[24] = "";// pin this leg to a device with this USB serial; "" = by slot order
};

struct AppConfig {
  uint32_t magic    = 0;      // CONFIG_MAGIC when valid
  char     ssid[33] = "";     // home Wi-Fi (STA) for run mode
  char     pass[65] = "";
  uint16_t tcpPort  = 4532;   // rigctld TCP port
  bool     wifiOn   = true;   // false = headless, Grove-serial control only
  uint32_t groveBaud = 115200;// rigctld-over-Grove UART baud (must match CardSat side)
  LegConfig downlink;         // VFOA / RX
  LegConfig uplink;           // VFOB / TX
};

// A USB device seen during enumeration (shown in the portal for assignment).
struct SeenDevice {
  bool     used = false;
  uint8_t  address = 0;
  uint16_t vid = 0, pid = 0;
  bool     isHub = false;
  char     product[32] = "";
  char     serial[24]  = "";
};

// ---- runtime structs that functions use by value (kept in the header so the
// Arduino prototype generator sees them) ----
#include <freertos/FreeRTOS.h>

struct RxRing {
  static const size_t CAP = 256;
  uint8_t buf[CAP];
  volatile size_t head = 0, tail = 0;
  portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
  void push(const uint8_t* d, size_t n) {
    portENTER_CRITICAL(&mux);
    for (size_t i = 0; i < n; i++) {
      size_t nh = (head + 1) % CAP;
      if (nh == tail) tail = (tail + 1) % CAP;
      buf[head] = d[i]; head = nh;
    }
    portEXIT_CRITICAL(&mux);
  }
  int pop() {
    int c = -1;
    portENTER_CRITICAL(&mux);
    if (tail != head) { c = buf[tail]; tail = (tail + 1) % CAP; }
    portEXIT_CRITICAL(&mux);
    return c;
  }
  void clear() { portENTER_CRITICAL(&mux); head = tail = 0; portEXIT_CRITICAL(&mux); }
};

struct RadioPort {
  const LegConfig* leg = nullptr;
  const RadioProfile* prof = nullptr;
  uint8_t  civAddr = 0;         // effective address (override or table default)
  uint32_t baud    = 9600;      // effective baud
  uint8_t  addr    = ESP_USB_HOST_ANY_ADDRESS;   // USB device address once bound
  bool     bound   = false;
  bool     online  = false;
  RxRing   rx;
  uint64_t lastFreq = 0;
  RigMode  lastMode = MODE_USB;
  bool     ptt = false;
  const char* legName = "?";
  bool assigned() const { return leg && leg->model != 0xFF; }
};

// The two radio legs are globals defined in the .ino; declared here so
// RigctlSession::cur() (and other header-side helpers) can reference them.
extern RadioPort gDown;   // VFOA / downlink
extern RadioPort gUp;     // VFOB / uplink

struct RigctlSession {
  Stream* io = nullptr;
  int     vfo = 0;                 // 0 = VFOA (downlink), 1 = VFOB (uplink)
  String  line;
  bool    wantClose = false;       // set by the 'q' command (TCP transport acts on it)
  RadioPort& cur() { return vfo == 1 ? gUp : gDown; }
  const char* vfoTok() { return vfo == 1 ? "VFOB" : "VFOA"; }
};
