// ===========================================================================
//  storage.cpp
// ===========================================================================
#include "storage.h"
#include "config.h"
#include <LittleFS.h>
#include <SD.h>
#include <SPI.h>

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

bool remount() {
  // Only the SD card shares the SPI bus with the LoRa SX1262. After RadioLib runs
  // a transaction it releases the bus at its own clock/mode (2 MHz / MODE0), which
  // can leave the already-mounted SD driver unable to talk to the card at 25 MHz.
  // Re-assert the SD pins and re-run SD.begin() to restore the card's bus config.
  // LittleFS is on internal flash and never needs this.
  if (!g_sd) return g_ready;
  SD.end();
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  bool ok = SD.begin(SD_CS_PIN, SPI, SD_FREQ_HZ);
  if (!ok) ok = SD.begin(SD_CS_PIN, SPI, 1000000);   // retry slower
  g_ready = ok; g_sd = ok; g_fs = ok ? (fs::FS*)&SD : (fs::FS*)&LittleFS;
#ifdef CARDSAT_CFG_DEBUG
  Serial.printf("[fs] remount SD -> %s\n", ok ? "ok" : "FAILED");
#endif
  return ok;
}

size_t freeBytes() {
  // On the internal LittleFS partition free space is tight and worth checking
  // before a streamed download; on an SD card it's effectively unlimited for
  // our purposes, so report a large value without probing the card.
  if (g_sd) return (size_t)8 * 1024 * 1024;
  if (!g_ready) return 0;
  uint32_t total = LittleFS.totalBytes(), used = LittleFS.usedBytes();
  return (used < total) ? (size_t)(total - used) : 0;
}

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
