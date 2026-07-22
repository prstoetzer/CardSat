// ===========================================================================
//  rig.cpp  -  Rig factory + shared helpers
// ===========================================================================
#include "rig.h"
#include "settings.h"   // CatType enum (avoid raw catType magic numbers)
#include "civ.h"
#include "icomnet.h"
#include "yaesu.h"
#include "kenwood.h"

// CAT serial trace sink (see rig.h). Null unless the serial-terminal screen sets
// it; catTrace() is the null-safe wrapper the backends call on every frame.
CatTraceFn catTraceSink = nullptr;
void catTrace(const char* dir, const uint8_t* b, size_t n) {
  if (catTraceSink) catTraceSink(dir, b, n);
}

RigMode Rig::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return RM_FM;
  if (u.indexOf("USB") >= 0) return RM_USB;
  if (u.indexOf("LSB") >= 0) return RM_LSB;
  if (u.indexOf("CW")  >= 0) return RM_CW;
  if (u.indexOf("AM")  >= 0) return RM_AM;
  if (u.indexOf("FSK") >= 0 || u.indexOf("RTTY") >= 0 ||
      u.indexOf("DATA") >= 0 || u.indexOf("DIG") >= 0) return RM_DATA;
  // Linear transponders are most often operated USB up / USB down.
  return RM_USB;
}

Rig* makeRig(RadioModel model, uint8_t catType, const char* host,
             uint16_t port, const char* user, const char* pass,
             uint32_t groveBaud) {
  if (model == RIG_NONE) return nullptr;   // no radio: CardSat runs as a tracker only
  // M24: nothrow allocation throughout. On low contiguous heap, return nullptr so the
  // caller (which already null-checks rig) reports "radio not ready" instead of crashing.
  if (catType == CAT_RIGCTL) {            // rigctld client (TCP): model-agnostic
    (void)user; (void)pass;
    return new (std::nothrow) RigctlRig(host, port);
  }
  if (catType == CAT_RIGCTL_GROVE) {      // rigctld client over the Grove UART (no Wi-Fi)
    (void)host; (void)port; (void)user; (void)pass;
    return new (std::nothrow) RigctlGroveRig(groveBaud ? groveBaud : 115200);   // C2: dedicated 32-bit baud
  }
  // Icom LAN (RS-BA1 UDP) network CAT: only for CI-V models.
  if (catType == CAT_NET && RADIOS[model].proto == PROTO_CIV)
    return new (std::nothrow) IcomNetRig(model, host, port, user, pass);
  switch (RADIOS[model].proto) {
    case PROTO_YAESU:   return new (std::nothrow) YaesuRig(model);
    case PROTO_KENWOOD: return new (std::nothrow) KenwoodRig(model);
    case PROTO_CIV:
    default:            return new (std::nothrow) CivRig(model);
  }
}

// Standard 39 EIA CTCSS tones in tenths of Hz, ascending. This exact order is
// shared with Hamlib's ft847_ctcss_list[] and the Kenwood tone list, so the
// index doubles as the Kenwood tone number (index+1) and the row into the
// FT-847 CAT code table. Icom encodes the frequency in BCD instead.
static const uint16_t CTCSS_TENTHS[39] = {
  670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
  948, 974, 1000,1035,1072,1109,1148,1188,1230,1273,
  1318,1365,1413,1462,1514,1567,1622,1679,1738,1799,
  1862,1928,2035,2107,2181,2257,2336,2418,2503
};

int ctcssToneIndex(float hz) {
  if (hz <= 0) return -1;
  int target = (int)lroundf(hz * 10.0f);   // tenths of Hz
  int best = -1, bestErr = 9999;
  for (int i = 0; i < 39; ++i) {
    int e = abs((int)CTCSS_TENTHS[i] - target);
    if (e < bestErr) { bestErr = e; best = i; }
  }
  // Reject if the nearest standard tone is more than ~1 Hz away (bad input).
  return (bestErr <= 10) ? best : -1;
}

float ctcssToneHz(int index) {
  if (index < 0 || index >= 39) return 0.0f;
  return CTCSS_TENTHS[index] / 10.0f;
}

// ---------------------------------------------------------------------------
//  RigctlRig - rigctld (Hamlib NET rigctl) TCP client backend
// ---------------------------------------------------------------------------
// ---- base (TCP) transport primitives --------------------------------------
bool RigctlRig::linkOpen() {
  if (_c.connected()) { _ok = true; return true; }
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < 3000) { _ok = false; return false; }   // throttle retries
  _lastTry = now; _c.stop();
  if (WiFi.status() != WL_CONNECTED || _host.length() == 0) { _ok = false; return false; }
  _ok = _c.connect(_host.c_str(), _port, 1500);
  if (_ok) { _probed = false; _failStreak = 0; }   // fresh connection -> re-probe, clear streak
  return _ok;
}
void   RigctlRig::linkClose() { _c.stop(); }
size_t RigctlRig::linkWrite(const uint8_t* d, size_t n) { return _c.write(d, n); }
int    RigctlRig::linkRead() { return _c.read(); }

// ---- shared protocol (transport-agnostic) ---------------------------------
// Read one non-empty reply line via the transport's linkRead(). "" on timeout.
String RigctlRig::readLine(uint32_t timeoutMs) {
  String line; uint32_t t = millis();
  while ((millis() - t) < timeoutMs) {
    int ch = linkRead();
    if (ch < 0) { delay(2); continue; }
    if (ch == '\n' || ch == '\r') { if (line.length()) break; else continue; }
    line += (char)ch;
  }
  return line;
}

// One-shot VFO-mode handshake, shared by every transport. We steer the two legs by
// selecting a VFO and then issuing plain set_freq/set_mode on it (downlink = VFOA,
// uplink = VFOB) -- what gpredict and mainstream Hamlib backends expect for a duplex
// sat rig, and far more portable than set_split_freq (which makes Hamlib tune the
// wrong VFO on Icoms). Works against any rigctld, including CardSat's own server and
// the CardSatDualRig companion. Probe \chk_vfo: a server started with --vfo answers
// "CHKVFO 1" and then wants the VFO inline on every command; otherwise we pre-select
// with V and send bare commands on currVFO.
void RigctlRig::probeVfoMode() {
  _vfo = -1; _vfoMode = false;
  const char* q = "\\chk_vfo\n";
  linkWrite((const uint8_t*)q, strlen(q));
  String r = readLine(300);
  if (r.indexOf("CHKVFO 1") >= 0) _vfoMode = true;
  _probed = true;
}

bool RigctlRig::ensure() {
  if (!linkOpen()) return false;
  if (!_probed) probeVfoMode();
  return _ok;
}

const char* RigctlRig::modeName(RigMode m) {
  switch (m) {
    case RM_LSB: return "LSB";  case RM_USB:  return "USB";
    case RM_CW:  return "CW";   case RM_FM:   return "FM";
    case RM_AM:  return "AM";   case RM_DATA: return "PKTUSB";
  }
  return "USB";
}

// Send one command line; return the first non-empty reply line ("" on failure).
String RigctlRig::xchg(const String& tx, uint32_t replyMs) {
  if (!ensure()) return "";
  uint32_t t0 = millis();
  while (linkRead() >= 0 && (millis() - t0) < 20) { }           // drain stale reply
  if (linkWrite((const uint8_t*)tx.c_str(), tx.length()) != tx.length()) {
    _ok = false; linkClose(); return "";
  }
  String r = readLine(replyMs);
  // M22: a silent peer returns "" here. Left unchecked, _ok stays true and every Doppler
  // tick pays the full 400 ms timeout while the UI still shows the rig engaged. Count
  // consecutive empty replies and, past a small threshold, mark not-ready and close the
  // link so ready() tells the truth and ensure() must re-establish (and re-probe) it.
  if (r.length() == 0) {
    if (_failStreak < 255) _failStreak++;
    if (_failStreak >= 3) { _ok = false; linkClose(); }
  } else {
    _failStreak = 0;                          // a real reply: the peer is alive
  }
  return r;
}

// ===========================================================================
//  RigctlGroveRig - the same VFO-mode protocol over the Grove UART (G1/G2).
//  Serial1 is shared with wired CI-V / Grove GPS / Grove rotator; the caller's
//  mutual-exclusion rules guarantee only one owner at a time.
// ===========================================================================
void RigctlGroveRig::begin(uint32_t, int /*uartNum*/, int rxPin, int txPin) {
  // C1: the base Rig::begin contract is (baud, uartNum, rxPin, txPin). Capture the pins
  // from args 3 and 4 -- an earlier signature named args 2/3 rx/tx, which stored uartNum
  // as _rx and rxPin as _tx (both GPIO 1) and dropped the real txPin, so the Grove cable
  // ran RX and TX on the same pin.
  _rx = rxPin; _tx = txPin; _lastTry = 0; _probed = false; _open = false;
  linkOpen();
}
bool RigctlGroveRig::linkOpen() {
  // M7: opening Serial1 always "succeeds" at the driver level, so an ABSENT companion used
  // to be marked ready forever -- each command then paid a full timeout and the UART was
  // never closed. Now: open the UART once, probe for a live companion, and only stay ready
  // if it answered. On no answer, close and back off so we don't hammer the bus / hitch the
  // UI. ensure() re-probes on the next attempt after the backoff.
  if (_open) return _ok;                        // already up: keep whatever readiness we have
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < 3000) { _ok = false; return false; }   // backoff after a failed probe
  _lastTry = now;
  if (!_serial) _serial = &Serial1;
  _serial->begin(_baud, SERIAL_8N1, _rx, _tx);
  _open = true; _probed = false;
  // Lightweight liveness probe: a rigctld/companion answers \dump_state or \chk_vfo. If we
  // get any reply line, treat the link as up; otherwise close and let the backoff apply.
  uint32_t t0 = millis();
  while (linkRead() >= 0 && (millis() - t0) < 20) { }   // drain
  const char* q = "\\chk_vfo\n";
  _serial->write((const uint8_t*)q, strlen(q));
  String r = readLine(400);
  if (r.length() == 0) {
    // No companion on the Grove UART: don't pretend to be ready.
    _serial->end(); _open = false; _ok = false;
    return false;
  }
  if (r.indexOf("CHKVFO 1") >= 0) _vfoMode = true;
  _probed = true;                               // probe already done here
  _ok = true; _failStreak = 0;
  return true;
}
void RigctlGroveRig::linkClose() {
  if (_serial && _open) _serial->end();
  _open = false; _ok = false;
}
size_t RigctlGroveRig::linkWrite(const uint8_t* d, size_t n) {
  if (!_serial || !_open) return 0;
  return _serial->write(d, n);
}
int RigctlGroveRig::linkRead() {
  if (!_serial || !_open) return -1;
  return _serial->read();
}

// Downlink is VFOA, uplink is VFOB. (The operator picks which band each VFO holds
// in the rig's own sat/duplex setup; we only need two consistent VFOs to steer.)
const char* RigctlRig::vfoTok(bool sub) { return sub ? "VFOA" : "VFOB"; }

// Make sub's VFO the target of the next bare command. In --vfo servers the VFO
// travels inline on each command instead (see cmd()), so this is a no-op there.
void RigctlRig::selectVfo(bool sub) {
  if (_vfoMode) return;
  int want = sub ? 0 : 1;
  if (_vfo == want) return;
  if (xchg(String("V ") + vfoTok(sub) + "\n") == "RPRT 0") _vfo = want;
  else _vfo = -1;
}

// Assemble a command line, inserting the VFO token inline when the server runs in
// --vfo mode. body is the value(s) after the command letter, if any.
String RigctlRig::cmd(char c, bool sub, const String& body) {
  String s; s += c;
  if (_vfoMode)      { s += ' '; s += vfoTok(sub); }
  if (body.length()) { s += ' '; s += body; }
  s += '\n';
  return s;
}

bool RigctlRig::setSubFreq (freq_t hz) { selectVfo(true);  return xchg(cmd('F', true,  String((unsigned long long)hz))) == "RPRT 0"; }
bool RigctlRig::setMainFreq(freq_t hz) { selectVfo(false); return xchg(cmd('F', false, String((unsigned long long)hz))) == "RPRT 0"; }
bool RigctlRig::setSubMode (RigMode m) { selectVfo(true);  return xchg(cmd('M', true,  String(modeName(m)) + " 0")) == "RPRT 0"; }
bool RigctlRig::setMainMode(RigMode m) { selectVfo(false); return xchg(cmd('M', false, String(modeName(m)) + " 0")) == "RPRT 0"; }

bool RigctlRig::readSubFreq(freq_t& hzOut) {
  selectVfo(true);
  String r = xchg(cmd('f', true));
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (freq_t)strtoull(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readMainFreq(freq_t& hzOut) {
  selectVfo(false);
  String r = xchg(cmd('f', false));
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (freq_t)strtoull(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readPtt(bool& tx) {
  String r = xchg("t\n");
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  tx = (r.toInt() != 0);
  return true;
}
