#ifndef CARDSAT_LOGSTORE_H
#define CARDSAT_LOGSTORE_H
// ===========================================================================
//  Logstore -- console output that survives, on SD or bare flash
// ===========================================================================
//  Why this exists. Engaging USB CAT or a USB rotator takes the USB PHY, which
//  kills the serial console for the rest of the session (the host is resident
//  until reboot -- see usbserial.h). Anything printed to Serial after that is
//  gone. That is exactly when a field problem needs a trace, so the console has
//  to land somewhere durable too.
//
//  Files live in /CardSat/Logs and are retrievable through the web portal, so
//  this works the same whether Store is on the SD card or internal LittleFS.
//
//  ---- Size discipline (LittleFS is SMALL) ---------------------------------
//  A Cardputer with no SD card has a few hundred KB of LittleFS shared with the
//  GP cache, config and everything else. An uncapped log fills it and breaks
//  those. So every file is capped and rotated ONCE (.1), and the cap is smaller
//  on flash than on SD. Rotation is a rename, not a copy: no double-space spike.
//
//  The ceiling is 2 x cap + ONE line per log: the size is checked BEFORE a write,
//  so the line that crosses the cap still lands (a ~200-byte overshoot, verified
//  by host model). Stated exactly rather than rounded down, because "hard
//  ceiling" is a promise a nearly-full LittleFS will hold you to. On flash that
//  is 2 x 16 KB x 2 logs = ~64 KB worst case, all of it reclaimable by deleting
//  /CardSat/Logs.
//
//  ---- LoRa / SD shared SPI ------------------------------------------------
//  The SX1262 and the microSD share one SPI bus (lora.h). RadioLib leaves the
//  bus at its own clock/mode, so LoRa calls Store::remount() after every RF
//  operation to restore the card. Writing to the SD mid-RF would be a genuine
//  corruption risk -- but it cannot happen here: LoraRadio::sendRaw()/poll()
//  and every Logstore call run on loopTask, and both RF paths remount before
//  they return (verified in lora.cpp: sendRaw line 16, poll lines 8 and 13).
//  The single task serializes them; the DIO1 ISR only sets a flag and touches
//  no filesystem. Logstore therefore never writes while the bus is RadioLib's.
//  If a future change moves RF or logging to another task, THAT is when this
//  needs a lock -- and this comment is the warning.
#include <Arduino.h>
#include "config.h"

namespace Logstore {

// One log per subsystem. Adding one means adding a name here; the file is
// created on first write.
enum Log : uint8_t {
  LOG_USB = 0,      // USB CAT + USB rotator: engage/bind/teardown traces
  LOG_CONSOLE,      // whatever would have gone to Serial
  LOG_N
};

// Append one line (a newline is added). Cheap when nothing is configured: a
// closed-file check and a return. Safe to call before Store is mounted.
void line(Log which, const char* text);
// As line(), but WITHOUT the uptime prefix. For the trace's "# ..." headers and
// "## ..." markers, which are read as a block and carry their own meaning; a
// timestamp on each would be noise. Same rotation and cap.
void raw(Log which, const char* text);
void rawf(Log which, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
// As line(), but WITHOUT the uptime prefix. For the trace's "# ..." headers and
// "## ..." markers, which are read as a block and carry their own meaning; a
// timestamp on each would be noise. Same rotation and cap.
void raw(Log which, const char* text);
void rawf(Log which, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void linef(Log which, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

// Mirror of Serial.print-style output: goes to the console AND the log, so a
// trace is readable live over USB *and* survives the console being taken away.
void consolef(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// Full path of a log, for the web portal's file list.
const char* path(Log which);

// Current size, or 0 if absent. For the portal's listing.
size_t size(Log which);

// Delete a log and its rotated .1. Returns false if the FS is not ready.
bool clear(Log which);

}  // namespace Logstore
#endif  // CARDSAT_LOGSTORE_H
