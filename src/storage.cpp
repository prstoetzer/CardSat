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
static SPIClass g_spi(HSPI);

bool begin() {
  g_ready = false; g_sd = false; g_fs = &LittleFS;

  // 1) Prefer the microSD card. Everything is kept in a DATA_DIR ("/CardSat")
  //    folder so the card stays tidy and easy to copy off.
  g_spi.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  if (SD.begin(SD_CS_PIN, g_spi)) {
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
