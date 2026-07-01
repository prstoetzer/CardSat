// ===========================================================================
//  lorarx.cpp  -  General-purpose LoRa RX / hex monitor (SX1262)
// ===========================================================================
//  See lorarx.h for the design contract. No satellites, no network: set the
//  SX1262 receive parameters, then watch frames as a scrolling hex+ASCII dump.
// ===========================================================================
#include "lorarx.h"

#if CARDSAT_HAS_LORARX

#include "app.h"
#include <M5Cardputer.h>

// Palette indices match app.cpp CL_*: BLACK=0 WHITE=1 GREEN=2 RED=3 YELLOW=4
// CYAN=5 ORANGE=6 GREY=7.
enum { C_BLK=0, C_WHT=1, C_GRN=2, C_RED=3, C_YEL=4, C_CYN=5, C_ORG=6, C_GRY=7 };

// SX1262 LoRa bandwidth ladder (Hz) for stepping on the config/monitor screens.
static const uint32_t BW_TABLE[] = {
  7800, 10400, 15600, 20800, 31250, 41700, 62500, 125000, 250000, 500000
};
static const int BW_COUNT = sizeof(BW_TABLE) / sizeof(BW_TABLE[0]);
static int bwIndex(uint32_t hz) {
  for (int i = 0; i < BW_COUNT; i++) if (BW_TABLE[i] == hz) return i;
  return 7; // default 125000
}

// Config parameter rows.
enum { ROW_FREQ=0, ROW_SF, ROW_BW, ROW_CR, ROW_SYNC, ROW_PREAMBLE, ROW_CRC, ROW_COUNT };

// --- radio + persistence ----------------------------------------------------

void LoraRxMon::applyRadio() {
  if (!_app->cfg.loraEnable) { _listening = false; return; }
  _listening = _app->lora.setRadioRx(_freqKHz, _sf, _bwHz, _cr, _sync, _preamble, _crc);
}

void LoraRxMon::persist() {
  Settings& c = _app->cfg;
  c.lrxFreqKHz = _freqKHz; c.lrxSf = _sf; c.lrxBwHz = _bwHz; c.lrxCr = _cr;
  c.lrxSync = _sync; c.lrxPreamble = _preamble; c.lrxCrc = _crc ? 1 : 0;
  c.save();
}

void LoraRxMon::setFreqMHz(const String& mhz) {
  if (!_active) return;
  double f = mhz.toFloat();                 // MHz
  if (f <= 0) return;                       // ignore blank / unparseable
  long khz = (long)llround(f * 1000.0);
  // SX1262 usable LoRa range ~150-960 MHz; clamp to keep the radio happy.
  if (khz < 150000) khz = 150000;
  if (khz > 960000) khz = 960000;
  _freqKHz = (uint32_t)khz;
  persist();
  if (_page == PAGE_MONITOR && _listening) applyRadio();
}

// --- facade -----------------------------------------------------------------

bool LoraRxMon::enter(App* app) {
  if (_active) return true;
  _app = app;
  // Seed the working parameters from persisted settings.
  Settings& c = app->cfg;
  _freqKHz = c.lrxFreqKHz; _sf = c.lrxSf; _bwHz = c.lrxBwHz; _cr = c.lrxCr;
  _sync = c.lrxSync; _preamble = c.lrxPreamble; _crc = (c.lrxCrc != 0);
  if (_sf < 7 || _sf > 12) _sf = 12;
  if (_cr < 5 || _cr > 8) _cr = 5;
  _page = PAGE_CONFIG; _cfgSel = 0; _freqStepHz = 1000;
  _ringHead = _ringUsed = 0; _pktCount = 0; _scroll = 0; _listening = false;
  _active = true;
  return true;
}

void LoraRxMon::exit() {
  if (!_active) return;
  _active = false; _listening = false;
  // Restore CardSat LoRa messaging on the shared radio.
  if (_app && _app->cfg.loraEnable) _app->loraStart();
}

void LoraRxMon::service() {
  if (!_active || _page != PAGE_MONITOR || !_listening) return;
  uint8_t buf[256]; size_t n = 0; float rssi = 0, snr = 0;
  if (_app->lora.poll(buf, sizeof(buf), n, rssi, snr) && n > 0) {
    _pktCount++;
    Frame& fr = _ring[_ringHead];
    fr.rawLen = (uint16_t)n;
    fr.len = (uint8_t)(n > FRAME_CAP ? FRAME_CAP : n);
    memcpy(fr.data, buf, fr.len);
    fr.rssi = (int16_t)rssi;
    fr.snr  = (int8_t)snr;
    _ringHead = (_ringHead + 1) % RING_FRAMES;
    if (_ringUsed < RING_FRAMES) _ringUsed++;
    // Live: jump to the newest frame. Paused: keep the viewed frame frozen by
    // advancing the scrollback offset in step with the newly arrived frame (so the
    // same packet stays on screen until it scrolls out of the ring).
    if (!_paused) _scroll = 0;
    else if (_scroll < RING_FRAMES - 1) _scroll++;
  }
}

void LoraRxMon::key(char c, bool enter, bool back) {
  if (!_active) return;

  if (_page == PAGE_CONFIG) {
    if (back || c == '`') { exit(); return; }   // ESC/DEL or backtick -> leave the mode
    if (c == ';') { _cfgSel = (_cfgSel + ROW_COUNT - 1) % ROW_COUNT; return; }  // up
    if (c == '.') { _cfgSel = (_cfgSel + 1) % ROW_COUNT; return; }              // down
    if (c == ',') { adjust(-1); return; }                                      // left
    if (c == '/') { adjust(+1); return; }                                      // right
    // On the frequency row, a key cycles the step size for convenience.
    if (c == 's' && _cfgSel == ROW_FREQ) {
      _freqStepHz = (_freqStepHz >= 1000000) ? 1000
                  : (_freqStepHz >= 100000)  ? 1000000
                  : (_freqStepHz >= 10000)   ? 100000
                  : (_freqStepHz >= 1000)    ? 10000 : 1000;
      return;
    }
    if (enter) {
      if (_cfgSel == ROW_FREQ) {
        // Open the shared numeric editor to type a frequency in MHz (edit code 240).
        _app->editTarget = 240;
        _app->editTitle  = "Frequency (MHz)";
        _app->editBuf    = String(_freqKHz / 1000.0, 3);
        _app->screen     = SCR_EDIT;
        _app->lastDrawMs = 0;
        return;
      }
      // Any other row: start receiving -> monitor page.
      persist();
      applyRadio();
      _page = PAGE_MONITOR; _scroll = 0;
      return;
    }
    return;
  }

  // PAGE_MONITOR
  if (back || c == '`') { _paused = false; _page = PAGE_CONFIG; return; }  // ESC/DEL/backtick -> config
  // Pause freezes the display (radio keeps receiving into the ring); resume jumps
  // back to the newest frame.
  if (c == 'p') {
    _paused = !_paused;
    if (_paused) _pausedAtCount = _pktCount;
    else _scroll = 0;
    return;
  }
  // Scrollback through the frame ring.
  if (c == ';') { if (_scroll < _ringUsed - 1) _scroll++; return; }  // older
  if (c == '.') { if (_scroll > 0) _scroll--; return; }              // newer
  // Live parameter tweak (re-applies to the radio immediately).
  bool changed = false;
  if (c == ',') { if (_freqKHz > 1) { _freqKHz -= (_freqStepHz / 1000); if (_freqKHz < 1) _freqKHz = 1; changed = true; } }
  else if (c == '/') { _freqKHz += (_freqStepHz / 1000); changed = true; }
  else if (c == 'f') {   // cycle frequency step
    _freqStepHz = (_freqStepHz >= 1000000) ? 1000
                : (_freqStepHz >= 100000)  ? 1000000
                : (_freqStepHz >= 10000)   ? 100000
                : (_freqStepHz >= 1000)    ? 10000 : 1000;
  }
  else if (c == 's') { _sf = (_sf >= 12) ? 7 : _sf + 1; changed = true; }
  else if (c == 'b') { _bwHz = BW_TABLE[(bwIndex(_bwHz) + 1) % BW_COUNT]; changed = true; }
  else if (c == 'c') { _cr = (_cr >= 8) ? 5 : _cr + 1; changed = true; }
  else if (c == 'x') { _ringHead = _ringUsed = 0; _pktCount = 0; _scroll = 0; }  // clear
  if (changed) { persist(); applyRadio(); }
}

// --- config-parameter adjust ------------------------------------------------

void LoraRxMon::adjust(int dir) {
  switch (_cfgSel) {
    case ROW_FREQ: {
      long step = (long)(_freqStepHz / 1000);       // kHz
      long f = (long)_freqKHz + dir * step;
      if (f < 1) f = 1;
      _freqKHz = (uint32_t)f;
      break;
    }
    case ROW_SF:
      _sf += dir; if (_sf < 7) _sf = 12; if (_sf > 12) _sf = 7;
      break;
    case ROW_BW: {
      int i = bwIndex(_bwHz) + dir;
      if (i < 0) i = BW_COUNT - 1; if (i >= BW_COUNT) i = 0;
      _bwHz = BW_TABLE[i];
      break;
    }
    case ROW_CR:
      _cr += dir; if (_cr < 5) _cr = 8; if (_cr > 8) _cr = 5;
      break;
    case ROW_SYNC:
      _sync = (uint8_t)(_sync + dir);   // wraps 0..255
      break;
    case ROW_PREAMBLE: {
      int p = (int)_preamble + dir;
      if (p < 4) p = 4; if (p > 64) p = 64;
      _preamble = (uint16_t)p;
      break;
    }
    case ROW_CRC:
      _crc = !_crc;
      break;
  }
}

// --- drawing ----------------------------------------------------------------

void LoraRxMon::draw(M5Canvas& canvas, App* app) {
  (void)app;
  if (!_active) return;
  if (_page == PAGE_CONFIG) drawConfig(canvas);
  else                      drawMonitor(canvas);
}

void LoraRxMon::drawConfig(M5Canvas& canvas) {
  canvas.fillScreen(C_BLK);
  _app->header("LoRa RX");                 // standard blue title bar (clock + battery)
  canvas.setTextSize(1);

  char buf[40];
  const char* labels[ROW_COUNT] = { "Freq", "SF", "Bandwidth", "Coding", "Sync", "Preamble", "CRC" };
  int y = 22;
  for (int r = 0; r < ROW_COUNT; r++) {
    bool selrow = (r == _cfgSel);
    // selected row: highlighted label block, like other CardSat list screens
    canvas.setTextColor(selrow ? C_BLK : C_GRY, selrow ? C_YEL : C_BLK);
    canvas.setCursor(6, y); canvas.printf(" %-9s ", labels[r]);
    canvas.setTextColor(selrow ? C_YEL : C_WHT, C_BLK);
    canvas.setCursor(92, y);
    switch (r) {
      case ROW_FREQ:
        snprintf(buf, sizeof(buf), "%.3f MHz", _freqKHz / 1000.0); break;
      case ROW_SF:
        snprintf(buf, sizeof(buf), "SF%u", (unsigned)_sf); break;
      case ROW_BW:
        if (_bwHz % 1000 == 0) snprintf(buf, sizeof(buf), "%lu kHz", (unsigned long)(_bwHz/1000));
        else                   snprintf(buf, sizeof(buf), "%.1f kHz", _bwHz/1000.0);
        break;
      case ROW_CR:
        snprintf(buf, sizeof(buf), "4/%u", (unsigned)_cr); break;
      case ROW_SYNC:
        snprintf(buf, sizeof(buf), "0x%02X", _sync); break;
      case ROW_PREAMBLE:
        snprintf(buf, sizeof(buf), "%u sym", (unsigned)_preamble); break;
      case ROW_CRC:
        snprintf(buf, sizeof(buf), "%s", _crc ? "on" : "off"); break;
      default: buf[0] = 0;
    }
    canvas.print(buf);
    // frequency step size, shown at the right of the freq row when selected
    if (r == ROW_FREQ && selrow) {
      canvas.setTextColor(C_GRY, C_BLK);
      canvas.setCursor(180, y);
      uint32_t st = _freqStepHz;
      if (st >= 1000000) canvas.print("+/-1M");
      else if (st >= 1000) canvas.printf("+/-%luk", (unsigned long)(st/1000));
      else canvas.printf("+/-%lu", (unsigned long)st);
    }
    y += 13;
  }

  if (!_app->cfg.loraEnable) {
    canvas.setTextColor(C_RED, C_BLK); canvas.setCursor(6, y + 4);
    canvas.print("Enable LoRa in Settings first");
  }

  // concise footer (fits 240px): differs on the freq row (ENTER types MHz).
  if (_cfgSel == ROW_FREQ) _app->footer(";. row  ,/ tune  ENT type  ` esc");
  else                     _app->footer(";. row  ,/ adjust  ENT start  ` esc");
}

void LoraRxMon::drawMonitor(M5Canvas& canvas) {
  canvas.fillScreen(C_BLK);
  _app->header("LoRa RX");                 // standard blue title bar
  canvas.setTextSize(1);

  // RF status line just under the header: freq / SF / BW / CR / state / count.
  canvas.setTextColor(C_CYN, C_BLK); canvas.setCursor(2, 18);
  canvas.printf("%.3f", _freqKHz / 1000.0);
  canvas.setTextColor(C_GRY, C_BLK); canvas.setCursor(62, 18);
  canvas.printf("SF%u", (unsigned)_sf);
  canvas.setCursor(92, 18);
  if (_bwHz % 1000 == 0) canvas.printf("BW%lu", (unsigned long)(_bwHz/1000));
  else                   canvas.printf("BW%.0f", _bwHz/1000.0);
  canvas.setCursor(148, 18); canvas.printf("4/%u", (unsigned)_cr);
  // state: PAUSE (paused) / RX (listening) / -- (idle)
  if (_paused)          { canvas.setTextColor(C_YEL, C_BLK); canvas.setCursor(172, 18); canvas.print("PAUSE"); }
  else if (_listening)  { canvas.setTextColor(C_GRN, C_BLK); canvas.setCursor(172, 18); canvas.print("RX"); }
  else                  { canvas.setTextColor(C_RED, C_BLK); canvas.setCursor(172, 18); canvas.print("--"); }
  canvas.setTextColor(C_YEL, C_BLK); canvas.setCursor(206, 18); canvas.printf("%lu", (unsigned long)_pktCount);

  if (_ringUsed == 0) {
    canvas.setTextColor(C_GRY, C_BLK); canvas.setCursor(6, 64);
    canvas.print(_listening ? "listening... no frames yet" : "not receiving");
    _app->footer("p pause  s/b/c f tune  ,/ freq  ` esc");
    return;
  }

  // Which frame to show: newest is (head-1); _scroll counts back from newest.
  int fidx = (_ringHead - 1 - _scroll + RING_FRAMES * 2) % RING_FRAMES;
  Frame& fr = _ring[fidx];

  // Frame meta line: index (from newest), RSSI/SNR, length, and paused "N new".
  canvas.setTextColor(C_ORG, C_BLK); canvas.setCursor(2, 30);
  canvas.printf("%ddBm SNR%d  len%u", fr.rssi, fr.snr, fr.rawLen);
  if (fr.rawLen > fr.len) { canvas.setTextColor(C_RED, C_BLK); canvas.print(" trunc"); }
  if (_scroll > 0) { canvas.setTextColor(C_GRY, C_BLK); canvas.printf("  -%d", _scroll); }
  if (_paused && _pktCount > _pausedAtCount) {
    canvas.setTextColor(C_YEL, C_BLK);
    canvas.printf("  +%lu new", (unsigned long)(_pktCount - _pausedAtCount));
  }

  // Hexdump: 16 bytes/row, a hex line then an ASCII line beneath it.
  int y = 42;
  for (int off = 0; off < fr.len && y < 118; off += 16) {
    canvas.setTextColor(C_GRY, C_BLK); canvas.setCursor(2, y);
    canvas.printf("%03X", off);
    canvas.setTextColor(C_WHT, C_BLK);
    for (int i = 0; i < 16 && off + i < fr.len; i++) {
      canvas.setCursor(26 + i * 13, y);
      canvas.printf("%02X", fr.data[off + i]);
    }
    int ay = y + 8;
    canvas.setTextColor(C_GRN, C_BLK);
    for (int i = 0; i < 16 && off + i < fr.len; i++) {
      uint8_t b = fr.data[off + i];
      char ch = (b >= 32 && b < 127) ? (char)b : '.';
      canvas.setCursor(26 + i * 13, ay);
      canvas.printf("%c", ch);
    }
    y += 18;
  }

  // Footer: on a paused view, advertise scroll; live view advertises pause + tune.
  if (_paused) _app->footer(";. frame  p resume  ,/ freq  ` esc");
  else         _app->footer("p pause  s/b/c f tune  ,/ freq  ` esc");
}

#endif // CARDSAT_HAS_LORARX
