// EXTRACTED VERBATIM from src/satdb.cpp at build time
#include "satdb.h"
#include <string.h>
#include <math.h>
#include <time.h>
#include <stdio.h>

static void encCatalog(uint32_t n, char out[6]) {
  if (n <= 99999u) { snprintf(out, 6, "%05lu", (unsigned long)n); return; }
  static const char* A = "ABCDEFGHJKLMNPQRSTUVWXYZ";   // skips I and O
  int hi = (int)(n / 10000), lo = (int)(n % 10000);
  if (hi >= 10 && hi <= 33) snprintf(out, 6, "%c%04d", A[hi - 10], lo);
  else snprintf(out, 6, "%05lu", (unsigned long)(n % 100000u));  // >339999: low 5 (TLE cosmetic only)
}

static void encNdot(double v, char out[12]) {
  char s = (v < 0) ? '-' : ' ';
  long m = llround(fabs(v) * 1e8);
  if (m > 99999999L) m = 99999999L;
  snprintf(out, 12, "%c.%08ld", s, m);
}

static void encExp(double v, char out[10]) {
  char s = (v < 0) ? '-' : ' ';
  double a = fabs(v);
  int e = 0;
  if (a != 0.0) {
    while (a >= 1.0) { a /= 10.0; e++; }
    while (a < 0.1)  { a *= 10.0; e--; }
  }
  long mant = llround(a * 1e5);
  if (mant >= 100000) { mant = 10000; e++; }
  if (e > 9)  e = 9;  if (e < -9) e = -9;
  snprintf(out, 10, "%c%05ld%c%01d", s, mant, (e < 0 ? '-' : '+'), (int)labs(e));
}

static void putAt(char* line, int col1, const char* s) {   // col1 is 1-indexed
  int i = col1 - 1;
  for (int k = 0; s[k]; k++) line[i + k] = s[k];
}

static int tleChecksum(const char* line) {
  int s = 0;
  for (int i = 0; i < 68 && line[i]; i++) {
    char c = line[i];
    if (c >= '0' && c <= '9') s += c - '0';
    else if (c == '-')        s += 1;
  }
  return s % 10;
}

bool SatDb::gpToTle(const SatEntry& s, char l1[72], char l2[72]) {
  if (s.meanMotion <= 0 || s.epochUnix <= 0) return false;
  memset(l1, ' ', 69); l1[69] = 0;
  memset(l2, ' ', 69); l2[69] = 0;

  char cat[6]; encCatalog(s.norad, cat);

  // International designator OBJECT_ID "YYYY-NNNP[PP]" -> "YYNNNPPP".
  char intl[9] = "        ";
  if (s.intlDes[0] && strlen(s.intlDes) >= 8 && s.intlDes[4] == '-') {
    intl[0] = s.intlDes[2]; intl[1] = s.intlDes[3];
    intl[2] = s.intlDes[5]; intl[3] = s.intlDes[6]; intl[4] = s.intlDes[7];
    int k = 5;
    for (size_t j = 8; j < strlen(s.intlDes) && k < 8; ++j) intl[k++] = s.intlDes[j];
  }

  // Epoch -> YYDDD.DDDDDDDD.
  time_t ip = (time_t)floor(s.epochUnix);
  double frac = s.epochUnix - (double)ip;
  struct tm tmv; gmtime_r(&ip, &tmv);
  double day = (tmv.tm_yday + 1)
             + (tmv.tm_hour * 3600 + tmv.tm_min * 60 + tmv.tm_sec + frac) / 86400.0;
  char epoch[16];
  snprintf(epoch, sizeof(epoch), "%02d%012.8f", tmv.tm_year % 100, day);

  char nd[12];  encNdot(s.ndot, nd);
  char ndd[10]; encExp(s.nddot, ndd);
  char bs[10];  encExp(s.bstar, bs);

  // --- line 1 ---
  l1[0] = '1'; putAt(l1, 3, cat); l1[7] = 'U';
  putAt(l1, 10, intl);
  putAt(l1, 19, epoch);
  putAt(l1, 34, nd);
  putAt(l1, 45, ndd);
  putAt(l1, 54, bs);
  l1[62] = '0';                                   // ephemeris type
  char es[6]; snprintf(es, sizeof(es), "%4u", (unsigned)(s.elsetNum % 10000));
  putAt(l1, 65, es);
  l1[68] = '0' + tleChecksum(l1);

  // --- line 2 ---
  // Guard every fixed-width field so a transient out-of-range element (e.g. a wild
  // Gauss-Newton iterate in the state-vector fitter) can never overflow its TLE column and
  // corrupt the line -- a malformed TLE can put SGP4's init/propagator into a spin. Angles
  // are wrapped into [0,360); mean-motion is clamped to the 2-digit field; non-finite -> 0.
  auto fin  = [](double x){ return (x == x && x - x == 0.0) ? x : 0.0; };   // NaN/Inf -> 0
  auto wrap = [&](double a){ a = fin(a); a = fmod(a, 360.0); if (a < 0) a += 360.0; return a; };
  double inclW = wrap((double)s.incl), raanW = wrap((double)s.raan);
  double argpW = wrap((double)s.argp), maW = wrap((double)s.ma);
  double mmW   = fin(s.meanMotion); if (mmW < 0.0) mmW = 0.0; if (mmW > 99.99999999) mmW = 99.99999999;
  char buf[16];
  l2[0] = '2'; putAt(l2, 3, cat);
  snprintf(buf, sizeof(buf), "%8.4f", inclW); putAt(l2, 9,  buf);
  snprintf(buf, sizeof(buf), "%8.4f", raanW); putAt(l2, 18, buf);
  long e7 = llround((double)s.ecc * 1e7); if (e7 < 0) e7 = 0; if (e7 > 9999999L) e7 = 9999999L;
  snprintf(buf, sizeof(buf), "%07ld", e7);     putAt(l2, 27, buf);
  snprintf(buf, sizeof(buf), "%8.4f", argpW); putAt(l2, 35, buf);
  snprintf(buf, sizeof(buf), "%8.4f", maW);   putAt(l2, 44, buf);
  snprintf(buf, sizeof(buf), "%11.8f", mmW);  putAt(l2, 53, buf);
  snprintf(buf, sizeof(buf), "%5lu", (unsigned long)(s.revAtEpoch % 100000u));
  putAt(l2, 64, buf);
  l2[68] = '0' + tleChecksum(l2);
  return true;
}
