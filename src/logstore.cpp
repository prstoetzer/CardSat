// ===========================================================================
//  Logstore -- see logstore.h for the design, the size discipline and the
//  LoRa/SD SPI reasoning.
// ===========================================================================
#include "logstore.h"
#include "storage.h"
#include <stdarg.h>

namespace Logstore {
namespace {

struct Def { const char* path; };
// Files, in Log order. /CardSat/Logs so the portal can list one directory and
// so a user can delete the lot without touching caches or config.
const Def DEFS[LOG_N] = {
  { LOG_DIR "/usb.log"     },
  { LOG_DIR "/console.log" },
};

// The cap is per-file; rotation doubles it. Flash is shared with the GP cache
// and config, so it gets a much smaller allowance than the SD card.
size_t capBytes() {
  return Store::onSD() ? (size_t)LOG_MAX_BYTES_SD : (size_t)LOG_MAX_BYTES_FLASH;
}

bool ensureDir() {
  if (!Store::ready()) return false;
  fs::FS& fs = Store::fs();
  if (fs.exists(LOG_DIR)) return true;
  return fs.mkdir(LOG_DIR);
}

// Rotate `p` to `p.1` when it exceeds the cap. A rename, not a copy: no
// transient double-space, which matters on a nearly-full LittleFS. The old .1
// is dropped, so the ceiling is a hard 2 x cap per log.
void rotateIfBig(const char* p) {
  fs::FS& fs = Store::fs();
  File f = fs.open(p, FILE_READ);
  if (!f) return;
  const size_t sz = f.size();
  f.close();
  if (sz < capBytes()) return;
  String old = String(p) + ".1";
  if (fs.exists(old.c_str())) fs.remove(old.c_str());
  fs.rename(p, old.c_str());
}

}  // namespace

const char* path(Log which) {
  return (which < LOG_N) ? DEFS[which].path : "";
}

size_t size(Log which) {
  if (which >= LOG_N || !Store::ready()) return 0;
  File f = Store::fs().open(DEFS[which].path, FILE_READ);
  if (!f) return 0;
  size_t s = f.size();
  f.close();
  return s;
}

bool clear(Log which) {
  if (which >= LOG_N || !Store::ready()) return false;
  fs::FS& fs = Store::fs();
  fs.remove(DEFS[which].path);
  String old = String(DEFS[which].path) + ".1";
  fs.remove(old.c_str());
  return true;
}

void line(Log which, const char* text) {
  if (which >= LOG_N || !text) return;
  if (!Store::ready()) return;         // pre-mount: nothing to write to yet
  if (!ensureDir()) return;
  rotateIfBig(DEFS[which].path);
  File f = Store::fs().open(DEFS[which].path, FILE_APPEND);
  if (!f) return;
  // Uptime prefix, not wall clock: the log is most useful before time is set,
  // and millis() is always available. Matches the USB trace's existing format.
  f.printf("%lu %s\n", (unsigned long)millis(), text);
  f.flush();                           // a crash must not lose the last line --
  f.close();                           // which is the one that matters.
}

void raw(Log which, const char* text) {
  if (which >= LOG_N || !text) return;
  if (!Store::ready()) return;
  if (!ensureDir()) return;
  rotateIfBig(DEFS[which].path);
  File f = Store::fs().open(DEFS[which].path, FILE_APPEND);
  if (!f) return;
  f.print(text);
  f.print('\n');
  f.flush();
  f.close();
}

void rawf(Log which, const char* fmt, ...) {
  char b[200];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  raw(which, b);
}

void linef(Log which, const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  line(which, b);
}

void consolef(const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  // Live console first (a no-op once USB CAT has taken the PHY), then the
  // durable copy. Order matters only for readability over a live console.
  Serial.println(b);
  line(LOG_CONSOLE, b);
}

}  // namespace Logstore
