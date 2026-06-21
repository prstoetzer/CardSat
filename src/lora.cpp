// ===========================================================================
//  lora.cpp  -  SX1262 LoRa radio wrapper implementation (RadioLib)
// ===========================================================================
//  *** UNTESTED hardware path -- see lora.h. ***
//  Everything here is compiled only when CARDSAT_HAS_LORA is set (RadioLib
//  installed). Without it, the class is a set of safe no-ops so the rest of the
//  firmware still builds and the messaging screen simply reports "radio off".
// ===========================================================================
#include "lora.h"

#if CARDSAT_HAS_LORA
#include <RadioLib.h>
#include <Wire.h>

// RadioLib module instance for the SX1262 at the Cap LoRa pinmap.
static SX1262 g_radio = new Module(LORA_PIN_NSS, LORA_PIN_DIO1, LORA_PIN_RST, LORA_PIN_BUSY);

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
  int st = g_radio.begin();                 // default SPI; uses the pins above
  if (st != RADIOLIB_ERR_NONE) return false;

  // Configure the RF antenna switch direction via the expander on first use.
  rfSwitchRx();

  if (!setRadio(freqKHz, sf, bwHz, txDbm)) return false;

  g_radio.setDio1Action(loraIsr);
  _ready = true;
  listen();
  return true;
}

bool LoraRadio::setRadio(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, int8_t txDbm) {
  float freqMHz = (float)freqKHz / 1000.0f;
  float bwKHz   = (float)bwHz / 1000.0f;
  if (g_radio.setFrequency(freqMHz)      != RADIOLIB_ERR_NONE) return false;
  if (g_radio.setSpreadingFactor(sf)     != RADIOLIB_ERR_NONE) return false;
  if (g_radio.setBandwidth(bwKHz)        != RADIOLIB_ERR_NONE) return false;
  if (g_radio.setCodingRate(5)           != RADIOLIB_ERR_NONE) return false; // 4/5
  g_radio.setOutputPower(txDbm);
  // A private sync word so CardSat traffic won't trigger on LoRaWAN gateways.
  g_radio.setSyncWord(0x12);
  return true;
}

bool LoraRadio::sendRaw(const uint8_t* data, size_t len) {
  if (!_ready) return false;
  rfSwitchTx();
  int st = g_radio.transmit((uint8_t*)data, len);  // blocking
  rfSwitchRx();
  listen();
  return (st == RADIOLIB_ERR_NONE);
}

void LoraRadio::listen() {
  if (!_ready) return;
  rfSwitchRx();
  g_radio.startReceive();
}

bool LoraRadio::poll(uint8_t* buf, size_t bufLen, size_t& outLen,
                     float& rssi, float& snr) {
  if (!_ready) return false;
  if (!g_irqFired) return false;
  g_irqFired = false;

  size_t n = g_radio.getPacketLength();
  if (n == 0 || n > bufLen) { g_radio.startReceive(); return false; }
  int st = g_radio.readData(buf, n);
  rssi = g_radio.getRSSI();
  snr  = g_radio.getSNR();
  g_radio.startReceive();                 // back to listening
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
