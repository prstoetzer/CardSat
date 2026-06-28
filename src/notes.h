#pragma once
#include <Arduino.h>

// Plain-text note storage. Notes are .txt files under /CardSat/notes/ on the
// active filesystem (LittleFS or SD -- so notes work even with no SD card). The
// UI (browser + editor) lives in app.cpp; this module is just the file I/O.
namespace Notes {

  static const char* NOTES_DIR = "/CardSat/notes";

  // Ensure /CardSat/notes exists. Returns false if it can't be created.
  bool ensureDir();

  // Enumerate note base names (without directory or the .txt suffix) into out[],
  // each buffer `nameCap` bytes, up to `max` entries, newest-first by file mtime.
  // `times` (may be null) is filled with each file's last-write time_t, parallel
  // to out[]. Returns the count. A name longer than nameCap-1 is skipped.
  int  list(char out[][32], time_t* times, int max, int nameCap);

  // Read the note `base` (no path, no .txt) into `dst`, capped at `maxBytes`
  // characters. Returns false if the file is missing or unreadable.
  bool read(const char* base, String& dst, size_t maxBytes);

  // Write `text` to the note `base`, replacing any existing file. Returns false
  // on any filesystem error.
  bool write(const char* base, const String& text);

  // Delete the note `base`. Returns true if the file no longer exists afterward.
  bool remove(const char* base);

  // True if a note with this base name already exists.
  bool exists(const char* base);

  // Sanitize a user-entered name in place: keep [A-Za-z0-9 _-], collapse the
  // rest to '_', trim surrounding spaces, and cap length to cap-1. Yields a safe
  // base name (no extension). Returns false if the result is empty.
  bool sanitizeName(char* name, size_t cap);

}
