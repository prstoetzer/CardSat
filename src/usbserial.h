#pragma once
// ===========================================================================
//  usbserial.h  --  USB-host serial transport for CAT (CAT_USB)
// ===========================================================================
//
//  Drives a USB<->serial adapter (FTDI / CP210x / CH34x) or a CDC-ACM device as
//  the CAT transport, so a radio can be reached through the Cardputer's USB-C
//  port instead of the G1/G2 UART and its level shifter.
//
//  WHY THIS IS SMALL: the three wire-level CAT backends (CivRig / YaesuRig /
//  KenwoodRig) already talk through `Stream* _stream` and know nothing about the
//  transport underneath -- only their begin() binds a UART. Rig::setExternalStream()
//  hands them a Stream instead, so every protocol, every radio and every command
//  works over USB unchanged. Nothing in the Doppler loop, calibration or UI knows.
//
//  ---- THE THREE HARD CONSTRAINTS -----------------------------------------------
//
//  1. USB HOST AND THE SERIAL CONSOLE SHARE THE S3'S ONE INTERNAL USB PHY.
//     Whichever way the console is built (HW CDC/JTAG or OTG TinyUSB CDC -- both
//     exist for S3 boards), it sits behind the same internal PHY the host stack
//     must take. So the console is released while USB CAT is engaged and restored
//     the moment it is not -- which is exactly what the operator asked for, and is
//     the pattern Mini-FT8 uses for its FATFS-to-PC mode. See consoleDown()/
//     consoleUp() in the .cpp. Whether the console RE-ATTACHES without a reboot
//     after host teardown is a bench question (the PHY mux may stay with OTG);
//     losing it until reboot would be cosmetic, not functional.
//
//  2. RAM. EspUsbHost's DeviceState slots are MEMBERS of the host object, sized in
//     the LIBRARY's header at compile time (8 on the S3), and the library's own
//     .cpp is a separate translation unit -- so the count cannot be shrunk from
//     CardSat's side: a per-file #define changes our view of the object without
//     changing the code that runs on it. That exact mismatch was the 0.9.58-wip
//     enable-USB-CAT freeze; see the comment in the .cpp. Two levers instead:
//     (a) the host object is heap-allocated at the FIRST engage rather than living
//     in .bss, so a CardSat that never uses USB CAT never pays for it (end() keeps
//     it resident after that -- see end()); (b) a GLOBAL define IS available in
//     the Arduino IDE after all -- the sketch-folder build_opt.h is passed to
//     every translation unit, libraries included (verified against arduino-esp32
//     3.2.1 platform.txt; details in the .cpp) -- and CardSat ships one with
//     -DESP_USB_HOST_MAX_DEVICES=4 (root hub + adapter, headroom for the USB
//     rotator support that shipped in 0.9.58).
//
//  3. COMPOSITE DEVICES. A plain USB<->serial cable (the FTDI CI-V cable this was
//     written for) is a single-interface device: the simple case. A modern rig
//     plugged in directly (IC-9100/IC-9700 USB-B) is COMPOSITE -- a serial
//     interface plus a USB Audio interface -- and EspUsbHost's own README warns the
//     ESP32-S3 "has a small number of USB host channels" which "composite devices,
//     hubs, audio ... can exhaust quickly". We bind only the serial interface and
//     never claim audio (CardSat has no use for rig audio). Whether that is enough
//     on a real composite rig is UNVERIFIED -- see usbLastError().
//
//  ---- FEASIBILITY / REVERSIBILITY ----------------------------------------------
//  The whole feature is behind CARDSAT_HAS_USBCAT. Compile it out and CardSat is
//  byte-for-byte what it was: makeRig() never returns a USB transport, the setting
//  row disappears, and Rig::setExternalStream() is simply never called. Nothing
//  else in the tree depends on it.
// ===========================================================================
#include <Arduino.h>
#include "config.h"

// The CARDSAT_HAS_USBCAT flag lives in config.h (settings.h needs it too).
#if CARDSAT_HAS_USBCAT

namespace UsbSerial {

  // Bring the USB host up and bind the first USB-serial device found.
  // Releases the USB CDC console first (constraint 1). Returns false and sets
  // lastError() if the host will not start or no serial device enumerates.
  bool begin(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);

  // Detach the CDC port and stop CAT. The IDF host stack and the ~11.8 KB host
  // object STAY RESIDENT until reboot, by design: EspUsbHost cannot release its
  // client (checked v2.3.0 and unchanged through v2.3.2: end() kills the client
  // task before running client-scoped
  // cleanup), so a real teardown leaves the stack installed with a live client --
  // and then even a rebind is refused. Bench-proven over eight revisions; the full
  // account is in end() in the .cpp. Consequences the caller must know:
  //   * A re-engage is FAST and reliable -- it rebinds the resident host.
  //   * The USB CDC console does NOT come back (the host still owns the PHY).
  //   * The RAM is not returned until reboot. One host, ever, so it is bounded.
  // Safe to call when not started.
  void end();

  bool    active();          // is the host up and a device bound?
  Stream* stream();          // the CAT transport, or nullptr when not active

  // Human-readable reason the last begin() failed, for the Settings/CAT self-test
  // UI. Empty when there is nothing to report.
  const char* lastError();

  // ---- THE 0.9.58-wip FREEZE, for the record ----------------------------------
  // Root cause, found by decoding a task-watchdog coredump backtrace:
  //
  //     loopTask -> loop() -> App::loop()+474 -> App::serviceSerialCli()+79
  //
  // It was never in this file, nor in EspUsbHost. begin() calls Serial.end() to
  // release the USB PHY (correct, and unavoidable). HWCDC::end() frees rx_queue,
  // after which HWCDC::available() returns **-1**, and App::serviceSerialCli()'s
  // `while (Serial.available())` treats -1 as true -- an infinite loop, no yield,
  // on a console that no longer exists. It ran at app.cpp:4827, BEFORE the Doppler
  // tick, which is why every tick marker stayed silent and the screen froze on
  // "engaged".
  //
  // The lesson worth keeping: tearing down a peripheral does not stop the rest of
  // the firmware from POLLING it. Anything that touches Serial must tolerate the
  // console being gone -- tools/check_stream_guards.py enforces the `> 0` form,
  // and every backend's reads now check the -1 sentinel.
  //
  // ---- Freeze forensics -------------------------------------------------------
  // begin() tears the console down before the riskiest calls, so a hang there is
  // MUTE: no serial, and the status bar never gets written. That is exactly what
  // the 0.9.58-wip "Radio On freezes, no message" report looked like, and it left
  // nothing to diagnose from.
  //
  // So begin() drops a breadcrumb in RTC_NOINIT RAM before each risky step and
  // clears it on every exit. RTC RAM survives a watchdog reset / ESP.restart()
  // (not a power cycle), so if the device hangs and is reset, the NEXT boot can
  // report exactly which call never returned -- on screen, no console needed.
  // Same mechanism the LoTW multi-batch run already uses (see app.cpp).
  enum Stage : uint8_t {
    USBCAT_STAGE_NONE = 0,       // not in begin(), or begin() completed
    USBCAT_STAGE_ALLOC,          // allocating the host / cdc objects
    USBCAT_STAGE_CALLBACK,       // registering onDeviceConnected
    USBCAT_STAGE_CONSOLE_DOWN,   // Serial.flush()/end()  <-- HWCDC::end() hazard
    USBCAT_STAGE_HOST_BEGIN,     // EspUsbHost::begin()   <-- PHY claim
    USBCAT_STAGE_ENUM_WAIT,      // bounded wait for connected()
    USBCAT_STAGE_BIND,           // cdc->begin()      -- attach + push baud
    USBCAT_STAGE_BIND_CFG,       // cdc->setConfig()  -- line coding
    USBCAT_STAGE_BIND_DTR,       // cdc->setDtr()
    USBCAT_STAGE_BIND_RTS,       // cdc->setRts()
    USBCAT_STAGE_BIND_DONE,      // past every library call; only bookkeeping left
    // ---- past begin(): the CALLER's steps ------------------------------------
    // begin() returning is not the end of the engage path, and a hang after it
    // looked identical on screen to a hang inside it -- BIND_DONE was simply the
    // last thing painted, because STAGE_NONE paints nothing. These make the rest
    // of the path visible. Reported by the caller, not by begin().
    USBCAT_STAGE_RIG_STREAM,     // rig->setExternalStream()
    USBCAT_STAGE_RIG_BEGIN,      // rig->begin()
    USBCAT_STAGE_RIG_ADDR,       // rig->setAddress()
    USBCAT_STAGE_RIG_DELAY,      // rig->setCmdDelay()
    USBCAT_STAGE_ENGAGED,        // fully up; the next CAT tick is the Doppler loop
    // ---- the first Doppler tick ---------------------------------------------
    // "engaged" was the last paint and the freeze followed it, so the hang is in
    // the tick -- which runs on the NEXT loop() and painted nothing of its own.
    // Every CAT loop now has a hard deadline (nothing can spin), so if it still
    // hangs here the cause is not a busy-loop and these will say which call.
    USBCAT_STAGE_TICK_ENTER,     // the tick ran at all (before ANY branch)
    USBCAT_STAGE_TICK_PTT,       // rig->readPtt()      -- first CAT traffic
    USBCAT_STAGE_TICK_READ,      // rigReadDownlinkFreq()
    USBCAT_STAGE_TICK_WRITE,     // the Doppler frequency writes
    USBCAT_STAGE_TICK_DONE,      // a full tick completed: tracking is alive
    // ---- teardown ------------------------------------------------------------
    // Engage is instrumented per step; disengage was not, which is why a leak
    // could be seen (largest free block 31.7 KB before an engage, 18 KB after a
    // disengage) but not ATTRIBUTED. Each step logs heap+largest, so the next
    // bench run says which call fails to give memory back.
    USBCAT_STAGE_END_CDC,        // cdc->end()      -- detach from the host
    USBCAT_STAGE_END_HOST,       // host->end()     -- stop tasks, uninstall IDF stack
    USBCAT_STAGE_END_DELETE,     // delete both objects
    USBCAT_STAGE_END_CONSOLE,    // consoleUp()     -- Serial.begin() reallocs the rings
    USBCAT_STAGE_END_DONE,       // teardown complete: compare against the engage header
  };

  // The stage recorded when the firmware last died inside begin(), or
  // USBCAT_STAGE_NONE if the last boot was clean. Valid only after
  // checkLastBootStage() has run. Human-readable via stageName().
  Stage       lastBootStage();
  const char* stageName(Stage s);

  // The stage begin() is executing RIGHT NOW, readable while it is still blocked.
  // USBCAT_STAGE_NONE when begin() is not running. This is the reporting path that
  // needs no reset at all -- see the note on stage() in usbserial.cpp.
  Stage       liveStage();

  // Stack headroom (bytes never used) of the library's two host tasks, or 0 if the
  // task is gone/not found. Lookups truncate to FreeRTOS's 15-char stored name --
  // the untruncated "EspUsbHostClient" (exactly 16 chars) tripped xTaskGetHandle's
  // configASSERT and was the fix28 disengage panic. taskStackBytes() is the value
  // fed to BOTH tasks (kTaskStack in the .cpp); size it from these numbers, never
  // from a guess. Valid while engaged; call before end().
  // These probe the LIVE tasks and are only valid while USB CAT is engaged and
  // NOT tearing down. Do not call them from an end()-stage callback: end() kills
  // the tasks, and a high-water read racing that teardown reads a freed TCB (it
  // returned free=28208 of an 8192-byte stack on the bench, and the same memory
  // is what IDLE0 then reaps -- an IDLE0 panic right after "end: done").
  uint32_t    hostTaskHeadroom();
  uint32_t    clientTaskHeadroom();
  uint32_t    taskStackBytes();

  // Safe accessors for the same numbers. end() snapshots both marks BEFORE it
  // touches anything; these return the cached values and never touch a TCB, so
  // they are valid during and after teardown. 0 = never sampled.
  // False once a teardown or failed engage could NOT release the IDF host stack:
  // re-engaging would just hit ESP_ERR_INVALID_STATE (259) again, so begin()
  // refuses and says so. Cleared only by a reboot.
  bool        hostReleased();
  // Why the last uninstall was refused, when it was. Valid after a failed
  // finishUninstall: clients/devices still registered per usb_host_lib_info(),
  // how many drain polls ran, the union of event flags seen, and
  // usb_host_device_free_all()'s return. Empty string if the last teardown was
  // clean. Exists so a stuck stack reports its cause in one bench run.
  String      uninstallDiag();
  uint32_t    hostHeadroomSnapshot();
  uint32_t    clientHeadroomSnapshot();

  // Announce a stage from OUTSIDE begin() (the caller's post-begin steps). Same
  // effect as begin()'s internal marker: RTC breadcrumb + live value + onStage.
  void        markStage(Stage s);

  // Optional: called by begin() immediately after each stage marker is set, so the
  // caller can paint it. begin() runs on the caller's task, so if it blocks, the
  // caller cannot paint anything itself -- this is the only way the screen can name
  // the call that hung. Keep the callback trivial (set a status, draw); it runs on
  // the doomed task too. Null (the default) = no reporting.
  extern void (*onStage)(Stage s);

  // Rotator trace sink. The rotator port has no STAGES (it is a short, linear
  // bind, not begin()'s multi-second gauntlet), but it needs the same
  // observability: with a bare USB-serial adapter and no radio, a silent failure
  // is indistinguishable from a missing adapter, a wrong key, or a dead cable.
  // usbserial does not touch the filesystem -- the app owns the sink, exactly as
  // it does for onStage.
  extern void (*onRotTrace)(const char* line);

  // Call ONCE early in setup(), before any begin(). Latches the breadcrumb left by
  // a previous boot, then arms the marker for this boot.
  void checkLastBootStage();

  // Description of the bound device (e.g. "FTDI FT232R 0403:6001"), for the UI.
  // Empty when nothing is bound.
  const char* deviceName();

  // ---- Rotator port (a SECOND CDC on the same resident host) -------------------
  // USB CAT and a USB rotator can run at once: the host has 4 device slots
  // (build_opt.h) and 4 CDC slots (ESP_USB_HOST_MAX_CDC_SERIALS), so the library
  // supports it structurally. What it does NOT do safely is pick which adapter is
  // which -- see the note on binding in the .cpp. These calls own that problem.
  //
  // EXPERIMENTAL. Two-adapter operation could not be bench-tested before release.
  // Single-adapter use (rotator only, radio elsewhere) is the tested path.
  // Tell the port WHICH adapter is the rotator (a serialDeviceKey from the picker,
  // or "" to accept the only adapter present) and at what baud. Call before
  // rotBegin(); the app pushes this from settings.
  void        rotConfigure(const char* key, uint32_t baud);
  // Tell the CAT port WHICH adapter is the radio (a serialDeviceKey from the
  // picker, or "" to accept the only adapter that is not the rotator's). Call
  // before begin(); the app pushes this from settings. Symmetric with
  // rotConfigure() -- with two adapters, BOTH ports must be nominated or the
  // assignment is a coin flip.
  void        catConfigure(const char* key);
  bool        rotBegin();         // bind the rotator's CDC; true if a port is live
  void        rotEnd();           // release the rotator's CDC (host stays up)
  bool        rotActive();
  Stream*     rotStream();        // nullptr unless rotActive()
  const char* rotDeviceName();
  const char* rotLastError();

  // Enumerated serial adapters, for the Settings picker. The user chooses which
  // adapter is the rotator; we persist its serial/VID:PID and re-find it by that
  // rather than trusting enumeration order.
  // Bring the host up (if it is not already) and enumerate. This is what the
  // Settings "Scan USB adapters" action calls: the picker cannot offer a choice
  // between adapters the host has never seen.
  //
  // NOT free, and deliberately explicit rather than automatic on entering the
  // settings screen. The host claims the S3's one USB PHY, which takes the serial
  // console away, and per end() the host stays RESIDENT until reboot -- so a scan
  // costs ~11.8 KB and the console for the rest of the session. Making that a
  // side effect of opening a menu would be a nasty surprise; making it a
  // keypress makes it a choice. If USB CAT or a USB rotator is already engaged
  // the host is up anyway and this is nearly free.
  //
  // Blocks up to ~2.5 s waiting for enumeration. Returns the adapter count.
  uint8_t     scanAdapters();

  uint8_t     serialDeviceCount();
  const char* serialDeviceLabel(uint8_t i);   // "FTDI FT232R 0403:6001 #A50285BI"
  const char* serialDeviceKey(uint8_t i);     // stable id to persist (see .cpp)
}

#endif  // CARDSAT_HAS_USBCAT
