#include "usbserial.h"

#if CARDSAT_HAS_USBCAT

#include <esp_task_wdt.h>   // TWDT "user" subscription: watches this CODE, not a task
#include <esp_heap_caps.h> // largest-free-block: fragmentation vs genuine OOM
#include <freertos/task.h> // uxTaskGetStackHighWaterMark(): size the stacks from data
#include <atomic>          // release fence when publishing an adapter registry entry

// ---- Why there is NO ESP_USB_HOST_MAX_DEVICES define here ----------------------
// There was one (0.9.58-wip pinned it to 1 right here, before the include), and it
// FROZE THE FIRMWARE the moment USB CAT was enabled. The mechanism, for the next
// person who is tempted:
//
//   The slot array is a MEMBER of the host object -- `DeviceState
//   devices_[ESP_USB_HOST_MAX_DEVICES]` in EspUsbHost.h, with more members laid out
//   after it -- and the library's own EspUsbHost.cpp is a separate translation unit
//   that sees the header's default (8 on the S3). A #define in this file changes
//   what sizeof(EspUsbHost) means HERE and nothing else: this file allocated a
//   1-slot object, then called onDeviceConnected()/begin() -- code compiled in the
//   library's unit, addressing members at 8-slot offsets. The very first library
//   call wrote past the end of the object, straight through whatever the linker
//   had placed next (starting with this file's own statics), and the host task then
//   initialized "slots" 1..7 in the same foreign memory. One-definition-rule
//   violations do not warn; they corrupt.
//
// The only CONSISTENT way to change the slot count is a global -D that the
// library's translation unit compiles under too. The Arduino IDE HAS one after
// all: a build_opt.h in the sketch folder is passed as an @-response-file to
// EVERY c/cpp compile -- sketch, core and libraries alike (verified against
// arduino-esp32 3.2.1 platform.txt: recipe.c.o.pattern and recipe.cpp.o.pattern
// both carry "@{build.opt.path}", and prebuild hook 5 copies the sketch's
// build_opt.h into the build dir). CardSat ships one with
// -DESP_USB_HOST_MAX_DEVICES=4 (root hub + adapter, headroom for the USB
// rotator that shipped in 0.9.58): 4 fewer DeviceState slots means a smaller host object AND a
// smaller CONTIGUOUS block for begin() to find on a fragmented heap -- watch
// the ALLOC-stage heap delta on the next bench engage for the exact number.
// NOTE: gcc response files cannot carry comments (hence this one lives here),
// and the IDE's core cache does not watch build_opt.h -- do a full rebuild
// after adding or editing it. The footprint is ALSO a lifetime problem: the
// host is heap-allocated between begin() and end() -- see s_host below.

#include <EspUsbHost.h>
#include <new>

namespace UsbSerial {

// taskHeadroomByName() is defined below, beside the public headroom accessors it
// backs, but snapshotHeadroom() (in the anonymous namespace) calls it from end()
// further up -- hence this forward declaration. It must sit at UsbSerial scope
// and carry `static`, matching the definition: declaring it INSIDE the anonymous
// namespace instead creates a SECOND function with internal linkage, and the
// accessors below then fail with "call of overloaded ... is ambiguous".
static uint32_t taskHeadroomByName(const char* name);

namespace {
  // ---- Heap-allocated for exactly the time USB CAT is engaged --------------------
  // At the library's 8-slot S3 default the host object is on the order of 10-20 KB:
  // each slot carries a 512-byte vendor-RX buffer plus interface/endpoint/audio
  // tables in static fields (the larger serial/network rings are heap-on-demand).
  // Holding that in .bss forever -- on a no-PSRAM board with ~55 KB of free heap,
  // where a stranded 6 KB once broke a TLS upload -- would tax every build that
  // merely COMPILED the feature in. Allocating in begin() and freeing in end()
  // makes USB CAT cost RAM only while a radio is actually being driven through it,
  // which is the same lifetime the IDF host stack (daemon task, class-driver task,
  // transfer buffers) already had. The price: begin() can now fail on a fragmented
  // heap -- and says so on the status bar, which beats silently owning the RAM for
  // a feature the operator never engages.
  EspUsbHost*          s_host   = nullptr;
  EspUsbHostCdcSerial* s_cdc    = nullptr;
  EspUsbHostCdcSerial* s_rotCdc = nullptr;   // rotator CDC port (shared host); declared here
                                             //   so CAT end() can check it for shared teardown
  EspUsbHostCdcSerial* s_cdc2   = nullptr;   // CAT-B: the second radio's CDC (dual-USB CAT),
                                             //   same shared-host rules as the rotator port
  bool                 s_active = false;
  bool                 s_bound  = false;   // a serial device enumerated
  bool                 s_sawDev = false;   // ANY device enumerated (see begin())
  char                 s_err[64] = "";
  char                 s_dev[48] = "";

  void setErr(const char* e) { snprintf(s_err, sizeof(s_err), "%s", e ? e : ""); }

  // ---- Console handover -------------------------------------------------------
  // The USB host must take the S3's ONE internal USB PHY, and the serial console
  // sits behind that same PHY. Drop the console before starting the host, bring it
  // back after stopping. The PC sees the port disappear and reappear -- terminal
  // programs generally cope, and Mini-FT8's FATFS-to-PC mode has the same property.
  //
  // ---- Why this is NOT a bare Serial.end() (0.9.58-wip freeze #2) --------------
  // A bare `Serial.flush(); Serial.end();` here froze the firmware on "Radio On"
  // with NO status message -- i.e. before begin() could reach any of its error
  // paths. `Serial` is not one class:
  //
  //   HardwareSerial.h: ARDUINO_USB_CDC_ON_BOOT && ARDUINO_USB_MODE -> HWCDCSerial
  //                     ARDUINO_USB_CDC_ON_BOOT && !ARDUINO_USB_MODE -> USBSerial
  //                     !ARDUINO_USB_CDC_ON_BOOT                     -> Serial0 (UART0)
  //
  // CardSat builds with CDC On Boot, and BOTH documented boards (M5StampS3,
  // ESP32S3 Dev Module) default to usb_mode=1 -- so `Serial` is HWCDCSerial, the
  // USB-Serial-JTAG peripheral. Two hazards in arduino-esp32 3.2.1's HWCDC.cpp:
  //
  //   1. HWCDC::end() calls esp_intr_free(intr_handle) with NO null guard, then
  //      sets intr_handle = NULL. intr_handle is a FILE-STATIC shared by every
  //      HWCDC path. Calling end() when begin() never ran -- or twice, which our
  //      old begin()-fails -> consoleUp() -> later end() sequence could do --
  //      passes NULL to esp_intr_free() and aborts/hangs inside the IDF.
  //   2. HWCDC::flush() takes tx_lock and can spin up to tx_timeout_ms. Bounded,
  //      so not the freeze itself, but pointless work on a port about to die.
  //
  // Guarding on the handle we control (s_consoleDown) makes the calls strictly
  // paired, so end() can never run without a matching begin(), and never twice.
  // The `if (Serial)` probe is deliberate: HWCDC::operator bool() reports whether
  // a host is actually attached, so a headless CardSat skips the flush entirely
  // rather than burning tx_timeout_ms draining into a port nobody is reading.
  bool s_consoleDown = false;

  // Did the last end() see the host tasks actually give their memory back? If not,
  // zombie tasks may still hold a pointer into freed memory and the IDF host stack
  // may still be installed -- a re-engage then reboots the device and takes SD and
  // WiFi with it (observed on the bench). Latch it and refuse instead.
  bool s_hostReleased = true;
  // True while an engage is being retried after an incomplete teardown, so a failure
  // can name that cause instead of reporting a generic error. Cleared on success.
  bool s_retryAfterStuck = false;
  // True: keep the USB host installed when the last port detaches (see
  // releaseHostIfIdle). Default ON -- a device whose firmware does not re-initialise
  // after re-enumeration cannot survive a teardown between engages.
  bool s_keepHostResident = true;
  void releaseHostNow();     // defined below; releaseHostIfIdle() calls it

  // Tell the DEVICE the port is closing before dropping it.
  //
  // On a CDC-ACM device, DTR is what signals "the host has the port open" -- it is
  // asserted at bind, and CDC has no other close notification. EspUsbHostCdcSerial::
  // end() only removes the object from the host's callback array; it never
  // de-asserts anything, so CardSat raised DTR and then simply vanished. A radio
  // that keys its CAT session off DTR therefore never sees the session end.
  //
  // Bench symptom this explains: after disengaging on the Cardputer, the TH-D75
  // would not accept CAT again until the RADIO was power-cycled -- there is no
  // Kenwood "CAT off" command, the port state IS DTR, and ours never dropped.
  //
  // Failures are ignored on purpose: if the radio has already been switched off the
  // control transfer cannot land, and that is exactly the case where nothing needs
  // saying. Order matches the bind (DTR then RTS), reversed in sense.
  void cdcClosePort(EspUsbHostCdcSerial* p) {
    if (!p) return;
    p->setDtr(false);
    p->setRts(false);
  }

  // M2: set when end()/rotEnd() timed out with USB tasks still alive. The host object is
  // retained (deleting it would be a use-after-free) and re-engage is blocked until a reboot.
  bool s_hostTeardownStuck = false;

  // Stack high-water marks, sampled by end() while the tasks are still alive and
  // idle (see snapshotHeadroom()). Cached because the ONLY safe time to read them
  // is before teardown starts, while the only useful time to REPORT them is after
  // -- from a stage callback that does slow SD I/O.

  uint32_t s_hostHeadroom   = 0;
  uint32_t s_clientHeadroom = 0;

  // One knob feeds both library tasks (EspUsbHostConfig::taskStackSize). Every
  // 1 KB cut here returns 2 KB of heap (two tasks).
  //
  // 4096, on two independent bench runs that agree closely:
  //     run 1: EspUsbHost used=1140 free=7052 | Client used=1804 free=6388
  //     run 2: EspUsbHost used=1132 free=7060 | Client used=1804 free=6388
  // Peak is the client task at 1,804 B. 4096 leaves ~2,292 B of headroom -- 2.3x
  // the measured peak -- and returns 8 KB of heap (two tasks x 4 KB).
  //
  // Both figures are with a CDC serial adapter, which is the only device class
  // CardSat drives today and the deepest of the ones it plausibly will: the CDC
  // path (control transfers, line coding, bulk in/out) is what these numbers
  // measured. A USB rotator would be another CDC/FTDI adapter -- same driver, same
  // depth. If a future device class ever lands (HID, a hub with several tiers),
  // re-read the END_CDC headroom log before trusting this: the log prints it on
  // every disengage precisely so the number is never a guess. Raise it back to
  // 8192 at the first sign of a stack-overflow panic in either task.
  // 4096 -> 6144. The 4096 figure came from END_CDC high-water-mark logs taken with a
  // single directly-attached device, and it does not survive a HUB:
  //     no hub:  EspUsbHost used 1156 of 4096, free 2940
  //     hub:     EspUsbHost used 2684 of 4096, free 1412
  // IDF's multi-level external hub support recurses through port enumeration, and this
  // file's own rule -- safe = used + 2048 -- wants 4732, MORE than was allocated. 1412
  // bytes of headroom on a task that reboots the device when it overflows is not a
  // margin, and hub scans were observed to be unreliable and to end in resets.
  //
  // 6144 gives 3460 bytes of headroom against the measured hub peak and still sits well
  // under the library's 8192 default. The high-water mark is printed at every disengage,
  // so this stays a measurement rather than a guess -- if a hub-with-devices peak ever
  // exceeds ~4 KB, raise it again.
  const uint32_t kTaskStack = 6144;

  // (The teardown poke timer was removed in fix37. It woke the daemon out of
  // usb_host_lib_handle_events(portMAX_DELAY) so its cleanup could run -- but under
  // the OLD (pre-2.4.1) library that cleanup could never complete: its end() killed
  // the CLIENT task first, and every call in the daemon's cleanup path was
  // client-scoped. Waking the daemon only got it far enough to fail. See the note
  // in end().)

  // Task-by-name lookups that cannot assert. Two traps in one, both from the
  // FreeRTOS source: (1) xTaskGetHandle() configASSERTs strlen(name) <
  // configMAX_TASK_NAME_LEN (16) -- and "EspUsbHostClient" is EXACTLY 16 chars,
  // which was the fix28 disengage panic (abort inside xTaskGetHandle, reached
  // from the END_CDC stage's headroom log); (2) xTaskCreate STORES names
  // truncated to 15 chars, so the full 16-char query could never match anyway.
  // Truncating to the stored form makes the lookup both safe and correct.
  TaskHandle_t taskByName(const char* name) {
    char q[configMAX_TASK_NAME_LEN];
    strncpy(q, name, sizeof(q) - 1);
    q[sizeof(q) - 1] = 0;
    return xTaskGetHandle(q);
  }

  // ---- Why the OLD library's uninstall did not stick (the 259) -- HISTORY --------
  // Bench, fix32, against the pre-2.4.1 library: teardown freed its memory and the
  // tasks exited, yet a re-engage failed with ESP_ERR_INVALID_STATE (259) from
  // usb_host_install() -- the IDF host stack was still installed. Cause, from the
  // two sources side by side:
  //
  //   * usb_host_uninstall() REFUSES unless process_pending_flags, lib_event_flags
  //     and flags.val are all zero (IDF v5.4 usb_host.c:585-588).
  //   * Only usb_host_lib_handle_events() clears them (line 647 / 669) and it is
  //     also what clears the handling_events flag (line 666).
  //   * That library's taskLoop() called usb_host_uninstall() and IGNORED ITS
  //     RETURN, then self-deleted. So the failure was silent: the task vanished
  //     (our wait passed, the heap came back) while the stack stayed installed.
  //
  // Our own wake poke was what dirtied the flags, and finishUninstall() -- a
  // hand-rolled "poll usb_host_lib_handle_events(0) until it reports nothing left,
  // then usb_host_uninstall() ourselves" pass -- finished the job. Both the poke
  // and the finisher were removed (fix37) when the library grew a correct end():
  // 2.4.1+ performs that exact handshake internally, and the pinned 2.5.2
  // (source-checked) splits it into releaseClientResources() /
  // uninstallHostLibrary() with the uninstall result checked and logged. The
  // mechanism stays recorded here because it explains end()'s ordering rules below
  // and will matter again if the library is ever swapped.

  // Sample both stacks' high-water marks. MUST be called before teardown starts:
  // uxTaskGetStackHighWaterMark walks a live TCB, and reading one mid-deletion
  // reads freed memory (the bench's free=28208-of-8192 nonsense).
  void snapshotHeadroom() {
    s_hostHeadroom   = taskHeadroomByName("EspUsbHost");
    s_clientHeadroom = taskHeadroomByName("EspUsbHostClient");
  }

  // (waitTasksGone removed in fix37: nothing stops the host any more, so there
  // are no exiting tasks to wait for. The reap-ordering it encoded -- a
  // self-deleting task is reaped later by IDLE0, so a name lookup going NULL does
  // NOT mean its stack is freed -- is worth remembering if anyone revisits this.)

  // ---- The RTC breadcrumb (see usbserial.h "Freeze forensics") ----------------
  // RTC_NOINIT survives a reset but NOT a power cycle, and comes up as garbage on
  // a cold boot -- hence the magic word, exactly as the LoTW batch state does.
  RTC_NOINIT_ATTR uint8_t  s_rtcStage;
  RTC_NOINIT_ATTR uint32_t s_rtcStageMagic;
  const uint32_t USBCAT_STAGE_MAGIC = 0x05BCA757;  // "USBCAT ST"
  Stage s_lastBootStage = USBCAT_STAGE_NONE;

  // Written before each risky call. Deliberately just two word stores -- no flash,
  // no lock, nothing that could itself hang on the path we are trying to measure.
  //
  // s_liveStage is the same value, readable by the UI while begin() is still
  // running. That matters because every reset-based scheme tried so far has failed
  // to report anything: the task watchdog does not fire (a blocking wait yields, so
  // nothing starves), the RST button clears RTC RAM, and a subscribed TWDT user did
  // not panic even after a minute. A hang that produces NO reset produces NO report.
  // The screen does not need a reset: App::draw() paints from a different code path
  // than the one that is blocked, so a plain byte read tells us where we stopped.
  volatile uint8_t s_liveStage = USBCAT_STAGE_NONE;

  // Emit one rotator trace line. No-op until the app installs a sink.
  inline void rotTrace(const char* line) { if (onRotTrace) onRotTrace(line); }

  inline void stage(Stage s) {
    s_rtcStage      = (uint8_t)s;
    s_rtcStageMagic = USBCAT_STAGE_MAGIC;
    s_liveStage     = (uint8_t)s;
    if (onStage) onStage(s);   // paint it now: we may not return from what comes next
  }

  // ---- The freeze watchdog ----------------------------------------------------
  // Why the breadcrumb needs its own reset: the observed freeze does NOT trip the
  // task watchdog. That is a strong clue -- the TWDT only fires when a task
  // STARVES, so a hang that yields (a semaphore take, a delay, a blocking wait)
  // keeps the idle task running and the TWDT quiet forever. Without a reset,
  // nothing reboots, so nothing ever reads the breadcrumb back.
  //
  // The mechanism is a TWDT "user" subscription. Three approaches were tried and
  // the first two do not survive contact with the Arduino toolchain:
  //
  //   * esp_timer ESP_TIMER_ISR dispatch: the enum value only EXISTS when
  //     CONFIG_ESP_TIMER_SUPPORTS_ISR_DISPATCH_METHOD is set. It is `default n`
  //     and Arduino ships a FIXED prebuilt sdkconfig a sketch cannot change --
  //     "'ESP_TIMER_ISR' was not declared in this scope". (ESP_TIMER_TASK is
  //     useless anyway: dispatched from a task, which is what is blocked.)
  //   * rtc_wdt.h: the whole API is wrapped in
  //     `#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2` -- the S3 is
  //     excluded. It would not have compiled either.
  //   * esp_task_wdt_add_user() IS the right tool, and is plain public API with
  //     no Kconfig-gated symbols in its header.
  //
  // Why a "user" and not esp_task_wdt_add(): a TASK subscription only fires when
  // the task STARVES, and the observed freeze does not starve anything (the bench
  // reports no watchdog at all) -- a blocking wait yields, the idle task runs, the
  // TWDT stays quiet. A USER subscription watches a SPAN OF CODE: begin() must
  // call esp_task_wdt_reset_user() before the timeout or the TWDT elapses. A
  // block that never returns never resets it, so it fires. That is exactly the
  // failure we cannot otherwise see.
  //
  // Arduino's own lib-builder defconfig sets CONFIG_ESP_TASK_WDT_PANIC=y, so a
  // timeout panics -- a SOFTWARE reset, which preserves RTC_NOINIT and therefore
  // the breadcrumb. (Verified in esp32-arduino-lib-builder/configs/defconfig.common.)
  //
  // Subscribed only around begin()'s risky span and unsubscribed the moment it
  // returns, so it can never reboot a healthy radio: the whole span is bounded
  // well under the timeout (host begin() <= 1 s internally, enum wait <= 2.5 s).
  esp_task_wdt_user_handle_t s_freezeWdt = nullptr;

  void armFreezeWatchdog(uint32_t /*ms*/) {
    // The TWDT's timeout is global and owned by the Arduino core; we do not
    // reconfigure it (that would change every other subscriber's contract).
    // Whatever it is, it is finite -- which is all we need: a hang here stops
    // resetting the user and the TWDT eventually panics with the breadcrumb intact.
    if (s_freezeWdt) return;                       // already subscribed
    if (esp_task_wdt_add_user("usbcat", &s_freezeWdt) != ESP_OK)
      s_freezeWdt = nullptr;                       // no watchdog; begin() still works
  }

  void feedFreezeWatchdog() {
    if (s_freezeWdt) esp_task_wdt_reset_user(s_freezeWdt);
  }

  void disarmFreezeWatchdog() {
    if (!s_freezeWdt) return;
    esp_task_wdt_delete_user(s_freezeWdt);
    s_freezeWdt = nullptr;
  }

  // ---- Port binding state ----------------------------------------------------
  // Which adapter each port owns. ANY_ADDRESS (0xff) = "not bound". These exist
  // so the two CDC ports can never both take devices_[first] -- see the binding
  // note at the CAT bind site and in the rotator port below.
  // Pick the adapter the RADIO should bind, mirroring rotBegin()'s logic exactly.
  // Returns an index into s_serDev, or -1 with s_err set.
  //
  // The exclusion MUST be symmetric. rotBegin() has always refused to take the
  // adapter the radio is driving -- but begin() had no reciprocal check, so with
  // ONE adapter plugged in the radio bound the very device the rotator was
  // already using and both reported "engaged" onto one wire. Doppler writes and
  // rotator commands down the same port: precisely the misbind the explicit-
  // address work exists to prevent, walking in through the unguarded door.
  int catPickAdapter();
  bool waitForAdapterKey(const char* key, uint32_t ms);  // dual-USB: await a nominated adapter
  int  cat2PickAdapter();                                // CAT-B's adapter choice (dual-USB CAT)

  // Physical-disconnect notices raised on the USB host task and consumed by the
  // main loop. volatile is sufficient for a one-way sticky bool: the writer only
  // ever sets it and the reader only ever clears it, so there is no value to tear.
  volatile bool s_catLost = false, s_cat2Lost = false, s_rotLost = false;
  uint8_t  s_catAddress = 0xff;      // the adapter the RADIO bound
  uint8_t  s_rotAddress = 0xff;      // the adapter the ROTATOR bound
  uint8_t  s_cat2Address = 0xff;     // the adapter the SECOND radio (CAT-B) bound
  char     s_catWantKey[40] = {0};   // adapter the user nominated as the RADIO
  char     s_rotWantKey[40] = {0};   // adapter the user nominated as the ROTATOR
  uint32_t s_rotBaud        = 9600;  // rotator line speed (app pushes from settings)

  // ---- Enumerated serial adapters -------------------------------------------
  // Filled by onDev() as devices enumerate; read by the Settings picker and by
  // rotBegin() to resolve the user's chosen adapter to a device ADDRESS. This is
  // the data that makes explicit binding possible -- without it a second CDC can
  // only say ANY_ADDRESS and race the first for whatever enumerated earliest.
  struct SerialDev {
    uint8_t  address;
    uint16_t vid, pid;
    char     label[48];
    char     key[40];
    // Tombstone (audit finding A). Set by onGone() on the host task when the
    // device disconnects; every resolver skips dead entries. Without this the
    // registry was append-only for the life of a shared host, so a replug during
    // a dual-port session (the very sessions dual USB exists for) left a stale
    // entry whose serial-first KEY matched the live one -- and every first-match
    // resolver bound the stale, dead ADDRESS. A tombstone is used instead of
    // compaction because removal must respect the publication rules here: the
    // writer is the host task, the readers are the main task mid-scan, and a
    // single byte store is safe where shifting entries under a reader is not.
    volatile uint8_t dead;
  };
  SerialDev s_serDev[4];
  // VOLATILE + a release barrier before the count is bumped (see onDev): this is
  // written on the USB host's task and read on the main task. The entry must be
  // fully populated BEFORE the count that publishes it becomes visible, or a
  // reader can see s_serDevN include a half-written label/key. This is publication
  // ordering, not mutual exclusion -- adds are append-only and the array is only
  // reset while no reader is mid-scan (host start), which is what makes that
  // sufficient here.
  volatile uint8_t s_serDevN = 0;
  volatile uint32_t s_lastDevMs = 0;   // when the newest adapter appeared (quiet-period timing)
  // A HUB is present on the bus. Two things change when it is, and both were wrong:
  //
  //  1. TIMING. A directly-attached hub enumerates fast, then the IDF hub driver
  //     still has to power its ports, wait bPwrOn2PwrGood, debounce ~100 ms per
  //     USB 2.0 s9.1.2, reset 10-50 ms and run a full enumeration PER CHILD --
  //     sequentially. So the first device to appear is the hub, and its children
  //     follow hundreds of milliseconds later. The old wait broke as soon as
  //     nothing new had arrived for 400 ms, which the hub satisfies on its own:
  //     the scan settled at ~700 ms and reported the hub and nothing else. That is
  //     the bench's "cannot see any USB devices beyond a powered hub" -- and, since
  //     the Cardputer has ONE port, it is also why dual-USB CAT (which REQUIRES a
  //     hub for two adapters) has never enumerated its radios.
  //
  //  2. SELECTABILITY. A hub is not an adapter. It has no serial OUT endpoint, so
  //     binding one can never carry CAT -- but onDev() registered every device, so
  //     the hub appeared in the Settings picker as a choice, and an un-nominated
  //     engage could take it as "the first adapter". (Before the 0.9.70 finding-B
  //     check that bind then reported ENGAGED on a hub.)
  volatile bool s_sawHub = false;
  // Set when a device enumerated but could not be recorded: the 4-slot adapter
  // registry was full. Surfaced by the scan so the operator is told, rather than
  // silently seeing one fewer adapter than is plugged in.
  volatile bool s_devRegistryFull = false;

  // Enumeration budgets. Behind a hub everything is slower and staged, so both the
  // settle window and the overall cap have to grow -- a cap that expires mid-scan
  // reports a partial bus, which is indistinguishable to the operator from a
  // missing adapter.
  // BOOTSTRAP BUG, fixed: the cap used to be `s_sawHub ? 9000 : 2500`, so the long
  // budget that exists FOR HUBS was gated on having already enumerated one. A hub
  // needing more than 2500 ms could therefore never be seen -- and a hub is the
  // slowest thing on the bus: it powers up its own controller, then IDF applies port
  // reset recovery (CONFIG_USB_HOST_EXT_PORT_RESET_RECOVERY_DELAY_MS) and a power-on
  // delay per downstream port. The same short window also caught a radio with a
  // slow-booting USB stack.
  //
  // The cap is a CEILING, not a wait: the loop below exits as soon as the bus goes
  // quiet with at least one device present, so a USB-serial adapter that appears in
  // 400 ms still returns in 400 ms. Raising it costs time only when something slow is
  // attached, or when nothing is attached at all -- and in the latter case a few extra
  // seconds is much cheaper than reporting "no adapters found" for a device that was
  // simply still waking up.
  inline uint32_t enumCapMs()   { return s_sawHub ? 12000 : 9000; }
  inline uint32_t enumQuietMs() { return s_sawHub ? 1200 : 400; }

  // Stable identity across replugs: serial number when the adapter reports one
  // (FTDI/CP210x usually do, CH340 usually does not), else VID:PID + address.
  // Serial-first matters because two adapters of the SAME model -- the likely
  // radio+rotator case -- are indistinguishable by VID:PID alone.
  void makeKey(char* out, size_t n, const EspUsbHostDeviceInfo& d) {
    if (d.serial && *d.serial) snprintf(out, n, "%04x:%04x/%s", d.vid, d.pid, d.serial);
    else                       snprintf(out, n, "%04x:%04x@%u", d.vid, d.pid, (unsigned)d.address);
  }

  // Find the registry slot for a configured key, tolerating an ADDRESS CHANGE.
  //
  // A device with no iSerialNumber is keyed VID:PID@address -- the TH-D75 and the
  // IC-705 are both exactly this. The USB address is assigned by enumeration order, so
  // it is NOT a property of the radio: plugging the same radio in through a hub, or
  // into a different hub port, or powering things up in a different order, gives it a
  // different address and the configured key stops matching. Nothing is wrong with the
  // radio and nothing is wrong with the bus; the leg simply finds no adapter.
  //
  // That is exactly the bench report "the D75 only works when connected directly":
  // configured direct it keys @1, behind a hub it enumerates at another address, and an
  // exact strcmp can never match again.
  //
  // So: exact match first, always. Only if that fails, and the wanted key is the
  // address form, fall back to the VID:PID part -- and ONLY when exactly one live
  // device carries it. That last condition is what preserves the reason the address is
  // in the key at all: two identical adapters (the likely radio + rotator case) stay
  // ambiguous and are never guessed between.
  int findAdapter(const char* wantKey) {
    if (!wantKey || !*wantKey) return -1;
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && strcmp(s_serDev[i].key, wantKey) == 0) return i;

    const char* at = strchr(wantKey, '@');
    if (!at) return -1;                       // serial-keyed: no fallback, and none needed
    const size_t vp = (size_t)(at - wantKey); // "vvvv:pppp"
    int hit = -1, n = 0;
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (s_serDev[i].dead) continue;
      if (strncmp(s_serDev[i].key, wantKey, vp) == 0 && s_serDev[i].key[vp] == '@') {
        hit = i; n++;
      }
    }
    if (n != 1) return -1;                    // 0 = absent, >1 = ambiguous; do not guess
    char b[128];
    snprintf(b, sizeof(b), "adapter matched by VID:PID (address moved: want %s, found %s)",
             wantKey, s_serDev[hit].key);
    rotTrace(b);
    return hit;
  }

  // ONE device-connected callback for both the CAT and rotator paths. Runs on the
  // host's own task: plain byte stores only, read back after a bounded wait.
  void onDev(const EspUsbHostDeviceInfo& d) {
    s_sawDev = true;
    // A hub is not a selectable adapter: no serial OUT endpoint, so it can never
    // carry CAT. Record that one is on the bus (it changes every enumeration
    // budget below) and do NOT put it in the picker. Note s_dev/s_lastDevMs are
    // updated FIRST so the "settled" timer still counts the hub's own arrival.
    if (d.isHub) {
      s_sawHub = true;
      s_lastDevMs = millis();
      return;
    }
    // The device ADDRESS leads the string: two identical adapters (the classic
    // dual-Prolific bench) produce byte-identical manufacturer/product/VID:PID,
    // and on a 240-px row the tail truncates first -- so the one distinguishing
    // token must come FIRST. The address is also exactly the id explicit binding
    // stores, so what the user reads is what the firmware binds.
    snprintf(s_dev, sizeof(s_dev), "#%u %s %s %04x:%04x",
             (unsigned)d.address,
             (d.manufacturer && *d.manufacturer) ? d.manufacturer : "USB",
             (d.product && *d.product) ? d.product : "serial",
             (unsigned)d.vid, (unsigned)d.pid);
    // Record it as a selectable adapter (finding A: update-or-insert, the
    // companion's model, adapted to this file's publication rules).
    //
    // 1) Dedup by address -- against LIVE entries only. A composite radio
    //    (IC-9100/9700) can raise the callback more than once per device; but a
    //    DEAD entry at this address means the bus REUSED the address for a new
    //    device, which must not be swallowed as a duplicate.
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && s_serDev[i].address == d.address) return;
    char key[40];
    makeKey(key, sizeof(key), d);
    // 2) Same KEY already known (typically a replug at a new address: serial-first
    //    keys are stable across replugs) -> refresh that slot IN PLACE, so the
    //    first-match resolvers find the live address at the same index and keyed
    //    devices can never accumulate duplicates. Publication: tombstone the slot,
    //    fence, rewrite, fence, un-tombstone -- readers skip it while it is torn.
    // 3) Else recycle any dead slot, same discipline -- replug churn can no longer
    //    fill the 4-slot array.
    // 4) Else append, exactly as before (fence, then count bump publishes it).
    int slot = -1;
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (strcmp(s_serDev[i].key, key) == 0) { slot = i; break; }
    if (slot < 0)
      for (uint8_t i = 0; i < s_serDevN; ++i)
        if (s_serDev[i].dead) { slot = i; break; }
    if (slot < 0) {
      if (s_serDevN >= (uint8_t)(sizeof(s_serDev) / sizeof(s_serDev[0]))) {
        // FULL. This used to drop the device with no record anywhere -- the operator
        // saw an adapter simply missing from the picker, with nothing in the log to
        // say why. A silent drop is the single most expensive failure mode in this
        // subsystem: it is indistinguishable from "the device did not enumerate",
        // which is a completely different problem with completely different fixes.
        s_devRegistryFull = true;
        return;
      }
      slot = s_serDevN;
    }
    SerialDev& e = s_serDev[slot];
    const bool inPlace = (slot < s_serDevN);
    if (inPlace) { e.dead = 1; std::atomic_thread_fence(std::memory_order_release); }
    e.address = d.address; e.vid = d.vid; e.pid = d.pid;
    snprintf(e.label, sizeof(e.label), "#%u %s %s %04x:%04x",   // address-first: see s_dev
             (unsigned)d.address,
             (d.manufacturer && *d.manufacturer) ? d.manufacturer : "USB",
             (d.product && *d.product) ? d.product : "serial",
             (unsigned)d.vid, (unsigned)d.pid);
    memcpy(e.key, key, sizeof(e.key));
    if (!inPlace) e.dead = 0;   // append into a recycled index: the slot may carry a stale
                                // tombstone from before the registry reset; a dead append
                                // would publish an entry no resolver can see
    std::atomic_thread_fence(std::memory_order_release);   // entry complete before it is published
    if (inPlace) e.dead = 0;                               // republish the refreshed slot
    else         s_serDevN++;                              // append: the count publishes it
    s_lastDevMs = millis();
  }

  // Device gone (finding A): tombstone its registry entry so no resolver can bind
  // the dead address. Runs on the host task; a single byte store, same discipline
  // as onDev(). The slot itself is recycled by the next insert.
  void onGone(const EspUsbHostDeviceInfo& d) {
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (s_serDev[i].address == d.address) s_serDev[i].dead = 1;
    // Tombstoning the registry was the whole of this callback until 0.9.73, and it
    // is not enough. active() is `s_active && s_bound`, neither of which the
    // registry touches, so an unplugged radio stayed LOGICALLY active -- and the
    // loop reconciler, seeing nothing to do, never rebuilt the port. A replug then
    // did nothing until the operator changed a setting or rebooted.
    //
    // Only flags are set here: this runs on the USB host task, and tearing a port
    // down from inside a host callback is how you free objects the host is still
    // walking. The main loop consumes them (serviceDisconnects()).
    if (d.address == s_catAddress)  s_catLost  = true;
    if (d.address == s_cat2Address) s_cat2Lost = true;
    if (d.address == s_rotAddress)  s_rotLost  = true;
  }

  void consoleDown() {
    if (s_consoleDown) return;        // strictly paired: never end() twice
    if (Serial) Serial.flush();       // only drain if a host is actually attached
    Serial.end();
    s_consoleDown = true;
  }
  void consoleUp() {
    if (!s_consoleDown) return;       // never begin() a console we did not end()
    Serial.begin(115200);
    s_consoleDown = false;
    // Deliberately no delay/wait here: a headless CardSat has no host attached and
    // must not stall the main loop waiting for one.
  }

  // Release the shared host when NEITHER port still owns it, restoring the console.
  // Every FAILED engage funnels through here so that "the adapter is not plugged in"
  // costs nothing lasting: before this existed, a failed engage left the host object,
  // the IDF stack, both USB tasks and the console down until reboot -- roughly 20 KB
  // held for a device that was never there, and the operator's serial port gone with
  // it. A SUCCESSFUL engage never calls this; end()/rotEnd() own that path.
  //
  // The M2 timeout rule is the same one end() and rotEnd() follow, and for the same
  // reason: on ESP_ERR_TIMEOUT the library leaves its tasks alive rather than freeing
  // in-flight transfers, so deleting the object would be a use-after-free and
  // restoring the console would claim the PHY before release is confirmed. Retain,
  // latch reboot-required, stay quiet.
  // RESIDENT HOST BETWEEN ENGAGES (0.9.70).
  //
  // Detaching the last port no longer uninstalls the USB host stack. The host is kept
  // installed and the device stays ENUMERATED; only the CDC port is detached. A full
  // release happens when the operator explicitly asks for it (releaseHostNow(), the
  // "Release USB" action), or when the device physically disconnects.
  //
  // WHY, measured on a TH-D75 and not guessed:
  //   * Re-engaging after a full teardown re-enumerates the radio. Enumeration,
  //     descriptor walk, CDC bind and DTR/RTS ALL succeed -- and the radio then
  //     accepts exactly TWO bulk-OUT packets (a double-buffered endpoint FIFO) and
  //     NAKs everything after. Its USB hardware is fine; its CAT application never
  //     comes back after re-enumeration, so nothing drains the endpoint. Waiting 1
  //     minute and 5 minutes did not help, so it is not a settle-time problem.
  //   * The same radio survives close/reopen indefinitely on a Mac (6/6 cycles),
  //     because closing a port there does NOT re-enumerate the device. Keeping the
  //     host resident reproduces exactly that, which is the whole point.
  //   * Switching the radio OFF still works as the operator expects: that physically
  //     disconnects the device, and powering it back on re-enumerates it from cold,
  //     which restarts the radio's CAT application -- the case the D75 handles fine.
  //
  // This applies to EVERY USB path -- CAT-A, CAT-B and the rotator -- deliberately.
  // Nothing about the failure is specific to a radio or to this model; any device
  // whose firmware does not re-initialise on re-enumeration would behave the same,
  // and this function is the one choke point all three paths already share.
  //
  // The teardown itself is NOT abandoned and is not broken: it was fixed and confirmed
  // on hardware (five consecutive cycles, every byte of heap returned). It is simply
  // no longer run on every disengage, because doing so costs the device its session.
  void releaseHostIfIdle() {
    if (s_keepHostResident) return;             // explicit release only
    releaseHostNow();
  }

  // The unconditional release. Used by releaseHostIfIdle() when residency is off, and
  // by the operator-facing "Release USB" action.
  void releaseHostNow() {
    if (!s_host || s_cdc || s_cdc2 || s_rotCdc) return;   // someone still owns it
    // Bring the bus to rest BEFORE tearing down. A bulk IN transfer is always
    // outstanding on a device that is not talking (the read pump re-arms instantly),
    // and an outstanding transfer is what makes usb_host_client_deregister() refuse
    // with 259 -- which strands the whole stack until a reboot. quiesce() stops the
    // pump, cancels what is in flight and waits for the completion while the tasks
    // are still healthy, which is the state the SUCCESSFUL teardowns were measured
    // in. Its return is advisory; end() runs either way.
    s_host->quiesce();
    s_host->clearLastError();   // sticky lastError_ would fake a wedge -- see the note at the disengage site
    s_host->end();                              // 2.4.1+: drain, deregister, uninstall
    if (s_host->lastError() == ESP_ERR_TIMEOUT) {
      s_hostTeardownStuck = true;
      s_hostReleased = false;
      return;                                   // do NOT delete, do NOT consoleUp()
    }
    delete s_host; s_host = nullptr;
    s_hostReleased = true;
    consoleUp();
  }
}

// Operator-facing full release. The host normally stays installed between engages so
// the device keeps its session (see releaseHostIfIdle); this is the way to actually
// give the PHY back -- which also restores the serial console.
void releaseUsbNow() {
  if (s_cdc || s_cdc2 || s_rotCdc) return;      // a port is still open; not idle yet
  releaseHostNow();
}

// True when the host is installed but nothing is bound: resident, and releasable.
bool usbHostResident() {
  return s_host && !s_cdc && !s_cdc2 && !s_rotCdc;
}

bool begin(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  if (s_active) return s_bound;
  // M2: a prior teardown timed out with tasks still alive. That WAS an unconditional
  // refuse-until-reboot. It is now one retry, for the same reasons as the
  // s_hostReleased gate below: the verdict that set this flag was frequently a false
  // positive (sticky lastError), and even a real timeout may have completed in the
  // seconds before the operator tried again. usb_host_install() refuses over a live
  // stack and reports 259 on its own, so let the hardware answer rather than a latch
  // set a minute ago. If it does fail, the flag is re-set and the message says the
  // retry was already spent.
  if (s_hostTeardownStuck) {
    rotTrace("cat: prior teardown timed out - attempting anyway (one retry)");
    s_hostTeardownStuck = false;
    s_retryAfterStuck = true;
  }
  s_err[0] = 0; s_dev[0] = 0; s_sawDev = false;

  // ---- Reuse a live host, or build one the first time --------------------------
  // Under 2.4.1+ a normal disengage releases the host, so most engages build fresh. But the
  // host may still be up because a USB ROTATOR started it (shared host, two adapters) -- in
  // which case s_host is live but s_cdc has never existed. Create just the missing CAT port
  // and fall into the rebind path; allocating a second EspUsbHost over the top of
  // a live one leaks it AND guarantees 259 from usb_host_install().
  if (s_host && !s_cdc) {
    s_cdc = new (std::nothrow) EspUsbHostCdcSerial(*s_host);
    if (!s_cdc) { setErr("Out of RAM for CAT port"); return false; }
  }
  if (s_host && s_cdc) {
    // Any failure below must not leave a half-bound CAT port on a shared host. rollbackCat()
    // drops the CAT port (and releases the host only if no rotator owns it), leaving a clean
    // not-active state so the next engage starts fresh instead of seeing a poisoned s_cdc.
    auto rollbackCat = [&]() {
      if (s_cdc) { cdcClosePort(s_cdc); s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
      releaseHostIfIdle();   // M2-safe: retains the host if end() times out
      s_active = false; s_bound = false; s_catAddress = 0xff;
      stage(USBCAT_STAGE_NONE);
    };
    // Re-pin on every rebind: the adapter may have been unplugged and replugged
    // at a new address while we were disengaged.
    if (s_serDevN > 0) {
      int pick = catPickAdapter();
      if (pick < 0) { rollbackCat(); return false; }   // catPickAdapter() set the error
      s_catAddress = s_serDev[pick].address;
      s_cdc->setAddress(s_catAddress);
    }
    stage(USBCAT_STAGE_BIND);
    if (!s_cdc->begin(baud)) { setErr("USB rebind failed"); rollbackCat(); return false; }
    EspUsbHostSerialConfig cfg;
    cfg.baud = baud; cfg.dataBits = dataBits;
    cfg.parity = (EspUsbHostSerialParity)parity;
    cfg.stopBits = (EspUsbHostSerialStopBits)stopBits;
    stage(USBCAT_STAGE_BIND_CFG);  s_cdc->setConfig(cfg);
    stage(USBCAT_STAGE_BIND_DTR);  s_cdc->setDtr(true);
    stage(USBCAT_STAGE_BIND_RTS);  s_cdc->setRts(true);
    stage(USBCAT_STAGE_BIND_DONE);
    // Bounded wait, as everywhere else post-setAddress (finding B): after a re-pin
    // to a replugged adapter the CDC interface can still be coming up, and
    // connected() is specific to the address just set.
    {
      const uint32_t t0 = millis();
      while (millis() - t0 < 2500 && !s_cdc->connected()) delay(20);
    }
    if (!s_cdc->connected()) { setErr("No USB device detected"); rollbackCat(); return false; }
    s_active = true; s_bound = true;
    stage(USBCAT_STAGE_NONE);
    return true;
  }

  // Allocate BEFORE touching the console: an out-of-RAM failure then leaves the
  // system exactly as it was, console included.
  // The wedge gate belongs HERE, not at the top: it must never block a REBIND.
  // s_hostReleased goes false only when a failed engage could not release a stack
  // it had installed, so a fresh install would hit 259 -- but a resident, healthy
  // host above has already returned by this point and is unaffected. The bench's
  // "USB stack wedged - reboot before re-engage" on a rebindable host was this
  // check sitting above the fast path and refusing an engage that would have
  // worked.
  // RECOVERABLE, NOT TERMINAL. This used to refuse outright and demand a reboot,
  // which punished the operator twice: the radio did not work AND the device had to
  // be power-cycled to try anything at all. Two bench facts made that indefensible:
  // the "stuck" verdict came from a STICKY lastError and was often wrong (see the
  // clearLastError work), and even a genuinely incomplete teardown may have finished
  // by the time the operator tries again seconds later.
  //
  // So ATTEMPT the engage and let it fail on its own evidence. begin() below refuses
  // to install over a live stack and reports 259, which is a real answer from the
  // hardware rather than a latch we set earlier. The latch is kept only to make the
  // message specific on the SECOND consecutive failure, and is cleared the moment an
  // engage succeeds. Worst case the operator retries and gets a clear error; best
  // case -- and the bench shows this happens -- it simply works.
  if (!s_hostReleased) {
    rotTrace("cat: previous teardown was incomplete - attempting anyway");
    s_hostReleased = true;          // spend the latch on this attempt
    s_retryAfterStuck = true;       // so a failure here can say so precisely
  }

  stage(USBCAT_STAGE_ALLOC);
  // The ~20 KB host object needs ONE CONTIGUOUS block, and the bench reports the
  // largest free block at 18 KB after a disengage (31.7 KB before the first
  // engage). Whether that is a leak or fragmentation, a re-engage lands here and
  // fails -- so say so precisely rather than reporting a generic OOM: the two need
  // different fixes and the operator cannot see the heap.
  const uint32_t freeB = ESP.getFreeHeap();
  const uint32_t bigB  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  s_host = new (std::nothrow) EspUsbHost;
  s_cdc  = s_host ? new (std::nothrow) EspUsbHostCdcSerial(*s_host) : nullptr;
  if (!s_cdc) {
    delete s_host; s_host = nullptr;
    // free vs largest tells the operator (and the log) WHICH problem this is:
    // plenty free but no big block = fragmentation, and a reboot clears it.
    char msg[64];
    if (freeB > 30000 && bigB < 22000)
      snprintf(msg, sizeof(msg), "USB: heap too fragmented (%luK free, %luK max)",
               (unsigned long)(freeB/1024), (unsigned long)(bigB/1024));
    else
      snprintf(msg, sizeof(msg), "Out of RAM for USB host (%luK free)",
               (unsigned long)(freeB/1024));
    setErr(msg);
    stage(USBCAT_STAGE_NONE);
    return false;
  }

  // Learn what enumerated (runs on the host's own task; plain byte stores, read
  // back only after the bounded wait below). A composite rig (IC-9100/9700 USB-B:
  // serial + audio) presents more than a plain cable; the library binds the serial
  // side by itself.
  stage(USBCAT_STAGE_CALLBACK);
  // Fresh host generation: clear the adapter registry so onDev repopulates it from THIS
  // host's enumeration. Without this, entries from a prior host session survive (stale
  // addresses/keys), and a device given a reused address could be rejected as a duplicate
  // or a scan could return devices that are no longer attached.
  s_serDevN = 0; s_sawDev = false; s_sawHub = false;
  s_host->onDeviceConnected(&onDev);   // records the device AND the adapter list
  s_host->onDeviceDisconnected(&onGone);  // finding A: tombstone on unplug

  // Order matters. The console must go down BEFORE the host claims the PHY (they
  // share it), but everything that can fail should be able to REPORT the failure,
  // and the status bar is the screen -- not the console -- so it survives either
  // way. What does NOT survive is a hang: with the console already torn down and
  // no status written, a freeze here is completely mute. That was the 0.9.58-wip
  // "Radio On freezes, no message" report. The teardown is now guarded (see
  // consoleDown) and the host's own begin() is bounded at 1 s internally --
  // usb_host_install() runs on the host's FreeRTOS task, so a wedged PHY grab
  // fails that timeout rather than blocking us here.
  // Subscribe the TWDT user across the risky span. NOTE (bench, 0.9.58-wip): this
  // did NOT fire on a real freeze even after a minute, so it is a backstop, not the
  // reporting path -- the stage paints above are. Kept because when it does fire it
  // gives a breadcrumb that survives the reboot. The ms argument is ignored: the
  // TWDT timeout is global and owned by the Arduino core (5 s, IDF default).
  armFreezeWatchdog(0);
  stage(USBCAT_STAGE_CONSOLE_DOWN);
  consoleDown();
  stage(USBCAT_STAGE_HOST_BEGIN);
  // ---- Pin the host tasks to core 0 (audit finding, v2.3.0) ---------------------
  // The library creates two tasks (EspUsbHost + EspUsbHostClient) at priority 5
  // with tskNO_AFFINITY. Arduino pins loopTask to CORE 1 at priority 1, and its
  // sdkconfig leaves core 1's idle task UNWATCHED by the task watchdog
  // (CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is not set). FreeRTOS is strict
  // priority: if a host task lands on core 1 and stops blocking, loopTask starves
  // FOREVER -- pinned, it cannot migrate -- and no watchdog can see it. The screen
  // freezes on the last paint with no panic and no reset: precisely the bench
  // symptom, every round.
  //
  // And the library has a path that stops blocking: handleTransfer() resubmits the
  // IN transfer on EVERY completion except NO_DEVICE/CANCELED -- including STALL,
  // ERROR and OVERFLOW, with no backoff and no stall-clear. A stalled endpoint
  // completes instantly with STALL again, so completion->resubmit becomes a
  // zero-delay spin on the priority-5 client task. IN traffic starts at bind:
  // "enumerates and binds, then freezes".
  //
  // Pinning both tasks to core 0 makes loopTask structurally immune: a USB spin
  // can no longer take the UI with it (screen, keys and disengage keep working).
  // Better, core 0's idle task IS watched -- so the same spin now trips the TWDT,
  // panics, reboots, and the RTC breadcrumb + SD stage log name the stage. The
  // invisible freeze becomes a diagnosed event. (WiFi also lives on core 0, at
  // priority ~23; it preempts the host tasks and is unaffected.)
  EspUsbHostConfig hostCfg;
  hostCfg.taskCore = 0;                // PRO_CPU: away from loopTask, watched by TWDT
  hostCfg.taskStackSize = kTaskStack;  // both tasks; size from the END_CDC headroom log
  // Stack sizing: kTaskStack (4096) feeds BOTH library tasks -- reduced from the
  // library's 8192 default after the END_CDC high-water-mark logs showed headroom.
  // Shrinking a stack on a guess would trade a clean OOM for a stack overflow (far
  // worse to diagnose), so this was cut against measured high-water data, not a guess.
  // The END_CDC stage still logs both tasks' high-water marks to SD
  // (usbStageTrampoline), so a fresh bench disengage re-validates the 4096 figure.
  // Every 1 KB cut returns 2 KB of heap; do not reduce further without new data.
  if (!s_host->begin(hostCfg)) {       // internally waits <= 1 s for its own task
    // Report WHY, not just THAT. The library records the failing esp_err_t and the
    // two causes need completely different fixes: ESP_ERR_NO_MEM means the host task
    // or the IDF stack could not be allocated (free RAM), while ESP_ERR_INVALID_STATE
    // from usb_host_install() means the USB peripheral is already claimed -- which is
    // what a console/PHY that was not fully released looks like. A bare "would not
    // start" cannot distinguish them, and the operator has no console to check.
    const int e = s_host->lastError();
    disarmFreezeWatchdog();
    // Tear down through end() rather than deleting here, so the host object gets a
    // The host never reached ready_, so nothing is resident to keep: delete the
    // objects directly and restore the console -- leaving it down after a failed
    // engage would cost the operator their serial port for nothing.
    //
    // But delete the OBJECTS is not the same as release the STACK. "begin() failed"
    // does NOT mean usb_host_install() failed: the daemon installs first, then
    // registers a client, allocates transfers, and so on. In 2.4.1+ the daemon owns
    // its own teardown -- on any post-install failure it runs the ALL_FREE handshake
    // and uninstalls with the return checked, and end() blocks until that completes.
    // So call end() here exactly as the disengage path does: it either fully releases
    // the stack or reports (via a still-set taskHandle_/lastError) that it could not,
    // and 2.4.1+'s begin() refuses to start over an incomplete shutdown rather than
    // returning 259 mid-operation. The daemon has already observed running_ = false by
    // this point; if install never happened, end() early-returns harmlessly.
    // lastError_ is STICKY (cleared only in begin()), so a timeout from ANY earlier
    // point in the session -- a bulk OUT the radio never drained, a control transfer
    // it ignored -- would still be reported here and make a clean release look like a
    // wedge. Bench-proven: teardowns completing in ~1080 ms, well inside end()'s own
    // 3000 ms and the client's 2500 ms waits, were still reported "reboot needed".
    // Clear first, so the test below can only see a timeout raised BY end().
    // Bring the bus to rest BEFORE tearing down. A bulk IN transfer is always
    // outstanding on a device that is not talking (the read pump re-arms instantly),
    // and an outstanding transfer is what makes usb_host_client_deregister() refuse
    // with 259 -- which strands the whole stack until a reboot. quiesce() stops the
    // pump, cancels what is in flight and waits for the completion while the tasks
    // are still healthy, which is the state the SUCCESSFUL teardowns were measured
    // in. Its return is advisory; end() runs either way.
    s_host->quiesce();
    s_host->clearLastError();
    s_host->end();
    const bool freed = (s_host->lastError() != ESP_ERR_TIMEOUT);
    s_active = false; s_bound = false;
    char msg[64];
    if (!freed) {
      // DO NOT delete, and DO NOT consoleUp(). end() timing out is documented by
      // the library as "tasks were left alive to avoid freeing in-flight
      // transfers" (EspUsbHost.cpp, the ESP_LOGW at the end of end()) -- so a live
      // task still holds `this`, and deleting either object hands it a dangling
      // pointer. consoleUp() is arguably worse: Serial.begin() reclaims the USB
      // PHY while the IDF host stack may still own it, and a two-owner PHY fails
      // in ways that look like anything except a teardown bug.
      //
      // Retaining costs ~11.8 KB and the serial console until a reboot. That is
      // the correct price: the two OTHER teardown sites in this file
      // (releaseHostNow() and end()) have always retained here, and this path was
      // simply missed when they were fixed. Found by the 0.9.72 USB review.
      s_hostTeardownStuck = true;      // block re-engage; only a reboot clears it
      s_hostReleased = false;
      setErr(s_retryAfterStuck
               ? "USB stack still held after retry - reboot to clear"
               : "USB stack stuck installed - retry, or reboot if it persists");
      stage(USBCAT_STAGE_NONE);
      return false;
    }
    delete s_cdc;  s_cdc  = nullptr;
    delete s_host; s_host = nullptr;
    s_hostReleased = true;
    consoleUp();
    if (e == 259)   // ESP_ERR_INVALID_STATE: the USB host stack is already installed
      // Kept as a backstop, but this should no longer be reachable via disengage:
      // end() now drains the pending events and calls usb_host_uninstall() itself,
      // rather than trusting the library's own call (which ignores its return and
      // silently leaves the stack installed -- the fix32 "259 on re-engage" bench
      // report). If this DOES appear now, the stack was left installed by a path
      // end() never ran (e.g. a panic mid-engage), and a reboot really is needed.
      snprintf(msg, sizeof(msg), "USB busy - reboot needed (259)");
    else
      snprintf(msg, sizeof(msg), "USB host would not start (err %d)", e);
    setErr(msg);
    stage(USBCAT_STAGE_NONE);
    return false;
  }
  s_active = true;

  // Give the device time to enumerate and the class driver to attach. The host runs
  // its own FreeRTOS task, so this is a bounded wait on it, not a busy poll.
  stage(USBCAT_STAGE_ENUM_WAIT);
  const uint32_t t0 = millis();                        // wrap-clean uint32 subtraction,
  // enumCapMs(), not a flat 2500: behind a hub the adapter enumerates AFTER the hub
  // and its siblings, which a fixed cap can easily expire before.
  while (millis() - t0 < enumCapMs() && !s_cdc->connected()) {
    delay(20);
    // Feed the TWDT user during the LEGITIMATE wait. The TWDT timeout is 5 s
    // (IDF default; Arduino does not override it) and this span plus the host's
    // own <=1 s begin() runs to ~3.5 s -- close enough that an unfed subscription
    // would panic on a perfectly healthy enumeration. Resetting here means only a
    // wait that never ENDS trips it, which is the failure we are hunting.
    feedFreezeWatchdog();
  }

  // Gate on connected(), NOT on cdc->begin()'s return: with no device bound, the
  // library's setSerialConfig() stores the config as a future default and returns
  // TRUE (verified in the v2.3.0 source), so begin() succeeding is not an
  // enumeration signal.
  if (!s_cdc->connected()) {
    // Distinguish the two failures -- they need different fixes and look identical
    // to the operator otherwise.
    //
    // EspUsbHost binds a serial device two ways: CDC-ACM by INTERFACE CLASS (0x02
    // control + 0x0A data -- standards-based, any compliant device), or a vendor
    // bridge by a hardcoded VID:PID ALLOW-LIST: FTDI 0x0403, CP210x 0x10c4, CH34x
    // 0x1a86, PL2303 0x067b, each with a fixed PID set. Vendor bridges are
    // interface class 0xFF with no standard descriptor, so there is nothing to
    // detect BY -- the library must know them by ID. A clone with an unlisted PID
    // enumerates fine and is simply not recognized.
    char msg[64];
    if (s_sawDev) snprintf(msg, sizeof(msg), "Not a known serial adapter: %s", s_dev);
    else          snprintf(msg, sizeof(msg), "%s", "No USB device detected");
    // This used to be a deliberate keep-alive: the host was left up with the port
    // unbound so that plugging the adapter in and retrying would rebind in
    // milliseconds instead of re-allocating ~20 KB. That reasoning was written when
    // end() could not actually release the stack, so keeping it cost nothing that
    // was recoverable anyway. Under 2.4.1+ it does release, and the trade inverted:
    // the common case is not "retry in five seconds", it is a cable that is not
    // plugged in at all, or a rig that is switched off -- after which the host, the
    // IDF stack, both USB tasks and the serial console stayed gone until reboot for
    // a device that never existed. Release it; a later retry pays a one-off ~1 s
    // re-allocation, which is the right price for not leaking on the failure case.
    disarmFreezeWatchdog();
    if (s_cdc) { cdcClosePort(s_cdc); s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
    releaseHostIfIdle();               // no-op if a USB rotator still holds the host
    s_active = false; s_bound = false; s_catAddress = 0xff;
    setErr(msg);
    stage(USBCAT_STAGE_NONE);
    return false;
  }

  // The bench froze with "binding serial device" on screen, so the hang is one of
  // the five calls below -- each now announces itself, because reading the v2.3.0
  // source did NOT identify a blocking one: cdc->begin() takes a spinlock and calls
  // setSerialBaudRate; setConfig/setDtr/setRts all funnel into configureCdcAcm() or
  // configureVendorSerial(), and BOTH only alloc-and-submit control transfers
  // (usb_host_transfer_submit_control) with a completion CALLBACK -- no waits, no
  // joins. On paper none of this can block. The screen will say which one does.
  // Bind the radio to ONE device address rather than leaving the port at
  // ANY_ADDRESS. With a single adapter this changes nothing. With a rotator
  // adapter ALSO plugged in it is the whole ballgame: findSerialDevice(ANY)
  // returns devices_[first-with-bulk-OUT], so two ANY-bound CDC ports both grab
  // the same adapter and the radio's Doppler writes can land on the rotator --
  // and "first" is enumeration order, which can change across a replug. Honor
  // the user's nominated radio adapter if there is one; otherwise take the first
  // enumerated serial device, which is exactly the historical single-adapter
  // behavior.
  if (s_serDevN > 0) {
    int pick = catPickAdapter();
    if (pick < 0) {
      // H2 rollback: catPickAdapter() failed AFTER s_active was set and the watchdog was
      // armed. Returning here bare would leave s_active=true (poisoning the next engage's
      // active() check) and the freeze watchdog armed (able to reboot a later healthy op).
      // Fully unwind: disarm, drop the port, release the host if no rotator owns it.
      disarmFreezeWatchdog();
      if (s_cdc) { cdcClosePort(s_cdc); s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
      releaseHostIfIdle();   // M2-safe: retains the host if end() times out
      s_active = false; s_bound = false; s_catAddress = 0xff;
      stage(USBCAT_STAGE_NONE);
      return false;                    // catPickAdapter() already set the error text
    }
    s_catAddress = s_serDev[pick].address;
    s_cdc->setAddress(s_catAddress);
  }
  stage(USBCAT_STAGE_BIND);
  s_cdc->begin(baud);                  // attaches to the host + pushes the baud
  EspUsbHostSerialConfig cfg;
  cfg.baud     = baud;
  cfg.dataBits = dataBits;
  cfg.parity   = (EspUsbHostSerialParity)parity;
  cfg.stopBits = (EspUsbHostSerialStopBits)stopBits;
  stage(USBCAT_STAGE_BIND_CFG);
  s_cdc->setConfig(cfg);
  // Many USB<->serial adapters hold the device in reset / ignore traffic until DTR
  // and RTS are asserted. Harmless on those that do not care.
  stage(USBCAT_STAGE_BIND_DTR);
  s_cdc->setDtr(true);
  stage(USBCAT_STAGE_BIND_RTS);
  s_cdc->setRts(true);
  stage(USBCAT_STAGE_BIND_DONE);

  // ---- Verify the PICKED address actually came up (audit finding B) -------------
  // The enum wait above proved connected() at ANY_ADDRESS -- i.e. "the FIRST
  // enumerated adapter is ready". setAddress() re-pointed the port at the PICKED
  // adapter, and connected() is address-specific (serialReady(address_) in the
  // library), so that earlier proof says nothing about THIS address. When the
  // nominated adapter is the slower of two, or its registry entry is stale after a
  // replug, the old code set s_bound=true on a port whose connected() was false --
  // "engaged" with every Doppler write silently going nowhere. cat2Begin() and
  // rotBegin() have always done this bounded wait after setAddress; this was the
  // one surface missing it.
  {
    const uint32_t t0 = millis();
    while (millis() - t0 < 2500 && !s_cdc->connected()) {
      delay(20);
      feedFreezeWatchdog();
    }
  }
  if (!s_cdc->connected()) {
    disarmFreezeWatchdog();
    if (s_cdc) { cdcClosePort(s_cdc); s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
    releaseHostIfIdle();               // M2-safe; no-op while the rotator/CAT-B own it
    s_active = false; s_bound = false; s_catAddress = 0xff;
    setErr("Radio adapter not responding");
    stage(USBCAT_STAGE_NONE);
    return false;
  }

  s_bound = true;
  s_retryAfterStuck = false;           // engage succeeded: the previous wedge is history
  s_hostTeardownStuck = false;         // and the stack is demonstrably usable again
  disarmFreezeWatchdog();              // healthy: never let it reboot a live radio
  stage(USBCAT_STAGE_NONE);            // reached the end: clear the breadcrumb
  return true;
}

void end() {
  // NOT gated on s_active: a begin() that failed part-way can leave objects behind.
  if (!s_host && !s_cdc) { s_active = false; s_bound = false; return; }
  disarmFreezeWatchdog();

  snapshotHeadroom();          // while both tasks are alive (see the note there)

  // ---- Full teardown via EspUsbHost's fixed end() (2.4.1+, pinned 2.5.2) --------
  // History: fix28-fix36 could not tear the IDF host stack down from outside the old
  // library, because its end() killed the CLIENT task first and then ran client-scoped
  // cleanup (releaseInterfaces / device_close / client_deregister) on a dead event queue,
  // leaving the stack installed -> 259 on re-engage. So 0.9.58 shipped a "resident host":
  // detach the CDC port, never stop the host, hold ~11.8 KB until reboot.
  //
  // EspUsbHost 2.4.1 fixes the ordering: end() signals the daemon, then the daemon drains
  // the client's in-flight transfers, closes devices, deregisters the client, pumps
  // library events until USB_HOST_LIB_EVENT_FLAGS_ALL_FREE, and calls usb_host_uninstall()
  // WITH its return checked -- the exact handshake we used to hand-roll in finishUninstall().
  // end() runs on THIS (main-loop) task, refuses if called from a USB task, and blocks up to
  // 3 s for the daemon to finish; on timeout it leaves the tasks alive rather than freeing
  // in-flight transfers (safe fallback, no crash). So it is safe to call here and it either
  // fully releases or cleanly reports it did not.
  //
  // Order: detach the CDC port, stop+uninstall the host, then delete the objects. begin()'s
  // 2.4.1+ guard (it refuses to start over taskHandle_/clientHandle_ that are not null)
  // protects a re-engage from racing an incomplete shutdown, which is the wedge s_hostReleased
  // used to guard by hand; we keep s_hostReleased as a belt-and-suspenders latch anyway.
  stage(USBCAT_STAGE_END_CDC);
  if (s_cdc) { cdcClosePort(s_cdc); s_cdc->end(); }   // close, then detach
  delete s_cdc;  s_cdc  = nullptr;

  // The host is SHARED with the USB rotator. Only tear it down when no port remains --
  // otherwise a live rotator would lose its host out from under it. Radio and rotator CAN
  // both be on USB at once (two adapters, each bound to its own device address), so this
  // guard is load-bearing, not just defensive: with the rotator still up, CAT disengage
  // must leave the host (and thus the rotator's port) running.
  stage(USBCAT_STAGE_END_HOST);
  // RESIDENT BY DEFAULT: the port is detached above, but the host stays installed and
  // the device stays enumerated, so a re-engage does not cost the device its session.
  // See releaseHostIfIdle() for the measurements behind this.
  if (s_host && !s_rotCdc && !s_cdc2 && !s_keepHostResident) {
    // Bring the bus to rest BEFORE tearing down. A bulk IN transfer is always
    // outstanding on a device that is not talking (the read pump re-arms instantly),
    // and an outstanding transfer is what makes usb_host_client_deregister() refuse
    // with 259 -- which strands the whole stack until a reboot. quiesce() stops the
    // pump, cancels what is in flight and waits for the completion while the tasks
    // are still healthy, which is the state the SUCCESSFUL teardowns were measured
    // in. Its return is advisory; end() runs either way.
    s_host->quiesce();
    s_host->clearLastError();   // sticky lastError_ would fake a wedge -- see the note at the disengage site
    s_host->end();             // 2.4.1+: drains client, deregisters, uninstalls, frees
    // M2: end() can TIME OUT (3 s) and, per the library, leave its tasks alive rather than
    // free in-flight transfers. Deleting the object then would be a use-after-free, and
    // restoring the console would claim the PHY before release is confirmed. On timeout,
    // RETAIN the host, latch reboot-required, and leave the console down so nothing races
    // the still-live tasks. A later engage is blocked by begin()'s non-null-handle guard.
    if (s_host->lastError() == ESP_ERR_TIMEOUT) {
      s_hostTeardownStuck = true;      // block re-engage; only a reboot clears this
      s_hostReleased = false;
      setErr("USB host stuck - reboot to reuse USB");
      s_active = false; s_bound = false; s_dev[0] = 0; s_catAddress = 0xff;
      stage(USBCAT_STAGE_END_DONE);
      return;                          // do NOT delete s_host, do NOT consoleUp()
    }
    delete s_host; s_host = nullptr;
    consoleUp();               // host released the PHY -> the serial console can return
  }

  s_active = false;
  s_bound  = false;
  s_dev[0] = 0;
  s_catAddress = 0xff;         // forget the bound adapter: a later rotator engage must not
                              // exclude an address the radio no longer holds (ANY_ADDRESS)
  s_hostReleased = true;       // stack released (or still held by the rotator, which is fine)
  stage(USBCAT_STAGE_END_DONE);
}

// active() now also asks the CDC object whether the device is still there.
// s_active/s_bound record what CardSat INTENDED; connected() records what the bus
// actually has. Before 0.9.73 only the intent was consulted, so an unplugged radio
// reported active forever.
bool    active()     { return s_active && s_bound && s_cdc && s_cdc->connected(); }
bool    catLost()    { return s_catLost; }
bool    cat2Lost()   { return s_cat2Lost; }
bool    rotLost()    { return s_rotLost; }
void    clearLostFlags() { s_catLost = false; s_cat2Lost = false; s_rotLost = false; }
Stream* stream()     { return active() ? s_cdc : nullptr; }
const char* lastError()  { return s_err; }
const char* deviceName() { return s_dev; }

void (*onStage)(Stage s) = nullptr;
void (*onRotTrace)(const char* line) = nullptr;

Stage lastBootStage() { return s_lastBootStage; }

Stage liveStage() { return (Stage)s_liveStage; }

void markStage(Stage s) { stage(s); }

// The library keeps its TaskHandle_t private, so find the tasks by the names it
// gives them at xTaskCreate -- via taskByName() above, which truncates to the
// 15-char form FreeRTOS actually stores. (The untruncated "EspUsbHostClient"
// lookup was the fix28 disengage panic: 16 chars trips xTaskGetHandle's
// configASSERT. xTaskGetHandle needs CONFIG_FREERTOS_USE_TRACE_FACILITY, which
// arduino-esp32's own lib-builder defconfig sets =y -- verified.)
// UNITS: uxTaskGetStackHighWaterMark returns BYTES on ESP-IDF -- do NOT scale it.
// Vanilla FreeRTOS divides its byte count by sizeof(StackType_t) and so returns
// WORDS, which is where the old "x4 for bytes" here came from. But IDF's Xtensa
// port defines `portSTACK_TYPE uint8_t` (portmacro.h:88), making sizeof(StackType_t)
// == 1, so prvTaskCheckFreeStackSpace's `ulCount /= sizeof(StackType_t)` is a
// divide by one and the value is already bytes. IDF stack sizes are byte-denominated
// throughout (task.h: "usStackDepth - the stack size DEFINED IN BYTES. Note that
// this differs from vanilla FreeRTOS"), which is what makes the two comparable.
// The x4 produced 28208 "free" on an 8192-byte stack -- 4x the true 7052 -- which
// is what the bench log's impossible figures actually were.
static uint32_t taskHeadroomByName(const char* name) {
  TaskHandle_t h = taskByName(name);
  return h ? (uint32_t)uxTaskGetStackHighWaterMark(h) : 0u;
}
uint32_t hostTaskHeadroom()   { return taskHeadroomByName("EspUsbHost"); }
uint32_t clientTaskHeadroom() { return taskHeadroomByName("EspUsbHostClient"); }
uint32_t taskStackBytes()     { return kTaskStack; }
// ===========================================================================
//  Rotator port -- a second CDC, bound by EXPLICIT device address
// ===========================================================================
//  Why this is not just "new EspUsbHostCdcSerial(*s_host)":
//
//  EspUsbHostCdcSerial::address_ defaults to ESP_USB_HOST_ANY_ADDRESS, and
//  EspUsbHost::findSerialDevice(ANY) returns the FIRST entry in devices_ that has
//  a bulk-OUT endpoint (verified in v2.3.0). With two adapters plugged in, "first"
//  is enumeration order -- so two ANY-bound ports both grab the same adapter, and
//  which adapter that is can change across a replug. The radio's Doppler writes
//  would go to the rotator. So: every port here binds an explicit address, chosen
//  by the user and re-found by a stable key.
//
//  The key is serial-number-first (EspUsbHostDeviceInfo::serial), falling back to
//  VID:PID+address. Serial numbers survive replugs and distinguish two adapters of
//  the SAME model, which VID:PID alone cannot -- and two identical FT232s is the
//  likely case for radio+rotator.
//
//  IC-9100/IC-9700 note (untestable here, so guarded rather than assumed): those
//  radios present an internal hub with BOTH a serial interface and a USB Audio
//  device. The library only ever claims the CDC-data/vendor-serial interface, and
//  hasSerialOutEndpoint is set only from a BULK OUT endpoint -- audio streaming
//  endpoints are isochronous, never bulk -- so an audio interface can never be
//  mistaken for the CAT port. That is structural, not luck. What audio DOES cost
//  is device slots: a 9700 is hub + serial + audio = up to 3 of the 4. Add a
//  rotator adapter (+ its own hub, if any) and the 4 slots can run out, which is
//  why slot exhaustion is reported as its own error below rather than a vague
//  "no device". devicesSeen() lets the log record exactly what enumerated.
namespace {
  // The adapter the user nominated as the rotator (a serialDeviceKey), and the
  // rotator's line speed. Both pushed in by the app from settings before rotBegin().

  bool                 s_rotActive = false;
  char                 s_rotDev[48] = {0};
  char                 s_rotErr[72] = {0};

  void setRotErr(const char* m) { snprintf(s_rotErr, sizeof(s_rotErr), "%s", m); }

  // ---- CAT-B (second radio) bookkeeping, shaped exactly like the rotator's ----
  bool                 s_cat2Active = false;
  char                 s_cat2Dev[48] = {0};
  char                 s_cat2Err[72] = {0};
  char                 s_cat2WantKey[40] = {0};

  void setErr2(const char* m) { snprintf(s_cat2Err, sizeof(s_cat2Err), "%s", m); }

  // Bring the host up for a ROTATOR-ONLY configuration (no USB CAT). Same host and
  // slots as CAT's begin() -- just without binding a radio CDC. Whoever gets here first
  // (radio or rotator) pays the ~11.8 KB and the console; the second one finds the host
  // already up and just binds a port. The host is released when the LAST owner disengages
  // (see end()/rotEnd()), not held for the whole session.
  bool hostUpForRotator() {
    if (s_host) return true;                    // already up (CAT, or a prior rotator)
    if (s_hostTeardownStuck) {                 // M2: prior teardown timed out.
      // One retry, same rationale as begin(): let usb_host_install() decide rather
      // than a latch. The rotator is the likelier path to be left stuck, because it
      // is engaged and released far more often than CAT.
      rotTrace("rot: prior teardown timed out - attempting anyway (one retry)");
      s_hostTeardownStuck = false;
    }
    if (!s_hostReleased) return false;          // a failed engage left a stack installed
    s_host = new (std::nothrow) EspUsbHost;
    if (!s_host) return false;
    s_serDevN = 0; s_sawDev = false; s_sawHub = false;   // fresh host: clear the registry
    s_host->onDeviceConnected(&onDev);          // same tracking as CAT
    s_host->onDeviceDisconnected(&onGone);      // finding A: tombstone on unplug
    consoleDown();                              // the host is about to claim the PHY
    EspUsbHostConfig hostCfg;
    hostCfg.taskCore = 0;
    hostCfg.taskStackSize = kTaskStack;
    if (!s_host->begin(hostCfg)) {
      const int e = s_host->lastError();
      s_host->end();            // 2.4.1+: daemon runs its own ALL_FREE uninstall
      // M2 (audit finding C): end() can TIME OUT and leave the library's tasks
      // alive. Deleting the host then is a use-after-free, and consoleUp() would
      // reclaim the PHY under tasks that still hold it. Every other teardown site
      // already checks this; this path -- reachable from rotator-only, CAT-B-first
      // and scanAdapters() engages -- was the one that did not. Same rule as
      // end()/rotEnd(): retain the object, latch reboot-required, console stays down.
      if (s_host->lastError() == ESP_ERR_TIMEOUT) {
        s_hostTeardownStuck = true;
        s_hostReleased = false;
        setRotErr("USB host stuck - reboot to reuse USB");
        return false;          // do NOT delete s_host, do NOT consoleUp()
      }
      delete s_host; s_host = nullptr;
      consoleUp();
      char m[64]; snprintf(m, sizeof(m), "USB host would not start (err %d)", e);
      setRotErr(m);
      return false;
    }
    // Let devices enumerate; the callback fills s_serDev. Stopping at the FIRST
    // device made the adapter list depend on enumeration order -- fine when only
    // one adapter is expected, wrong as soon as two are (dual-USB CAT, or CAT plus
    // a USB rotator, especially through a hub where the devices come up
    // staggered). Wait instead for a QUIET PERIOD: keep going until nothing new
    // has appeared for a while, bounded by the same overall cap as before.
    // Hub-aware settle. The budgets widen the moment a hub is seen (see enumCapMs),
    // and "settled" requires at least one NON-HUB device: a hub alone is never the
    // thing we are looking for, and treating its arrival as the end of enumeration
    // is exactly what hid every downstream adapter.
    // NOTE on cost: this settle loop runs ONLY on a cold host -- hostUpForRotator()
    // returns early when s_host already exists -- so the long window is paid once per
    // host lifetime, not per scan. Bench timings confirm it: 9111 ms for the first
    // scan after boot, 80 ms for every scan after that while the host stays resident.
    const uint32_t t0 = millis();
    while (millis() - t0 < enumCapMs()) {
      delay(25);
      feedFreezeWatchdog();
      if (s_serDevN == 0) continue;                          // no adapter yet: keep waiting
      if (millis() - s_lastDevMs >= enumQuietMs()) break;     // settled
    }
    return true;
  }

}

void catConfigure(const char* key) {
  snprintf(s_catWantKey, sizeof(s_catWantKey), "%s", key ? key : "");
}

// ---- CAT-B: the second radio's CDC port (dual-USB CAT) ------------------------
// Shaped like the rotator port: bind one more CDC on the shared host, to ONE
// nominated adapter, with the same shared-teardown rules. The line settings come
// from the SECOND leg's radio (its own baud; 8N2 for old-binary Yaesu), because
// the two radios need not match.
void cat2Configure(const char* key) {
  snprintf(s_cat2WantKey, sizeof(s_cat2WantKey), "%s", key ? key : "");
}

bool cat2Begin(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  // Already open: report success WITHOUT reconfiguring. To change the adapter or
  // the line settings, the caller must cat2End() first -- which App::usbCatTeardown()
  // does on every settings re-apply. (0.9.68 shipped without that teardown, so a
  // baud/model/adapter change on the uplink leg silently kept the old session.)
  //
  // But "open" must mean ALIVE (audit minor 2): if the adapter was unplugged while
  // engaged, the port object exists and connected() is false -- returning success
  // here made a settings re-apply claim a working uplink over a dead wire. Drop the
  // dead port and fall through to a fresh bind instead: with the finding-A registry
  // the replugged adapter's slot already carries its new address, so the rebind is
  // exactly the recovery the operator expects from "apply settings again".
  if (s_cat2Active && s_cdc2) {
    if (s_cdc2->connected()) return true;
    cat2End();                 // releaseHostIfIdle() inside is a no-op while CAT-A/rot own it
  }
  s_cat2Err[0] = 0;
  if (!s_host) {
    // Order of engaging the two CAT ports must not matter (same rule as the
    // rotator): whoever gets here first starts the bare host. hostUpForRotator()
    // is exactly that starter -- the name predates a second CAT port.
    if (!hostUpForRotator()) { setErr2("USB host would not start"); return false; }
  }
  int pick = cat2PickAdapter();
  if (pick < 0) { releaseHostIfIdle(); return false; }   // error text already set
  s_cdc2 = new (std::nothrow) EspUsbHostCdcSerial(*s_host);
  if (!s_cdc2) { setErr2("Out of RAM for 2nd CAT port"); releaseHostIfIdle(); return false; }
  // THE critical call, exactly as for the rotator: bind this port to ONE device
  // address so it can never race CAT-A for the first adapter in devices_.
  s_cat2Address = s_serDev[pick].address;
  s_cdc2->setAddress(s_cat2Address);
  if (!s_cdc2->begin(baud)) {
    delete s_cdc2; s_cdc2 = nullptr; s_cat2Address = 0xff;
    setErr2("2nd CAT port would not open");
    releaseHostIfIdle();
    return false;
  }
  EspUsbHostSerialConfig cfg;
  cfg.baud     = baud;
  cfg.dataBits = dataBits;
  cfg.parity   = (EspUsbHostSerialParity)parity;
  cfg.stopBits = (EspUsbHostSerialStopBits)stopBits;
  s_cdc2->setConfig(cfg);
  s_cdc2->setDtr(true);
  s_cdc2->setRts(true);
  // Bounded wait for the CDC interface to come up, exactly as CAT-A and the
  // rotator do -- connected() reflects USB readiness, not the radio answering.
  {
    const uint32_t t0 = millis();
    while (millis() - t0 < 2500 && !s_cdc2->connected()) {
      delay(20);
      feedFreezeWatchdog();
    }
  }
  if (!s_cdc2->connected()) {
    delete s_cdc2; s_cdc2 = nullptr; s_cat2Address = 0xff;
    setErr2("2nd adapter not responding");
    releaseHostIfIdle();
    return false;
  }
  snprintf(s_cat2Dev, sizeof(s_cat2Dev), "%s", s_serDev[pick].label);
  s_cat2Active = true;
  return true;
}

void cat2End() {
  if (s_cdc2) { cdcClosePort(s_cdc2); s_cdc2->end(); delete s_cdc2; s_cdc2 = nullptr; }
  s_cat2Active = false;
  s_cat2Address = 0xff;
  s_cat2Dev[0] = 0;
  releaseHostIfIdle();   // M2-safe; no-op while CAT-A or the rotator still owns it
}

bool    cat2Active()      { return s_cat2Active && s_cdc2 && s_cdc2->connected(); }
Stream* cat2Stream()      { return cat2Active() ? s_cdc2 : nullptr; }
const char* cat2DeviceName() { return s_cat2Dev; }
const char* cat2LastError()  { return s_cat2Err; }

void rotConfigure(const char* key, uint32_t baud) {
  snprintf(s_rotWantKey, sizeof(s_rotWantKey), "%s", key ? key : "");
  s_rotBaud = baud ? baud : 9600;
}

uint8_t scanAdapters() {
  // hostUpForRotator() IS a scan: it brings the host up, registers onDev and
  // waits for enumeration. The name is historical (the rotator was the first
  // caller); the behavior is exactly what a scan needs, so reuse it rather than
  // write a second copy of the host bring-up that could drift from it.
  rotTrace("scan: adapters");
  s_devRegistryFull = false;                 // per-scan verdict, not a sticky latch

  // RE-ENUMERATE FROM COLD when nothing is using the bus.
  //
  // The adapter registry is rebuilt only when a host is CREATED. Before the host
  // became resident that happened on every scan, so every scan produced a fresh, true
  // list. With a resident host it never happens again: the list only accumulates
  // whatever the callbacks reported, so an unplugged radio stays listed as
  // "(unplugged)" forever and a device attached afterwards may not appear at all.
  // Bench log: the TH-D75 tombstone survived six consecutive scans while a Prolific
  // adapter plugged in during that window was never listed.
  //
  // Residency exists to stop a re-enumeration killing a LIVE radio session. A manual
  // scan with no port open has no session to protect, so the honest thing is to give
  // the operator a real enumeration. Costs the cold-start window (~9 s) on a scan the
  // operator explicitly asked for; that is exactly what it cost before residency.
  // REFRESH THE REGISTRY FROM THE LIVE HOST -- do NOT tear it down.
  //
  // The previous attempt released the host so the next hostUpForRotator() would
  // re-enumerate from cold. It does not work: the bench log shows the release
  // succeeding and the immediate re-install failing with "host would not start", every
  // time, leaving the operator worse off than the stale list they had. The IDF host
  // cannot be reinstalled the instant it is uninstalled.
  //
  // The library already knows what is attached RIGHT NOW -- getDevices() reads its live
  // device table -- so the registry can be rebuilt from reality without touching the
  // host at all. That fixes the actual complaint (a scan not reflecting a plug or
  // unplug) without the teardown that caused the regression.
  if (s_host) {
    EspUsbHostDeviceInfo live[ESP_USB_HOST_MAX_DEVICES];
    // LET THE DEVICE TABLE SETTLE BEFORE READING IT.
    //
    // hostUpForRotator() returns immediately when the host is already resident, so a
    // re-scan used to read the table about 70 ms after the operator pressed the key --
    // far less than the ~900 ms a single device needs to enumerate. Plug something in,
    // scan straight away, see no change: that is why re-scanning looks broken after the
    // first enumeration. Bench log: thirteen consecutive re-scans, every one ~70 ms,
    // none of which could have observed an enumeration in progress.
    //
    // Poll until the count stops moving, or the cap expires. Costs two reads and a
    // 50 ms delay when nothing is changing, and does NOT pay the ~9 s cold-start
    // window -- the host is already up and enumerating on its own task while we wait.
    constexpr uint32_t SCAN_SETTLE_QUIET_MS = 400;
    constexpr uint32_t SCAN_SETTLE_CAP_MS   = 2500;
    size_t n = s_host->getDevices(live, ESP_USB_HOST_MAX_DEVICES);
    const uint32_t settleT0 = millis();
    uint32_t lastChangeMs   = settleT0;
    size_t   lastN          = n;
    while ((uint32_t)(millis() - settleT0) < SCAN_SETTLE_CAP_MS) {
      delay(50);
      n = s_host->getDevices(live, ESP_USB_HOST_MAX_DEVICES);
      if (n != lastN) { lastN = n; lastChangeMs = millis(); continue; }
      if ((uint32_t)(millis() - lastChangeMs) >= SCAN_SETTLE_QUIET_MS) break;
    }
    // Tombstone every entry, then re-publish the ones still present. An entry that is
    // gone stays dead, which is what makes an unplug visible.
    for (uint8_t i = 0; i < s_serDevN; ++i) s_serDev[i].dead = 1;
    std::atomic_thread_fence(std::memory_order_release);
    for (size_t i = 0; i < n; ++i) onDev(live[i]);
    char b[64];
    snprintf(b, sizeof(b), "scan: refreshed from live host (%u device(s))", (unsigned)n);
    rotTrace(b);
  }
  if (!hostUpForRotator()) { rotTrace("scan: host would not start"); return 0; }

  // SECOND PASS: the bus may still be settling.
  //
  // A cold enumeration that finds nothing is NOT the same as nothing being attached.
  // Bench: with the hub attached and powered from boot, the first pass fails and the
  // stack then recovers the root port on its own and enumerates completely about nine
  // seconds later -- after this scan has already reported "no adapters found". Scanning
  // again finds everything, which is the whole reason the "power dance" appeared to
  // work: unplugging and replugging simply took longer than the recovery. Waiting a
  // while after boot and scanning once has the same effect, with no cable touched.
  //
  // So when the first pass comes up empty, keep watching the live host for a second
  // window before declaring nothing there. Costs nothing when devices were already
  // found, and nothing when the host never came up.
  if (s_host && liveDeviceCount() == 0) {
    constexpr uint32_t SCAN_SECOND_PASS_MS = 12000;
    constexpr uint32_t SCAN_POLL_MS        =   250;
    rotTrace("scan: nothing yet - watching for late enumeration");
    const uint32_t t0 = millis();
    while ((uint32_t)(millis() - t0) < SCAN_SECOND_PASS_MS) {
      delay(SCAN_POLL_MS);
      EspUsbHostDeviceInfo late[ESP_USB_HOST_MAX_DEVICES];
      const size_t n = s_host->getDevices(late, ESP_USB_HOST_MAX_DEVICES);
      if (n == 0) continue;
      for (size_t i = 0; i < n; ++i) onDev(late[i]);
      if (liveDeviceCount() > 0) {
        char b[72];
        snprintf(b, sizeof(b), "scan: late enumeration after %u ms",
                 (unsigned)(millis() - t0));
        rotTrace(b);
        break;
      }
    }
  }
  if (s_devRegistryFull)
    rotTrace("scan: MORE devices than the 4-slot registry - some are not listed");
  for (uint8_t i = 0; i < s_serDevN; ++i) {
    // 96 clipped the KEY on long adapter names -- and the key is the one field
    // the operator must copy into Settings. 160 covers label(48) + key(40) + framing.
    char b[160];
    snprintf(b, sizeof(b), "scan: adapter[%u]%s addr=%u %s key=%s",
             (unsigned)i, s_serDev[i].dead ? " (unplugged)" : "",
             (unsigned)s_serDev[i].address, s_serDev[i].label, s_serDev[i].key);
    rotTrace(b);
  }
  if (s_sawHub) rotTrace("scan: hub present - extended enumeration window used");
  if (liveDeviceCount() == 0)
    // (A "check PORTA 5V" hint briefly lived here, from a line in another project's
    // README. It was WRONG for this hardware: VBUS on the OTG port is present and
    // measured, an IC-705 charges from it, and a bus-powered serial adapter enumerates
    // without any external supply. Sending operators to check their wiring over a
    // theory that the bench had already disproven would have wasted their time.)
    rotTrace(s_sawHub ? "scan: hub seen but NO adapters behind it"
                      : "scan: no adapters found");
  // A scan is a TEMPORARY owner: if neither CAT nor the rotator has a bound port, the host
  // was brought up solely to enumerate, so release it now rather than holding ~11.8 KB and
  // the console for the rest of the session. If either port is live (a scan while engaged),
  // leave the host up -- it belongs to that owner.
  // The exclusion set must cover ALL THREE ports (the s_cdc2 check was missing --
  // it predated CAT-B). releaseHostIfIdle() always enforced the full set itself,
  // so nothing ever released wrongly; but the trace below claimed "releasing" while
  // CAT-B alone held the host, and a log that lies is worse than no log.
  if (s_host && !s_cdc && !s_cdc2 && !s_rotCdc) {
    // SAY WHAT ACTUALLY HAPPENS. Since the host became resident by default,
    // releaseHostIfIdle() returns immediately without releasing anything -- so this
    // line claimed a release that never occurred. That is the same defect the comment
    // above complains about ("a log that lies is worse than no log"), reintroduced by
    // a later change, and it matters here: an operator reading "releasing" would
    // reasonably expect the serial console back, and be puzzled when it stays down.
    // The host was just re-created by this scan's cold enumeration, so it is up and
    // idle. Leaving it resident is right -- an engage moments later must not pay the
    // enumeration again, which is the whole point of residency -- but say so plainly:
    // this line used to claim a release that residency had already made impossible.
    rotTrace(s_keepHostResident
               ? "scan: host stays resident (Fn+u on Track releases it)"
               : "scan: releasing temporary host");
    releaseHostIfIdle();       // M2-safe; restores the console when the PHY is really free
  }
  // Return what is PRESENT, not how many slots have ever been used. The caller renders
  // this straight into the status line, which is how an unplugged adapter came to be
  // reported as "1 adapter found" beside a log reading "0 device(s)".
  return liveDeviceCount();
}

// SLOT COUNT, not the number of adapters present. Callers use this as an ITERATION
// BOUND for serialDeviceLabel()/serialDeviceKey(), so it must keep counting tombstoned
// slots or live entries above a dead one become unreachable. Use liveDeviceCount() for
// any "how many adapters are there" decision.
uint8_t serialDeviceCount() { return s_serDevN; }
// How many adapters are ACTUALLY present. s_serDevN counts slots ever used, and the
// live-host refresh tombstones rather than removes, so after an unplug the two differ.
// Every emptiness test in this file used s_serDevN and therefore reported one adapter
// when the log on the same screen said zero.
uint8_t liveDeviceCount() {
  uint8_t n = 0;
  for (uint8_t i = 0; i < s_serDevN; ++i) if (serialDeviceLive(i)) ++n;
  return n;
}
// A registry slot counts as selectable only when the host has actually claimed a
// serial OUT endpoint for it. onDev() excludes hubs and nothing else, so anything
// that enumerates -- an audio function, a HID device, a composite sibling -- would
// otherwise appear in the adapter picker as if it could carry CAT.
//
// Filtering HERE rather than at insert time is deliberate: serialReady() resolves
// to hasSerialOutEndpoint, which is set while interfaces are claimed, and whether
// that has happened by the time the connect callback fires is not something to
// depend on. By the time anything reads the picker, enumeration has settled and
// the answer is definitive.
bool serialDeviceLive(uint8_t i) {
  if (i >= s_serDevN || s_serDev[i].dead) return false;
  if (!s_host) return true;                 // no host to ask: do not hide anything
  return s_host->serialReady(s_serDev[i].address);
}
const char* serialDeviceLabel(uint8_t i) { return i < s_serDevN ? s_serDev[i].label : ""; }
const char* serialDeviceKey(uint8_t i)   { return i < s_serDevN ? s_serDev[i].key   : ""; }

bool rotActive()             { return s_rotActive && s_rotCdc && s_rotCdc->connected(); }
Stream* rotStream()          { return rotActive() ? (Stream*)s_rotCdc : nullptr; }
const char* rotDeviceName()  { return s_rotDev; }
const char* rotLastError()   { return s_rotErr; }

namespace {
// Mirror of rotBegin()'s adapter selection, for the radio. Same order, same
// refusals, same words -- the two ports differ only in which one they exclude.
int catPickAdapter() {
  int pick = -1;
  if (s_catWantKey[0]) {
    // Dual-USB: the radio's adapter may enumerate AFTER the rotator's (if the rotator
    // brought the host up first), so wait for this specific key before deciding it's
    // missing. Order of engaging radio vs rotator must not matter.
    waitForAdapterKey(s_catWantKey, 2500);
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && strcmp(s_serDev[i].key, s_catWantKey) == 0) { pick = i; break; }
    if (pick < 0) pick = findAdapter(s_catWantKey);   // same radio, different address
    if (pick < 0) { setErr("Radio adapter not found (replug/re-select)"); return -1; }
  } else {
    // No nominated adapter: take the first one neither the ROTATOR nor CAT-B is
    // using. With a single adapter and another port on it, that leaves none --
    // which is the honest answer, not a silent double-bind.
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (s_serDev[i].dead) continue;                       // finding A
      if (rotActive() && s_serDev[i].address == s_rotAddress) continue;
      if (s_cdc2 && s_serDev[i].address == s_cat2Address) continue;
      pick = i; break;
    }
    if (pick < 0) {
      setErr(s_serDevN == 1 ? "Only adapter is the rotator's"
                            : "Pick the radio adapter in Settings");
      return -1;
    }
  }
  // Nominated or not, never take the rotator's or CAT-B's wire.
  if (rotActive() && s_serDev[pick].address == s_rotAddress) {
    setErr("That adapter is the rotator's");
    return -1;
  }
  if (s_cdc2 && s_serDev[pick].address == s_cat2Address) {
    setErr("That adapter is the 2nd radio's");
    return -1;
  }
  return pick;
}

// CAT-B's adapter selection: same order, same refusals as catPickAdapter(), with
// the exclusion set widened to BOTH other ports (CAT-A and the rotator). An
// un-nominated CAT-B binds only when the exclusions leave exactly one candidate.
int cat2PickAdapter() {
  int pick = -1;
  if (s_cat2WantKey[0]) {
    waitForAdapterKey(s_cat2WantKey, 2500);
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && strcmp(s_serDev[i].key, s_cat2WantKey) == 0) { pick = i; break; }
    if (pick < 0) pick = findAdapter(s_cat2WantKey);  // same radio, different address
    if (pick < 0) { setErr2("2nd adapter not found (replug)"); return -1; }
  } else {
    int free = -1, freeN = 0;
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (s_serDev[i].dead) continue;                       // finding A
      if (s_active && s_cdc && s_serDev[i].address == s_catAddress) continue;
      if (rotActive() && s_serDev[i].address == s_rotAddress) continue;
      free = i; freeN++;
    }
    if (freeN == 1) pick = free;                 // unambiguous: take the one left over
    else {
      setErr2(freeN == 0 ? "No free adapter for 2nd radio"
                         : "Nominate the 2nd radio's adapter");
      return -1;
    }
  }
  if (s_active && s_cdc && s_serDev[pick].address == s_catAddress) {
    setErr2("That adapter is the radio's"); return -1;
  }
  if (rotActive() && s_serDev[pick].address == s_rotAddress) {
    setErr2("That adapter is the rotator's"); return -1;
  }
  return pick;
}

// Wait (bounded) for a SPECIFICALLY NOMINATED adapter key to enumerate. In a dual-USB
// setup (radio on one adapter, rotator on another) the two devices enumerate in an
// arbitrary order, and the host bring-up wait only blocks until the FIRST device appears
// -- which may be the other port's. Without this, engaging the second port could look for
// its adapter before the callback had registered it and fail with "not found", purely on
// enumeration order. Returns true once the key is present (or immediately if key is empty
// -- "auto" adapters have nothing specific to wait for). The onDev callback keeps filling
// s_serDev on the host task while we spin, so this observes new arrivals.
bool waitForAdapterKey(const char* key, uint32_t ms) {
  if (!key || !key[0]) return true;
  const uint32_t t0 = millis();
  for (;;) {
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && strcmp(s_serDev[i].key, key) == 0) return true;
    if (findAdapter(key) >= 0) return true;           // present, just at a new address
    if (millis() - t0 >= ms) return false;
    delay(25);
    feedFreezeWatchdog();
  }
}
}  // namespace

bool rotBegin() {
  if (rotActive()) return true;
  s_rotErr[0] = 0;
  rotTrace("rot: begin");
  if (!s_host) {
    // The rotator can bring the host up by itself: rotator-only (no USB CAT) is a
    // first-class configuration. begin() with a null baud request is not a thing,
    // so ask the caller to engage CAT first ONLY if that is what they wanted --
    // otherwise hostUpForRotator() below starts a bare host.
    rotTrace("rot: starting host (rotator-only)");
    if (!hostUpForRotator()) { setRotErr("USB host would not start"); rotTrace(s_rotErr); return false; }
  }
  rotTrace("rot: host up");
  // Name every adapter the host enumerated. With a generic USB-serial adapter and
  // no CAT engaged this is the ONLY way to see whether the device was found at
  // all, what it identifies as, and which key to persist.
  for (uint8_t i = 0; i < s_serDevN; ++i) {
    // 96 clipped the KEY on long adapter names -- and the key is the one field
    // the operator must copy into Settings. 160 covers label(48) + key(40) + framing.
    char b[160];
    snprintf(b, sizeof(b), "rot: adapter[%u]%s addr=%u %s key=%s",
             (unsigned)i, s_serDev[i].dead ? " (unplugged)" : "",
             (unsigned)s_serDev[i].address, s_serDev[i].label, s_serDev[i].key);
    rotTrace(b);
  }
  if (s_serDevN == 0) rotTrace("rot: NO adapters enumerated");

  // Pick the adapter. If the user nominated one (rotUsbKey), find it by key; if
  // not, and exactly one serial adapter is present, use it. Never guess between
  // two -- that is the misbind this whole path exists to prevent.
  //
  // EVERY failure exit from here down must call releaseHostIfIdle(). rotBegin() can
  // START the host itself (hostUpForRotator() above, the rotator-only configuration),
  // and a bare `return false` after that left the host object, the IDF stack, both USB
  // tasks and the serial console down until reboot -- for a rotator that was never
  // plugged in. releaseHostIfIdle() is a no-op when USB CAT still holds the host, so
  // a rotator that fails to bind can never take the radio's transport down with it.
  int pick = -1;
  if (s_rotWantKey[0]) {
    // Dual-USB: the rotator's adapter may enumerate AFTER the radio's, so wait for
    // this specific key before deciding it's missing -- order must not matter.
    waitForAdapterKey(s_rotWantKey, 2500);
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (!s_serDev[i].dead && strcmp(s_serDev[i].key, s_rotWantKey) == 0) { pick = i; break; }
    if (pick < 0) pick = findAdapter(s_rotWantKey);   // same rotator, different address
    if (pick < 0) {
      setRotErr("Rotator adapter not found (replug/re-select)");
      char b[96]; snprintf(b, sizeof(b), "rot: want key=%s but no adapter matches", s_rotWantKey);
      rotTrace(b); rotTrace(s_rotErr);
      releaseHostIfIdle();
      return false;
    }
  } else if (s_serDevN == 0) {
    setRotErr("No USB serial adapter detected"); rotTrace(s_rotErr);
    releaseHostIfIdle(); return false;
  } else {
    // No nominated adapter: take the first the RADIO is not driving. Mirror of
    // catPickAdapter(), deliberately -- the two ports differ only in which one
    // they exclude, and the old code here took adapter[0] unconditionally and
    // leaned on the radio check below to catch it. That worked but reported
    // "That adapter is the radio's" when the truth was "the ONLY adapter is the
    // radio's", which sends the operator hunting for a setting to change instead
    // of for a second adapter to plug in.
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (s_serDev[i].dead) continue;                       // finding A
      if (s_active && s_cdc && s_serDev[i].address == s_catAddress) continue;
      if (s_cdc2 && s_serDev[i].address == s_cat2Address) continue;
      pick = i; break;
    }
    if (pick < 0) {
      setRotErr(s_serDevN == 1 ? "Only adapter is the radio's"
                               : "Pick the rotator adapter in Settings");
      rotTrace(s_rotErr);
      releaseHostIfIdle();
      return false;
    }
    if (s_serDevN == 1) rotTrace("rot: one adapter present, using it");
  }

  // Nominated or not, never take either radio's wire.
  if (s_active && s_cdc && s_serDev[pick].address == s_catAddress) {
    setRotErr("That adapter is the radio's"); rotTrace(s_rotErr);
    releaseHostIfIdle(); return false;
  }
  if (s_cdc2 && s_serDev[pick].address == s_cat2Address) {
    setRotErr("That adapter is the 2nd radio's"); rotTrace(s_rotErr);
    releaseHostIfIdle(); return false;
  }
  { char b[80]; snprintf(b, sizeof(b), "rot: binding addr=%u baud=%lu",
                         (unsigned)s_serDev[pick].address, (unsigned long)s_rotBaud);
    rotTrace(b); }

  s_rotCdc = new (std::nothrow) EspUsbHostCdcSerial(*s_host);
  if (!s_rotCdc) { setRotErr("Out of RAM for rotator port"); rotTrace(s_rotErr);
                   releaseHostIfIdle(); return false; }
  // THE critical call: bind this port to ONE device. Without it the port is
  // ANY_ADDRESS and races the CAT port for the first adapter in devices_.
  s_rotCdc->setAddress(s_serDev[pick].address);
  if (!s_rotCdc->begin(s_rotBaud)) {
    delete s_rotCdc; s_rotCdc = nullptr;
    setRotErr("Rotator port would not open");
    rotTrace(s_rotErr);
    releaseHostIfIdle();
    return false;
  }
  rotTrace("rot: port open");
  EspUsbHostSerialConfig cfg;
  cfg.baud = s_rotBaud; cfg.dataBits = 8;
  cfg.parity = (EspUsbHostSerialParity)0; cfg.stopBits = (EspUsbHostSerialStopBits)0;
  s_rotCdc->setConfig(cfg);
  s_rotCdc->setDtr(true);
  s_rotCdc->setRts(true);
  // Wait for the CDC interface to finish coming up, exactly as the CAT path does. The
  // adapter needs time to enumerate its endpoints and the class driver to attach; checking
  // connected() immediately (as this used to) fails on a slower adapter even though the
  // port is fine. connected() reflects USB CDC readiness, NOT whether a rotator answered --
  // so this must not depend on a device being wired to the far end of the serial line.
  {
    const uint32_t t0 = millis();
    while (millis() - t0 < 2500 && !s_rotCdc->connected()) {
      delay(20);
      feedFreezeWatchdog();
    }
  }
  if (!s_rotCdc->connected()) {
    delete s_rotCdc; s_rotCdc = nullptr;
    setRotErr("Rotator adapter not responding");
    rotTrace(s_rotErr);
    releaseHostIfIdle();
    return false;
  }
  snprintf(s_rotDev, sizeof(s_rotDev), "%s", s_serDev[pick].label);
  s_rotAddress = s_serDev[pick].address;   // so CAT can refuse to steal it back
  s_rotActive = true;
  { char b[80]; snprintf(b, sizeof(b), "rot: ENGAGED %s", s_rotDev); rotTrace(b); }
  return true;
}

void rotEnd() {
  if (s_rotCdc) { rotTrace("rot: releasing port"); cdcClosePort(s_rotCdc); s_rotCdc->end(); delete s_rotCdc; s_rotCdc = nullptr; }
  s_rotActive = false;
  s_rotAddress = 0xff;
  s_rotDev[0] = 0;
  // Route through the SHARED choke point rather than open-coding a second teardown.
  // This block used to duplicate releaseHostIfIdle()'s logic, which meant the rotator
  // silently missed every fix the CAT path received -- the quiesce, the sticky-error
  // clear, and now host residency. The rotator is exactly as likely as a radio to be
  // a device that will not re-initialise after re-enumeration, so it gets the same
  // treatment by construction instead of by remembering to copy changes across.
  rotTrace(s_keepHostResident ? "rot: port released (host stays resident)"
                              : "rot: releasing host");
  releaseHostIfIdle();
}

bool     hostReleased()           { return s_hostReleased; }
bool     hostTeardownStuck()      { return s_hostTeardownStuck; }
String   uninstallDiag() {
  // EspUsbHost 2.4.1+ performs the drain/deregister/uninstall handshake itself and logs
  // any failure via ESP_LOG. We no longer hand-roll it, so there is no extra forensic
  // string to surface here; the About screen falls back to hostReleased()/lastError().
  return String();
}
uint32_t hostHeadroomSnapshot()   { return s_hostHeadroom; }
uint32_t clientHeadroomSnapshot() { return s_clientHeadroom; }

const char* stageName(Stage s) {
  switch (s) {
    case USBCAT_STAGE_ALLOC:        return "allocating USB host";
    case USBCAT_STAGE_CALLBACK:     return "registering callback";
    case USBCAT_STAGE_CONSOLE_DOWN: return "closing serial console";
    case USBCAT_STAGE_HOST_BEGIN:   return "starting USB host";
    case USBCAT_STAGE_ENUM_WAIT:    return "waiting for device";
    case USBCAT_STAGE_BIND:         return "bind: cdc begin";
    case USBCAT_STAGE_BIND_CFG:     return "bind: set config";
    case USBCAT_STAGE_BIND_DTR:     return "bind: set DTR";
    case USBCAT_STAGE_BIND_RTS:     return "bind: set RTS";
    case USBCAT_STAGE_BIND_DONE:    return "bind: done";
    case USBCAT_STAGE_RIG_STREAM:   return "rig: set stream";
    case USBCAT_STAGE_RIG_BEGIN:    return "rig: begin";
    case USBCAT_STAGE_RIG_ADDR:     return "rig: set address";
    case USBCAT_STAGE_RIG_DELAY:    return "rig: set delay";
    case USBCAT_STAGE_ENGAGED:      return "engaged (CAT tick next)";
    case USBCAT_STAGE_TICK_ENTER:   return "tick: entered";
    case USBCAT_STAGE_TICK_PTT:     return "tick: read PTT";
    case USBCAT_STAGE_TICK_READ:    return "tick: read freq";
    case USBCAT_STAGE_TICK_WRITE:   return "tick: write freq";
    case USBCAT_STAGE_TICK_DONE:    return "tick: done (tracking)";
    case USBCAT_STAGE_END_CDC:      return "end: cdc detach";
    case USBCAT_STAGE_END_HOST:     return "end: host stop";
    case USBCAT_STAGE_END_DELETE:   return "end: delete objects";
    case USBCAT_STAGE_END_CONSOLE:  return "end: console up";
    case USBCAT_STAGE_END_DONE:     return "end: done";
    default:                        return "";
  }
}

void checkLastBootStage() {
  // Trust the breadcrumb on the MAGIC WORD alone, not on the reset reason. The
  // first version gated on esp_reset_reason() and reported nothing at all, which
  // is how this comment came to exist. Two lessons, both verified in IDF v5.4:
  //
  //   1. ESP_RST_UNKNOWN is 0 -- the FIRST enumerator. Excluding it as
  //      "untrustworthy" throws away the most common case: the S3's reset-cause
  //      switch (components/esp_system/port/soc/esp32s3/reset_reason.c) has NO
  //      case for an external-pin reset, so the RST button falls to `default:`
  //      and reports ESP_RST_UNKNOWN. The old filter discarded exactly the reset
  //      an operator is most likely to perform.
  //   2. The magic word already does this job, and does it better. RTC RAM comes
  //      up as garbage on a cold boot; the odds of garbage matching a specific
  //      32-bit constant AND a stage byte in 1..BIND are negligible. The reset
  //      reason adds nothing the magic does not already cover, and (per 1) it
  //      subtracts.
  //
  // NOTE ON WHAT THIS CANNOT SEE: RTC_NOINIT survives a *restart* (esp_restart,
  // panic, watchdog), NOT a power cycle -- and the Cardputer's RST button pulls
  // the chip's EN line, which is a full chip reset that clears the RTC domain
  // too. So a breadcrumb CANNOT survive RST or a battery pull. Only a software
  // restart or a watchdog reset preserves it. That is why the freeze path also
  // arms a watchdog (see armFreezeWatchdog): a polite block on a semaphore
  // starves no task and would otherwise hang forever with no reset at all.
  if (s_rtcStageMagic == USBCAT_STAGE_MAGIC &&
      s_rtcStage != USBCAT_STAGE_NONE && s_rtcStage <= USBCAT_STAGE_END_DONE) {
    s_lastBootStage = (Stage)s_rtcStage;
  } else {
    s_lastBootStage = USBCAT_STAGE_NONE;
  }
  // Arm for this boot: clear the marker so a clean run reports nothing next time.
  s_rtcStage      = USBCAT_STAGE_NONE;
  s_rtcStageMagic = USBCAT_STAGE_MAGIC;
}

}  // namespace UsbSerial

#endif  // CARDSAT_HAS_USBCAT
