// Host harness for the orbital-zone membership math, extracted live from src/app.cpp.
// Pins the SAA geographic ellipse and the IGRF-14 Van Allen belt gates to known points
// and orbits. The belt tests use McIlwain (L, B/B0) from a real field-line trace, so the
// cases below include the ones that motivated the model change: a high-latitude LEO sits
// ON a belt shell but far off its magnetic equator, and must NOT read as in the belt.
// Field-model accuracy itself is checked in tools/host_geomag against ppigrf.
#include <cstdio>
#include <cmath>
#include <cstdint>
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
  // --- Inner belt (zone 3): shell L 1.2-2.5 AND B/B0 <= 3 ---
  check("inner-belt eq 3000km",     zoneContains(3, 0, 20, 3000, true), true);
  check("ISS eq Pacific not inner", zoneContains(3, 0,-150, 420, true), false); // L~1.07
  check("ISS 51.6N 420km not inner",zoneContains(3,51.6, 20, 420, true), false);// off-equator
  // The SAA IS the inner belt reaching down to LEO -- with a real field model that
  // falls out of the same test instead of needing the drawn ellipse.
  check("ISS in the SAA is inner",  zoneContains(3,-27,-53,  420, true), true);
  // --- Outer belt (zone 4): shell L 3-7 AND B/B0 <= 3 ---
  check("QO-100 GEO outer",         zoneContains(4,0.4, 26,35786,true), true);
  check("outer-belt eq 20000km",    zoneContains(4, 0, 20,20000, true), true);
  check("LEO not outer",            zoneContains(4, 0, 20,  420, true), false);
  // THE REGRESSION THIS MODEL EXISTS FOR: a 1200 km polar satellite crosses the
  // L=5.5 shell at 65N, but at 184x its equatorial field -- out on the belt's
  // horn, not in the belt. The old altitude-floor model reported these as transits.
  check("polar 1200km 65N not outer", zoneContains(4, 65, 20, 1200, true), false);
  check("polar 1200km 50N not inner", zoneContains(3, 50, 20, 1200, true), false);
  check("polar 1200km 70N not outer", zoneContains(4, 70,-40, 1200, true), false);
  check("polar 1400km 60S not outer", zoneContains(4,-60,140, 1400, true), false);

  // --- L-shell spot values ---
  double Lgeo = lShellAt(0.4, 26, 35786);
  if (Lgeo < 6.3 || Lgeo > 6.9) { fails++; printf("  FAIL QO-100 L=%.2f (want 6.3-6.9)\n", Lgeo); }
  // Traced shell vs the analytic dipole: same ballpark, and B/B0 separates the cases.
  { ShellInfo a = shellAt(65, 20, 1200), b = shellAt(0, 20, 20000);
    if (!(a.bRatio > 50.0f))  { fails++; printf("  FAIL polar B/B0=%.1f (want >50)\n", a.bRatio); }
    if (!(b.bRatio < 1.5f))   { fails++; printf("  FAIL MEO eq B/B0=%.2f (want <1.5)\n", b.bRatio); }
    if (!(b.shellL > 3.5f && b.shellL < 4.8f))
      { fails++; printf("  FAIL MEO shell L=%.2f (want 3.5-4.8)\n", b.shellL); } }
  double Leq = lShellAt(0, 0, 420);
  if (Leq < 1.0 || Leq > 1.2) { fails++; printf("  FAIL ISS-eq L=%.2f (want 1.0-1.2)\n", Leq); }

  // --- Drift: a fixed point at the reference centre stays in as the ellipse creeps west ---
  g_testYears = 10.0;
  check("SAA centre still in +10y",  zoneContains(0,-27,-53,600,true), true);

  printf("zones verify: %s\n", fails ? "FAIL" : "PASS");
  return fails ? 1 : 0;
}
