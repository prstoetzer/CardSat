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
  bool start();
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

private:
  State    _state   = IDLE;
  File     _file;
  uint32_t _startMs = 0;
  uint32_t _dataBytes = 0;                     // PCM payload written so far
  char     _path[64] = {0};
  bool     _primed = false;                    // a record buffer is in flight
  uint8_t  _bufIdx = 0;                         // which buffer is being filled
  const char* _err = "";
  bool     _spkWasOn = false;

  void writeWavHeader(uint32_t dataBytes);     // (re)write the 44-byte RIFF header
  void finalize(bool ok);
};
