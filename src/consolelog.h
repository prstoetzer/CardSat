#ifndef CARDSAT_CONSOLELOG_H
#define CARDSAT_CONSOLELOG_H
// ===========================================================================
//  ConsoleLog -- a tee on Serial, so the console survives being taken away
// ===========================================================================
//  The problem. Engaging USB CAT or a USB rotator claims the S3's one USB PHY,
//  which kills the serial console for the rest of the session (the host is
//  resident until reboot -- see usbserial.h). Every Serial.print after that goes
//  nowhere. That is exactly when a field problem needs a trace.
//
//  The mechanism. arduino-esp32 defines `Serial` as a MACRO aliasing the real
//  device (`#define Serial HWCDCSerial` -- HardwareSerial.h:413, which is this
//  board's config: ARDUINO_USB_MODE=1 + ARDUINO_USB_CDC_ON_BOOT=1). So we can
//  redefine that macro to point at a tee. Print::write() is virtual and EVERY
//  Serial.print/printf/println funnels through it, so one object captures all
//  ~181 call sites with NONE of them edited. Verified on the host before being
//  written here.
//
//  ---- Cost, and why it is buffered -----------------------------------------
//  Logstore::line() is open+write+flush+close per call: ~6 ms on LittleFS
//  (modelled -- the bench will supply the real constant; every line is
//  timestamped, so the log profiles itself). CardSat ships CIV_DEBUG=1, so
//  Doppler tracking emits ~8 console lines/sec. Unbuffered that is ~48 ms/s of
//  BLOCKING flash I/O on loopTask -- the same task that runs Doppler, the UI,
//  WiFi and LoRa, and the same task that a LoTW upload already pushed into a
//  task-watchdog reset. ~5% of wall time, permanently, for a diagnostic.
//
//  So: accumulate into a buffer and write it out in one go. ~10x fewer file
//  operations, ~0.5% instead of ~5%.
//
//  The tradeoff is real and worth stating: a HARD crash loses whatever is still
//  in the buffer -- which is often the very tail you wanted. Mitigated three
//  ways: (1) flush() is called before every ESP.restart() and before a USB
//  engage, the two places we KNOW the tail matters; (2) an idle timer flushes a
//  partial buffer so a quiet log is never more than a second stale; (3) the USB
//  engage trace does NOT go through here -- it is written unbuffered by
//  Logstore directly, because that is the trace that has to survive a freeze.
//
//  Off by default. A diagnostic that costs 0.5% of the loop should be something
//  the operator turns on when diagnosing, not a tax everyone pays.
#include <Arduino.h>

namespace ConsoleLog {

// Install the tee. Call once from setup(), after Serial.begin(). Cheap and
// idempotent; does nothing until enable(true).
void begin();

// Turn file capture on/off at runtime (the Settings toggle). Turning it OFF
// flushes what is pending -- no silent loss.
void enable(bool on);
bool enabled();

// Write out whatever is buffered. Call before anything that may not return:
// ESP.restart(), a USB engage. Cheap and safe when nothing is pending.
void flush();

// Per-loop tick: flushes a partial buffer once it has sat for a moment, so a
// quiet console still lands. Call from App::loop().
void poll();

}  // namespace ConsoleLog

#endif  // CARDSAT_CONSOLELOG_H
