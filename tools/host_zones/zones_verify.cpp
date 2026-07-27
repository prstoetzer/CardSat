// Host harness for the orbital-zone membership math, extracted live from src/app.cpp.
// Pins the SAA geographic ellipse and the centred-dipole L-shell / belt gates to known
// points and orbits. A regression in the field model or the boundaries fails here.
#include <cstdio>
#include <cmath>
#include "zones_region.inc"

static int fails = 0;
static void check(const char* name, bool got, bool want) {
  if (got != want) { fails++; printf("  FAIL %-34s got %-3s want %-3s\n",
                                     name, got?"IN":"OUT", want?"IN":"OUT"); }
}
int main() {
  // --- SAA (zone 0), east-positive longitude, drift year 0 (2025.0) ---
  g_testYears = 0.0;
  check("SAA center (-27,-53)",     zoneContains(0,-27,-53,600,true),  true);
  check("SAA Brazil (-20,-45)",     zoneContains(0,-20,-45,600,true),  true);
  check("SAA mid-Atlantic (-30,-10)",zoneContains(0,-30,-10,600,true), true);
  check("Gulf of Guinea (0,0)",     zoneContains(0,  0,  0,600,true),  false);
  check("New York (40,-74)",        zoneContains(0, 40,-74,600,true),  false);
  check("Tokyo (35,139)",           zoneContains(0, 35,139,600,true),  false);
  // --- Eclipse (zone 1) ---
  check("eclipse when !sunlit",     zoneContains(1, 0, 0, 600, false), true);
  check("no eclipse when sunlit",   zoneContains(1, 0, 0, 600, true),  false);
  // --- Polar (zone 2) ---
  check("polar 72N",                zoneContains(2, 72, 0, 600, true), true);
  check("polar -80",                zoneContains(2,-80, 0, 600, true), true);
  check("not polar 45N",            zoneContains(2, 45, 0, 600, true), false);
  // --- Inner belt (zone 3): L in 1.2-2.5 AND alt>=1000 ---
  check("inner-belt eq 3000km",     zoneContains(3, 0, 0, 3000, true), true);
  check("ISS eq 420km not inner",   zoneContains(3, 0, 0,  420, true), false);  // alt gate
  check("ISS 51.6N 420km not inner",zoneContains(3,51.6,0, 420, true), false);  // alt gate (L~3)
  // --- Outer belt (zone 4): L in 3-7 AND alt>=1000 ---
  check("QO-100 GEO outer",         zoneContains(4,0.4,26,35786,true), true);
  check("outer-belt eq 20000km",    zoneContains(4, 0, 0,20000, true), true);
  check("LEO not outer",            zoneContains(4, 0, 0,  420, true), false);

  // --- L-shell spot values ---
  double Lgeo = lShellAt(0.4, 26, 35786);
  if (Lgeo < 6.3 || Lgeo > 6.9) { fails++; printf("  FAIL QO-100 L=%.2f (want 6.3-6.9)\n", Lgeo); }
  double Leq = lShellAt(0, 0, 420);
  if (Leq < 1.0 || Leq > 1.2) { fails++; printf("  FAIL ISS-eq L=%.2f (want 1.0-1.2)\n", Leq); }

  // --- Drift: a fixed point at the reference centre stays in as the ellipse creeps west ---
  g_testYears = 10.0;
  check("SAA centre still in +10y",  zoneContains(0,-27,-53,600,true), true);

  printf("zones verify: %s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
