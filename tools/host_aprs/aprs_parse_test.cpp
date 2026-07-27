// Host harness for the APRS position decoder that App::serviceAprsIs() feeds.
//
// The three feed parsers added in 0.9.65 shipped without ever seeing live data, and
// two of them were wrong. This one is testable without a radio or a socket: APRS
// position encoding is fully determined by the packet text, so published vectors
// pin the decoder exactly. Vectors below are from aprslib's documented parse output
// (aprs-python.readthedocs.io) -- an independent implementation, not our own claim.
//
// Build/run:  g++ -O2 -o /tmp/aprs_parse_test aprs_parse_test.cpp && /tmp/aprs_parse_test
//
// NOTE ON BOB'S EXAMPLES: www.aprs.org/aprs12/mic-e-examples.txt states that TOCALL
// "ABCDEF" decodes to latitude 1234.56N. It does not -- under the spec's own
// destination mapping (A-J = 0-9) it decodes to 0123.45. That document is
// illustrative, not a conformance vector. Do not "fix" this decoder against it.

#include <cstdio>
#include <cstring>
#include <cmath>
#include <cstdlib>

// ---------------------------------------------------------------------------
//  Decoded position. Fields mirror AprsSta so the firmware side is a copy, not
//  a translation. speedKt/course are -1 when the format carries no course/speed.
// ---------------------------------------------------------------------------
struct AprsPos {
  double lat, lon;
  char   symTable, symCode;
  int    course, speedKt;
  bool   valid;
};

// MIC-E destination character -> latitude digit, plus the two flag bits it carries.
// '0'-'9' digit, message bit 0. 'A'-'J' digit 0-9 with bit 1 (custom message).
// 'P'-'Y' digit 0-9 with bit 1 (standard message). 'K','L','Z' are position
// ambiguity (space); we treat an ambiguous digit as 0 and mark the fix coarse.
static bool miceDestDigit(char c, int& digit, int& bit, bool& ambiguous) {
  ambiguous = false;
  if (c >= '0' && c <= '9') { digit = c - '0'; bit = 0; return true; }
  if (c >= 'A' && c <= 'J') { digit = c - 'A'; bit = 1; return true; }
  if (c >= 'P' && c <= 'Y') { digit = c - 'P'; bit = 1; return true; }
  if (c == 'K' || c == 'L' || c == 'Z') { digit = 0; bit = (c == 'L') ? 0 : 1; ambiguous = true; return true; }
  return false;
}

// ---------------------------------------------------------------------------
//  MIC-E. Latitude and the two hemisphere flags live in the AX.25 destination;
//  longitude, course and speed are offset-by-28 bytes at the head of the info
//  field. The +100 longitude offset flag is why a naive decoder puts European
//  stations in the Atlantic.
// ---------------------------------------------------------------------------
static bool decodeMicE(const char* dest, const char* info, AprsPos& p) {
  if (strlen(dest) < 6 || strlen(info) < 9) return false;

  int  d[6], b[6];
  bool amb[6], anyAmb = false;
  for (int i = 0; i < 6; ++i) {
    if (!miceDestDigit(dest[i], d[i], b[i], amb[i])) return false;
    if (amb[i]) anyAmb = true;
  }
  (void)anyAmb;

  double latDeg = d[0] * 10 + d[1];
  double latMin = d[2] * 10 + d[3] + (d[4] * 10 + d[5]) / 100.0;
  p.lat = latDeg + latMin / 60.0;
  if (!b[3]) p.lat = -p.lat;            // destination char 4 bit: 1 = North

  const unsigned char* u = (const unsigned char*)info;
  int lonOffset = b[4];                 // char 5 bit: 1 = add 100 degrees
  int west      = b[5];                 // char 6 bit: 1 = West

  int lonDeg = u[1] - 28;
  if (lonOffset) lonDeg += 100;
  if (lonDeg >= 180 && lonDeg <= 189)      lonDeg -= 80;
  else if (lonDeg >= 190 && lonDeg <= 199) lonDeg -= 190;
  int lonMin = u[2] - 28; if (lonMin >= 60) lonMin -= 60;
  int lonHun = u[3] - 28;
  p.lon = lonDeg + (lonMin + lonHun / 100.0) / 60.0;
  if (west) p.lon = -p.lon;

  int sp = u[4] - 28, dc = u[5] - 28, se = u[6] - 28;
  int speed  = sp * 10 + dc / 10;
  int course = (dc % 10) * 100 + se;
  if (speed  >= 800) speed  -= 800;
  if (course >= 400) course -= 400;
  p.speedKt = speed;
  p.course  = course;

  p.symCode  = (char)u[7];
  p.symTable = (char)u[8];
  p.valid = (p.lat >= -90 && p.lat <= 90 && p.lon >= -180 && p.lon <= 180);
  return p.valid;
}

// ---------------------------------------------------------------------------
//  Uncompressed: DDMM.hhN/DDDMM.hhW$   (table char sits BETWEEN lat and lon)
// ---------------------------------------------------------------------------
static bool decodeUncompressed(const char* s, AprsPos& p) {
  if (strlen(s) < 19) return false;
  char buf[10];
  memcpy(buf, s, 2); buf[2] = 0; double latDeg = atof(buf);
  memcpy(buf, s + 2, 5); buf[5] = 0; double latMin = atof(buf);
  char ns = s[7];
  p.symTable = s[8];
  memcpy(buf, s + 9, 3); buf[3] = 0; double lonDeg = atof(buf);
  memcpy(buf, s + 12, 5); buf[5] = 0; double lonMin = atof(buf);
  char ew = s[17];
  p.symCode = s[18];
  if ((ns != 'N' && ns != 'S') || (ew != 'E' && ew != 'W')) return false;
  p.lat = latDeg + latMin / 60.0; if (ns == 'S') p.lat = -p.lat;
  p.lon = lonDeg + lonMin / 60.0; if (ew == 'W') p.lon = -p.lon;
  p.course = p.speedKt = -1;
  p.valid = true;
  return true;
}

// ---------------------------------------------------------------------------
//  Compressed: /YYYYXXXX$csT -- base-91, table char FIRST, symbol after the
//  eight position bytes. Distinguished from uncompressed by a non-digit lead.
// ---------------------------------------------------------------------------
static bool decodeCompressed(const char* s, AprsPos& p) {
  if (strlen(s) < 13) return false;
  for (int i = 1; i <= 8; ++i) if (s[i] < '!' || s[i] > '{') return false;
  long y = 0, x = 0;
  for (int i = 1; i <= 4; ++i) y = y * 91 + (s[i] - 33);
  for (int i = 5; i <= 8; ++i) x = x * 91 + (s[i] - 33);
  p.symTable = s[0];
  p.symCode  = s[9];
  p.lat =  90.0 - (double)y / 380926.0;
  p.lon = -180.0 + (double)x / 190463.0;
  p.course = p.speedKt = -1;
  p.valid = (p.lat >= -90 && p.lat <= 90 && p.lon >= -180 && p.lon <= 180);
  return p.valid;
}

// ---------------------------------------------------------------------------
//  Full TNC2 line: SRCCALL>DEST,path:info
//  Returns false for every packet that carries no position (messages, telemetry,
//  status, bulletins) -- the common case on a busy feed, so it must be cheap.
// ---------------------------------------------------------------------------
static bool aprsDecodeLine(const char* line, char* callOut, size_t callCap, AprsPos& p) {
  memset(&p, 0, sizeof(p));
  p.course = p.speedKt = -1;
  const char* gt = strchr(line, '>');
  if (!gt) return false;
  const char* colon = strchr(gt, ':');
  if (!colon) return false;

  size_t cl = (size_t)(gt - line);
  if (cl == 0 || cl >= callCap) return false;
  memcpy(callOut, line, cl); callOut[cl] = 0;

  char dest[16] = {0};
  const char* de = gt + 1;
  size_t dl = 0;
  while (de[dl] && de[dl] != ',' && de[dl] != ':' && dl < sizeof(dest) - 1) { dest[dl] = de[dl]; ++dl; }
  dest[dl] = 0;

  const char* info = colon + 1;
  if (!*info) return false;
  char t = info[0];

  if (t == '`' || t == '\'' || t == 0x1C || t == 0x1D) return decodeMicE(dest, info, p);

  if (t == '!' || t == '=' || t == '/' || t == '@') {
    const char* body = info + 1;
    if (t == '/' || t == '@') {          // 7-byte timestamp precedes the position
      if (strlen(body) < 7) return false;
      body += 7;
    }
    if (body[0] >= '0' && body[0] <= '9') return decodeUncompressed(body, p);
    return decodeCompressed(body, p);
  }
  return false;
}

// ---------------------------------------------------------------------------
//  Vectors. Expected values are aprslib's published output.
// ---------------------------------------------------------------------------
static int failures = 0;

static void check(const char* what, double got, double want, double tol) {
  bool ok = fabs(got - want) <= tol;
  if (!ok) { printf("    FAIL %-10s got %.8f want %.8f\n", what, got, want); ++failures; }
}

static void checkI(const char* what, int got, int want) {
  if (got != want) { printf("    FAIL %-10s got %d want %d\n", what, got, want); ++failures; }
}

static void run(const char* name, const char* line, const char* wantCall,
                double wantLat, double wantLon, int wantCourse, int wantSpeed,
                char wantTable, char wantSym) {
  printf("  %s\n", name);
  char call[16]; AprsPos p;
  if (!aprsDecodeLine(line, call, sizeof(call), p)) {
    printf("    FAIL decode returned false\n"); ++failures; return;
  }
  if (strcmp(call, wantCall)) { printf("    FAIL call got %s want %s\n", call, wantCall); ++failures; }
  check("lat", p.lat, wantLat, 1e-6);
  check("lon", p.lon, wantLon, 1e-6);
  if (wantCourse >= 0) checkI("course", p.course, wantCourse);
  if (wantSpeed  >= 0) checkI("speed",  p.speedKt, wantSpeed);
  if (wantTable) checkI("table", p.symTable, wantTable);
  if (wantSym)   checkI("symbol", p.symCode, wantSym);
}

static void runReject(const char* name, const char* line) {
  printf("  %s\n", name);
  char call[16]; AprsPos p;
  if (aprsDecodeLine(line, call, sizeof(call), p)) {
    printf("    FAIL expected no position, got %.5f,%.5f\n", p.lat, p.lon); ++failures;
  }
}

int main() {
  printf("APRS position decoder -- published vectors\n");

  run("uncompressed, with timestamp",
      "FROMCALL>TOCALL:/092345z4903.50N/07201.75W>Test1234",
      "FROMCALL", 49.05833333333333, -72.02916666666667, -1, -1, '/', '>');

  run("compressed, base-91",
      "M0XER-4>APRS64,TF3RPF,WIDE2*,qAR,TF3SUT-2:!/.(M4I^C,O `DXa/A=040849",
      "M0XER-4", 64.11987367625208, -19.070654142799384, -1, -1, '/', 'O');

  run("mic-e, east + 100-degree offset",
      "FROMCALL>SUSUR1:`CF\"l#![/`\"3z}_ ",
      "FROMCALL", 35.58683333333333, 139.701, 305, 0, '/', '[');

  runReject("status report carries no position", "N0CALL>APRS:>Just a status");
  runReject("message carries no position",       "N0CALL>APRS::N8HM     :hello{01");
  runReject("malformed, no colon",               "N0CALL>APRS");
  runReject("truncated uncompressed",            "N0CALL>APRS:!4903.50N/");

  printf(failures ? "\naprs decoder: FAIL (%d)\n" : "\naprs decoder: PASS\n", failures);
  return failures ? 1 : 0;
}
