#pragma once
// ===========================================================================
//  usbhelper.h  --  CardSat's client for the CardSatUsbHelper companion
// ===========================================================================
//
//  WHAT IT IS. A second USB host on the end of a Grove cable. CardSat keeps one
//  USB device on the Cardputer and hands another to an M5StickS3 running
//  companion/CardSatUsbHelper, which acts as a pure byte pipe. Every CAT dialect,
//  every rotator grammar and all of the Doppler logic stays here; the helper moves
//  bytes and manages the CDC line state.
//
//  WHY IT EXISTS. The ESP32-S3 has OTG_NUM_HOST_CHAN = 8 host channels for the
//  whole bus, one per open pipe including each device's default control pipe:
//  a hub costs 2, a CDC radio 3, a vendor-serial adapter 4. The IC-705 carries its
//  own internal TI TUSB2046 hub, so it costs 5 by itself.
//
//      hub + IC-705           = 7   works
//      hub + TH-D75 + FTDI    = 8   works, no headroom
//      hub + TH-D75 + IC-705  = 10  cannot be made to fit
//
//  usb_host_interface_claim() takes every endpoint of an interface or none, so
//  only endpoints on an interface of their own can be skipped -- true for CDC
//  control, not for vendor-serial adapters. There is no arrangement of 8 channels
//  that holds two USB radios when one is an IC-705. A second MCU brings its own 8.
//
//  ---- WHY THIS FILE IS SMALL --------------------------------------------------
//
//  The same trick usbserial.h plays: the CAT backends (CivRig / YaesuRig /
//  KenwoodRig / PlainCatRig) and every serial rotator backend already talk through
//  a `Stream*` and know nothing about the transport beneath. stream() hands them
//  one, so every protocol, every radio, every rotator and every command works over
//  the helper unchanged. Nothing in the Doppler loop, calibration or UI knows the
//  bytes are taking a detour through a Grove cable.
//
//  ---- THE THREE THINGS THAT CONSTRAIN THE DESIGN ------------------------------
//
//  1. ONE GROVE UART. The helper claims UART1 on G1/G2 -- the same wire as wired
//     CI-V, the Grove GPS, a Grove rotator, rigctl-Grove and any LEGBUS_GROVE leg.
//     Only one of those can be configured at a time, and the app enforces it
//     (App::catUsesGroveWire / rotTransportConflict). A dual rig using the helper
//     therefore has its other leg on local USB or LAN, never on Grove.
//
//  2. ONE DEVICE ON THE HELPER. v1 carries exactly one USB device, which may be a
//     single-rig radio, ONE dual-rig leg, or the rotator -- not two of them. The
//     helper is a single exclusive resource and the settings layer refuses a second
//     claimant, the same way it refuses two Grove legs.
//
//  3. NO PSRAM HERE. The ring buffers are heap-allocated by begin() and released
//     by end(), so a CardSat that never uses the helper pays nothing. On the Stick
//     the equivalent buffers are four times larger because it has 8 MB of PSRAM;
//     that asymmetry is deliberate and is why the credit window is what it is.
//
//  ---- LIVENESS AND RECOVERY ---------------------------------------------------
//
//  The helper is STATELESS: it stores nothing across reboots and generates a fresh
//  random epoch each boot, reported in CSUH_T_HELLO. When this client sees the
//  epoch change it knows the far end restarted, drops its device list, resets both
//  credit windows and re-issues the OPEN by itself. So a helper that browns out or
//  is unplugged mid-pass recovers with no operator action -- which is the whole
//  reason for preferring a stateless helper over one that remembers its config.
//
//  Wire protocol: csuh_proto.h (shared byte-identically with the companion).
// ===========================================================================
#include <Arduino.h>
#include "config.h"
#include "csuh_proto.h"

// Compile-time flag, mirroring CARDSAT_HAS_USBCAT. The helper needs no USB host
// stack on this side -- it is a UART client -- so it is available even in a build
// with USB CAT compiled out, which is exactly the build where a second USB port is
// most useful.
#if CARDSAT_HAS_USBHELPER

// Counters reported by the helper (CSUH_T_STAT). Every one of these exists so a
// question that would otherwise be answered by guessing has a number instead:
// "is the link clean at this baud" is crcErr + cobsErr, and "am I losing CAT
// bytes" is overrun. A silently dropped CI-V byte does not look like a link
// fault, it looks like a radio fault, and would be chased as one.
struct CsuhStats {
  uint32_t framesRx = 0;   // frames the helper decoded OK
  uint32_t framesTx = 0;   // frames the helper sent
  uint32_t crcErr   = 0;   // frames that failed CRC (link quality)
  uint32_t cobsErr  = 0;   // frames that failed COBS/length checks
  uint32_t usbRx    = 0;   // bytes in from the USB device
  uint32_t usbTx    = 0;   // bytes out to the USB device
  uint32_t overrun  = 0;   // bytes lost to a full ring -- must stay 0
  uint32_t heap     = 0;   // helper free heap
  uint32_t uptime   = 0;   // helper seconds since boot
  // 0.9.73 trailing extension (CSUH_STAT_LEN 39): enumeration diagnostics.
  uint8_t  seen     = 0;   // raw non-hub registry entries on the helper
  uint8_t  usable   = 0;   // subset passing the serial-capability filter
  uint8_t  hostUp   = 0;   // 1 = helper's usb_host stack installed OK
  bool     ext      = false; // true when the helper sent the extended block
  bool     valid    = false;
};

// The transport handed to a Rig or Rotator backend. Every accessor pumps the link
// first, because the backends are written against a UART: they write a command and
// then spin on available() waiting for a reply, and if that spin did not service
// the link no reply could ever arrive. That is the same shape usbserial's CDC
// stream has, for the same reason.
class HelperStream : public Stream {
public:
  int    available() override;
  int    read() override;
  int    peek() override;
  size_t write(uint8_t b) override;
  size_t write(const uint8_t* d, size_t n) override;
  void   flush() override;
  using Print::write;
};

namespace UsbHelper {

  // ---- link lifecycle -----------------------------------------------------
  // Claim the Grove UART and start talking. `linkBaud` must be one of
  // CSUH_BAUDS[]; anything else is clamped to the default, because the helper
  // only ever scans that list and a rate outside it could never link at all.
  // Returns false only if the ring buffers could not be allocated -- the link
  // itself comes up asynchronously, so "did it work" is linked(), not this.
  bool  begin(uint32_t linkBaud);
  // Release the UART and the rings. Sends a CLOSE first so the helper drops DTR
  // on the radio (a CDC device has no other close notification, and a radio that
  // keys its CAT session off DTR otherwise believes the session is still open --
  // measured on a TH-D75, where it meant CAT could not be re-established without
  // power-cycling the RADIO).
  void  end();
  bool  started();
  // Pump the link. Called from the main loop AND from every HelperStream
  // accessor. Cheap when there is nothing to do.
  void  service();

  bool        linked();            // HELLO seen and the peer is answering
  const char* helperVersion();     // firmware version string from HELLO ("" if none)
  uint32_t    linkBaud();          // the rate actually in use
  uint32_t    lastSeenMs();        // millis() of the last valid frame (0 = never)

  // ---- device enumeration (the picker on SCR_USBHELPER) -------------------
  void        requestScan();            // ask the helper for its device list
  uint8_t     deviceCount();
  const char* deviceLabel(uint8_t i);   // "IC-705 0c26:0036"
  const char* deviceKey(uint8_t i);     // "0c26:0036@2" -- the string persisted
  bool        deviceIsOpen(uint8_t i);
  // Reboot the helper to force a clean re-enumeration. Safe because the helper is
  // stateless; it is also the ONLY recovery when a USB host stack has wedged, and
  // unlike an in-place teardown it cannot itself get stuck.
  void        rescan();

  // ---- the port -----------------------------------------------------------
  // Nominate WHICH device on the helper to use ("" = the only one present). Call
  // before open(); the app pushes this from cfg.helperKey.
  void        configure(const char* key);
  // Ask the helper to open the nominated device. Returns immediately: the OPEN is
  // a round trip, so active() is what says it succeeded. Re-issued automatically
  // after a helper reboot.
  bool        open(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);
  void        close();
  bool        active();                 // a port is open on the far end
  Stream*     stream();                 // nullptr unless active()
  const char* deviceName();             // what the helper says it bound
  const char* lastError();              // "" when there is nothing to report
  // Change line coding on an already-open port (a per-leg baud edit).
  void        setLine(uint32_t baud, uint8_t dataBits, uint8_t parity, uint8_t stopBits);
  // Drive the CDC control lines explicitly. Rarely needed: open() asserts both.
  void        setModem(bool dtr, bool rts);

  // ---- diagnostics --------------------------------------------------------
  void              requestStats();     // ask; the reply lands asynchronously
  const CsuhStats&  stats();
  // Frames this side decoded / failed, for the link-quality readout. The helper
  // reports its own half in stats(); both halves matter, because a cable fault is
  // usually worse in one direction than the other.
  uint32_t    rxFrames();
  uint32_t    restarts();      // helper reboots seen (epoch changes after the first)
  uint32_t    helloReqTx();    // tenth bench: handshake pipeline stage counters
  uint32_t    helloRx();
  uint32_t    lastRttMs();     // audit F11: last validated ping round-trip
  const char* lastErr();       // last setErr() text ("" when clear)
  void        rxClassCounts(uint32_t* pong, uint32_t* stat, uint32_t* data,
                            uint32_t* credit, uint32_t* event, uint8_t* lastTy);
  uint32_t    linkDrops();     // twelfth bench: dead-timer firings
  uint32_t    lastValidAge();  // ms since the last VALID frame (live)
  uint8_t     txCredit();      // DATA_OUT credit currently held
  uint32_t    localCrcErr();   // OUR decode failures (helper->CardSat direction)
  uint32_t    localCobsErr();
  uint32_t    rxCrcErrors();
  uint32_t    rxCobsErrors();
  uint32_t    txFrames();
  // The last EVENT the helper raised, as text for the status line ("" if none).
  const char* lastEvent();
}

#endif  // CARDSAT_HAS_USBHELPER
