// AUTO-EXTRACTED from src/app.cpp by extract_decay.py -- do not edit.
static double expAtmosphere(double hkm) {
  static const double B[][3] = {
    {100,5.297e-7,5.877},{110,9.661e-8,7.263},{120,2.438e-8,9.473},
    {130,8.484e-9,12.636},{140,3.845e-9,16.149},{150,2.070e-9,22.523},
    {180,5.464e-10,29.740},{200,2.789e-10,37.105},{250,7.248e-11,45.546},
    {300,2.418e-11,53.628},{350,9.518e-12,53.298},{400,3.725e-12,58.515},
    {450,1.585e-12,60.828},{500,6.967e-13,63.822},{600,1.454e-13,71.835},
    {700,3.614e-14,88.667},{800,1.170e-14,124.64},{900,5.245e-15,181.05},
    {1000,3.019e-15,268.00}
  };
  const int N = (int)(sizeof(B) / sizeof(B[0]));
  if (hkm >= 1100) return 0.0;            // above the table -> treat as drag-free
  if (hkm < 100)  hkm = 100;
  int i = 0; for (; i < N - 1; ++i) if (hkm < B[i + 1][0]) break;
  return B[i][1] * exp(-(hkm - B[i][0]) / B[i][2]);
}

// Density scale factors for the assumed solar-activity level. Thermospheric
// density at LEO altitudes varies ~an order of magnitude over the solar cycle;
// these bracket the static table (tuned to roughly cycle-mean conditions).
static double solarDensityScale(uint8_t act) {
  switch (act) {
    case SOLAR_LOW:  return 0.35;   // solar minimum
    case SOLAR_HIGH: return 3.0;    // solar maximum
    default:         return 1.0;    // cycle mean
  }
}

// ---- Trapped-drag helpers for the decay model --------------------------------
// Modified Bessel functions I0/I1, EXPONENTIALLY SCALED: these return
// exp(-z)*In(z). King-Hele's per-revolution drag integral needs I0 and I1 of
// z = a*e/H, which for an eccentric orbit runs into the hundreds -- In(z) itself
// overflows a double there, while the scaled form stays O(1/sqrt(z)). Polynomial
// approximations from Abramowitz & Stegun 9.8.1-9.8.4 (|err| < 2e-7).
static double besselI0e(double z) {
  if (z < 3.75) {
    double t = z / 3.75, t2 = t * t;
    double v = 1.0 + t2*(3.5156229 + t2*(3.0899424 + t2*(1.2067492
             + t2*(0.2659732 + t2*(0.0360768 + t2*0.0045813)))));
    return v * exp(-z);
  }
  double t = 3.75 / z;
  return (0.39894228 + t*(0.01328592 + t*(0.00225319 + t*(-0.00157565
        + t*(0.00916281 + t*(-0.02057706 + t*(0.02635537
        + t*(-0.01647633 + t*0.00392377)))))))) / sqrt(z);
}
static double besselI1e(double z) {
  if (z < 3.75) {
    double t = z / 3.75, t2 = t * t;
    double v = z * (0.5 + t2*(0.87890594 + t2*(0.51498869 + t2*(0.15084934
             + t2*(0.02658733 + t2*(0.00301532 + t2*0.00032411))))));
    return v * exp(-z);
  }
  double t = 3.75 / z;
  return (0.39894228 + t*(-0.03988024 + t*(-0.00362018 + t*(0.00163801
        + t*(-0.01031555 + t*(0.02282967 + t*(-0.02895312
        + t*(0.01787654 + t*(-0.00420059)))))))))/ sqrt(z);
}

// Local scale height (m) of the exponential atmosphere at this altitude. Exact
// for the band model -- each band IS an exponential with a published H.
static double atmScaleHeightM(double hkm) {
  static const double B[][2] = {
    {100,5.877},{110,7.263},{120,9.473},{130,12.636},{140,16.149},{150,22.523},
    {180,29.740},{200,37.105},{250,45.546},{300,53.628},{350,53.298},{400,58.515},
    {450,60.828},{500,63.822},{600,71.835},{700,88.667},{800,124.64},{900,181.05},
    {1000,268.00}
  };
  const int N = (int)(sizeof(B) / sizeof(B[0]));
  if (hkm < 100) hkm = 100;
  int i = 0; for (; i < N - 1; ++i) if (hkm < B[i + 1][0]) break;
  return B[i][1] * 1000.0;
}

// ---- Empirical calibration (0.9.68) ------------------------------------------
// Fitted against 244 catalogued objects that actually re-entered (Space-Track TIP
// decay epochs + gp_history element sets, 2025-2026), scored at 30/14/7/3 days
// before re-entry, and cross-checked against the observed mean-motion derivative
// of ~1500 catalogued objects. See docs/design/DECAY_MODEL_0_9_68.md.
//
//   DECAY_DENS_CAL   density multiplier for the B*-derived path. The exponential
//                    table is "nominal"; the fit wants 1.30x at ~250 km.
//   DECAY_DENS_HKM   how that multiplier grows with altitude. The re-entry set is
//                    all low-altitude (final 40 days), so it cannot constrain this
//                    -- the value comes from the n-dot ensemble at 400-600 km and
//                    is the WEAKEST-CONSTRAINED number in the model.
//   DECAY_ANCHOR_DRAG  n-dot is a fitted mean over the element set's fit span, so
//                    it lags the instantaneous rate while decay accelerates; the
//                    fit wants 1.15x. (A DENSITY factor would cancel out of the
//                    anchored path exactly -- verified -- so this must be applied
//                    to the drag, not the density.)
static constexpr double DECAY_DENS_CAL    = 1.30;
static constexpr double DECAY_DENS_HKM    = 300.0;
static constexpr double DECAY_ANCHOR_DRAG = 1.15;
static constexpr double DECAY_DENS_CAP    = 8.0;

static double decayDensCal(double hkm) {
  double s = DECAY_DENS_CAL * exp((hkm - 250.0) / DECAY_DENS_HKM);
  return (s > DECAY_DENS_CAP) ? DECAY_DENS_CAP : s;
}

// Which measurement the estimate was built from, for the UI: a measured decay
// rate and a fitted drag term deserve different confidence.
enum : uint8_t { DECAY_SRC_NONE = 0, DECAY_SRC_NDOT = 1, DECAY_SRC_BSTAR = 2 };

// Days to re-entry. Two anchors, in order of preference:
//
//   1. OBSERVED n-dot. The element set's mean-motion derivative IS a measurement
//      of the current decay rate: adot = -(2/3)(a/n) * ndot. Back-solving the
//      ballistic coefficient from it makes the present rate right BY CONSTRUCTION
//      and removes every calibration the B* path needs -- the B*->Cd*A/m
//      conversion, the absolute density normalization, and the solar-activity
//      scale (which cancels exactly, since it multiplies both the back-solve and
//      the integration). It also absorbs attitude and true area, because it is a
//      measurement of the object rather than a model of it. Scored 0.99x median
//      against real re-entries, 92% within +/-30%.
//
//   2. B* fallback, for objects whose n-dot is absent, negative (rising or
//      post-maneuver) or below the noise floor. Cd*A/m = 12.741621 * B* -- the
//      textbook SGP4 conversion. (0.9.67 and earlier used 38 * B* together with a
//      da/dt that was a factor of 2 too large; the two errors partly cancelled at
//      ISS altitude, where the constant was tuned, and left the model predicting
//      ~1/5 of the true remaining life across the re-entry set.)
//
// Both integrate the same King-Hele decay: drag is evaluated at perigee with the
// eccentricity factor exp(-z)(I0(z) + 2e*I1(z)), z = a*e/H, which accounts for
// the satellite spending almost none of an eccentric revolution near perigee --
// without it a GTO reads ~40 days instead of years. Energy comes out of APOGEE
// while the perigee altitude stays nearly fixed until the orbit circularizes.
//
// densScale brackets solar activity (B* path only). Returns -1 = no usable data,
// 1e9 = effectively stable, else days to a ~120 km perigee. srcOut (optional)
// reports which anchor produced the number.
static double estimateDecayDays(const SatEntry& s, double densScale,
                                uint8_t* srcOut = nullptr) {
  if (srcOut) *srcOut = DECAY_SRC_NONE;
  if (s.meanMotion <= 0 || densScale <= 0) return -1;
  const double MU = 3.986004418e14, RE = 6.378137e6, TP = 6.283185307179686;
  double nn = s.meanMotion * TP / 86400.0;                 // rad/s
  double a  = pow(MU / (nn * nn), 1.0 / 3.0);              // m
  double e  = s.ecc; if (e < 0) e = 0; if (e > 0.95) e = 0.95;
  double rp = a * (1.0 - e), ra = a * (1.0 + e);
  double hp0 = (rp - RE) / 1000.0;                          // perigee altitude (km)
  if (hp0 < 80.0) return 0;

  // Density at a perigee altitude, INCLUDING both calibrations. One function, used
  // for the n-dot back-solve and for every integration step, so the solar scale
  // and the density calibration cancel out of the anchored path exactly.
  auto rhoAt = [&](double hkm) {
    return expAtmosphere(hkm) * densScale * decayDensCal(hkm);
  };
  auto kingHele = [&](double aa, double ee, double hkm) {
    if (ee <= 1e-4) return 1.0;
    double z = aa * ee / atmScaleHeightM(hkm);
    if (z <= 0.05) return 1.0;
    return besselI0e(z) + 2.0 * ee * besselI1e(z);
  };

  double B = 0;                                             // effective Cd*A/m (m^2/kg)
  uint8_t src = DECAY_SRC_NONE;
  // ---- anchor 1: the observed decay rate ------------------------------------
  // s.ndot is MEAN_MOTION_DOT, i.e. ndot/2 in rev/day^2, so ndot = 2*s.ndot.
  {
    double adot = -(2.0 / 3.0) * (a / s.meanMotion) * (2.0 * (double)s.ndot);  // m/day
    double rho0 = rhoAt(hp0);
    // Require a decaying, above-noise rate on an orbit where drag is the plausible
    // cause. A negative or tiny n-dot means rising, freshly maneuvered, or pure
    // fit noise -- and on a high orbit it is absorbing third-body terms, not drag.
    if (adot < -0.5 && rho0 > 0 && hp0 < 1000.0) {
      double f0 = kingHele(a, e, hp0);
      double cand = DECAY_ANCHOR_DRAG * (-adot / 86400.0)
                  / (rho0 * sqrt(MU * a) * f0);
      // Sanity window on the implied ballistic coefficient: a real satellite is
      // ~1e-3..1e-1 m^2/kg, debris and balloons wider. Anything outside means the
      // n-dot was not drag, so fall through to B*.
      if (cand > 1e-4 && cand < 50.0) { B = cand; src = DECAY_SRC_NDOT; }
    }
  }
  // ---- anchor 2: B* ---------------------------------------------------------
  if (src == DECAY_SRC_NONE) {
    if (s.bstar <= 0) return -1;
    B = 12.741621 * (double)s.bstar;
    src = DECAY_SRC_BSTAR;
  }
  if (srcOut) *srcOut = src;

  double tDays = 0;
  for (int it = 0; it < 200000; ++it) {
    double hp = rp - RE;
    double hkm = hp / 1000.0;
    a = 0.5 * (rp + ra);
    double ecur = (ra - rp) / (ra + rp);
    // Re-entry threshold. A near-circular orbit is finished once perigee reaches
    // ~120 km. An ECCENTRIC one is not: it sweeps through perigee far too quickly
    // to be stopped there -- the same King-Hele suppression applied above -- and
    // routinely survives many revolutions with perigee well below 120 km while the
    // orbit circularizes. A flat 120 km cut declared such objects already down:
    // CZ-3B R/B (e = 0.175) sat at a 111 km perigee and lived another 2.5 days,
    // which is now a pinned case in tools/host_decay.
    double hpEnd = (ecur <= 0.02) ? 120e3 : 90e3;
    if (hp < hpEnd) return tDays;                           // re-entry
    double rho = rhoAt(hkm);
    if (rho <= 0) return 1e9;                               // above the table
    double dadt = -B * rho * sqrt(MU * a) * kingHele(a, ecur, hkm);   // m/s
    if (dadt >= 0) return 1e9;
    // Adaptive step: shrink near the end so the fast final plunge isn't overshot.
    double dt = -((hp - 120e3) * 0.20 + 500.0) / dadt;
    double capDays = (hp < 200e3) ? 0.15 : (hp < 350e3 ? 2.0 : 20.0);
    if (dt > capDays * 86400.0) dt = capDays * 86400.0;
    if (dt < 1.0) dt = 1.0;
    double da = dadt * dt;                                  // SMA decrement (<0)
    // King-Hele: drag acting at perigee takes energy out of APOGEE; the perigee
    // altitude holds nearly constant until the orbit is circular. (0.9.67 split
    // the drop between the two, which lowered perigee far too fast -- and since
    // perigee altitude sets the density, that error fed back on itself.)
    if (ecur > 1e-3) {
      ra += 2.0 * da;
      if (ra < rp) { double m = 0.5 * (ra + rp); ra = rp = m; }
    } else {
      ra += da; rp += da;                                   // circular: uniform
    }
    tDays += dt / 86400.0;
    if (tDays > 36500.0) return 1e9;                        // > 100 yr
  }
  return tDays;
}

