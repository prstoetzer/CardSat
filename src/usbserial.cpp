#include "usbserial.h"

#if CARDSAT_HAS_USBCAT

#include <esp_task_wdt.h>   // TWDT "user" subscription: watches this CODE, not a task
#include <esp_heap_caps.h> // largest-free-block: fragmentation vs genuine OOM
#include <usb/usb_host.h> // usb_host_lib_unblock(): the escape hatch EspUsbHost omits
#include <freertos/task.h> // uxTaskGetStackHighWaterMark(): size the stacks from data
#include <esp_timer.h>    // one-shot unblock poke during teardown (see end())

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
  const uint32_t kTaskStack = 4096;

  // (The teardown poke timer was removed in fix37. It woke the daemon out of
  // usb_host_lib_handle_events(portMAX_DELAY) so its cleanup could run -- but that
  // cleanup can never complete: EspUsbHost::end() kills the CLIENT task first, and
  // every call in the daemon's cleanup path is client-scoped. Waking the daemon
  // only got it far enough to fail. See the note in end().)

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

  // ---- Why the library's own uninstall does not stick (the 259) -----------------
  // Bench, fix32: teardown freed its memory and the tasks exited, yet a re-engage
  // failed with ESP_ERR_INVALID_STATE (259) from usb_host_install() -- the IDF host
  // stack was still installed. Cause, from the two sources side by side:
  //
  //   * usb_host_uninstall() REFUSES unless process_pending_flags, lib_event_flags
  //     and flags.val are all zero (IDF v5.4 usb_host.c:585-588).
  //   * Only usb_host_lib_handle_events() clears them (line 647 / 669) and it is
  //     also what clears the handling_events flag (line 666).
  //   * EspUsbHost's taskLoop() calls usb_host_uninstall() and IGNORES ITS RETURN,
  //     then self-deletes. So the failure is silent: the task vanishes (our wait
  //     passes, the heap comes back) while the stack stays installed.
  //
  // Our own poke is what dirties the flags: usb_host_lib_unblock() sets a pending
  // flag to wake the daemon, the daemon sees !running_ and leaves the loop WITHOUT
  // another handle_events() call, so that flag is never cleared. The daemon's
  // uninstall then fails on the very flag we set to get it out.
  //
  // So finish the job here, after the tasks are gone: poll handle_events(0) until
  // it reports nothing left (that clears all three fields), then uninstall
  // ourselves. Both are legal from any task -- neither checks caller identity, and
  // by this point the daemon is dead and cannot race us. If the library DID manage
  // its own uninstall, p_host_lib_obj is already NULL and both calls return
  // INVALID_STATE harmlessly, which is why this is safe to run unconditionally.

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

  uint8_t  s_catAddress = 0xff;      // the adapter the RADIO bound
  uint8_t  s_rotAddress = 0xff;      // the adapter the ROTATOR bound
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
  };
  SerialDev s_serDev[4];
  uint8_t   s_serDevN = 0;

  // Stable identity across replugs: serial number when the adapter reports one
  // (FTDI/CP210x usually do, CH340 usually does not), else VID:PID + address.
  // Serial-first matters because two adapters of the SAME model -- the likely
  // radio+rotator case -- are indistinguishable by VID:PID alone.
  void makeKey(char* out, size_t n, const EspUsbHostDeviceInfo& d) {
    if (d.serial && *d.serial) snprintf(out, n, "%04x:%04x/%s", d.vid, d.pid, d.serial);
    else                       snprintf(out, n, "%04x:%04x@%u", d.vid, d.pid, (unsigned)d.address);
  }

  // ONE device-connected callback for both the CAT and rotator paths. Runs on the
  // host's own task: plain byte stores only, read back after a bounded wait.
  void onDev(const EspUsbHostDeviceInfo& d) {
    s_sawDev = true;
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
    // Record it as a selectable adapter. Deduplicate by address: a composite
    // radio (IC-9100/9700) can raise the callback more than once per device.
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (s_serDev[i].address == d.address) return;
    if (s_serDevN >= (uint8_t)(sizeof(s_serDev) / sizeof(s_serDev[0]))) return;  // full
    SerialDev& e = s_serDev[s_serDevN];
    e.address = d.address; e.vid = d.vid; e.pid = d.pid;
    snprintf(e.label, sizeof(e.label), "#%u %s %s %04x:%04x",   // address-first: see s_dev
             (unsigned)d.address,
             (d.manufacturer && *d.manufacturer) ? d.manufacturer : "USB",
             (d.product && *d.product) ? d.product : "serial",
             (unsigned)d.vid, (unsigned)d.pid);
    makeKey(e.key, sizeof(e.key), d);
    s_serDevN++;
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
}

bool begin(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits) {
  if (s_active) return s_bound;
  // M2: a prior teardown timed out with tasks still alive. Re-engaging over that would race
  // the live tasks and usb_host_install() would refuse (259). Refuse cleanly until a reboot.
  if (s_hostTeardownStuck) { setErr("USB host stuck - reboot to reuse USB"); return false; }
  s_err[0] = 0; s_dev[0] = 0; s_sawDev = false;

  // ---- Reuse a live host, or build one the first time --------------------------
  // Under 2.4.1 a normal disengage releases the host, so most engages build fresh. But the
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
      if (s_cdc) { s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
      if (s_host && !s_rotCdc) {
        s_host->end(); delete s_host; s_host = nullptr;
        s_hostReleased = true; consoleUp();
      }
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
  if (!s_hostReleased) {
    setErr("USB stack wedged - reboot before re-engage");
    return false;
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
  s_serDevN = 0; s_sawDev = false;
  s_host->onDeviceConnected(&onDev);   // records the device AND the adapter list

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
    // registers a client, allocates transfers, and so on. In 2.4.1 the daemon owns
    // its own teardown -- on any post-install failure it runs the ALL_FREE handshake
    // and uninstalls with the return checked, and end() blocks until that completes.
    // So call end() here exactly as the disengage path does: it either fully releases
    // the stack or reports (via a still-set taskHandle_/lastError) that it could not,
    // and 2.4.1's begin() refuses to start over an incomplete shutdown rather than
    // returning 259 mid-operation. The daemon has already observed running_ = false by
    // this point; if install never happened, end() early-returns harmlessly.
    s_host->end();
    const bool freed = (s_host->lastError() != ESP_ERR_TIMEOUT);
    delete s_cdc;  s_cdc  = nullptr;
    delete s_host; s_host = nullptr;
    s_hostReleased = freed;
    s_active = false; s_bound = false;
    consoleUp();
    char msg[64];
    if (!freed) {
      // We could not release the stack, so a re-engage would just hit 259 again.
      // Latch it (s_hostReleased is false) and say so plainly rather than letting
      // the operator retry into the same wall.
      setErr("USB stack stuck installed - reboot before re-engage");
      stage(USBCAT_STAGE_NONE);
      return false;
    }
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
  while (millis() - t0 < 2500 && !s_cdc->connected()) { // same idiom as the perf loop
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
    // enumerates fine and is simply not recognised.
    char msg[64];
    if (s_sawDev) snprintf(msg, sizeof(msg), "Not a known serial adapter: %s", s_dev);
    else          snprintf(msg, sizeof(msg), "%s", "No USB device detected");
    // The host DID start, so it owns the PHY. Leave it up with the port unbound so a
    // retry after plugging the adapter in rebinds in milliseconds instead of re-allocating
    // ~20 KB. (This is a deliberate keep-alive for the immediate retry case, distinct from
    // a normal disengage, which does release the host via end() under 2.4.1.)
    disarmFreezeWatchdog();
    if (s_cdc) s_cdc->end();
    s_active = false; s_bound = false;
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
  // and "first" is enumeration order, which can change across a replug. Honour
  // the user's nominated radio adapter if there is one; otherwise take the first
  // enumerated serial device, which is exactly the historical single-adapter
  // behaviour.
  if (s_serDevN > 0) {
    int pick = catPickAdapter();
    if (pick < 0) {
      // H2 rollback: catPickAdapter() failed AFTER s_active was set and the watchdog was
      // armed. Returning here bare would leave s_active=true (poisoning the next engage's
      // active() check) and the freeze watchdog armed (able to reboot a later healthy op).
      // Fully unwind: disarm, drop the port, release the host if no rotator owns it.
      disarmFreezeWatchdog();
      if (s_cdc) { s_cdc->end(); delete s_cdc; s_cdc = nullptr; }
      if (s_host && !s_rotCdc) {
        s_host->end(); delete s_host; s_host = nullptr;
        s_hostReleased = true; consoleUp();
      }
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

  s_bound = true;
  disarmFreezeWatchdog();              // healthy: never let it reboot a live radio
  stage(USBCAT_STAGE_NONE);            // reached the end: clear the breadcrumb
  return true;
}

void end() {
  // NOT gated on s_active: a begin() that failed part-way can leave objects behind.
  if (!s_host && !s_cdc) { s_active = false; s_bound = false; return; }
  disarmFreezeWatchdog();

  snapshotHeadroom();          // while both tasks are alive (see the note there)

  // ---- Full teardown via EspUsbHost 2.4.1's fixed end() ------------------------
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
  // 2.4.1 guard (it refuses to start over taskHandle_/clientHandle_ that are not null)
  // protects a re-engage from racing an incomplete shutdown, which is the wedge s_hostReleased
  // used to guard by hand; we keep s_hostReleased as a belt-and-suspenders latch anyway.
  stage(USBCAT_STAGE_END_CDC);
  if (s_cdc) s_cdc->end();     // detach the CDC port first
  delete s_cdc;  s_cdc  = nullptr;

  // The host is SHARED with the USB rotator. Only tear it down when no port remains --
  // otherwise a live rotator would lose its host out from under it. Radio and rotator CAN
  // both be on USB at once (two adapters, each bound to its own device address), so this
  // guard is load-bearing, not just defensive: with the rotator still up, CAT disengage
  // must leave the host (and thus the rotator's port) running.
  stage(USBCAT_STAGE_END_HOST);
  if (s_host && !s_rotCdc) {
    s_host->end();             // 2.4.1: drains client, deregisters, uninstalls, frees
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

bool    active()     { return s_active && s_bound; }
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

  // Bring the host up for a ROTATOR-ONLY configuration (no USB CAT). Same host and
  // slots as CAT's begin() -- just without binding a radio CDC. Whoever gets here first
  // (radio or rotator) pays the ~11.8 KB and the console; the second one finds the host
  // already up and just binds a port. The host is released when the LAST owner disengages
  // (see end()/rotEnd()), not held for the whole session.
  bool hostUpForRotator() {
    if (s_host) return true;                    // already up (CAT, or a prior rotator)
    if (s_hostTeardownStuck) return false;      // M2: prior teardown timed out; reboot needed
    if (!s_hostReleased) return false;          // a failed engage left a stack installed
    s_host = new (std::nothrow) EspUsbHost;
    if (!s_host) return false;
    s_serDevN = 0; s_sawDev = false;            // fresh host: clear stale adapter registry
    s_host->onDeviceConnected(&onDev);          // same tracking as CAT
    consoleDown();                              // the host is about to claim the PHY
    EspUsbHostConfig hostCfg;
    hostCfg.taskCore = 0;
    hostCfg.taskStackSize = kTaskStack;
    if (!s_host->begin(hostCfg)) {
      const int e = s_host->lastError();
      s_host->end();            // 2.4.1: daemon runs its own ALL_FREE uninstall
      delete s_host; s_host = nullptr;
      consoleUp();
      char m[64]; snprintf(m, sizeof(m), "USB host would not start (err %d)", e);
      setRotErr(m);
      return false;
    }
    // Let devices enumerate; the callback fills s_serDev.
    const uint32_t t0 = millis();
    while (millis() - t0 < 2500 && s_serDevN == 0) delay(25);
    return true;
  }

}

void catConfigure(const char* key) {
  snprintf(s_catWantKey, sizeof(s_catWantKey), "%s", key ? key : "");
}

void rotConfigure(const char* key, uint32_t baud) {
  snprintf(s_rotWantKey, sizeof(s_rotWantKey), "%s", key ? key : "");
  s_rotBaud = baud ? baud : 9600;
}

uint8_t scanAdapters() {
  // hostUpForRotator() IS a scan: it brings the host up, registers onDev and
  // waits for enumeration. The name is historical (the rotator was the first
  // caller); the behaviour is exactly what a scan needs, so reuse it rather than
  // write a second copy of the host bring-up that could drift from it.
  rotTrace("scan: adapters");
  if (!hostUpForRotator()) { rotTrace("scan: host would not start"); return 0; }
  for (uint8_t i = 0; i < s_serDevN; ++i) {
    // 96 clipped the KEY on long adapter names -- and the key is the one field
    // the operator must copy into Settings. 160 covers label(48) + key(40) + framing.
    char b[160];
    snprintf(b, sizeof(b), "scan: adapter[%u] addr=%u %s key=%s",
             (unsigned)i, (unsigned)s_serDev[i].address, s_serDev[i].label, s_serDev[i].key);
    rotTrace(b);
  }
  if (s_serDevN == 0) rotTrace("scan: no adapters found");
  // A scan is a TEMPORARY owner: if neither CAT nor the rotator has a bound port, the host
  // was brought up solely to enumerate, so release it now rather than holding ~11.8 KB and
  // the console for the rest of the session. If either port is live (a scan while engaged),
  // leave the host up -- it belongs to that owner.
  if (s_host && !s_cdc && !s_rotCdc) {
    rotTrace("scan: releasing temporary host");
    s_host->end();
    delete s_host; s_host = nullptr;
    s_hostReleased = true;
    consoleUp();               // scan-only host released the PHY -> console can return
  }
  return s_serDevN;
}

uint8_t serialDeviceCount() { return s_serDevN; }
const char* serialDeviceLabel(uint8_t i) { return i < s_serDevN ? s_serDev[i].label : ""; }
const char* serialDeviceKey(uint8_t i)   { return i < s_serDevN ? s_serDev[i].key   : ""; }

bool rotActive()             { return s_rotActive && s_rotCdc; }
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
      if (strcmp(s_serDev[i].key, s_catWantKey) == 0) { pick = i; break; }
    if (pick < 0) { setErr("Radio adapter not found (replug/re-select)"); return -1; }
  } else {
    // No nominated adapter: take the first one the ROTATOR is not using. With a
    // single adapter and the rotator on it, that leaves none -- which is the
    // honest answer, not a silent double-bind.
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (rotActive() && s_serDev[i].address == s_rotAddress) continue;
      pick = i; break;
    }
    if (pick < 0) {
      setErr(s_serDevN == 1 ? "Only adapter is the rotator's"
                            : "Pick the radio adapter in Settings");
      return -1;
    }
  }
  // Nominated or not, never take the rotator's wire.
  if (rotActive() && s_serDev[pick].address == s_rotAddress) {
    setErr("That adapter is the rotator's");
    return -1;
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
      if (strcmp(s_serDev[i].key, key) == 0) return true;
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
    snprintf(b, sizeof(b), "rot: adapter[%u] addr=%u %s key=%s",
             (unsigned)i, (unsigned)s_serDev[i].address, s_serDev[i].label, s_serDev[i].key);
    rotTrace(b);
  }
  if (s_serDevN == 0) rotTrace("rot: NO adapters enumerated");

  // Pick the adapter. If the user nominated one (rotUsbKey), find it by key; if
  // not, and exactly one serial adapter is present, use it. Never guess between
  // two -- that is the misbind this whole path exists to prevent.
  int pick = -1;
  if (s_rotWantKey[0]) {
    // Dual-USB: the rotator's adapter may enumerate AFTER the radio's, so wait for
    // this specific key before deciding it's missing -- order must not matter.
    waitForAdapterKey(s_rotWantKey, 2500);
    for (uint8_t i = 0; i < s_serDevN; ++i)
      if (strcmp(s_serDev[i].key, s_rotWantKey) == 0) { pick = i; break; }
    if (pick < 0) {
      setRotErr("Rotator adapter not found (replug/re-select)");
      char b[96]; snprintf(b, sizeof(b), "rot: want key=%s but no adapter matches", s_rotWantKey);
      rotTrace(b); rotTrace(s_rotErr);
      return false;
    }
  } else if (s_serDevN == 0) {
    setRotErr("No USB serial adapter detected"); rotTrace(s_rotErr); return false;
  } else {
    // No nominated adapter: take the first the RADIO is not driving. Mirror of
    // catPickAdapter(), deliberately -- the two ports differ only in which one
    // they exclude, and the old code here took adapter[0] unconditionally and
    // leaned on the radio check below to catch it. That worked but reported
    // "That adapter is the radio's" when the truth was "the ONLY adapter is the
    // radio's", which sends the operator hunting for a setting to change instead
    // of for a second adapter to plug in.
    for (uint8_t i = 0; i < s_serDevN; ++i) {
      if (s_active && s_cdc && s_serDev[i].address == s_catAddress) continue;
      pick = i; break;
    }
    if (pick < 0) {
      setRotErr(s_serDevN == 1 ? "Only adapter is the radio's"
                               : "Pick the rotator adapter in Settings");
      rotTrace(s_rotErr);
      return false;
    }
    if (s_serDevN == 1) rotTrace("rot: one adapter present, using it");
  }

  // Nominated or not, never take the radio's wire.
  if (s_active && s_cdc && s_serDev[pick].address == s_catAddress) {
    setRotErr("That adapter is the radio's"); rotTrace(s_rotErr); return false;
  }
  { char b[80]; snprintf(b, sizeof(b), "rot: binding addr=%u baud=%lu",
                         (unsigned)s_serDev[pick].address, (unsigned long)s_rotBaud);
    rotTrace(b); }

  s_rotCdc = new (std::nothrow) EspUsbHostCdcSerial(*s_host);
  if (!s_rotCdc) { setRotErr("Out of RAM for rotator port"); rotTrace(s_rotErr); return false; }
  // THE critical call: bind this port to ONE device. Without it the port is
  // ANY_ADDRESS and races the CAT port for the first adapter in devices_.
  s_rotCdc->setAddress(s_serDev[pick].address);
  if (!s_rotCdc->begin(s_rotBaud)) {
    delete s_rotCdc; s_rotCdc = nullptr;
    setRotErr("Rotator port would not open");
    rotTrace(s_rotErr);
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
    return false;
  }
  snprintf(s_rotDev, sizeof(s_rotDev), "%s", s_serDev[pick].label);
  s_rotAddress = s_serDev[pick].address;   // so CAT can refuse to steal it back
  s_rotActive = true;
  { char b[80]; snprintf(b, sizeof(b), "rot: ENGAGED %s", s_rotDev); rotTrace(b); }
  return true;
}

void rotEnd() {
  if (s_rotCdc) { rotTrace("rot: releasing port"); s_rotCdc->end(); delete s_rotCdc; s_rotCdc = nullptr; }
  s_rotActive = false;
  s_rotAddress = 0xff;
  s_rotDev[0] = 0;
  // Shared host: tear it down only when CAT isn't still using it (symmetric with end()).
  if (s_host && !s_cdc) {
    rotTrace("rot: releasing host");
    s_host->end();             // 2.4.1: full drain/deregister/uninstall
    // M2: same timeout handling as end() -- on a stuck teardown, retain the host, latch
    // reboot-required, and leave the console down rather than deleting under live tasks.
    if (s_host->lastError() == ESP_ERR_TIMEOUT) {
      rotTrace("rot: host teardown TIMED OUT - reboot needed");
      s_hostTeardownStuck = true;
      s_hostReleased = false;
      return;                  // do NOT delete s_host, do NOT consoleUp()
    }
    delete s_host; s_host = nullptr;
    s_hostReleased = true;
    consoleUp();               // host released the PHY -> serial console can return
  }
}

bool     hostReleased()           { return s_hostReleased; }
bool     hostTeardownStuck()      { return s_hostTeardownStuck; }
String   uninstallDiag() {
  // EspUsbHost 2.4.1 performs the drain/deregister/uninstall handshake itself and logs
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
