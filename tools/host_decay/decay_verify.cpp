// Host harness for the orbital-decay estimator (src/app.cpp), extracted live.
//
// WHAT IS PINNED HERE
//  * Twelve objects that really re-entered, each with a real element set from a
//    known number of days before re-entry. The estimate must land within a factor
//    of two of the truth, and the MEDIAN across them must be close to 1.0. Under
//    the 0.9.67 model these scored ~0.21x (i.e. it predicted a fifth of the true
//    remaining life), so this file is what stops that regressing.
//  * Eccentric orbits, which no re-entry case can cover (eccentric objects rarely
//    re-enter): a GTO and a Molniya must NOT read as imminent. Before the
//    King-Hele eccentricity factor a GTO read ~43 days.
//  * The n-dot anchor must be preferred where usable, and B* used otherwise.
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>
#include <vector>
#include <string>

// --- minimal stand-ins for the firmware types the estimator touches ----------
struct SatEntry { float bstar=0, meanMotion=0, ecc=0, ndot=0; };
enum { SOLAR_LOW, SOLAR_MEAN, SOLAR_HIGH, SOLAR_AUTO };
// solarDensityScale() comes from the extracted region -- it ships in app.cpp too.
#include "decay_region.inc"

static int fails = 0;

struct Case { const char* name; double bstar, mm, ecc, ndot, actualDays; };
// Real re-entries: element set at T-minus N days, actual days remaining.
static const Case REENTRY[] = {
  { "CZ-3B R/B",      1.29800000e-03, 12.45636427, 0.1752679, 3.44042730e-01,  2.54 },
  { "METEOR PRIRODA", 6.86360730e-04, 16.25368256, 0.0005836, 1.98483000e-02,  2.86 },
  { "TIANHUI 1-02",   4.34511870e-04, 16.28153365, 0.0001865, 1.59206400e-02,  2.89 },
  { "YAOGAN 13",      1.13530000e-03, 16.19279489, 0.0003584, 2.01804100e-02,  2.96 },
  { "GENESIS 2",      7.83300000e-04, 16.21664110, 0.0019706, 1.80354500e-02,  3.03 },
  { "VOLGA R/B",      5.23260000e-04, 16.18988852, 0.0009445, 9.16600000e-03,  7.04 },
  { "COSMOS 1455",    7.59175170e-04, 16.11245015, 0.0008049, 7.64301000e-03,  7.52 },
  { "CZ-2C R/B",      5.22700000e-04, 16.05135243, 0.0011017, 3.85054000e-03, 13.17 },
  { "CZ-2C DEB",      1.90790000e-03, 15.88319335, 0.0012538, 5.24003000e-03, 13.74 },
  { "CZ-4B R/B",      1.27670000e-03, 15.98021380, 0.0025511, 5.98461000e-03, 13.91 },
  { "ZY 3",           1.06810000e-03, 16.02015450, 0.0003349, 6.05911000e-03, 14.00 },
  { "CZ-2C R/B(30d)", 9.25741920e-04, 15.90042907, 0.0013408, 2.92678000e-03, 30.22 },
};

int main() {
  printf("--- real re-entries: predicted vs actual remaining life ---\n");
  std::vector<double> ratios;
  for (const Case& c : REENTRY) {
    SatEntry s; s.bstar=(float)c.bstar; s.meanMotion=(float)c.mm;
    s.ecc=(float)c.ecc; s.ndot=(float)c.ndot;
    uint8_t src = 0;
    double p = estimateDecayDays(s, 1.0, &src);
    double r = (p > 0) ? p / c.actualDays : -1;
    bool ok = (r >= 0.5 && r <= 2.0);
    if (!ok) fails++;
    ratios.push_back(r);
    printf("%s %-16s pred %6.2f d  actual %6.2f d  ratio %5.2f  [%s]\n",
           ok ? "ok  " : "FAIL", c.name, p, c.actualDays, r,
           src == 1 ? "n-dot" : src == 2 ? "B*" : "none");
  }
  std::sort(ratios.begin(), ratios.end());
  double med = ratios[ratios.size()/2];
  bool medok = (med > 0.75 && med < 1.35);
  if (!medok) fails++;
  printf("%s median ratio %.2f (want 0.75-1.35; the 0.9.67 model scored ~0.21)\n",
         medok ? "ok  " : "FAIL", med);

  // --- eccentric orbits: the King-Hele factor is the whole story here --------
  printf("\n--- eccentric orbits (no re-entry data exists; theory-pinned) ---\n");
  struct E { const char* name; double bstar, mm, ecc; double minDays; } EC[] = {
    { "GTO 300x35786",   1.0e-4,  2.25, 0.7300, 365.0 },
    { "Molniya",         8.0e-5,  2.01, 0.7400, 365.0 },
    { "HEO 400x20000",   2.0e-4,  4.00, 0.5000, 365.0 },
  };
  for (const E& e : EC) {
    SatEntry s; s.bstar=(float)e.bstar; s.meanMotion=(float)e.mm; s.ecc=(float)e.ecc; s.ndot=0;
    uint8_t src=0;
    double p = estimateDecayDays(s, 1.0, &src);
    bool ok = (p < 0) || (p >= e.minDays);
    if (!ok) fails++;
    printf("%s %-16s %s  (must be >= %.0f d; before the eccentricity factor a GTO read ~43 d)\n",
           ok ? "ok  " : "FAIL", e.name,
           p >= 36500 ? "stable" : (p < 0 ? "n/a" : (std::to_string((long)p) + " d").c_str()),
           e.minDays);
  }

  // --- anchor selection ------------------------------------------------------
  printf("\n--- anchor selection ---\n");
  { SatEntry s; s.bstar=2.03e-4f; s.meanMotion=15.50f; s.ecc=0.0007f; s.ndot=1.2e-4f;
    uint8_t src=0; double p=estimateDecayDays(s,1.0,&src);
    bool ok = (src==1) && p>200 && p<3000;
    if(!ok) fails++;
    printf("%s ISS-class with usable n-dot -> %s, %.0f d (want n-dot, 200-3000 d)\n",
           ok?"ok  ":"FAIL", src==1?"n-dot":"B*", p); }
  { SatEntry s; s.bstar=5.0e-4f; s.meanMotion=15.90f; s.ecc=0.002f; s.ndot=0.0f;
    uint8_t src=0; double p=estimateDecayDays(s,1.0,&src);
    bool ok = (src==2) && p>0;
    if(!ok) fails++;
    printf("%s no n-dot -> %s, %.0f d (want B* fallback)\n",
           ok?"ok  ":"FAIL", src==1?"n-dot":src==2?"B*":"none", p); }
  { SatEntry s; s.bstar=5.0e-4f; s.meanMotion=15.90f; s.ecc=0.002f; s.ndot=-3.0e-4f;
    uint8_t src=0; estimateDecayDays(s,1.0,&src);
    bool ok = (src==2);
    if(!ok) fails++;
    printf("%s negative n-dot (rising/maneuvered) -> %s (want B* fallback)\n",
           ok?"ok  ":"FAIL", src==1?"n-dot":src==2?"B*":"none"); }
  { // The solar scale must CANCEL out of an n-dot anchored estimate.
    SatEntry s; s.bstar=2.03e-4f; s.meanMotion=15.50f; s.ecc=0.0007f; s.ndot=1.2e-4f;
    double lo=estimateDecayDays(s,solarDensityScale(SOLAR_LOW));
    double hi=estimateDecayDays(s,solarDensityScale(SOLAR_HIGH));
    bool ok = fabs(lo-hi) < 0.02*std::max(lo,hi);
    if(!ok) fails++;
    printf("%s solar scale cancels when anchored (%.0f d vs %.0f d)\n",
           ok?"ok  ":"FAIL", lo, hi); }

  printf(fails ? "\n%d FAILURE(S)\n" : "\nall decay-model cases pass\n", fails);
  return fails ? 1 : 0;
}
