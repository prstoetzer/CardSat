// ===========================================================================
//  ConsoleLog -- see consolelog.h for the mechanism and the cost reasoning.
//  The Tee CLASS lives in config.h (call sites need a complete type); this file
//  owns the buffering, the toggle and the file writes.
// ===========================================================================
#include "consolelog.h"
#include "logstore.h"
#include "config.h"

// The object every `Serial.print` in CardSat resolves to, via the macro in
// config.h. Non-static: every translation unit binds to THIS one.
ConsoleLog::Tee CardSatSerialTee;

namespace ConsoleLog {
namespace {

// Accumulated console bytes, written out in one go. 512 B is sized from the
// measured line rate, not guessed: Doppler tracing runs ~8 lines/s (CIV_DEBUG=1
// ships enabled), so this turns ~8 file operations/sec into ~1. Small enough
// that a hard crash loses only a few lines.
constexpr size_t   BUF_BYTES = 512;
// Flush a PARTIAL buffer after this long, so a quiet console still lands rather
// than sitting in RAM waiting for traffic that may never come.
constexpr uint32_t IDLE_MS   = 1000;

char     s_buf[BUF_BYTES];
size_t   s_len     = 0;
bool     s_on      = false;
uint32_t s_lastPut = 0;
// Reentrancy guard. Logstore (or Store beneath it) can itself Serial.print on
// error -- which lands straight back in teeByte() and would recurse until the
// stack died. With this, a write during a write is simply dropped from the file
// (it still reaches the live console).
bool     s_inFlush = false;

void flushLocked() {
  if (s_len == 0 || s_inFlush) return;
  s_inFlush = true;
  s_buf[s_len] = 0;
  // raw(): console text already carries its own prefixes ("[net] ...") and
  // arrives as whole lines; Logstore's uptime stamp would double up.
  Logstore::raw(Logstore::LOG_CONSOLE, s_buf);
  s_len = 0;
  s_inFlush = false;
}

}  // namespace

// ---- The byte sinks the Tee in config.h calls ------------------------------
void teeByte(uint8_t c) {
  if (!s_on || s_inFlush) return;
  if (s_len >= BUF_BYTES - 2) flushLocked();   // leave room for the NUL
  if (c == '\r') return;                       // CRLF -> LF: one form in the file
  s_buf[s_len++] = (char)c;
  s_lastPut = millis();
}

void teeBytes(const uint8_t* b, size_t n) {
  if (!s_on || s_inFlush || !b) return;
  for (size_t i = 0; i < n; ++i) teeByte(b[i]);
}

void begin() { s_lastPut = millis(); }

void enable(bool on) {
  if (s_on == on) return;
  if (!on) flushLocked();               // never silently drop what is captured
  s_on = on;
  if (on) Logstore::raw(Logstore::LOG_CONSOLE, "---- console capture on ----");
}

bool enabled() { return s_on; }

void flush() { flushLocked(); }

void poll() {
  if (!s_on || s_len == 0) return;
  if (millis() - s_lastPut >= IDLE_MS) flushLocked();
}

}  // namespace ConsoleLog
