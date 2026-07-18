// Orbital/Doppler audit harness: exercises the REAL predict.cpp + gpToTle against
// a fresh TLE; emits machine-readable blocks for the skyfield cross-check.
#include "predict.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static double f(const char* s, int a, int n) {           // fixed-column field
  char b[32]; memcpy(b, s + a, n); b[n] = 0; return atof(b);
}
static double tleEpochToUnix(const char* l1) {
  int yy = (int)f(l1, 18, 2); int year = yy < 57 ? 2000 + yy : 1900 + yy;
  double doy = f(l1, 20, 12);
  struct tm t = {}; t.tm_year = year - 1900; t.tm_mday = 1; t.tm_mon = 0;
  time_t jan1 = timegm(&t);
  return (double)jan1 + (doy - 1.0) * 86400.0;
}
static double sciField(const char* s, int a) {           // TLE " 36258-3" -> 0.36258e-3
  double sign = (s[a] == '-') ? -1.0 : 1.0;
  char mant[8]; memcpy(mant, s + a + 1, 5); mant[5] = 0;
  char exp2[4]; exp2[0] = s[a + 6]; exp2[1] = s[a + 7]; exp2[2] = 0;
  return sign * (atof(mant) / 100000.0) * pow(10.0, atoi(exp2));
}

int main(int argc, char** argv) {
  FILE* fp = fopen("iss.tle", "r");
  char name[64], l1[80], l2[80];
  fgets(name, sizeof(name), fp); fgets(l1, sizeof(l1), fp); fgets(l2, sizeof(l2), fp);
  fclose(fp);
  for (char* p : {name, l1, l2}) { size_t n = strlen(p); while (n && (p[n-1]=='\n'||p[n-1]=='\r'||p[n-1]==' ')) p[--n]=0; }

  SatEntry s = {};
  strncpy(s.name, name, sizeof(s.name)-1);
  s.norad = (uint32_t)f(l1, 2, 5);
  { // intl designator "YYNNNPPP" -> "YYYY-NNNPPP"
    char yy[3] = {l1[9], l1[10], 0}; int y = atoi(yy); int year = y < 57 ? 2000+y : 1900+y;
    snprintf(s.intlDes, sizeof(s.intlDes), "%04d-%c%c%c%c%c", year, l1[11],l1[12],l1[13],l1[14],l1[15]==' '?0:l1[15]);
    for (int i = 0; i < (int)sizeof(s.intlDes); ++i) if (s.intlDes[i]=='\0') break; }
  s.epochUnix  = tleEpochToUnix(l1);
  s.ndot       = (float)f(l1, 33, 10);
  s.nddot      = (float)sciField(l1, 44);
  s.bstar      = (float)sciField(l1, 53);
  s.elsetNum   = (uint16_t)f(l1, 64, 4);
  s.incl       = (float)f(l2, 8, 8);
  s.raan       = (float)f(l2, 17, 8);
  { char eb[10] = "0."; memcpy(eb+2, l2+26, 7); eb[9]=0; s.ecc = (float)atof(eb); }
  s.argp       = (float)f(l2, 34, 8);
  s.ma         = (float)f(l2, 43, 8);
  s.meanMotion = f(l2, 52, 11);
  s.revAtEpoch = (uint32_t)f(l2, 63, 5);

  // ---- A: gpToTle round trip vs the original lines ----
  char o1[72], o2[72];
  bool ok = SatDb::gpToTle(s, o1, o2);
  printf("== GPTOTLE ==\nok=%d\nIN1|%s|\nOU1|%s|\nIN2|%s|\nOU2|%s|\n", ok, l1, o1, l2, o2);

  Observer o; o.lat = 38.90; o.lon = -77.04; o.altM = 20;
  Predictor pred; pred.setSite(o);
  if (!pred.setSat(s)) { printf("setSat FAILED\n"); return 1; }

  time_t t0 = (argc > 1) ? (time_t)atoll(argv[1]) : time(nullptr);
  printf("== SITE ==\nlat=%.4f lon=%.4f altM=%.0f t0=%ld\n", o.lat, o.lon, o.altM, (long)t0);

  // ---- B: passes over 24 h ----
  PassPredict pp[12];
  int np = pred.predictPasses(t0, 0.0f, pp, 12, t0 + 86400);
  printf("== PASSES ==\n");
  for (int i = 0; i < np; ++i)
    printf("P %ld %ld %ld %.2f %.1f %.1f\n", (long)pp[i].aos, (long)pp[i].los,
           (long)pp[i].tca, pp[i].maxEl, pp[i].azAos, pp[i].azLos);

  // ---- C: samples across pass 1 ----
  printf("== TRACK ==\n");
  if (np > 0) for (time_t t = pp[0].aos; t <= pp[0].los; t += 15) {
    LiveLook L = pred.look(t);
    printf("T %ld %.3f %.3f %.3f %.6f %d\n", (long)t, L.az, L.el, L.rangeKm, L.rangeRate, L.sunlit ? 1 : 0);
  }

  // ---- D: Doppler + round-trip hold invariants over pass 1 ----
  printf("== DOPPLER ==\n");
  const uint32_t DL = 435500000u, UL = 145900000u;   // inverting V/U style pair
  double worstOTR = 0, worstFD = 0, worstFU = 0;
  if (np > 0) for (time_t t = pp[0].aos; t <= pp[0].los; t += 15) {
    double rr = pred.rangeRateAt((double)t);
    double beta = rr * 1000.0 / 299792458.0;
    uint32_t rx, tx;
    Predictor::dopplerFreqs(DL, UL, rr, 0, 0, rx, tx);
    // OTR: bird frame -- ground rx corresponds to emitted rx/(1-beta) == DL; heard tx*(1-beta) == UL
    double e1 = fabs((double)rx / (1.0 - beta) - (double)DL);
    double e2 = fabs((double)tx * (1.0 - beta) - (double)UL);
    if (e1 > worstOTR) worstOTR = e1;
    if (e2 > worstOTR) worstOTR = e2;
    // Fixed-downlink hold: simulate inverting transponder K = DL+UL
    uint32_t txH = Predictor::uplinkForFixedDownlink(DL, UL, true, rr, 0, 0);
    double heard = (double)txH * (1.0 - beta);
    double emit  = (double)(DL + UL) - heard;
    double gnd   = emit * (1.0 - beta);
    double eFD = fabs(gnd - (double)DL);
    if (eFD > worstFD) worstFD = eFD;
    // Fixed-uplink hold: park TX at UL
    uint32_t rxH = Predictor::downlinkForFixedUplink(DL, UL, true, rr, 0, 0);
    double heard2 = (double)UL * (1.0 - beta);
    double emit2  = (double)(DL + UL) - heard2;
    double gnd2   = emit2 * (1.0 - beta);
    double eFU = fabs(gnd2 - (double)rxH);
    if (eFU > worstFU) worstFU = eFU;
  }
  printf("worstOTR_Hz=%.3f worstFixedDL_Hz=%.3f worstFixedUL_Hz=%.3f\n", worstOTR, worstFD, worstFU);

  // ---- D2: lookFor vs look, sample-by-sample over pass 1 ----
  printf("== LOOKFOR ==\n");
  if (np > 0) {
    double wAz=0, wEl=0, wRg=0, wRr=0, wLat=0, wLon=0, wAlt=0; int sunMis=0, nn=0;
    for (time_t t = pp[0].aos; t <= pp[0].los; t += 15) {
      LiveLook L = pred.look(t);
      float az, el, rg, rr, la, lo, al; bool su;
      if (!pred.lookFor(s, t, az, el, rg, rr, la, lo, al, su)) continue;
      auto ang = [](double a, double b){ double d = fabs(a-b); if (d>180) d=360-d; return d; };
      if (ang(az,L.az)>wAz) wAz=ang(az,L.az);
      if (fabs(el-L.el)>wEl) wEl=fabs(el-L.el);
      if (fabs(rg-L.rangeKm)>wRg) wRg=fabs(rg-L.rangeKm);
      if (fabs(rr-L.rangeRate)>wRr) wRr=fabs(rr-L.rangeRate);
      if (ang(lo,L.subLon)>wLon) wLon=ang(lo,L.subLon);
      if (fabs(la-L.subLat)>wLat) wLat=fabs(la-L.subLat);
      if (fabs(al-L.satAltKm)>wAlt) wAlt=fabs(al-L.satAltKm);
      if (su != L.sunlit) sunMis++;
      nn++;
    }
    printf("n=%d worst: az=%.4f el=%.4f rng=%.4fkm rr=%.5fkm/s sub=%.4f/%.4f alt=%.3fkm sunMis=%d\n",
           nn, wAz, wEl, wRg, wRr, wLat, wLon, wAlt, sunMis);
  }

  // ---- H: higher-orbit pass finding (Molniya-class + geosynchronous) ----
  // Elements are synthetic but physical; epoch is borrowed from the primary sat so
  // element age matches the regime the rest of the audit runs in. TLE lines are
  // emitted through the audited gpToTle so compare.py feeds skyfield the exact
  // same elements. The GEO case self-calibrates: mean anomaly is stepped until the
  // bird is parked above the site, guaranteeing the always-visible path is the one
  // under test (that path is the 0.9.59 synthetic-pass fallback).
  printf("== HIORBIT ==\n");
  {
    SatEntry m{}; m.norad = 90001;
    strncpy(m.name, "TEST-MOLNIYA", sizeof(m.name) - 1);
    m.epochUnix = s.epochUnix;
    m.incl = 63.43f; m.raan = 40.0f; m.ecc = 0.72f; m.argp = 270.0f;
    m.ma = 10.0f; m.meanMotion = 2.006; m.bstar = 0; m.ndot = 0;
    char m1[72], m2[72];
    if (SatDb::gpToTle(m, m1, m2)) {
      printf("SAT MOLNIYA\nL1|%s|\nL2|%s|\n", m1, m2);
      Predictor pm; pm.setSite(o);
      if (pm.setSat(m)) {
        PassPredict mp[12];
        int nm = pm.predictPasses(t0, 0.0f, mp, 12, t0 + 86400);
        for (int i = 0; i < nm; ++i)
          printf("P %ld %ld %ld %.2f\n", (long)mp[i].aos, (long)mp[i].los,
                 (long)mp[i].tca, mp[i].maxEl);
      }
    }
    SatEntry g{}; g.norad = 90002;
    strncpy(g.name, "TEST-GEO", sizeof(g.name) - 1);
    g.epochUnix = s.epochUnix;
    g.incl = 0.05f; g.raan = 100.0f; g.ecc = 0.0002f; g.argp = 0.0f;
    g.ma = 0.0f; g.meanMotion = 1.0027; g.bstar = 0; g.ndot = 0;
    Predictor pg; pg.setSite(o);
    for (int tries = 0; tries < 8; ++tries) {       // park it above the site
      if (!pg.setSat(g)) break;
      LiveLook L = pg.look(t0);
      if (L.el > 10.0) break;
      g.ma += 45.0f; if (g.ma >= 360.0f) g.ma -= 360.0f;
      g.elsetNum++;      // defeat Sgp4::init()'s identical-line-1 cache (see temeStateAt)
    }
    char g1[72], g2[72];
    if (SatDb::gpToTle(g, g1, g2) && pg.setSat(g)) {
      printf("SAT GEO\nL1|%s|\nL2|%s|\n", g1, g2);
      PassPredict gp2[4];
      int ng = pg.predictPasses(t0, 0.0f, gp2, 4, t0 + 86400);
      for (int i = 0; i < ng; ++i)
        printf("P %ld %ld %ld %.2f\n", (long)gp2[i].aos, (long)gp2[i].los,
               (long)gp2[i].tca, gp2[i].maxEl);
    }
  }

  // ---- E: eclipse transitions over 24 h (60 s grid) ----
  printf("== SUNLIT ==\n");
  int prev = -1;
  for (time_t t = t0; t <= t0 + 86400; t += 60) {
    int su = pred.sunlitAt(t) ? 1 : 0;
    if (su != prev) { printf("S %ld %d\n", (long)t, su); prev = su; }
  }

  // ---- F: beta angle now ----
  printf("== BETA ==\nbeta=%.3f\n", pred.betaAngleDeg(t0, s.incl, s.raan));
  return 0;
}
