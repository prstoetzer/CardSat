// ===========================================================================
//  lora.cpp  -  SX1262 LoRa radio wrapper implementation (RadioLib)
// ===========================================================================
//  Hardware-verified (0.9.39): TX/RX text messaging is confirmed working on the
//  M5Stack Cap LoRa (SX1262), tested two-way against a LilyGo T-LoRa unit running
//  the companion CardSat Pager firmware. Everything here is compiled only when
//  CARDSAT_HAS_LORA is set (RadioLib installed). Without it, the class is a set of
//  safe no-ops so the firmware still builds and the messaging screen reports
//  "radio off".
// ===========================================================================
#include "lora.h"

#if CARDSAT_HAS_LORA
#include <RadioLib.h>
#include <Wire.h>
#include "storage.h"   // Store::remount() to restore the SD bus after RF SPI use

// RadioLib module instance for the SX1262 at the Cap LoRa pinmap.
// NOTE: allocated on the heap via a pointer (constructed in begin()) rather than
// a by-value global. A by-value global forces the SX1262 destructor/vtable into
// this translation unit's static init/exit machinery; when this code is inlined
// into the large single-file CardSat.ino, that pushes a literal pool out of L32R
// range and the Xtensa linker fails with "dangerous relocation: l32r ... out of
// range". A pointer keeps the heavy object construction inside begin() instead.
static Module* g_mod = nullptr;
static SX1262* g_radio = nullptr;

// DIO1 fires on RX-done / TX-done; the ISR just sets a flag (must live in IRAM
// on ESP32, or the wake-from-flash race drops the interrupt).
static volatile bool g_irqFired = false;
static void IRAM_ATTR loraIsr() { g_irqFired = true; }

// --- PI4IOE5V6408 RF switch ------------------------------------------------
// The cap routes the SX1262 RF port to either the PA (TX) or LNA (RX) through
// this IO expander. The exact bit mapping follows the M5Stack Cap LoRa example;
// if a unit shows TX but no RX (or vice-versa) this is the first thing to check.
static void rfWrite(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(LORA_RFSW_I2C_ADDR);
  Wire.write(reg); Wire.write(val);
  Wire.endTransmission();
}

void LoraRadio::rfSwitchTx() {
  // Output register: drive the switch line(s) to the TX path.
  rfWrite(0x05, 0x01);   // PI4IOE5V6408 output port; bit pattern per cap wiring
}
void LoraRadio::rfSwitchRx() {
  rfWrite(0x05, 0x02);   // RX path
}

bool LoraRadio::begin(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, int8_t txDbm) {
  _ready = false;
  // The SD card and LoRa share one SPI bus (SCK40/MISO39/MOSI14). Make sure the
  // SD chip-select is idle-HIGH (deselected) before we touch the bus, and keep
  // the LoRa NSS HIGH until RadioLib drives it, so neither device sees stray
  // clocks meant for the other.
  pinMode(SD_CS_PIN_SHARED, OUTPUT);  digitalWrite(SD_CS_PIN_SHARED, HIGH);
  pinMode(LORA_PIN_NSS, OUTPUT);      digitalWrite(LORA_PIN_NSS, HIGH);

  if (!g_radio) {                            // construct once (heap, not static)
    g_mod   = new Module(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);
    g_radio = new SX1262(g_mod);
  }
  if (!g_radio) return false;

  // ---- Presence probe (do this BEFORE touching the shared SPI bus) --------
  // The SX1262 and the microSD card share one SPI bus. If no Cap LoRa module is
  // attached, letting RadioLib run still claims/reconfigures that bus, and on the
  // ESP32 the SD driver cannot reliably be restored afterwards -- so the card
  // becomes unwritable (settings, logs and caches silently stop persisting).
  // Rather than try to recover the bus after the fact, detect an absent module
  // up front and skip LoRa entirely, leaving the working SD bus untouched.
  //
  // Probe: pull BUSY (GPIO6) down, then pulse RST low->high. A PRESENT SX1262
  // drives BUSY HIGH the moment reset is released and holds it through its
  // power-on calibration (~1 ms) before settling LOW (ready). An ABSENT module
  // leaves the pulled-down pin reading LOW throughout. We poll in a tight loop
  // starting the instant reset is released so we cannot miss the calibration
  // pulse (missing it would false-negative a real module and wrongly disable
  // LoRa), and we accept presence if BUSY is ever seen HIGH within the window.
  pinMode(LORA_PIN_BUSY, INPUT_PULLDOWN);
  pinMode(LORA_PIN_RST, OUTPUT);
  digitalWrite(LORA_PIN_RST, LOW);  delay(2);
  digitalWrite(LORA_PIN_RST, HIGH);         // release reset; poll immediately
  bool present = false;
  for (int i = 0; i < 2000; ++i) {          // ~20 ms window, tight (~10us/iter)
    if (digitalRead(LORA_PIN_BUSY) == HIGH) { present = true; break; }
    delayMicroseconds(10);
  }
  if (!present) {
    // No module -> do NOT disturb the SPI bus or the SD card. LoRa stays
    // unavailable; every LoRa call is already a safe no-op when !_ready.
    Serial.println("[lora] no Cap LoRa detected (BUSY low after reset) - skipping, SD bus left intact");
    return false;
  }

  // Module present: now it is safe to share the SPI bus on the SD pins rather
  // than letting RadioLib re-init SPI with its own defaults.
  SPI.begin(LORA_PIN_SCK, LORA_PIN_MISO, LORA_PIN_MOSI, LORA_PIN_NSS);

  int st = g_radio->begin();                // uses the shared SPI bus
  if (st != RADIOLIB_ERR_NONE) {
    // The radio isn't present/answering (e.g. no Cap LoRa attached). RadioLib's
    // begin() has already reconfigured the shared SPI bus, so we MUST restore the
    // SD card's bus config before returning -- otherwise every later SD access
    // fails on units running from the card. (The success path below remounts too.)
    digitalWrite(SD_CS_PIN_SHARED, HIGH);
    Store::remount();
    return false;
  }

  // Configure the RF antenna switch direction via the expander on first use.
  rfSwitchRx();

  if (!setRadio(freqKHz, sf, bwHz, txDbm)) { digitalWrite(SD_CS_PIN_SHARED, HIGH); Store::remount(); return false; }

  g_radio->setDio1Action(loraIsr);
  _ready = true;
  listen();
  Store::remount();     // restore the SD bus after RadioLib reconfigured SPI
  return true;
}

bool LoraRadio::setRadio(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, int8_t txDbm) {
  if (!g_radio) return false;
  // setRadio() runs a burst of SX1262 SPI commands on the bus shared with the SD
  // card. begin() brackets its SPI work by keeping the SD chip-select idle-HIGH;
  // this path must do the same, or a channel/SF/BW change can leave the SD line
  // mid-transaction and the very next cfg.save() write fails (corrupting the
  // config file -> settings revert on the next boot). Deselect SD before, and
  // re-assert HIGH after, leaving the card reachable.
  pinMode(SD_CS_PIN_SHARED, OUTPUT);  digitalWrite(SD_CS_PIN_SHARED, HIGH);
  float freqMHz = (float)freqKHz / 1000.0f;
  float bwKHz   = (float)bwHz / 1000.0f;
  bool ok = true;
  if (g_radio->setFrequency(freqMHz)      != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setSpreadingFactor(sf) != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setBandwidth(bwKHz)  != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setCodingRate(5)     != RADIOLIB_ERR_NONE) ok = false; // 4/5
  if (ok) {
    g_radio->setOutputPower(txDbm);
    // A private sync word so CardSat traffic won't trigger on LoRaWAN gateways.
    g_radio->setSyncWord(0x12);
  }
  // After the SX1262 commands, RadioLib leaves the shared SPI bus at its own
  // clock/mode (2 MHz / MODE0); the SD card was mounted at 25 MHz and can no
  // longer be reached until its bus is re-established. A bare CS-deselect is not
  // enough (that was tried and the SD still failed) -- fully remount the card so
  // the NEXT SD access (sat list, logs, cfg.save) works.
  Store::remount();
  return ok;
}

// Full RX reconfiguration for the LoRa RX / hex monitor (receive arbitrary LoRa
// signals). Same shared-SPI/SD discipline as setRadio() (deselect SD, run the
// SX1262 burst, remount SD).
bool LoraRadio::setRadioRx(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, uint8_t cr,
                           uint8_t syncWord, uint16_t preamble, bool crcOn) {
  if (!g_radio) return false;
  pinMode(SD_CS_PIN_SHARED, OUTPUT);  digitalWrite(SD_CS_PIN_SHARED, HIGH);
  float freqMHz = (float)freqKHz / 1000.0f;
  float bwKHz   = (float)bwHz / 1000.0f;
  if (cr < 5) cr = 5; if (cr > 8) cr = 8;
  bool ok = true;
  if (g_radio->setFrequency(freqMHz)         != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setSpreadingFactor(sf)  != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setBandwidth(bwKHz)     != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setCodingRate(cr)       != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setSyncWord(syncWord)   != RADIOLIB_ERR_NONE) ok = false;
  if (ok && g_radio->setPreambleLength(preamble) != RADIOLIB_ERR_NONE) ok = false;
  if (ok) {
    // CRC: many satellites transmit with LoRa CRC; some disable it. Match the sat.
    g_radio->setCRC(crcOn ? 1 : 0);
    g_radio->startReceive();               // back to continuous listen
  }
  Store::remount();
  return ok;
}

bool LoraRadio::sendRaw(const uint8_t* data, size_t len) {
  if (!_ready || !g_radio) return false;
  pinMode(SD_CS_PIN_SHARED, OUTPUT);  digitalWrite(SD_CS_PIN_SHARED, HIGH);
  rfSwitchTx();
  int st = g_radio->transmit((uint8_t*)data, len);  // blocking
  rfSwitchRx();
  listen();
  // DIO1 fires on BOTH RX-done and TX-done into the same g_irqFired flag. The
  // transmit() we just did raised TX-done, so the flag is now set even though no
  // packet was received. Clear it AFTER re-arming receive, or the next poll() would
  // treat this TX-done as an incoming packet and read stale bytes out of the radio's
  // buffer -- which surfaced as our own just-sent message "echoing" back (often with
  // leftover text from a previous message appended). startReceive() above also clears
  // the SX1262's hardware IRQ status; this clears our software latch to match.
  g_irqFired = false;
  Store::remount();     // restore the SD bus after RadioLib reconfigured SPI
  return (st == RADIOLIB_ERR_NONE);
}

void LoraRadio::listen() {
  if (!_ready || !g_radio) return;
  rfSwitchRx();
  g_radio->startReceive();
}

bool LoraRadio::poll(uint8_t* buf, size_t bufLen, size_t& outLen,
                     float& rssi, float& snr) {
  if (!_ready || !g_radio) return false;
  if (!g_irqFired) return false;
  g_irqFired = false;

  size_t n = g_radio->getPacketLength();
  if (n == 0 || n > bufLen) { g_radio->startReceive(); Store::remount(); return false; }
  int st = g_radio->readData(buf, n);
  rssi = g_radio->getRSSI();
  snr  = g_radio->getSNR();
  g_radio->startReceive();                 // back to listening
  Store::remount();     // restore the SD bus so a log/cfg write after RX works
  if (st != RADIOLIB_ERR_NONE) return false;
  outLen = n;
  return true;
}

#else  // -------- CARDSAT_HAS_LORA not set: safe no-op stubs ----------------

bool LoraRadio::begin(uint32_t, uint8_t, uint32_t, int8_t) { _ready = false; return false; }
bool LoraRadio::setRadio(uint32_t, uint8_t, uint32_t, int8_t) { return false; }
bool LoraRadio::sendRaw(const uint8_t*, size_t) { return false; }
bool LoraRadio::poll(uint8_t*, size_t, size_t&, float&, float&) { return false; }
void LoraRadio::listen() {}
void LoraRadio::rfSwitchTx() {}
void LoraRadio::rfSwitchRx() {}

#endif
