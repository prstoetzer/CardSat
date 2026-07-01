#pragma once
// ===========================================================================
//  lorarx.h  -  General-purpose LoRa RX / hex monitor (SX1262)
// ===========================================================================
//  A standalone tool to receive and inspect ANY LoRa signal (not just
//  satellites): set the full SX1262 receive parameter set, then watch incoming
//  frames as a scrolling hex + ASCII dump with RSSI/SNR, with live tuning.
//
//  Two screens:
//    - CONFIG: frequency + every SX1262 LoRa RX parameter (SF, bandwidth,
//      coding rate, sync word, preamble, CRC). ENTER starts receiving.
//    - MONITOR: classic hexdump (16 bytes/row, hex line + ASCII line beneath),
//      newest frame at the bottom, scrolls as frames arrive. Frequency and
//      SF/BW/CR are adjustable live here to peak a signal.
//
//  DESIGN CONTRACT:
//    - No satellite infrastructure. No GP/TLE, no Doppler, no SatNOGS, no
//      network. Just the radio and the bytes.
//    - Self-contained: the app touches this only through the LoraRxMon facade.
//    - Minimal resident RAM: the frame ring is a fixed .bss buffer inside the
//      facade object; there is no per-frame heap allocation. Entering takes over
//      the shared SX1262 (CardSat messaging RX pauses); exit() restores it.
//    - The RX parameters persist in Settings (lrx* fields) across reboots.
//    - Reversible: the whole feature is behind `#if CARDSAT_HAS_LORARX`.
//      See docs/design/LORARX_IMPLEMENTATION.md.
// ===========================================================================
#include <Arduino.h>

class M5Canvas;   // M5GFX sprite, passed to draw()

#ifndef CARDSAT_HAS_LORARX
#define CARDSAT_HAS_LORARX 1
#endif

#if CARDSAT_HAS_LORARX

class App;

class LoraRxMon {
public:
  // Enter the mode: seed parameters from Settings and show the CONFIG screen.
  // Does NOT touch the radio yet (listening starts from the config screen).
  bool enter(App* app);

  // Leave the mode: stop receiving and restore CardSat LoRa messaging. Safe to
  // call when inactive (no-op).
  void exit();

  bool active() const { return _active; }

  // Commit a free-text frequency (in MHz) typed on the shared SCR_EDIT screen
  // (edit code 240). Called by App::keyEdit. Clamps to the SX1262 range and
  // re-applies to the radio if currently listening. No-op if inactive.
  void setFreqMHz(const String& mhz);

  // Called every main-loop iteration while active: drain any received frame into
  // the ring. Cheap; no-op if inactive or still on the config screen.
  void service();

  // Render whichever sub-screen is active (config or monitor).
  void draw(M5Canvas& canvas, App* app);

  // Handle a key. `back` steps monitor->config, then config->exit.
  void key(char c, bool enter, bool back);

private:
  enum Page : uint8_t { PAGE_CONFIG = 0, PAGE_MONITOR = 1 };

  // A captured frame (fixed size; no heap). Only the first CAP bytes are kept.
  static const int FRAME_CAP   = 64;   // max bytes stored per frame
  static const int RING_FRAMES = 12;   // frames retained for scrollback

  struct Frame {
    uint8_t  data[FRAME_CAP];
    uint8_t  len;         // bytes actually stored (<= FRAME_CAP)
    uint16_t rawLen;      // true received length (may exceed FRAME_CAP)
    int16_t  rssi;
    int8_t   snr;
  };

  App*     _app     = nullptr;
  bool     _active  = false;
  Page     _page    = PAGE_CONFIG;
  bool     _listening = false;

  // working copy of the RX parameters (persisted to Settings on change)
  uint32_t _freqKHz = 433775;
  uint8_t  _sf      = 12;
  uint32_t _bwHz    = 125000;
  uint8_t  _cr      = 5;
  uint8_t  _sync    = 0x12;
  uint16_t _preamble= 8;
  bool     _crc     = true;

  // config-screen state
  int      _cfgSel  = 0;      // selected parameter row
  uint32_t _freqStepHz = 1000;// current frequency step (steppable: 1k/10k/100k/1M)

  // frame ring
  Frame    _ring[RING_FRAMES];
  int      _ringHead = 0;     // next write slot
  int      _ringUsed = 0;
  uint32_t _pktCount = 0;
  int      _scroll   = 0;     // monitor scrollback (0 = newest at bottom)
  bool     _paused   = false; // freeze the display (radio keeps filling the ring)
  uint32_t _pausedAtCount = 0;// packet count when paused (for the "N new" indicator)

  // helpers (members so they can touch App privates via friendship)
  void applyRadio();          // push current params to the SX1262 (RX)
  void persist();             // save current params to Settings
  void drawConfig(M5Canvas& canvas);
  void drawMonitor(M5Canvas& canvas);
  void adjust(int dir);       // change the selected config parameter by dir
};

#endif // CARDSAT_HAS_LORARX
