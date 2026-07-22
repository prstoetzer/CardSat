#include "notes.h"
#include "storage.h"
#include <FS.h>
#include <string.h>

namespace Notes {

// Build the full "/CardSat/notes/<base>.txt" path into buf.
static void pathFor(const char* base, char* buf, size_t cap) {
  snprintf(buf, cap, "%s/%s.txt", NOTES_DIR, base);
}

bool ensureDir() {
  fs::FS& fsx = Store::fs();
  if (fsx.exists(NOTES_DIR)) return true;
  return fsx.mkdir(NOTES_DIR);
}

bool exists(const char* base) {
  if (!base || !base[0]) return false;
  char path[96];
  pathFor(base, path, sizeof(path));
  return Store::fs().exists(path);
}

int list(char out[][32], time_t* times, int max, int nameCap) {
  if (max <= 0) return 0;
  // M28: the row width is fixed at 32 by the signature; clamp nameCap so a caller that
  // passes a larger value can never make the copies/terminators below write past a row.
  if (nameCap > 32) nameCap = 32;
  if (nameCap < 1)  nameCap = 1;
  if (!ensureDir()) return 0;
  fs::FS& fsx = Store::fs();
  File dir = fsx.open(NOTES_DIR);
  if (!dir || !dir.isDirectory()) return 0;

  int n = 0;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (f.isDirectory()) { f.close(); continue; }
    const char* nm = f.name();
    const char* slash = strrchr(nm, '/');
    const char* bn = slash ? slash + 1 : nm;       // basename (some cores give a path)
    size_t L = strlen(bn);
    bool isTxt = L > 4 && strcasecmp(bn + L - 4, ".txt") == 0;
    if (isTxt && n < max) {
      size_t baseLen = L - 4;                       // strip ".txt"
      if (baseLen <= (size_t)(nameCap - 1)) {
        memcpy(out[n], bn, baseLen);
        out[n][baseLen] = 0;
        if (times) times[n] = f.getLastWrite();     // file mtime (0 if FS lacks it)
        n++;
      }
    }
    f.close();
  }
  dir.close();

  // Newest-first insertion sort by mtime; ties (or no-mtime filesystems, where all
  // are 0) fall back to case-insensitive A->Z so the order is still stable. Name
  // and time move together. n is small (<= max).
  for (int i = 1; i < n; ++i) {
    char keyName[32];
    strncpy(keyName, out[i], sizeof(keyName) - 1); keyName[sizeof(keyName) - 1] = 0;
    time_t keyT = times ? times[i] : 0;
    int j = i - 1;
    while (j >= 0) {
      time_t jt = times ? times[j] : 0;
      bool jAfterKey = (jt != keyT) ? (jt < keyT)            // older sorts below newer
                                    : (strcasecmp(out[j], keyName) > 0);
      if (!jAfterKey) break;
      strncpy(out[j + 1], out[j], nameCap - 1); out[j + 1][nameCap - 1] = 0;
      if (times) times[j + 1] = times[j];
      --j;
    }
    strncpy(out[j + 1], keyName, nameCap - 1); out[j + 1][nameCap - 1] = 0;
    if (times) times[j + 1] = keyT;
  }
  return n;
}

bool read(const char* base, String& dst, size_t maxBytes) {
  dst = "";
  if (!base || !base[0]) return false;
  char path[96];
  pathFor(base, path, sizeof(path));
  fs::FS& fsx = Store::fs();
  if (!fsx.exists(path)) return false;
  File f = fsx.open(path, "r");
  if (!f) return false;
  size_t n = f.size();
  if (n > maxBytes) n = maxBytes;       // truncate oversized files to the buffer cap
  dst.reserve(n + 1);
  size_t got = 0;
  while (f.available() && got < maxBytes) {
    int ch = f.read();
    if (ch < 0) break;
    dst += (char)ch;
    got++;
  }
  f.close();
  return true;
}

bool write(const char* base, const String& text) {
  if (!base || !base[0]) return false;
  if (!ensureDir()) return false;
  char path[96];
  pathFor(base, path, sizeof(path));
  // M29: transactional write -- the previous note survives a short write or power loss
  // instead of being truncated the moment saving begins.
  return Store::writeFileAtomic(path, (const uint8_t*)text.c_str(), text.length());
}

bool remove(const char* base) {
  if (!base || !base[0]) return false;
  char path[96];
  pathFor(base, path, sizeof(path));
  fs::FS& fsx = Store::fs();
  if (!fsx.exists(path)) return true;   // already gone
  fsx.remove(path);
  return !fsx.exists(path);
}

bool sanitizeName(char* name, size_t cap) {
  if (!name || cap < 2) return false;
  // Trim leading spaces.
  char* p = name;
  while (*p == ' ') p++;
  // Copy allowed chars, mapping disallowed ones to '_'.
  char tmp[64];
  size_t lim = (cap - 1 < sizeof(tmp) - 1) ? cap - 1 : sizeof(tmp) - 1;
  size_t o = 0;
  for (; *p && o < lim; ++p) {
    char ch = *p;
    bool ok = (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == ' ' || ch == '_' || ch == '-';
    tmp[o++] = ok ? ch : '_';
  }
  tmp[o] = 0;
  // Trim trailing spaces/underscores.
  while (o > 0 && (tmp[o - 1] == ' ')) tmp[--o] = 0;
  if (o == 0) return false;
  strncpy(name, tmp, cap - 1);
  name[cap - 1] = 0;
  return true;
}

} // namespace Notes
