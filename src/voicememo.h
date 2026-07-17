#pragma once
// ===========================================================================
//  voicememo.h  -  SD-card-only voice memo recorder (PDM mic -> WAV)
// ===========================================================================
//
//  An SD-card-REQUIRED feature: records a short voice memo from the Cardputer
//  ADV's built-in PDM microphone (via M5Unified's M5.Mic) and streams it to a
//  16-bit mono WAV under AUDIO_DIR on the SD card. Retrieval is by reading the
//  card on a computer.
//
//  Cooperative by design: start() opens the file and begins capture; poll() is
//  called every loop tick and pulls one small block of samples from the mic into
//  the file, so the radio/rotator/web services keep running between blocks. The
//  recording auto-stops at MEMO_MAX_SECS or when stop() is called (a second 'v').
//
//  The mic and speaker share the I2S peripheral, so we end the speaker before
//  recording and restore it afterwards. No large clip buffer lives in RAM -- only
//  one small block at a time -- so this is safe on the no-PSRAM heap.
//
//  *** UNTESTED on hardware as of v0.9.20 -- the mic/I2S coexistence with the SPI
//  display and SD writes wants on-device confirmation. Use at your own risk. ***
// ===========================================================================
#include <Arduino.h>
#include <FS.h>

class VoiceMemo {
public:
  enum State { IDLE, RECORDING, ERROR };

  // Begin a memo. Returns false (and sets lastError) if SD isn't present, the
  // audio folder can't be made, the card is too full, or the mic won't start.
  // satName (optional) is the satellite being tracked at record time; a
  // sanitized short form is encoded into the filename so the memo browser can
  // show which bird the memo was made on.
  bool start(const char* satName = nullptr);
  // Pull one block from the mic into the file; call every loop tick while
  // recording. Auto-finalizes at the time cap. No-op when idle.
  void poll();
  // Finalize the current memo (patch the WAV header, close, restore speaker).
  void stop();

  bool   isRecording() const { return _state == RECORDING; }
  State  state()       const { return _state; }
  uint32_t elapsedMs() const;                 // since start (0 if idle)
  uint32_t secondsLeft() const;               // until the cap
  const char* lastError() const { return _err; }
  const char* path()      const { return _path; }

  // ---- Browser / playback (SCR_MEMOS) -------------------------------------
  // One row in the memo browser, parsed from the WAV filename + size.
  struct MemoEntry {
    char     file[64];     // bare filename (no dir), e.g. memo_20260619_143200_AO-91.wav
    char     sat[16];      // parsed satellite tag, or "" if none
    uint64_t stamp;        // YYYYMMDD*1000000 + HHMMSS for sorting, or 0 if unknown
    uint16_t year; uint8_t mon, day, hh, mm, ss;   // parsed time fields (0 if unknown)
    uint32_t secs;         // duration in seconds (from file size)
    bool     haveTime;     // true if the filename carried a real timestamp
  };
  // Enumerate AUDIO_DIR into out[], newest-first, up to max entries. Returns the
  // count. Safe when SD is absent (returns 0).
  static int  listMemos(MemoEntry* out, int max);
  // Delete a memo by bare filename (under AUDIO_DIR). Returns true on success.
  static bool deleteMemo(const char* file);
  // Play a memo (blocking, but polls the keyboard so any key cancels). Streams
  // the WAV from SD in small blocks via the speaker -- no full-clip buffer, so
  // it's safe on the no-PSRAM heap. Returns false (and sets lastError) on a bad
  // file or speaker failure. cancelKey is called each block; if it returns true,
  // playback stops early.
  bool playMemo(const char* file, bool (*cancelPoll)(), uint8_t volume = 200);

private:
  State    _state   = IDLE;
  File     _file;
  uint32_t _startMs = 0;
  uint32_t _dataBytes = 0;                     // PCM payload written so far
  char     _path[64] = {0};
  bool     _primed = false;                    // a record buffer is in flight
  int16_t* _recBuf = nullptr;                  // 2-block ping-pong; heap only while recording
  uint8_t  _bufIdx = 0;                         // which buffer is being filled
  uint16_t _peak   = 0;                         // peak |sample| seen (silence detect)
  uint16_t _rawPeak = 0;                        // peak |raw AC| before gain (calibration)
  int32_t  _hpPrevX = 0;                        // high-pass filter: previous input
  int32_t  _hpPrevY = 0;                        // high-pass filter: previous output
  bool     _hpPrimed = false;                   // filter seeded from first sample
  const char* _err = "";
  bool     _spkWasOn = false;

  void buildWavHeader(uint8_t h[44], uint32_t dataBytes);  // fill a 44-byte RIFF header
  void writeWavHeader(uint32_t dataBytes);     // (re)write the 44-byte RIFF header
  void finalize(bool ok);
};
