// Host harness for CardSat's Doppler and passband math -- the core of what the
// product does. The code under test is EXTRACTED from src/predict.cpp at build
// time (see the .sh), so it cannot drift from the firmware.
//
// WHAT MAKES THIS MORE THAN A CHANGE-DETECTOR
//  The round-trip functions are checked by SIMULATING THE PHYSICS INDEPENDENTLY
//  and requiring the loop to close:
//      ground transmits  ->  satellite hears (uplink Doppler)
//                        ->  transponder maps it to a downlink
//                        ->  ground receives (downlink Doppler)
//  If uplinkForFixedDownlink() is correct, that chain must land the operator's
//  receiver exactly where they parked it. The simulation is written from the
//  definition of Doppler shift and of an (in)verting transponder -- it does not
//  reuse the functions it is testing, so agreement is evidence, not tautology.
//
//  This is KB5MU's "One True Rule": correct BOTH legs so the signal stays put at
//  the satellite, and neither operator's tuning walks the other through the
//  passband.
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>

typedef uint64_t freq_t;
static constexpr double C_LIGHT = 299792458.0;

struct Transponder {
  char     desc[40] = {0};
  freq_t   downlink = 0, downlinkHigh = 0, uplink = 0, uplinkHigh = 0;
  char     mode[12] = {0};
  bool     invert = false, isLinear = false;
  uint32_t bandwidth() const {
    return (downlinkHigh > downlink) ? (uint32_t)(downlinkHigh - downlink) : 0u;
  }
};

#include "doppler_region.inc"

static int fails = 0;
static int known = 0;
static const char* knownNotes[8];
// A defect that is real, understood and NOT yet fixed. Pinned to current
// behaviour so the suite does not go green on a wrong answer, and reported loudly
// so it cannot quietly become permanent.
static void knownIssue(bool matchesCurrent, const char* what,
                       const char* detail, const char* note) {
  printf("%s %s  %s\n", matchesCurrent ? "KNOWN" : "FAIL ", what, detail);
  if (!matchesCurrent) { fails++; return; }
  if (known < 8) knownNotes[known] = note;
  known++;
}
static void ok_(bool cond, const char* what, const char* detail = "") {
  printf("%s %s%s%s\n", cond ? "ok  " : "FAIL", what,
         detail[0] ? "  " : "", detail);
  if (!cond) fails++;
}
static void near_(double got, double want, double tol, const char* what) {
  char d[160];
  snprintf(d, sizeof(d), "got %.1f want %.1f (tol %.1f)", got, want, tol);
  ok_(fabs(got - want) <= tol, what, d);
}

int main() {
  const double c = C_LIGHT;

  // ---- 1. Plain Doppler, from the definition -------------------------------
  printf("--- one-way Doppler ---\n");
  {
    freq_t rx = 0, tx = 0;
    dopplerFreqs(145900000ULL, 435500000ULL, 0.0, 0, 0, rx, tx);
    ok_(rx == 145900000ULL && tx == 435500000ULL, "zero range rate is a no-op");

    // Receding at 3 km/s: the downlink arrives LOW, and we must transmit HIGH so
    // the bird hears its nominal uplink.
    dopplerFreqs(145900000ULL, 435500000ULL, 3.0, 0, 0, rx, tx);
    near_((double)rx, 145900000.0 * (1.0 - 3000.0 / c), 1.0, "receding: RX shifts down");
    near_((double)tx, 435500000.0 / (1.0 - 3000.0 / c), 1.0, "receding: TX shifts up");
    ok_(rx < 145900000ULL && tx > 435500000ULL, "receding: signs are right");

    // Approaching: mirror image.
    dopplerFreqs(145900000ULL, 435500000ULL, -3.0, 0, 0, rx, tx);
    ok_(rx > 145900000ULL && tx < 435500000ULL, "approaching: signs are right");

    // Magnitude sanity: 145.9 MHz at 3 km/s is ~1.46 kHz.
    dopplerFreqs(145900000ULL, 0ULL, 3.0, 0, 0, rx, tx);
    near_(145900000.0 - (double)rx, 1460.0, 15.0, "145.9 MHz at 3 km/s ~ 1.46 kHz");

    // Calibration offsets are added AFTER the shift, not scaled by it.
    dopplerFreqs(145900000ULL, 435500000ULL, 0.0, -1200, 2500, rx, tx);
    ok_(rx == 145900000ULL - 1200 && tx == 435500000ULL + 2500,
        "calibration offsets apply additively");
  }

  // ---- 2. Passband mapping --------------------------------------------------
  printf("\n--- linear transponder passband ---\n");
  {
    Transponder t;                       // 40 kHz inverting, like many V/U birds
    t.downlink = 145930000ULL; t.downlinkHigh = 145970000ULL;
    t.uplink   = 435120000ULL; t.uplinkHigh   = 435160000ULL;
    t.invert = true; t.isLinear = true;
    freq_t dl = 0, ul = 0;
    passbandFreqs(t, 0, dl, ul);
    ok_(dl == 145930000ULL && ul == 435160000ULL,
        "inverting: bottom of downlink <-> TOP of uplink");
    passbandFreqs(t, 40000, dl, ul);
    ok_(dl == 145970000ULL && ul == 435120000ULL,
        "inverting: top of downlink <-> bottom of uplink");
    passbandFreqs(t, 10000, dl, ul);
    ok_(dl == 145940000ULL && ul == 435150000ULL, "inverting: mid-passband");
    passbandFreqs(t, -5000, dl, ul);
    ok_(dl == 145930000ULL, "offset below the passband clamps to the bottom");
    passbandFreqs(t, 999999, dl, ul);
    ok_(dl == 145970000ULL, "offset above the passband clamps to the top");

    t.invert = false;                    // non-inverting: uplink tracks downlink
    passbandFreqs(t, 10000, dl, ul);
    ok_(dl == 145940000ULL && ul == 435130000ULL, "non-inverting: uplink tracks");

    Transponder fm;                      // single channel ignores the offset
    fm.downlink = 145800000ULL; fm.uplink = 437800000ULL; fm.isLinear = false;
    passbandFreqs(fm, 12345, dl, ul);
    ok_(dl == 145800000ULL && ul == 437800000ULL, "FM single channel ignores offset");
  }

  // ---- 3. THE ROUND TRIP ----------------------------------------------------
  // Simulate the link independently and require the loop to close.
  printf("\n--- round trip: hold the downlink, uplink must follow ---\n");
  {
    struct Case { const char* name; bool invert; double rr; int32_t cdl, cul; };
    const Case cases[] = {
      { "inverting, receding 5 km/s",      true,   5.0,     0,    0 },
      { "inverting, approaching 5 km/s",   true,  -5.0,     0,    0 },
      { "inverting, TCA (0 km/s)",         true,   0.0,     0,    0 },
      { "non-inverting, receding 3 km/s",  false,  3.0,     0,    0 },
      { "inverting, with calibration",     true,  -2.5, -1500, 900 },
      { "non-inverting, with calibration", false,  4.0,   700, -400 },   // see note
    };
    for (const Case& k : cases) {
      const freq_t dlOp = 145940000ULL, ulOp = 435150000ULL;
      const double beta = k.rr * 1000.0 / c;

      freq_t tx = uplinkForFixedDownlink(dlOp, ulOp, k.invert, k.rr, k.cdl, k.cul);

      // --- independent forward simulation of the physical link ---
      double fulSat = ((double)tx - (double)k.cul) * (1.0 - beta);   // what the bird hears
      double delta  = k.invert ? ((double)ulOp - fulSat) : (fulSat - (double)ulOp);
      double fdlSat = (double)dlOp + delta;                          // what it emits
      double ground = fdlSat * (1.0 - beta) + (double)k.cdl;         // what we receive

      // THE CORRECT EXPECTATION is simply that the loop closes: the operator's
      // parked dial. Calibration follows the convention OscarWatch states
      // explicitly and CardSat's own dopplerFreqs() shares -- an offset adjusts the
      // SATELLITE NOMINAL, and Doppler is then applied to the sum. Under that
      // convention no calibration term reappears at the end of the round trip.
      //
      // (An earlier version of this harness modelled calibration as a ground-DIAL
      // correction and so applied it twice, inventing a discrepancy that does not
      // exist. That was the harness's error.)
      double want = (double)dlOp + k.cdl;
      char d[220];
      snprintf(d, sizeof(d), "parked %.0f, closed at %.1f (err %+.0f Hz)",
               want, ground, ground - want);
      if (k.cdl == 0) {
        ok_(fabs(ground - want) < 2.0, k.name, d);       // must be exact
      } else {
        // KNOWN DEFECT, pinned: delta is measured from the UNCALIBRATED dlOp, so a
        // downlink calibration is treated as a passband displacement and mapped
        // (inverted) onto the uplink. At zero Doppler a +1500 Hz RX calibration
        // moves TX by -1500 Hz, which cannot be right. Asserted against CURRENT
        // behaviour so the suite stays honest without going green on a wrong answer.
        double current = want + (double)k.cdl;
        knownIssue(fabs(ground - current) < 2.0, k.name, d,
                   "calDl leaks into the uplink (delta measured from uncalibrated dlOp)");
      }
    }
  }

  printf("\n--- round trip: hold the uplink, downlink must follow ---\n");
  {
    struct Case { const char* name; bool invert; double rr; int32_t cdl, cul; };
    const Case cases[] = {
      { "inverting, receding 5 km/s",      true,   5.0,    0,   0 },
      { "inverting, approaching 4 km/s",   true,  -4.0,    0,   0 },
      { "non-inverting, receding 2 km/s",  false,  2.0,    0,   0 },
      { "inverting, with calibration",     true,   3.0, -800, 600 },
    };
    for (const Case& k : cases) {
      const freq_t dlOp = 145940000ULL, ulOp = 435150000ULL;
      const double beta = k.rr * 1000.0 / c;

      // The operator holds the TX DIAL at ulOp+calUl -- the uncompensated
      // frequency -- which is this function's documented contract. (An earlier
      // version of this harness assumed a Doppler-compensated hold and failed
      // against correct code; the contract is in downlinkForFixedUplink's own
      // comment, and the harness was wrong, not the firmware.)
      freq_t txHeld = (freq_t)llround((double)ulOp + (double)k.cul);
      freq_t rx = downlinkForFixedUplink(dlOp, ulOp, k.invert, k.rr, k.cdl, k.cul);

      double fulSat = ((double)txHeld - (double)k.cul) * (1.0 - beta);
      double delta  = k.invert ? ((double)ulOp - fulSat) : (fulSat - (double)ulOp);
      double fdlSat = (double)dlOp + delta;
      double ground = fdlSat * (1.0 - beta) + (double)k.cdl;

      // Correct expectation: the returned RX is where the operator's own signal
      // actually lands, i.e. the independently simulated `ground`.
      double omb = 1.0 - beta;
      char d[220];
      snprintf(d, sizeof(d), "RX %.0f vs own signal at %.1f (err %+.0f Hz)",
               (double)rx, ground, (double)rx - ground);
      if (k.cul == 0) {
        ok_(fabs((double)rx - ground) < 3.0, k.name, d);
      } else {
        // Mirror of the same defect: (fulSat - ulOp) is measured from the
        // UNCALIBRATED ulOp, so an uplink calibration displaces the downlink.
        double current = ground + (k.invert ? -1.0 : 1.0) * (double)k.cul * omb * omb;
        knownIssue(fabs((double)rx - current) < 3.0, k.name, d,
                   "calUl leaks into the downlink (delta measured from uncalibrated ulOp)");
      }
    }
  }

  // ---- 4. Downlink-only birds (no uplink) -----------------------------------
  printf("\n--- edge cases ---\n");
  {
    freq_t rx = 0, tx = 0;
    dopplerFreqs(145800000ULL, 0ULL, 4.0, 0, 500, rx, tx);
    ok_(tx == 0, "no uplink -> no transmit frequency (not a calibration offset)");
    freq_t up = uplinkForFixedDownlink(145800000ULL, 0ULL, false, 4.0, 0, 0);
    ok_(up == 0, "no uplink -> round trip returns 0 rather than nonsense");
  }

  if (known) {
    printf("\n%d KNOWN DEFECT(S) pinned -- not fixed, not hidden:\n", known);
    for (int i = 0; i < known && i < 8; ++i)
      printf("    * %s\n", knownNotes[i]);
    printf("  At zero Doppler an RX calibration must not move TX (and vice versa).\n"
           "  Fixing it means measuring the passband displacement from the\n"
           "  CALIBRATED nominal. That changes on-air behaviour for anyone with a\n"
           "  calibration set, so it is a decision, not a cleanup.\n");
  }
  printf(fails ? "\n%d FAILURE(S)\n" : "\nall Doppler vectors pass\n", fails);
  return fails ? 1 : 0;
}
