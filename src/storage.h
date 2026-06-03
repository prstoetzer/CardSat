// ===========================================================================
//  storage.h -- filesystem abstraction (internal LittleFS, SD-card fallback)
// ===========================================================================
//  CardSat persists everything to flash via LittleFS. When the firmware is run
//  through a launcher (e.g. bmorcelli's Launcher) the flashed partition table
//  is the launcher's, and it may not include a SPIFFS/LittleFS data partition
//  -- so LittleFS.begin() fails and every file open returns an error. In that
//  situation a microSD card is almost always present (the launcher boots from
//  it), so we transparently fall back to the SD card.
//
//  All persistence goes through Store::fs(), which points at whichever
//  filesystem mounted. Call Store::begin() once at startup before any file I/O.
#pragma once
#include <FS.h>

namespace Store {
  bool   begin();            // mount LittleFS (format on fail), else SD card
  fs::FS& fs();              // the active filesystem (LittleFS or SD)
  bool   ready();            // true if some filesystem mounted
  bool   onSD();             // true if we fell back to the SD card
  bool   formatInternal();   // wipe internal LittleFS (factory reset); never the SD
}
