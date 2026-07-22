// ===========================================================================
//  storage.h -- filesystem abstraction (internal LittleFS, SD-card fallback)
// ===========================================================================
//  CardSat keeps everything in a "/CardSat" folder on the microSD card by
//  default -- config, cached elements, favorites, calibration, transponders.
//  If no SD card is present it falls back to the internal LittleFS partition
//  (same "/CardSat" folder) so the firmware still works standalone.
//
//  All persistence goes through Store::fs(), which points at whichever
//  filesystem mounted. Call Store::begin() once at startup before any file I/O.
#pragma once
#include <FS.h>

namespace Store {
  bool   begin();            // mount LittleFS (format on fail), else SD card
  bool   remount();          // re-establish the SD bus/mount after another driver
                             // (e.g. the LoRa SX1262) shared and reconfigured the
                             // SPI bus; no-op when running on internal LittleFS
  fs::FS& fs();              // the active filesystem (LittleFS or SD)
  bool   ready();            // true if some filesystem mounted
  bool   onSD();             // true if we fell back to the SD card
  bool   formatInternal();   // wipe internal LittleFS (factory reset); never the SD
  size_t freeBytes();        // approx free space on the active FS (large if on SD)
  // Transactional whole-file replace: temp-write -> verify -> rotate -> promote -> restore
  // on failure. The previous good file survives any interrupted/short write. (H13/H19/M29/M32)
  bool   writeFileAtomic(const char* path, const uint8_t* data, size_t len);
}
