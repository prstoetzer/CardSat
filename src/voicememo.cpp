// ===========================================================================
//  voicememo.cpp  -  SD-card-only voice memo recorder (PDM mic -> WAV)
// ===========================================================================
#include "voicememo.h"
#include "config.h"
#include "storage.h"
#include <M5Cardputer.h>   // M5.Mic / .Speaker (ADV: needs IDF 5.4.x, see MANUAL.md)
#include <M5Unified.h>
#include <time.h>

// One capture block. At 16 kHz mono 16-bit, 1024 samples = 64 ms = 2048 bytes.
// Small enough that pulling one per loop tick leaves the radio/rotator/web
// services responsive, large enough to keep the per-block overhead modest.
static constexpr size_t MEMO_BLOCK_SAMPLES = 1024;

// Little-endian helpers for the WAV header fields.
static void put32(uint8_t* p, uint32_t v) { p[0]=v; p[1]=v>>8; p[2]=v>>16; p[3]=v>>24; }
static void put16(uint8_t* p, uint16_t v) { p[0]=v; p[1]=v>>8; }

void VoiceMemo::buildWavHeader(uint8_t h[44], uint32_t dataBytes) {
  // Canonical 44-byte PCM WAV header (RIFF / fmt / data).
  const uint32_t sr   = MEMO_SAMPLE_HZ;
  const uint16_t ch   = 1, bits = 16;
  const uint32_t br   = sr * ch * bits / 8;        // byte rate
  const uint16_t ba   = ch * bits / 8;             // block align
  memcpy(h + 0,  "RIFF", 4);
  put32(h + 4,  36 + dataBytes);                   // chunk size
  memcpy(h + 8,  "WAVE", 4);
  memcpy(h + 12, "fmt ", 4);
  put32(h + 16, 16);                               // fmt chunk size
  put16(h + 20, 1);                                // PCM
  put16(h + 22, ch);
  put32(h + 24, sr);
  put32(h + 28, br);
  put16(h + 32, ba);
  put16(h + 34, bits);
  memcpy(h + 36, "data", 4);
  put32(h + 40, dataBytes);
}

void VoiceMemo::writeWavHeader(uint32_t dataBytes) {
  // Write the header at the front of the currently-open _file. Used only for the
  // initial placeholder in start(); the final size is patched in finalize() by
  // reopening the file (a backward seek-then-write on an open "w" handle does not
  // reliably commit on the SD/FS driver, which left the data size at 0 -> silent).
  uint8_t h[44];
  buildWavHeader(h, dataBytes);
  _file.seek(0);
  _file.write(h, sizeof(h));
}

bool VoiceMemo::start() {
  _err = "";
  if (_state == RECORDING) return true;

  // SD-card REQUIRED: this feature only writes to a physical card.
  if (!Store::onSD()) { _err = "SD card required"; _state = ERROR; return false; }

  // Ensure the audio folder exists.
  fs::FS& fsx = Store::fs();
  if (!fsx.exists(AUDIO_DIR) && !fsx.mkdir(AUDIO_DIR)) {
    _err = "mkdir failed"; _state = ERROR; return false;
  }

  // Refuse if the card is implausibly full (best-effort; SD free is large).
  if (Store::freeBytes() < (size_t)MEMO_MIN_FREE_KB * 1024) {
    _err = "SD nearly full"; _state = ERROR; return false;
  }

  // Build a timestamped filename: /CardSat/audio/memo_YYYYMMDD_HHMMSS.wav, or a
  // millis()-based name if the clock isn't set.
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmv; gmtime_r(&now, &tmv);
    snprintf(_path, sizeof(_path), "%s/memo_%04d%02d%02d_%02d%02d%02d.wav",
             AUDIO_DIR, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
             tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    snprintf(_path, sizeof(_path), "%s/memo_%lu.wav", AUDIO_DIR,
             (unsigned long)millis());
  }

  _file = fsx.open(_path, "w");
  if (!_file) { _err = "open failed"; _state = ERROR; return false; }

  // The mic and speaker share I2S: stop the speaker, then start the mic.
  // NOTE: on the Cardputer ADV the ES8311 mic only works when CardSat is built
  // against ESP-IDF 5.4.x; on 5.5.x M5Unified's mic path can't clock the codec
  // (upstream regression, espressif/esp-idf#18621). See MANUAL.md.
  _spkWasOn = M5Cardputer.Speaker.isEnabled();
  if (_spkWasOn) M5Cardputer.Speaker.end();
  {
    auto mc = M5.Mic.config();
    mc.sample_rate = MEMO_SAMPLE_HZ;
    M5.Mic.config(mc);
  }
  if (!M5.Mic.begin()) {
    _file.close(); fsx.remove(_path);
    if (_spkWasOn) M5Cardputer.Speaker.begin();
    _err = "mic start failed"; _state = ERROR; return false;
  }

  writeWavHeader(0);                 // placeholder sizes; patched on finalize
  _dataBytes = 0;
  _primed    = false;
  _bufIdx    = 0;
  _peak      = 0;
  _rawPeak   = 0;
  _hpPrevX   = 0;
  _hpPrevY   = 0;
  _hpPrimed  = false;
  _startMs   = millis();
  _state     = RECORDING;
  Serial.printf("[memo] recording -> %s\n", _path);
  return true;
}

void VoiceMemo::poll() {
  if (_state != RECORDING) return;

  // Hard time cap.
  if (millis() - _startMs >= MEMO_MAX_SECS * 1000UL) { finalize(true); return; }

  if (!M5.Mic.isEnabled()) return;

  // M5.Mic.record() is asynchronous: it queues a buffer to be filled by
  // I2S DMA and returns once accepted -- the data is NOT ready on return. We
  // double-buffer: kick a record into buffer A, and once the mic finishes
  // filling it (isRecording() goes false), write A to SD and kick buffer B,
  // ping-ponging. (Writing immediately after record() returns would capture
  // empty/garbage data.)
  static int16_t buf[2][MEMO_BLOCK_SAMPLES];

  if (!_primed) {
    if (M5.Mic.record(buf[_bufIdx], MEMO_BLOCK_SAMPLES, MEMO_SAMPLE_HZ)) _primed = true;
    return;
  }
  if (M5.Mic.isRecording()) return;          // wait for the in-flight buffer

  int16_t* done = buf[_bufIdx];
  _bufIdx ^= 1;
  M5.Mic.record(buf[_bufIdx], MEMO_BLOCK_SAMPLES, MEMO_SAMPLE_HZ);   // next capture

  // Remove DC bias with a one-pole high-pass (primed from the first sample to
  // avoid a startup transient), then apply modest gain with clipping.
  if (!_hpPrimed) { _hpPrevX = done[0]; _hpPrevY = 0; _hpPrimed = true; }
  for (size_t i = 0; i < MEMO_BLOCK_SAMPLES; i++) {
    int32_t x = done[i];
    int32_t y = x - _hpPrevX + ((_hpPrevY * 4075) / 4096);
    _hpPrevX = x;
    _hpPrevY = y;
    { int32_t ry = y < 0 ? -y : y; if (ry > _rawPeak) _rawPeak = (uint16_t)(ry > 65535 ? 65535 : ry); }
    int32_t s = y * MEMO_AC_GAIN;
    if (s >  32767) s =  32767;
    if (s < -32768) s = -32768;
    done[i] = (int16_t)s;
    int16_t v = done[i]; if (v < 0) v = -v;
    if ((uint16_t)v > _peak) _peak = (uint16_t)v;
  }

  size_t bytes = MEMO_BLOCK_SAMPLES * sizeof(int16_t);
  size_t w = _file.write((uint8_t*)done, bytes);
  _dataBytes += w;
  if (w < bytes) { _err = "SD write short"; finalize(false); }       // card full/err
}

void VoiceMemo::stop() {
  if (_state == RECORDING) finalize(true);
}

void VoiceMemo::finalize(bool ok) {
  // Let any in-flight capture settle, then stop the mic and restore the speaker.
  if (_primed && M5.Mic.isEnabled()) {
    uint32_t t0 = millis();
    while (M5.Mic.isRecording() && millis() - t0 < 200) { M5.delay(1); }
  }
  M5.Mic.end();
  if (_spkWasOn) M5Cardputer.Speaker.begin();

  if (_file) {
    _file.close();
    // Patch the real RIFF/data sizes by reopening for random-access update. A
    // seek(0)+write on the original "w" handle did not commit on this SD driver,
    // leaving the data chunk size at 0 so players treated the file as empty
    // (audible samples present on disk, but silent playback). Reopen "r+",
    // overwrite the 44-byte header, done.
    if (ok && _path[0]) {
      uint8_t h[44];
      buildWavHeader(h, _dataBytes);
      File pf = Store::fs().open(_path, "r+");
      if (pf) {
        pf.seek(0);
        pf.write(h, sizeof(h));
        pf.close();
      } else {
        Serial.println("[memo] WARN: could not reopen to patch WAV header");
      }
    }
  }
  Serial.printf("[memo] %s (%lu bytes, %lu ms, peak %u, rawAC %u%s)\n",
                ok ? "saved" : "aborted", (unsigned long)_dataBytes,
                (unsigned long)(millis() - _startMs), (unsigned)_peak,
                (unsigned)_rawPeak,
                _rawPeak == 0 ? " -- SILENT, mic captured no signal" : "");
  if (!ok && _path[0]) Store::fs().remove(_path);   // discard a broken file
  _state = IDLE; _primed = false;
}

uint32_t VoiceMemo::elapsedMs() const {
  return _state == RECORDING ? (millis() - _startMs) : 0;
}
uint32_t VoiceMemo::secondsLeft() const {
  if (_state != RECORDING) return 0;
  uint32_t el = (millis() - _startMs) / 1000;
  return el >= MEMO_MAX_SECS ? 0 : (MEMO_MAX_SECS - el);
}
