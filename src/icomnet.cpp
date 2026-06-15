// ===========================================================================
//  icomnet.cpp  -  Icom LAN (RS-BA1 UDP) CAT backend
// ===========================================================================
//  Byte layouts and the connect/auth/keepalive sequence follow ICOM_LAN_PROTOCOL.md
//  (reverse-engineered from nonoo/kappanhang, cross-checked vs wfview and
//  microenh/NetworkIcom). All multi-byte session IDs are big-endian; the small
//  header seq at [6:8] is little-endian; the CI-V inner seq at [19:21] is
//  big-endian. Handshake/control packets are sent twice for loss resilience.
//
//  Limitations (first hardware-targeted cut): no transmit retransmit buffer --
//  a radio retransmit request is answered with an idle carrying that seq (the
//  kappanhang "not buffered" fallback); a dropped CAT frame is simply re-sent on
//  the next Doppler cycle. The audio stream is never opened (see ICOM_LAN_PROTOCOL.md
//  section 7 -- the one item to confirm against real hardware).
// ===========================================================================
#include "icomnet.h"

#define ICOMNET_DEBUG 1
#if ICOMNET_DEBUG
  #define NLOG(...)  do { Serial.printf(__VA_ARGS__); } while (0)
#else
  #define NLOG(...)  do {} while (0)
#endif

// Big-endian 32-bit read/write.
void IcomNetRig::put32be(uint8_t* b, uint32_t v) {
  b[0] = (uint8_t)(v >> 24); b[1] = (uint8_t)(v >> 16);
  b[2] = (uint8_t)(v >> 8);  b[3] = (uint8_t)(v);
}
static inline uint32_t rd32be(const uint8_t* b) {
  return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
         ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}

// Icom passcode substitution (ASCII 32..126 -> byte). See ICOM_LAN_PROTOCOL.md s6.
void IcomNetRig::passcode(const char* s, uint8_t out[16]) {
  static const uint8_t seq[95] = {
    0x47,0x5d,0x4c,0x42,0x66,0x20,0x23,0x46,0x4e,0x57, // 32-41
    0x45,0x3d,0x67,0x76,0x60,0x41,0x62,0x39,0x59,0x2d, // 42-51
    0x68,0x7e,0x7c,0x65,0x7d,0x49,0x29,0x72,0x73,0x78, // 52-61
    0x21,0x6e,0x5a,0x5e,0x4a,0x3e,0x71,0x2c,0x2a,0x54, // 62-71
    0x3c,0x3a,0x63,0x4f,0x43,0x75,0x27,0x79,0x5b,0x35, // 72-81
    0x70,0x48,0x6b,0x56,0x6f,0x34,0x32,0x6c,0x30,0x61, // 82-91
    0x6d,0x7b,0x2f,0x4b,0x64,0x38,0x2b,0x2e,0x50,0x40, // 92-101
    0x3f,0x55,0x33,0x37,0x25,0x77,0x24,0x26,0x74,0x6a, // 102-111
    0x28,0x53,0x4d,0x69,0x22,0x5c,0x44,0x31,0x36,0x58, // 112-121
    0x3b,0x7a,0x51,0x5f,0x52                            // 122-126
  };
  for (int i = 0; i < 16; ++i) out[i] = 0;
  int len = (int)strlen(s);
  for (int i = 0; i < len && i < 16; ++i) {
    int p = (int)(uint8_t)s[i] + i;
    if (p > 126) p = 32 + p % 127;
    if (p < 32) p = 32;                 // defensive (control chars not expected)
    out[i] = seq[p - 32];
  }
}

CivMode IcomNetRig::toCiv(RigMode m) {
  switch (m) {
    case RM_LSB: return CIV_LSB; case RM_USB: return CIV_USB;
    case RM_CW:  return CIV_CW;  case RM_FM:  return CIV_FM;
    case RM_AM:  return CIV_AM;  case RM_DATA:return CIV_RTTY;
    default:     return CIV_USB;
  }
}
void IcomNetRig::freqToBcd(uint32_t hz, uint8_t out[5]) {
  for (int i = 0; i < 5; ++i) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}

bool IcomNetRig::wifiUp() const { return WiFi.status() == WL_CONNECTED; }

void IcomNetRig::begin(uint32_t, int, int, int) {
  // No UART. Arm the state machine; service() drives the connection once WiFi is
  // up. _serPort is _ctlPort+1, audio (never opened) would be _ctlPort+2.
  resetSession();
  _tLastTryMs = millis() - 5000;   // allow an immediate first attempt
}

IcomNetRig::~IcomNetRig() { teardown(true); }

void IcomNetRig::resetSession() {
  _ctl.stop(); _ser.stop();
  _state = NS_IDLE;
  _ctlLocalSID = _ctlRemoteSID = _serLocalSID = _serRemoteSID = 0;
  _gotAuthID = _gotA8 = _authOK = _serOpened = false;
  _ctlTxSeq = _serTxSeq = 1;
  _authInner = 0; _civSeq = 0; _ctlPingSeq = _serPingSeq = 0;
  memset(_authID, 0, sizeof(_authID));
  memset(_a8reply, 0, sizeof(_a8reply));
}

void IcomNetRig::startConnect() {
  resetSession();
  _tLastTryMs = millis();
  if (!wifiUp() || _host.length() == 0) { _state = NS_IDLE; return; }
  if (!_ctl.begin(_ctlPort)) { _state = NS_IDLE; return; }
  _ctlLocalSID = (((uint32_t)WiFi.localIP() & 0xFFFF) << 16) | _ctlPort;
  uint32_t ms = millis();
  _tStateMs = _tLastRxMs = _tPingCtlMs = _tIdleCtlMs = ms;
  _state = NS_CTL_OPEN;
  NLOG("[NET] connecting to %s:%u (CI-V %02X)\n", _host.c_str(), _ctlPort, _addr);
  sendAreYouThere(true);
}

void IcomNetRig::failReconnect(const char* why) {
  NLOG("[NET] reconnect: %s\n", why);
  teardown(false);
  _state = NS_IDLE;
  _tLastTryMs = millis();        // backoff before the next attempt
}

void IcomNetRig::teardown(bool graceful) {
  if (graceful && _state == NS_CONNECTED) {
    if (_serOpened) sendSerOpenClose(true);
    sendAuth(0x01);              // de-auth on the control stream
    sendDisconnect(false);
    sendDisconnect(true);
    delay(20);
  }
  _ctl.stop(); _ser.stop();
  _serOpened = false;
}

// --- raw / framed senders --------------------------------------------------
void IcomNetRig::sendRaw(WiFiUDP& u, const uint8_t* d, size_t n) {
  // The remote port is encoded by which socket: _ctl -> _ctlPort, _ser -> +1.
  uint16_t port = (&u == &_ser) ? (uint16_t)(_ctlPort + 1) : _ctlPort;
  if (!wifiUp()) return;
  u.beginPacket(_host.c_str(), port);
  u.write(d, n);
  u.endPacket();
}
void IcomNetRig::ctlTracked(uint8_t* d, size_t n) {
  d[6] = (uint8_t)_ctlTxSeq; d[7] = (uint8_t)(_ctlTxSeq >> 8); _ctlTxSeq++;
  sendRaw(_ctl, d, n);
}
void IcomNetRig::serTracked(uint8_t* d, size_t n) {
  d[6] = (uint8_t)_serTxSeq; d[7] = (uint8_t)(_serTxSeq >> 8); _serTxSeq++;
  sendRaw(_ser, d, n);
}

void IcomNetRig::sendAreYouThere(bool ctl) {
  uint8_t p[16]; memset(p, 0, sizeof(p));
  p[0] = 0x10; p[4] = 0x03;
  put32be(p + 8, ctl ? _ctlLocalSID : _serLocalSID);   // remoteSID still unknown (0)
  WiFiUDP& u = ctl ? _ctl : _ser;
  sendRaw(u, p, 16); sendRaw(u, p, 16);
}
void IcomNetRig::sendAreYouReady(bool ctl) {
  uint8_t p[16]; memset(p, 0, sizeof(p));
  p[0] = 0x10; p[4] = 0x06; p[6] = 0x01;
  put32be(p + 8,  ctl ? _ctlLocalSID  : _serLocalSID);
  put32be(p + 12, ctl ? _ctlRemoteSID : _serRemoteSID);
  WiFiUDP& u = ctl ? _ctl : _ser;
  sendRaw(u, p, 16); sendRaw(u, p, 16);
}
void IcomNetRig::sendDisconnect(bool ctl) {
  uint8_t p[16]; memset(p, 0, sizeof(p));
  p[0] = 0x10; p[4] = 0x05;
  put32be(p + 8,  ctl ? _ctlLocalSID  : _serLocalSID);
  put32be(p + 12, ctl ? _ctlRemoteSID : _serRemoteSID);
  WiFiUDP& u = ctl ? _ctl : _ser;
  sendRaw(u, p, 16); sendRaw(u, p, 16);
}
void IcomNetRig::sendIdle(bool ctl, uint16_t seq) {
  uint8_t p[16]; memset(p, 0, sizeof(p));
  p[0] = 0x10; p[6] = (uint8_t)seq; p[7] = (uint8_t)(seq >> 8);
  put32be(p + 8,  ctl ? _ctlLocalSID  : _serLocalSID);
  put32be(p + 12, ctl ? _ctlRemoteSID : _serRemoteSID);
  sendRaw(ctl ? _ctl : _ser, p, 16);
}

void IcomNetRig::sendPing(bool ctl) {
  uint8_t p[21]; memset(p, 0, sizeof(p));
  p[0] = 0x15; p[4] = 0x07;
  uint16_t seq = ctl ? _ctlPingSeq++ : _serPingSeq++;
  p[6] = (uint8_t)seq; p[7] = (uint8_t)(seq >> 8);
  put32be(p + 8,  ctl ? _ctlLocalSID  : _serLocalSID);
  put32be(p + 12, ctl ? _ctlRemoteSID : _serRemoteSID);
  p[16] = 0x00;                                  // request
  uint32_t pc = _pingPayload++;
  p[17] = (uint8_t)pc; p[18] = (uint8_t)(pc >> 8);
  p[19] = (uint8_t)(pc >> 16); p[20] = (uint8_t)(pc >> 24);
  sendRaw(ctl ? _ctl : _ser, p, 21);
}
void IcomNetRig::replyPing(bool ctl, const uint8_t* r) {
  uint8_t p[21]; memset(p, 0, sizeof(p));
  p[0] = 0x15; p[4] = 0x07;
  p[6] = r[6]; p[7] = r[7];                      // echo the radio's seq
  put32be(p + 8,  ctl ? _ctlLocalSID  : _serLocalSID);
  put32be(p + 12, ctl ? _ctlRemoteSID : _serRemoteSID);
  p[16] = 0x01;                                  // reply
  p[17] = r[17]; p[18] = r[18]; p[19] = r[19]; p[20] = r[20];
  sendRaw(ctl ? _ctl : _ser, p, 21);
}

void IcomNetRig::sendLogin() {
  uint8_t p[128]; memset(p, 0, sizeof(p));
  p[0] = 0x80;
  put32be(p + 8, _ctlLocalSID); put32be(p + 12, _ctlRemoteSID);
  p[19] = 0x70; p[20] = 0x01;
  p[23] = (uint8_t)_authInner; p[24] = (uint8_t)(_authInner >> 8);
  p[26] = (uint8_t)esp_random(); p[27] = (uint8_t)esp_random();
  uint8_t u[16], w[16];
  passcode(_user.c_str(), u); passcode(_pass.c_str(), w);
  memcpy(p + 64, u, 16); memcpy(p + 80, w, 16);
  const char* nm = "CardSat";
  memcpy(p + 96, nm, strlen(nm));                // null-padded by memset
  ctlTracked(p, 128); _authInner++;
  NLOG("[NET] login sent\n");
}
void IcomNetRig::sendAuth(uint8_t magic) {
  uint8_t p[64]; memset(p, 0, sizeof(p));
  p[0] = 0x40;
  put32be(p + 8, _ctlLocalSID); put32be(p + 12, _ctlRemoteSID);
  p[19] = 0x30; p[20] = 0x01; p[21] = magic;
  p[23] = (uint8_t)_authInner; p[24] = (uint8_t)(_authInner >> 8);
  memcpy(p + 26, _authID, 6);
  ctlTracked(p, 64); _authInner++;
}
void IcomNetRig::sendConnInfo() {
  uint8_t p[144]; memset(p, 0, sizeof(p));
  p[0] = 0x90;
  put32be(p + 8, _ctlLocalSID); put32be(p + 12, _ctlRemoteSID);
  p[19] = 0x80; p[20] = 0x01; p[21] = 0x03;
  p[23] = (uint8_t)_authInner; p[24] = (uint8_t)(_authInner >> 8);
  memcpy(p + 26, _authID, 6);
  memcpy(p + 32, _a8reply, 16);
  strncpy((char*)(p + 64), RADIOS[_model].name, 15);   // radio model name, plaintext
  uint8_t u[16]; passcode(_user.c_str(), u); memcpy(p + 96, u, 16);
  uint16_t sr = 16000, serP = (uint16_t)(_ctlPort + 1), audP = (uint16_t)(_ctlPort + 2), txb = 100;
  p[112] = 0x01; p[113] = 0x01; p[114] = 0x04; p[115] = 0x04;
  p[118] = (uint8_t)(sr >> 8);  p[119] = (uint8_t)sr;
  p[122] = (uint8_t)(sr >> 8);  p[123] = (uint8_t)sr;
  p[126] = (uint8_t)(serP >> 8); p[127] = (uint8_t)serP;
  p[130] = (uint8_t)(audP >> 8); p[131] = (uint8_t)audP;
  p[134] = (uint8_t)(txb >> 8);  p[135] = (uint8_t)txb;  p[136] = 0x01;
  ctlTracked(p, 144); _authInner++;
  NLOG("[NET] conninfo sent\n");
}
void IcomNetRig::sendSerOpenClose(bool close) {
  uint8_t p[22]; memset(p, 0, sizeof(p));
  p[0] = 0x16;
  put32be(p + 8, _serLocalSID); put32be(p + 12, _serRemoteSID);
  p[16] = 0xc0; p[17] = 0x01; p[18] = 0x00;
  p[19] = (uint8_t)(_civSeq >> 8); p[20] = (uint8_t)_civSeq;   // big-endian
  p[21] = close ? 0x00 : 0x05;
  serTracked(p, 22); _civSeq++;
}

// --- receive ---------------------------------------------------------------
bool IcomNetRig::isPing(const uint8_t* r, int n) const {
  return n == 21 && r[1] == 0 && r[2] == 0 && r[3] == 0 && r[4] == 0x07 && r[5] == 0;
}
bool IcomNetRig::isRetransReq(const uint8_t* r, int n) const {
  return n >= 16 && ((r[0] == 0x10 && r[4] == 0x01) || (r[0] == 0x18 && r[4] == 0x01));
}

void IcomNetRig::pumpCtl() {
  int n;
  while ((n = _ctl.parsePacket()) > 0) {
    uint8_t buf[320]; if (n > (int)sizeof(buf)) n = sizeof(buf);
    int r = _ctl.read(buf, n); if (r <= 0) break;
    _tLastRxMs = millis();
    handleCtl(buf, r);
  }
}
void IcomNetRig::pumpSer() {
  int n;
  while ((n = _ser.parsePacket()) > 0) {
    uint8_t buf[320]; if (n > (int)sizeof(buf)) n = sizeof(buf);
    int r = _ser.read(buf, n); if (r <= 0) break;
    _tLastRxMs = millis();
    handleSer(buf, r);
  }
}

void IcomNetRig::handleCtl(const uint8_t* r, int n) {
  if (isPing(r, n)) { if (r[16] == 0x00) replyPing(true, r); return; }
  if (isRetransReq(r, n)) { sendIdle(true, (uint16_t)(r[6] | (r[7] << 8))); return; }

  if (n == 16) {
    if (r[4] == 0x04) {                          // i-am-here
      _ctlRemoteSID = rd32be(r + 8);
      if (_state == NS_CTL_OPEN) { sendAreYouReady(true); _state = NS_CTL_READY; _tStateMs = millis(); }
    } else if (r[4] == 0x06) {                   // ready
      if (_state == NS_CTL_READY) { sendLogin(); _state = NS_CTL_LOGIN; _tStateMs = millis(); }
    }
    return;
  }
  if (n == 96 && r[0] == 0x60) {                 // login reply
    if (_state != NS_CTL_LOGIN) return;
    if (r[48] == 0xff && r[49] == 0xff && r[50] == 0xff && r[51] == 0xfe) {
      failReconnect("invalid username/password"); return;
    }
    memcpy(_authID, r + 26, 6); _gotAuthID = true;
    _tPingCtlMs = _tIdleCtlMs = millis();
    sendAuth(0x02); sendAuth(0x05);
    _state = NS_CTL_AUTH; _tStateMs = millis();
    NLOG("[NET] login ok, auth sent\n");
    return;
  }
  if (n == 168 && r[0] == 0xa8) {                // capabilities
    memcpy(_a8reply, r + 66, 16); _gotA8 = true;
    if (_state == NS_CTL_AUTH && _authOK && _gotA8) { sendConnInfo(); _state = NS_CTL_CONN; _tStateMs = millis(); }
    return;
  }
  if (n == 64 && r[0] == 0x40) {                 // auth reply
    if (r[21] == 0x05) _authOK = true;
    if (_state == NS_CTL_AUTH && _authOK && _gotA8) { sendConnInfo(); _state = NS_CTL_CONN; _tStateMs = millis(); }
    return;
  }
  if (n == 80 && r[0] == 0x50) {                 // status
    if (r[48] == 0xff && r[49] == 0xff && r[50] == 0xff) { failReconnect("auth rejected"); return; }
    if (r[48] == 0x00 && r[49] == 0x00 && r[50] == 0x00 && r[64] == 0x01) { failReconnect("radio disconnected"); return; }
    return;
  }
  if (n == 144 && r[0] == 0x90) {                // conninfo success
    if (_state != NS_CTL_CONN || r[96] != 1) return;
    _ctlRemoteSID = rd32be(r + 8); _ctlLocalSID = rd32be(r + 12);
    memcpy(_authID, r + 26, 6);
    _ser.begin((uint16_t)(_ctlPort + 1));
    _serLocalSID = (((uint32_t)WiFi.localIP() & 0xFFFF) << 16) | (uint16_t)(_ctlPort + 1);
    _serTxSeq = 1; _civSeq = 0; _serPingSeq = 0;
    _tPingSerMs = _tIdleSerMs = millis();
    sendAreYouThere(false);
    _state = NS_SER_OPEN; _tStateMs = millis();
    NLOG("[NET] control up; opening serial stream\n");
    return;
  }
}

void IcomNetRig::handleSer(const uint8_t* r, int n) {
  if (isPing(r, n)) { if (r[16] == 0x00) replyPing(false, r); return; }
  if (isRetransReq(r, n)) { sendIdle(false, (uint16_t)(r[6] | (r[7] << 8))); return; }
  if (n == 16) {
    if (r[4] == 0x04) {                          // i-am-here
      _serRemoteSID = rd32be(r + 8);
      if (_state == NS_SER_OPEN) { sendAreYouReady(false); _state = NS_SER_READY; _tStateMs = millis(); }
    } else if (r[4] == 0x06) {                   // ready
      if (_state == NS_SER_READY) {
        sendSerOpenClose(false); _serOpened = true;
        _tReauthMs = millis(); _state = NS_CONNECTED; _tStateMs = millis();
        NLOG("[NET] CONNECTED (serial open)\n");
      }
    }
    return;
  }
  // CI-V transceive/echo data (r[16]==0xc1) is consumed by readFreqNet on demand;
  // unsolicited frequency-change broadcasts are ignored here.
}

void IcomNetRig::service() {
  if (!wifiUp()) { if (_state != NS_IDLE) resetSession(); return; }
  if (_state == NS_IDLE) {
    if (millis() - _tLastTryMs >= 4000) startConnect();
    return;
  }
  pumpCtl(); pumpSer();
  uint32_t ms = millis();
  if (ms - _tLastRxMs > 8000) { failReconnect("link timeout"); return; }
  if (_state != NS_CONNECTED && ms - _tStateMs > 4000) { failReconnect("handshake stalled"); return; }

  if (_state >= NS_CTL_READY) {
    if (ms - _tPingCtlMs >= 500) { sendPing(true); _tPingCtlMs = ms; }
    if (ms - _tIdleCtlMs >= 500) {
      uint8_t p[16]; memset(p, 0, sizeof(p)); p[0] = 0x10;
      put32be(p + 8, _ctlLocalSID); put32be(p + 12, _ctlRemoteSID);
      ctlTracked(p, 16); _tIdleCtlMs = ms;
    }
  }
  if (_state >= NS_SER_READY) {
    if (ms - _tPingSerMs >= 500) { sendPing(false); _tPingSerMs = ms; }
    if (ms - _tIdleSerMs >= 500) {
      uint8_t p[16]; memset(p, 0, sizeof(p)); p[0] = 0x10;
      put32be(p + 8, _serLocalSID); put32be(p + 12, _serRemoteSID);
      serTracked(p, 16); _tIdleSerMs = ms;
    }
  }
  if (_state == NS_CONNECTED && ms - _tReauthMs >= 60000) { sendAuth(0x05); _tReauthMs = ms; }
}

// --- CI-V over the serial stream -------------------------------------------
bool IcomNetRig::sendCivPayload(const uint8_t* pl, size_t pllen) {
  if (_state != NS_CONNECTED) return false;
  uint8_t fr[24]; size_t fn = 0;
  fr[fn++] = 0xFE; fr[fn++] = 0xFE; fr[fn++] = _addr; fr[fn++] = 0xE0;
  for (size_t i = 0; i < pllen && fn < sizeof(fr) - 1; ++i) fr[fn++] = pl[i];
  fr[fn++] = 0xFD;
  uint8_t N = (uint8_t)fn;
  uint8_t p[21 + 24]; memset(p, 0, 21 + N);
  p[0] = (uint8_t)(0x15 + N);
  put32be(p + 8, _serLocalSID); put32be(p + 12, _serRemoteSID);
  p[16] = 0xc1; p[17] = N; p[18] = 0x00;
  p[19] = (uint8_t)(_civSeq >> 8); p[20] = (uint8_t)_civSeq;   // big-endian
  memcpy(p + 21, fr, N);
  serTracked(p, 21 + N); _civSeq++;
  NLOG("[NET CI-V TX] cmd %02X (%u B)\n", pl[0], (unsigned)pllen);
  pumpCtl(); pumpSer();                          // keep keepalives alive, drain ACK/echo
  return true;
}
void IcomNetRig::selBand(bool sub) {
  const RadioProfile& p = RADIOS[_model];
  if (p.selLen) sendCivPayload(sub ? p.selSub : p.selMain, p.selLen);
}
bool IcomNetRig::setFreqNet(bool sub, uint32_t hz) {
  selBand(sub);
  uint8_t pl[6]; pl[0] = 0x05; freqToBcd(hz, &pl[1]);
  return sendCivPayload(pl, 6);
}
bool IcomNetRig::setModeNet(bool sub, CivMode m, uint8_t filter) {
  selBand(sub);
  uint8_t pl[3] = { 0x06, (uint8_t)m, filter };
  return sendCivPayload(pl, 3);
}
bool IcomNetRig::readFreqNet(bool sub, uint32_t& hzOut) {
  if (_state != NS_CONNECTED || !RADIOS[_model].canReadFreq) return false;
  selBand(sub);
  while (_ser.parsePacket() > 0) { uint8_t d[64]; _ser.read(d, sizeof(d)); }  // drain stale
  uint8_t pl[1] = { 0x03 };
  sendCivPayload(pl, 1);
  uint32_t t0 = millis();
  while (millis() - t0 < (readBudgetMs ? readBudgetMs : 300)) {
    int n = _ser.parsePacket();
    if (n <= 0) { pumpCtl(); delay(2); continue; }
    uint8_t buf[96]; int r = _ser.read(buf, sizeof(buf)); if (r <= 0) continue;
    _tLastRxMs = millis();
    if (isPing(buf, r)) { if (buf[16] == 0x00) replyPing(false, buf); continue; }
    if (r >= 22 && buf[16] == 0xc1) {
      int N = buf[17];
      if (21 + N <= r && N >= 11) {
        const uint8_t* f = buf + 21;
        if (f[0] == 0xFE && f[1] == 0xFE && f[2] == 0xE0 && f[3] == _addr &&
            f[4] == 0x03 && f[10] == 0xFD) {
          uint32_t hz = 0;
          for (int k = 9; k >= 5; --k) hz = hz * 100 + (f[k] >> 4) * 10 + (f[k] & 0x0F);
          hzOut = hz;
          NLOG("[NET CI-V] %s freq %lu Hz\n", sub ? "SUB" : "MAIN", (unsigned long)hz);
          return true;
        }
      }
    }
  }
  return false;
}

// --- Rig interface ---------------------------------------------------------
bool IcomNetRig::setMainFreq(uint32_t hz) { return setFreqNet(false, hz); }
bool IcomNetRig::setSubFreq (uint32_t hz) { return setFreqNet(true,  hz); }
bool IcomNetRig::setMainMode(RigMode m)   { return setModeNet(false, toCiv(m)); }
bool IcomNetRig::setSubMode (RigMode m)   { return setModeNet(true,  toCiv(m)); }
bool IcomNetRig::readSubFreq (uint32_t& hzOut) { return readFreqNet(true,  hzOut); }
bool IcomNetRig::readMainFreq(uint32_t& hzOut) { return readFreqNet(false, hzOut); }

// Read PTT/transmit state via CI-V 0x1C 0x00 over the serial stream. Reply CI-V
// frame: FE FE E0 <addr> 1C 00 <00=RX|01=TX> FD. Rigs that don't answer get
// marked unsupported after a few misses so the loop stops asking.
bool IcomNetRig::readPtt(bool& tx) {
  if (_state != NS_CONNECTED || _pttRead == 0) return false;
  while (_ser.parsePacket() > 0) { uint8_t d[64]; _ser.read(d, sizeof(d)); }  // drain stale
  uint8_t pl[2] = { 0x1C, 0x00 };
  sendCivPayload(pl, 2);
  uint32_t t0 = millis();
  while (millis() - t0 < (readBudgetMs ? readBudgetMs : 200)) {
    int n = _ser.parsePacket();
    if (n <= 0) { pumpCtl(); delay(2); continue; }
    uint8_t buf[96]; int r = _ser.read(buf, sizeof(buf)); if (r <= 0) continue;
    _tLastRxMs = millis();
    if (isPing(buf, r)) { if (buf[16] == 0x00) replyPing(false, buf); continue; }
    if (r >= 22 && buf[16] == 0xc1) {
      int N = buf[17];
      if (21 + N <= r && N >= 8) {
        const uint8_t* f = buf + 21;
        if (f[0]==0xFE && f[1]==0xFE && f[2]==0xE0 && f[3]==_addr &&
            f[4]==0x1C && f[5]==0x00 && f[7]==0xFD) {
          tx = (f[6] != 0x00);
          _pttRead = 1; _pttFails = 0;
          NLOG("[NET CI-V] PTT %s\n", tx ? "TX" : "RX");
          return true;
        }
      }
    }
  }
  if (_pttRead != 1 && ++_pttFails >= 3) _pttRead = 0;
  return false;
}

bool IcomNetRig::enableSatMode(bool on) {
  if (!RADIOS[_model].hasSatMode) return false;
  // Satmode command differs by rig: IC-9100/9700 use 0x16/0x5A, but the IC-910
  // uses 0x1A/0x07 (per its CONTROL COMMAND table). Both bytes come from the profile.
  uint8_t pl[3] = { RADIOS[_model].satModeCmd,
                    RADIOS[_model].satModeSub,
                    (uint8_t)(on ? 0x01 : 0x00) };
  return sendCivPayload(pl, 3);
}
bool IcomNetRig::setCtcss(bool on, float toneHz) {
  if (!RADIOS[_model].hasTone) return false;
  if (on && toneHz > 0) {
    int t = (int)lroundf(toneHz * 10.0f);
    uint8_t b1 = (uint8_t)((((t / 1000) % 10) << 4) | ((t / 100) % 10));
    uint8_t b2 = (uint8_t)((((t / 10)   % 10) << 4) | (t % 10));
    uint8_t freq[4] = { 0x1B, 0x00, b1, b2 }; sendCivPayload(freq, 4);
    uint8_t enc[3]  = { 0x16, RADIOS[_model].toneEncSub, 0x01 }; return sendCivPayload(enc, 3);
  }
  uint8_t off[3] = { 0x16, RADIOS[_model].toneEncSub, 0x00 };
  return sendCivPayload(off, 3);
}
