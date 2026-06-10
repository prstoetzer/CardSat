#pragma once
// ===========================================================================
//  icomnet.h  -  Icom LAN (RS-BA1 UDP) CAT backend  (IcomNetRig : Rig)
// ===========================================================================
//
//  Native network control of an IC-9700 (and the wider IC-705/7610/785x family)
//  over the radio's built-in Ethernet/Wi-Fi port -- no PC or rigctld bridge.
//  CardSat speaks the same undocumented UDP protocol as Icom's RS-BA1, wfview
//  and kappanhang. Only the transport differs from the wired CivRig: the CI-V
//  frames carried are identical (FE FE <radio> E0 <payload> FD), so the One True
//  Rule Doppler loop, MAIN/SUB selection and calibration are unchanged.
//
//  CAT-only: we open the CONTROL stream (UDP 50001) and the SERIAL/CI-V stream
//  (UDP 50002). The AUDIO stream (50003) is never opened.
//
//  The connection runs as a non-blocking state machine pumped from service()
//  every loop tick (keepalives are timing-sensitive). The Rig CI-V methods send
//  fire-and-forget once CONNECTED; readback methods pump the serial socket
//  briefly for the reply. Protocol details and byte layouts: ICOM_LAN_PROTOCOL.md
//
//  Radio setup (IC-9700 MENU > SET > Network): Network Control = ON, a User1 id
//  + password, Control port 50001, and CI-V Transceive ON (so the radio pushes
//  changes). Username/password are entered in Settings (CAT type = Icom LAN).
// ===========================================================================
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include "rig.h"
#include "civ.h"           // CivMode + reuse of the CI-V command vocabulary

class IcomNetRig : public Rig {
public:
  IcomNetRig(RadioModel m, const char* host, uint16_t port,
             const char* user, const char* pass)
    : _model(m), _addr(RADIOS[m].civAddr), _host(host),
      _ctlPort(port ? port : 50001), _user(user), _pass(pass) {
    snprintf(_nameBuf, sizeof(_nameBuf), "%s/LAN", RADIOS[m].name);
  }
  ~IcomNetRig() override;

  void begin(uint32_t baud, int uartNum, int rxPin, int txPin) override;
  bool ready() const override { return _state == NS_CONNECTED; }

  // Pumped every loop tick: advances the connect/auth state machine, answers and
  // sends keepalives, drains both sockets, re-auths, and reconnects on loss.
  void service() override;

  bool setMainFreq(uint32_t hz) override;        // uplink (TX) on MAIN
  bool setSubFreq (uint32_t hz) override;        // downlink (RX) on SUB
  bool setMainMode(RigMode m)   override;
  bool setSubMode (RigMode m)   override;
  bool readSubFreq(uint32_t& hzOut) override;
  bool readMainFreq(uint32_t& hzOut) override;
  bool enableSatMode(bool on)   override;
  bool setCtcss(bool on, float toneHz) override;
  void selectSubBand()          override { if (ready()) selBand(true); }
  void selectMainBand()         override { if (ready()) selBand(false); }

  bool canReadFreq() const override { return RADIOS[_model].canReadFreq; }
  bool hasSatMode()  const override { return RADIOS[_model].hasSatMode; }
  bool hasTone()     const override { return RADIOS[_model].hasTone; }
  bool selVerified() const override { return RADIOS[_model].selVerified; }
  const char* name() const override { return _nameBuf; }

  void    setAddress(uint8_t a) override { _addr = a; }
  uint8_t address() const       override { return _addr; }

private:
  // ---- connection state machine -----------------------------------------
  enum NetState : uint8_t {
    NS_IDLE = 0,     // not started / waiting for WiFi or reconnect backoff
    NS_CTL_OPEN,     // control: sent are-you-there, expecting i-am-here
    NS_CTL_READY,    // control: sent are-you-ready, expecting ready
    NS_CTL_LOGIN,    // control: sent login, expecting login reply (token)
    NS_CTL_AUTH,     // control: sent auth #1/#2, expecting auth ok + capabilities
    NS_CTL_CONN,     // control: sent conninfo, expecting conninfo success
    NS_SER_OPEN,     // serial: sent are-you-there, expecting i-am-here
    NS_SER_READY,    // serial: sent are-you-ready, expecting ready
    NS_CONNECTED     // serial open + CI-V flowing
  };

  RadioModel _model;
  uint8_t    _addr;
  String     _host;
  uint16_t   _ctlPort;
  String     _user, _pass;
  char       _nameBuf[24];

  WiFiUDP    _ctl, _ser;
  NetState   _state = NS_IDLE;

  uint32_t _ctlLocalSID = 0, _ctlRemoteSID = 0;
  uint32_t _serLocalSID = 0, _serRemoteSID = 0;
  uint8_t  _authID[6];      bool _gotAuthID = false;
  uint8_t  _a8reply[16];    bool _gotA8 = false;
  bool     _authOK = false;
  bool     _serOpened = false;

  // Sequence counters (see ICOM_LAN_PROTOCOL.md): pkt0 "tracked" seq is shared by
  // idle + every big packet on a stream (starts 1); auth inner seq is embedded in
  // login/auth/conninfo (starts 0); ping seq + civ seq are independent.
  uint16_t _ctlTxSeq = 1, _serTxSeq = 1;
  uint16_t _authInner = 0;
  uint16_t _civSeq = 0;
  uint16_t _ctlPingSeq = 0, _serPingSeq = 0;
  uint32_t _pingPayload = 1;

  // Timers (millis()).
  uint32_t _tStateMs = 0;     // entered current state
  uint32_t _tLastRxMs = 0;    // last packet from radio (link-dead detection)
  uint32_t _tLastTryMs = 0;   // last (re)connect attempt (backoff)
  uint32_t _tPingCtlMs = 0, _tIdleCtlMs = 0;
  uint32_t _tPingSerMs = 0, _tIdleSerMs = 0;
  uint32_t _tReauthMs = 0;

  bool wifiUp() const;
  void resetSession();
  void startConnect();
  void teardown(bool graceful);
  void failReconnect(const char* why);

  // raw + framed senders
  static void put32be(uint8_t* b, uint32_t v);
  void sendRaw(WiFiUDP& u, const uint8_t* d, size_t n);
  void ctlTracked(uint8_t* d, size_t n);         // stamps [6:8]=_ctlTxSeq++
  void serTracked(uint8_t* d, size_t n);         // stamps [6:8]=_serTxSeq++
  void sendAreYouThere(bool ctl);
  void sendAreYouReady(bool ctl);
  void sendDisconnect(bool ctl);
  void sendPing(bool ctl);
  void replyPing(bool ctl, const uint8_t* r);
  void sendIdle(bool ctl, uint16_t seq);
  void sendLogin();
  void sendAuth(uint8_t magic);
  void sendConnInfo();
  void sendSerOpenClose(bool close);

  // receive pumps + dispatch
  void pumpCtl();
  void pumpSer();
  void handleCtl(const uint8_t* r, int n);
  void handleSer(const uint8_t* r, int n);
  bool isPing(const uint8_t* r, int n) const;
  bool isRetransReq(const uint8_t* r, int n) const;

  // CI-V helpers (mirror CivRig, but wrapped in the serial UDP packet)
  static CivMode toCiv(RigMode m);
  static void    freqToBcd(uint32_t hz, uint8_t out[5]);
  void selBand(bool sub);
  bool sendCivPayload(const uint8_t* pl, size_t pllen);
  bool setFreqNet(bool sub, uint32_t hz);
  bool setModeNet(bool sub, CivMode m, uint8_t filter = 0x01);
  bool readFreqNet(bool sub, uint32_t& hzOut);

  // Obfuscated credential (Icom passcode substitution) -> 16 bytes.
  static void passcode(const char* s, uint8_t out[16]);
};
