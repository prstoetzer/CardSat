#pragma once
// ===========================================================================
//  rig.h  -  abstract transceiver interface (one backend per CAT family)
// ===========================================================================
//
//  The whole application talks to the radio through this narrow interface, so
//  SGP4 prediction, the One True Rule Doppler loop, calibration and the UI are
//  all protocol-agnostic. Concrete backends:
//      CivRig     (civ.cpp)     Icom CI-V          IC-820/821/910/970/9100/9700
//      YaesuRig   (yaesu.cpp)   Yaesu 5-byte CAT   FT-847, FT-736R
//      KenwoodRig (kenwood.cpp) Kenwood ASCII CAT  TS-790, TS-2000
//      IcomNetRig (icomnet.cpp) Icom LAN (RS-BA1)  IC-9700 over UDP, no wiring
//      RigctlRig  (below)       Hamlib NET rigctl  any rig behind a rigctld
//  The three wire-level backends take a Stream*, so their transport is a
//  runtime choice: the G1/G2 UART or a USB<->serial adapter (CAT_USB).
//
//  Convention used everywhere in the app (kept regardless of how a given rig
//  labels its VFOs): "Sub" = downlink / RX, "Main" = uplink / TX.
// ===========================================================================
#include <Arduino.h>
#include <WiFi.h>            // WiFiClient for the rigctl (network) backend
#include "radio_profiles.h"

// Protocol-neutral operating modes; each backend maps these to its own codes.
enum RigMode : uint8_t { RM_LSB, RM_USB, RM_CW, RM_FM, RM_AM, RM_DATA };

class Rig {
public:
  virtual ~Rig() {}

  // Open the CAT serial port. The backend already knows its own model/params.
  virtual void begin(uint32_t baud, int uartNum, int rxPin, int txPin) = 0;
  virtual bool ready() const = 0;

  // Pumped every loop tick. Network backends (Icom LAN) advance their connection
  // state machine and answer keepalives here; wired backends need nothing.
  virtual void service() {}

  // Inter-command pacing: pause this many ms after each CAT frame (CAT Delay),
  // so a slow radio keeps up. Overwritten from the CAT Delay setting at engage.
  void setCmdDelay(uint16_t ms) { cmdDelayMs = ms; }
  // Upper bound (ms) on how long a single blocking CAT read may wait, so slow
  // I/O (especially the LAN backend) can't stall the cooperative main loop.
  // 0 = use the backend's built-in default. Set from the CAT cycle rate at engage.
  void setReadBudgetMs(uint16_t ms) { readBudgetMs = ms; }

  // ---- External transport (USB-serial) --------------------------------------
  // Supply a ready-made Stream for the wire-level backends to talk through,
  // INSTEAD of them opening the on-board UART in begin(). Used by CAT_USB, where a
  // USB<->serial adapter (FTDI/CP210x/CH34x) is the transport and the ESP32 UART is
  // not involved at all.
  //
  // Why a setter rather than a new backend: the three wire-level backends
  // (CivRig/YaesuRig/KenwoodRig) already talk through `Stream* _stream` and know
  // nothing about what is underneath. Only their begin() binds a UART. So the whole
  // of CAT -- every protocol, every radio, every command -- works unchanged over any
  // Stream. Set this before begin(); begin() then skips all UART/pin setup.
  //
  // Lifetime: the caller owns the Stream and must keep it alive for as long as the
  // Rig is, and must clear it (or delete the Rig) before tearing the Stream down.
  //
  // VIRTUAL, and it must stay that way. Each backend's begin() caches this pointer
  // in its own `_stream` member, so clearing ONLY extStream here leaves the backend
  // holding a second copy -- which is exactly the 0.9.58-wip fix31 crash: disengage
  // cleared extStream, UsbSerial::end() deleted the CDC object, and the next CAT
  // call dereferenced the backend's stale _stream (loopTask LoadProhibited in the
  // rig path, right after "end: done"). The bug was latent for as long as the
  // caching existed and only became fatal when end() started actually deleting.
  // Overrides clear BOTH, so one call from the caller is enough.
  virtual void setExternalStream(Stream* s) { extStream = s; }
  Stream* externalStream() const { return extStream; }
protected:
  uint16_t cmdDelayMs = 70;
  uint16_t readBudgetMs = 0;
  Stream*  extStream = nullptr;      // non-null => begin() must not touch the UART
public:

  // Independent downlink (Sub/RX) and uplink (Main/TX) control.
  virtual bool setMainFreq(freq_t hz) = 0;   // uplink (TX)
  virtual bool setSubFreq (freq_t hz) = 0;   // downlink (RX)
  virtual bool setMainMode(RigMode m)   = 0;
  virtual bool setSubMode (RigMode m)   = 0;

  // Read the downlink (Sub/RX) frequency. Returns false if unsupported.
  virtual bool readSubFreq(freq_t& hzOut) = 0;
  virtual bool readMainFreq(freq_t& hzOut) = 0;

  // Read the rig's PTT/transmit state into tx. Returns false if the backend
  // can't report it (the default), in which case the caller must not assume
  // anything about transmit state. The Doppler loop uses this to skip the
  // downlink knob read while transmitting (a rig often reports the TX VFO then).
  virtual bool readPtt(bool& tx) { (void)tx; return false; }

  // Send a raw control line to the backend and return one reply line ("" if
  // unsupported/failed). Used by the Dual-Rig setup screen to talk to the
  // CardSatDualRig companion's \csdr_* config escape over whichever rigctl
  // transport is active (net or Grove). No-op on wire-level backends -- only the
  // rigctl backends carry a line protocol a companion can answer.
  virtual String vendorLine(const String& line) { (void)line; return String(); }

  // Toggle the rig's own satellite mode. Icom: actively forced OFF (we drive
  // MAIN/SUB ourselves). Yaesu/Kenwood: no-op -- their full-duplex/sat mode is
  // set up by the operator and must NOT be disturbed.
  virtual bool enableSatMode(bool on) = 0;

  // Leave band access on the downlink so the operator's dial stays on RX
  // (meaningful for Icom; no-op elsewhere).
  virtual void selectSubBand() = 0;
  virtual void selectMainBand() = 0;

  // Assign which frequency BAND (2 m / 70 cm / 23 cm) sits on the MAIN vs SUB
  // VFO, so the uplink/downlink land on the correct bands when radio control is
  // engaged. Only radios with a CAT band-assignment command implement this
  // (IC-9100/IC-9700 via CI-V 07 D2); everyone else returns false and the
  // operator sets the band pair up on the radio. mainHz/subHz are the actual
  // frequencies whose bands should be placed on MAIN/SUB respectively.
  // UNTESTED on hardware -- see canAssignBand() and the CivRig implementation.
  virtual bool assignBands(freq_t mainHz, freq_t subHz) {
    (void)mainHz; (void)subHz; return false;
  }

  // Set the transmit CTCSS (PL) tone encoder. Used for FM satellites whose
  // uplink requires a subaudible tone (SO-50, AO-91, ISS, PO-101...). The tone
  // is applied to the uplink (Main/TX). on=false disables it. Backends that
  // can't drive CTCSS over CAT return false (the default).
  virtual bool setCtcss(bool on, float toneHz) { (void)on; (void)toneHz; return false; }

  // Capabilities / identity (read from the model's RadioProfile).
  virtual bool canReadFreq() const = 0;
  virtual bool hasSatMode()  const = 0;
  virtual bool hasTone()     const { return false; }   // CAT CTCSS supported
  virtual bool canAssignBand() const { return false; } // CAT MAIN/SUB band assign
  virtual bool selVerified() const = 0;
  virtual const char* name() const = 0;

  // CI-V address (Icom only; harmless no-ops on other backends).
  virtual void    setAddress(uint8_t) {}
  virtual uint8_t address() const { return 0; }

  // CI-V wiring mode (Icom wired only; no-op elsewhere). Call before begin().
  // 0 = separate TX/RX, 1 = single-pin on TX (G2), 2 = single-pin on RX (G1).
  virtual void    setPinMode(uint8_t) {}

  // Raw serial diagnostics: write arbitrary bytes straight to the CAT port (the
  // serial-terminal tool). Returns false if the backend has no byte stream (e.g.
  // the Icom LAN backend, which is packetized, not a raw UART). Default: no-op.
  virtual bool    sendRaw(const uint8_t*, size_t) { return false; }

  // Map a SatNOGS/AMSAT mode string ("FM","USB","CW","DATA"...) to a RigMode.
  static RigMode modeFromString(const String& s);
};

// Index (0..38) of the nearest standard CTCSS tone to hz, or -1 if hz<=0 or no
// tone is within tolerance. The 39-tone EIA list is shared by the Yaesu code
// table and the Kenwood tone numbers; Icom encodes the frequency directly.
int  ctcssToneIndex(float hz);
// The standard tone (in Hz) at a given index, or 0 if out of range.
float ctcssToneHz(int index);

// rigctld (Hamlib "NET rigctl") TCP CLIENT. CardSat drives a radio attached to
// a rigctld server elsewhere on the LAN (default port 4532). To carry both legs
// of a satellite QSO over one link it uses Hamlib VFO mode: the downlink (Sub/RX)
// rides VFOA and the uplink (Main/TX) rides VFOB, each steered with plain
// set_freq/set_mode after a set_vfo -- the portable convention gpredict and the
// Hamlib backends use (set_split_freq makes Hamlib tune the wrong VFO on Icoms).
// The socket opens lazily and reconnects (throttled) so a missing server never
// hangs the Doppler loop. Model-agnostic: the remote rigctld owns the radio.
class RigctlRig : public Rig {
public:
  RigctlRig(const char* host, uint16_t port) : _host(host), _port(port) {}
  void begin(uint32_t, int, int, int) override { _lastTry = 0; ensure(); }
  bool ready() const override { return _ok; }
  void service() override { ensure(); }
  bool setMainFreq(freq_t hz) override;   // uplink   -> VFOB set_freq
  bool setSubFreq (freq_t hz) override;   // downlink -> VFOA set_freq
  bool setMainMode(RigMode m)   override;   // uplink   -> VFOB set_mode
  bool setSubMode (RigMode m)   override;   // downlink -> VFOA set_mode
  bool readSubFreq (freq_t& hzOut) override;   // VFOA get_freq
  bool readMainFreq(freq_t& hzOut) override;   // VFOB get_freq
  bool readPtt(bool& tx) override;               // get_ptt
  bool enableSatMode(bool) override { return false; }  // remote rig is operator-configured
  void selectSubBand()  override {}
  void selectMainBand() override {}
  bool canReadFreq() const override { return true; }
  bool hasSatMode()  const override { return false; }
  bool selVerified() const override { return _ok; }
  const char* name() const override { return "rigctl"; }
  // Raw \csdr_* / rigctl line passthrough for the Dual-Rig setup screen. Sends one
  // line (newline appended if missing) and returns the single reply line. Inherited
  // unchanged by RigctlGroveRig, so it automatically uses the Grove transport there.
  String vendorLine(const String& line) override {
    // H12: \csdr_models / \csdr_get can return >1 KB of JSON. At low Grove baud the reply
    // alone can exceed the default 400 ms (e.g. ~1.4 s for the model catalogue at 9600).
    // Give a baud-aware deadline for these large vendor replies so they don't time out;
    // ordinary short RPRT commands keep the default.
    String l = line.endsWith("\n") ? line : line + "\n";
    uint32_t baud = linkBaud();
    uint32_t replyMs = 400;
    if (baud && (line.indexOf("csdr_models") >= 0 || line.indexOf("csdr_get") >= 0 ||
                 line.indexOf("csdr_status") >= 0)) {
      // ~2 KB budget at this baud (bytes*10 bits / baud), plus headroom, min 2 s.
      uint32_t ms = (uint32_t)((2048UL * 10UL * 1000UL) / baud) + 500;
      replyMs = ms < 2000 ? 2000 : ms;
    }
    return xchg(l, replyMs);
  }
  // Effective UART baud of this transport, or 0 for the network path (no framing delay).
  virtual uint32_t linkBaud() const { return 0; }
protected:
  // ---- transport boundary --------------------------------------------------
  // The VFO-mode protocol (below) is transport-agnostic: it only ever calls these
  // four primitives, so a subclass that overrides them speaks the identical
  // protocol over a different wire. The base drives a TCP socket (rigctld over the
  // network); RigctlGroveRig overrides them to drive the Grove UART instead. Keeping
  // ONE copy of the command logic is deliberate -- the VFO-mode fix must never drift
  // between the two transports.
  virtual bool   linkOpen();                         // ensure the link is up; sets _ok
  virtual void   linkClose();                        // drop/reset the link
  virtual size_t linkWrite(const uint8_t* d, size_t n);
  virtual int    linkRead();                         // one byte, or -1 if none ready

  // ---- shared state + protocol (inherited unchanged by the Grove variant) ----
  String     _host;
  uint16_t   _port;
  bool       _ok = false;
  WiFiClient _c;                          // used only by the base (TCP) transport
  uint32_t   _lastTry = 0;
  int    _vfo = -1;        // server's currently-selected VFO: 0=A (downlink), 1=B (uplink), -1=unknown
  bool   _vfoMode = false; // server was started with --vfo (VFO travels inline on each command)
  bool   _probed = false;  // \chk_vfo probe done since the link last came up
  uint8_t _failStreak = 0; // M22: consecutive empty replies; closes the link past a threshold
  bool   ensure();                       // bring the link up + probe once; updates _ok
  void   probeVfoMode();                 // one-shot \chk_vfo probe (shared by all transports)
  String readLine(uint32_t timeoutMs);   // read one reply line from linkRead()
  String xchg(const String& tx, uint32_t replyMs = 400);   // send one line, return one reply line ("" on fail)
  void   selectVfo(bool sub);            // pre-select downlink(A)/uplink(B) VFO on currVFO servers
  String cmd(char c, bool sub, const String& body = "");  // build a line; VFO inline in --vfo mode
  static const char* vfoTok(bool sub);   // "VFOA" (downlink) / "VFOB" (uplink)
  static const char* modeName(RigMode m);
};

// rigctl over the Grove UART (G1/G2) instead of TCP: same VFO-mode protocol, wired
// transport. This is CardSat driving the CardSatDualRig companion (or any rigctld
// speaking device) over a Grove cable with no Wi-Fi. Claims UART1 like wired CI-V,
// so it shares the Grove mutual-exclusion rules. The host/port members are unused.
class RigctlGroveRig : public RigctlRig {
public:
  RigctlGroveRig(uint32_t baud) : RigctlRig("", 0), _baud(baud) {}
  ~RigctlGroveRig() override { linkClose(); }   // M8: release Serial1 when the backend is deleted
  uint32_t linkBaud() const override { return _baud; }   // H12: for baud-aware vendor timeouts
  void begin(uint32_t, int uartNum, int rxPin, int txPin) override;   // opens the Grove UART (rx=arg3, tx=arg4)
  const char* name() const override { return "rigctl-grove"; }
protected:
  bool   linkOpen() override;
  void   linkClose() override;
  size_t linkWrite(const uint8_t* d, size_t n) override;
  int    linkRead() override;
private:
  HardwareSerial* _serial = nullptr;    // Serial1 on G1/G2; owned elsewhere (static)
  uint32_t _baud;
  int      _rx = -1, _tx = -1;
  bool     _open = false;
};

// Construct the backend for a model. Caller owns the returned pointer.
// catType 0 = wired CI-V/CAT (UART); 1 = Icom LAN (network) for CI-V models, in
// which case host/port/user/pass configure the RS-BA1 UDP connection.
Rig* makeRig(RadioModel model, uint8_t catType = 0, const char* host = "",
             uint16_t port = 50001, const char* user = "", const char* pass = "",
             uint32_t groveBaud = 115200);   // C2: Grove baud (32-bit; catPort can't hold 115200)

// CAT serial trace sink. When set (by the serial-terminal screen), every backend
// reports each raw CAT frame it sends or receives here: dir is "TX" or "RX", b/n
// are the raw bytes. Off (nullptr) by default so there is zero overhead normally.
// Used only for the on-device diagnostic monitor; does not affect operation.
typedef void (*CatTraceFn)(const char* dir, const uint8_t* b, size_t n);
extern CatTraceFn catTraceSink;
void catTrace(const char* dir, const uint8_t* b, size_t n);  // safe wrapper (null-checks)
