// ===========================================================================
//  voicememo.cpp  -  SD-card-only voice memo recorder (PDM mic -> WAV)
// ===========================================================================
#include "voicememo.h"
#include "config.h"
#include "storage.h"
#include <M5Cardputer.h>   // M5.Mic / .Speaker (ADV: needs IDF 5.4.x, see MANUAL.md)
#include <M5Unified.h>
#include <time.h>
#include <string.h>

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

bool VoiceMemo::start(const char* satName) {
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

  // Rent the 4 KB ping-pong capture buffer for the life of the recording (RAM
  // Tier 1: it used to be permanent .bss inside poll()). Freed in finalize().
  _recBuf = (int16_t*)malloc(2 * MEMO_BLOCK_SAMPLES * sizeof(int16_t));
  if (!_recBuf) { _err = "Out of RAM"; _state = ERROR; return false; }

  // Sanitize the satellite name into a short filename-safe tag: keep only
  // [A-Za-z0-9-], cap at 12 chars. e.g. "AO-91" -> "AO-91", "ISS (ZARYA)" ->
  // "ISSZARYA". Empty if no sat. The browser parses this back out of the name.
  char tag[16] = {0};
  if (satName && *satName) {
    int j = 0;
    for (const char* p = satName; *p && j < 12; ++p) {
      char ch = *p;
      if ((ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
          (ch >= '0' && ch <= '9') || ch == '-')
        tag[j++] = ch;
    }
    tag[j] = 0;
  }

  // Build a timestamped filename: /CardSat/audio/memo_YYYYMMDD_HHMMSS[_TAG].wav,
  // or a millis()-based name if the clock isn't set.
  time_t now = time(nullptr);
  if (now > 1700000000) {
    struct tm tmv; gmtime_r(&now, &tmv);
    if (tag[0])
      snprintf(_path, sizeof(_path), "%s/memo_%04d%02d%02d_%02d%02d%02d_%s.wav",
               AUDIO_DIR, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min, tmv.tm_sec, tag);
    else
      snprintf(_path, sizeof(_path), "%s/memo_%04d%02d%02d_%02d%02d%02d.wav",
               AUDIO_DIR, tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
               tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
  } else {
    if (tag[0])
      snprintf(_path, sizeof(_path), "%s/memo_%lu_%s.wav", AUDIO_DIR,
               (unsigned long)millis(), tag);
    else
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
  if (!_recBuf) return;                        // defensive: start() rents this
  int16_t (*buf)[MEMO_BLOCK_SAMPLES] =
      reinterpret_cast<int16_t (*)[MEMO_BLOCK_SAMPLES]>(_recBuf);

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
  if (_recBuf) { free(_recBuf); _recBuf = nullptr; }   // rented in start()
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

// ===========================================================================
//  Browser / playback support (SCR_MEMOS)
// ===========================================================================

// Parse a memo filename into a MemoEntry. Recognizes:
//   memo_YYYYMMDD_HHMMSS[_TAG].wav   (clock was set)
//   memo_<millis>[_TAG].wav          (clock not set -> haveTime=false)
// Fills time fields + sat tag + a sortable stamp. Returns false if it doesn't
// look like a memo file at all.
static bool parseMemoName(const char* fn, VoiceMemo::MemoEntry& e) {
  e.sat[0] = 0; e.stamp = 0; e.haveTime = false;
  e.year = 0; e.mon = e.day = e.hh = e.mm = e.ss = 0;
  if (strncmp(fn, "memo_", 5) != 0) return false;
  const char* p = fn + 5;

  // Try the dated form: 8 digits, '_', 6 digits.
  int nd = 0; while (p[nd] >= '0' && p[nd] <= '9') nd++;
  if (nd == 8 && p[8] == '_') {
    int td = 0; const char* q = p + 9;
    while (q[td] >= '0' && q[td] <= '9') td++;
    if (td == 6) {
      char b[16];
      memcpy(b, p, 8); b[8] = 0;
      long ymd = atol(b);
      e.year = (uint16_t)(ymd / 10000);
      e.mon  = (uint8_t)((ymd / 100) % 100);
      e.day  = (uint8_t)(ymd % 100);
      memcpy(b, p + 9, 6); b[6] = 0;
      long hms = atol(b);
      e.hh = (uint8_t)(hms / 10000);
      e.mm = (uint8_t)((hms / 100) % 100);
      e.ss = (uint8_t)(hms % 100);
      e.stamp = (uint64_t)ymd * 1000000ull + (uint64_t)hms;
      e.haveTime = true;
      // Optional _TAG before .wav
      const char* tag = p + 15;            // after HHMMSS
      if (*tag == '_') {
        tag++;
        int j = 0;
        while (*tag && *tag != '.' && j < 15) e.sat[j++] = *tag++;
        e.sat[j] = 0;
      }
      return true;
    }
  }

  // Undated form: memo_<digits>[_TAG].wav -> no real time; sort by the number.
  if (nd > 0) {
    char b[16]; int c = nd < 15 ? nd : 15;
    memcpy(b, p, c); b[c] = 0;
    e.stamp = (uint64_t)atol(b);           // millis-based; sorts undated among themselves
    const char* tag = p + nd;
    if (*tag == '_') {
      tag++;
      int j = 0;
      while (*tag && *tag != '.' && j < 15) e.sat[j++] = *tag++;
      e.sat[j] = 0;
    }
    return true;
  }
  return false;
}

int VoiceMemo::listMemos(MemoEntry* out, int max) {
  if (max <= 0 || !Store::onSD()) return 0;
  fs::FS& fsx = Store::fs();
  File dir = fsx.open(AUDIO_DIR);
  if (!dir || !dir.isDirectory()) return 0;

  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    const char* nm = f.name();
    // f.name() may be a full path on some cores; reduce to the basename.
    const char* slash = strrchr(nm, '/');
    const char* base = slash ? slash + 1 : nm;
    // Only .wav memo files.
    size_t L = strlen(base);
    bool isWav = L > 4 && strcasecmp(base + L - 4, ".wav") == 0;
    if (isWav && n < max) {
      MemoEntry e;
      if (parseMemoName(base, e)) {
        strncpy(e.file, base, sizeof(e.file) - 1);
        e.file[sizeof(e.file) - 1] = 0;
        uint32_t sz = (uint32_t)f.size();
        uint32_t pcm = sz > 44 ? sz - 44 : 0;          // strip WAV header
        e.secs = pcm / (2u * MEMO_SAMPLE_HZ);          // 16-bit mono
        out[n++] = e;
      }
    }
    f.close();
  }
  dir.close();

  // Sort newest-first by stamp (simple insertion sort; n is small, <= 64).
  for (int i = 1; i < n; ++i) {
    MemoEntry key = out[i];
    int j = i - 1;
    while (j >= 0 && out[j].stamp < key.stamp) { out[j + 1] = out[j]; --j; }
    out[j + 1] = key;
  }
  return n;
}

bool VoiceMemo::deleteMemo(const char* file) {
  if (!Store::onSD() || !file || !*file) return false;
  char path[96];
  snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, file);
  return Store::fs().remove(path);
}

bool VoiceMemo::playMemo(const char* file, bool (*cancelPoll)(), uint8_t volume) {
  _err = "";
  if (_state == RECORDING) { _err = "busy recording"; return false; }
  if (!Store::onSD()) { _err = "SD card required"; return false; }

  char path[96];
  snprintf(path, sizeof(path), "%s/%s", AUDIO_DIR, file);
  File f = Store::fs().open(path, "r");
  if (!f) { _err = "open failed"; return false; }

  // Read the 44-byte header to learn the sample rate; fall back to MEMO_SAMPLE_HZ.
  uint8_t hdr[44];
  uint32_t rate = MEMO_SAMPLE_HZ;
  if (f.read(hdr, 44) == 44 &&
      hdr[0]=='R'&&hdr[1]=='I'&&hdr[2]=='F'&&hdr[3]=='F') {
    uint32_t r = (uint32_t)hdr[24] | ((uint32_t)hdr[25]<<8) |
                 ((uint32_t)hdr[26]<<16) | ((uint32_t)hdr[27]<<24);
    if (r >= 8000 && r <= 48000) rate = r;
  } else {
    f.seek(44);   // not a clean header; assume our format and skip 44 bytes
  }

  // The mic and speaker share I2S. If the mic happens to be up (it shouldn't be
  // outside recording), end it. Ensure the speaker is running.
  if (M5.Mic.isEnabled()) M5.Mic.end();
  bool spkWasOn = M5Cardputer.Speaker.isEnabled();
  if (!spkWasOn) M5Cardputer.Speaker.begin();
  M5Cardputer.Speaker.setVolume(volume);

  // Stream blocks: read PCM from SD into a small buffer, hand to playRaw, wait
  // for it to drain (polling the cancel hook), repeat. No whole-clip buffer.
  // Heap-on-demand (RAM Tier 1): 2 KB alive only while a memo plays.
  int16_t* pbuf = (int16_t*)malloc(MEMO_PLAY_SAMPLES * sizeof(int16_t));
  if (!pbuf) {
    f.close();
    if (!spkWasOn) M5Cardputer.Speaker.end();
    _err = "Out of RAM";
    return false;
  }
  bool cancelled = false;
  for (;;) {
    size_t got = f.read((uint8_t*)pbuf, MEMO_PLAY_SAMPLES * sizeof(int16_t));
    if (got < 2) break;                       // EOF
    size_t nsamp = got / 2;
    // Wait for the previous block to finish so we don't overrun the channel.
    while (M5Cardputer.Speaker.isPlaying()) {
      M5.delay(1);
      if (cancelPoll && cancelPoll()) { cancelled = true; break; }
    }
    if (cancelled) break;
    M5Cardputer.Speaker.playRaw(pbuf, nsamp, rate, false, 1, 0);
  }
  // Let the final block finish unless cancelled.
  if (!cancelled) {
    while (M5Cardputer.Speaker.isPlaying()) {
      M5.delay(1);
      if (cancelPoll && cancelPoll()) break;
    }
  } else {
    M5Cardputer.Speaker.stop();
  }

  f.close();
  if (!spkWasOn) { /* leave speaker on; app expects it available */ }
  free(pbuf);                                  // rented above; drain is done
  return true;
}
