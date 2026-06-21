#pragma once
// ===========================================================================
//  lora.h  -  SX1262 LoRa radio wrapper for CardSat-to-CardSat text messaging
// ===========================================================================
//  Thin abstraction over the M5Stack Cap LoRa (SX1262) using the RadioLib
//  library. This isolates the one part of the messaging feature that cannot be
//  host-compiled or verified without the physical radio: all RadioLib / SPI /
//  RF-switch calls live here, behind a small C++ interface the rest of the app
//  uses. The messaging protocol, history buffer, and UI live in app.cpp and are
//  hardware-independent.
//
//  *** UNTESTED hardware path. The SX1262 init, the PI4IOE5V6408 RF-switch
//  control, and TX/RX have been written to the M5Stack Cap LoRa reference and
//  the SX1262 datasheet, but have NOT been confirmed on a device. Build and use
//  at your own risk; verify TX/RX between two units before relying on it. ***
//
//  Requires the RadioLib library (install via Arduino Library Manager). The
//  whole module is wrapped in `#if CARDSAT_HAS_LORA` so a build without RadioLib
//  still compiles (the messaging screen then reports the radio unavailable).
// ===========================================================================
#include <Arduino.h>

// Define CARDSAT_HAS_LORA=1 in the build (or here) once RadioLib is installed.
#ifndef CARDSAT_HAS_LORA
#define CARDSAT_HAS_LORA 0
#endif

// Cap LoRa (SX1262) SPI pinmap, confirmed from the M5Stack Cap LoRa pinmap /
// Arduino tutorial: NSS=G5, DIO1(IRQ)=G4, RST=G3, BUSY=G6. The PI4IOE5V6408 IO
// expander on the cap's I2C bus drives the RF antenna switch.
//
// IMPORTANT: the LoRa SX1262 and the microSD card share ONE SPI bus on the
// Cardputer ADV: SCK=G40, MISO=G39, MOSI=G14 (the SD pins in config.h). They
// differ only in chip-select (SD CS=G12, LoRa NSS=G5). RadioLib must therefore
// be given this exact bus and must not re-`SPI.begin()` it with other pins, or
// the shared bus is reconfigured out from under the SD driver and the card
// becomes inaccessible. begin() below shares the bus and restores SD's CS line.
static constexpr int  LORA_PIN_NSS  = 5;
static constexpr int  LORA_PIN_DIO1 = 4;
static constexpr int  LORA_PIN_RST  = 3;
static constexpr int  LORA_PIN_BUSY = 6;
static constexpr int  LORA_PIN_SCK  = 40;   // shared with SD (SD_SCK_PIN)
static constexpr int  LORA_PIN_MISO = 39;   // shared with SD (SD_MISO_PIN)
static constexpr int  LORA_PIN_MOSI = 14;   // shared with SD (SD_MOSI_PIN)
static constexpr int  SD_CS_PIN_SHARED = 12; // SD chip-select (must stay HIGH/idle)
static constexpr uint8_t LORA_RFSW_I2C_ADDR = 0x43;   // PI4IOE5V6408 (cap default)

// One received frame, handed up to the app layer.
struct LoraRx {
  char    text[64];   // decoded message payload (null-terminated, fixed)
  char    from[14];   // sender callsign (null-terminated, fixed)
  float   rssi;       // dBm
  float   snr;        // dB
};

class LoraRadio {
public:
  // Bring up the SX1262 at the given parameters. freqKHz so 433775 == 433.775
  // MHz. sf 6..12, bwHz e.g. 125000, txDbm typ. 0..22. Returns true on success.
  // A false return (or a build without RadioLib) leaves the radio "unavailable"
  // and every other call is a safe no-op.
  bool begin(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, int8_t txDbm);
  bool ready() const { return _ready; }

  // Reconfigure the carrier / spreading factor on the fly (e.g. user changed the
  // band or SF). Cheap; returns false if the radio isn't up.
  bool setRadio(uint32_t freqKHz, uint8_t sf, uint32_t bwHz, int8_t txDbm);

  // Transmit a raw payload (already framed by the caller). Blocks for the LoRa
  // air time (seconds at SF12!). Returns true if the chip reported TX done.
  bool sendRaw(const uint8_t* data, size_t len);

  // Non-blocking receive poll: returns true and fills `out` (raw bytes + rssi/
  // snr) if a packet was waiting. Call frequently from loop().
  bool poll(uint8_t* buf, size_t bufLen, size_t& outLen, float& rssi, float& snr);

  // Put the radio back into continuous receive (called internally after TX).
  void listen();

private:
  bool _ready = false;
  void rfSwitchTx();   // PI4IOE5V6408: route RF to PA / antenna for transmit
  void rfSwitchRx();   // PI4IOE5V6408: route RF to LNA / antenna for receive
};
