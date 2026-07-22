// ===========================================================================
//  predict.cpp
// ===========================================================================
#include "predict.h"
#include "config.h"
#include <math.h>

static const double DEG = M_PI / 180.0;
static const double RE_KM = 6378.135;          // WGS72 equatorial radius (matches the TLE element set)

// Geocentric unit vector to the Sun in equatorial inertial coords (ECI).
// Low-precision almanac, good to ~0.01 deg -- ample for shadow / az-el.
static void sunEciUnit(double jd, double& x, double& y, double& z) {
  double n   = jd - 2451545.0;
  double L   = fmod(280.460 + 0.9856474 * n, 360.0);   // mean longitude
  double g   = fmod(357.528 + 0.9856003 * n, 360.0) * DEG;
  double lam = (L + 1.915 * sin(g) + 0.020 * sin(2 * g)) * DEG;  // ecliptic lon
  double eps = (23.439 - 0.0000004 * n) * DEG;          // obliquity
  x = cos(lam);
  y = cos(eps) * sin(lam);
  z = sin(eps) * sin(lam);
}

// Greenwich mean sidereal time (radians) for a given Julian date.
static double gmstRad(double jd) {
  double T = (jd - 2451545.0) / 36525.0;
  double g = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
             + 0.000387933 * T * T - T * T * T / 38710000.0;
  g = fmod(g, 360.0); if (g < 0) g += 360.0;
  return g * DEG;
}

void Predictor::setSite(const Observer& o) {
  _o = o;
  _sat.site(o.lat, o.lon, o.altM);
}

bool Predictor::setSat(SatEntry& s) {
  strncpy(_name, s.name, sizeof(_name)-1); _name[sizeof(_name)-1]=0;
  // The SGP4 library ingests elements through twoline2rv, so render the stored
  // GP mean elements into a TLE line-pair (SGP4 is encoding-agnostic).
  if (!SatDb::gpToTle(s, _l1, _l2)) { _haveSat = false; return false; }
  _sat.init(_name, _l1, _l2);
  _haveSat = (_sat.satrec.error == 0);
  _mmRevDay = s.meanMotion;          // recorded for the high-orbit pass finder
  _epochUnix = s.epochUnix;          // for fractional-time range rate (rangeRateAt)
  return _haveSat;
}

// Forward model for the state-vector -> GP fitter: initialise SGP4 from a candidate
// SatEntry's GP elements and propagate to `unixSec`, returning the TEME state. Uses a
// LOCAL Sgp4 object so it never disturbs the live tracking propagator (_sat).
bool Predictor::temeStateAt(SatEntry& s, double unixSec, double r[3], double v[3]) {
  char l1[72], l2[72];
  if (!SatDb::gpToTle(s, l1, l2)) return false;
  // Sgp4::init() short-circuits (returns without re-parsing) when line 1 is byte-identical to
  // the previous call: `if (strcmp(longstr1, line1) == 0) return false;`. The fitter perturbs
  // only LINE 2 elements (incl/ecc/raan/argp/ma/mm) with a fixed line 1, so every candidate
  // shares the same line 1 -> init caches the FIRST candidate and every later evaluation returns
  // its state unchanged -> a zero Jacobian and no convergence. Defeat the cache by varying a
  // byte SGP4 ignores for propagation: the element-set-number field (cols 65-68 on line 1). We
  // cycle it so consecutive calls never match, forcing a genuine twoline2rv() each time. The
  // checksum (col 69) is recomputed so the line stays well-formed.
  static uint16_t fitTick = 0;
  { unsigned v4 = (unsigned)(fitTick++ % 9999) + 1;   // 1..9999, never the same twice in a row
    char es[6]; snprintf(es, sizeof(es), "%4u", v4);
    for (int i = 0; i < 4; ++i) l1[64 + i] = es[i];
    int sum = 0;                                       // recompute the line-1 checksum (col 69)
    for (int i = 0; i < 68; ++i) { char c = l1[i];
      if (c >= '0' && c <= '9') sum += c - '0'; else if (c == '-') sum += 1; }
    l1[68] = '0' + (sum % 10); }
  // static: an Sgp4 carries a full elsetrec (hundreds of bytes). Keeping it off the
  // stack matters because this is the deepest frame before SGP4's own large frames.
  // Safe here: single-threaded, and fp.init() fully re-initialises it every call.
  static Sgp4 fp;
  if (!fp.init((char*)"FIT", l1, l2)) return false;     // now always re-parses (line 1 differs)
  if (fp.satrec.error != 0) return false;
  double tsince = (unixSec - s.epochUnix) / 60.0;       // minutes since element epoch
  r[0] = r[1] = r[2] = v[0] = v[1] = v[2] = 0.0;
  sgp4(wgs72, fp.satrec, tsince, r, v);                 // TEME position (km), velocity (km/s)
  if (fp.satrec.error != 0) return false;
  return true;
}

bool Predictor::lookFor(SatEntry& s, time_t t, float& az, float& el, float& rangeKm,
                        float& rrKmS, float& subLat, float& subLon, float& altKm,
                        bool& sunlit) {
  double r[3], v[3];
  if (!temeStateAt(s, (double)t, r, v)) return false;

  // Observer position + velocity in TEME -- the same expressions as rangeRateAt().
  double jd  = (double)t / 86400.0 + 2440587.5;
  double th  = gmstRad(jd);
  double ct = cos(th), st = sin(th);
  double phi = _o.lat * DEG, lam = _o.lon * DEG, hKm = _o.altM / 1000.0;
  double e2  = 6.694318e-3;                     // WGS72 first eccentricity^2
  double sphi = sin(phi), cphi = cos(phi);
  double Nn  = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double xe = (Nn + hKm) * cphi * cos(lam);
  double ye = (Nn + hKm) * cphi * sin(lam);
  double ze = (Nn * (1.0 - e2) + hKm) * sphi;
  double ox = xe * ct - ye * st, oy = xe * st + ye * ct, oz = ze;
  const double we = 7.2921150e-5;
  double ovx = -we * oy, ovy = we * ox, ovz = 0.0;

  double rx = r[0] - ox, ry = r[1] - oy, rz = r[2] - oz;
  double vx = v[0] - ovx, vy = v[1] - ovy, vz = v[2] - ovz;
  double rmag = sqrt(rx * rx + ry * ry + rz * rz);
  if (rmag <= 0) return false;
  rangeKm = (float)rmag;
  rrKmS   = (float)((rx * vx + ry * vy + rz * vz) / rmag);

  // Topocentric az/el: ENU basis at the observer (same basis as look()'s Sun block).
  double ost = sin(th + lam), oct = cos(th + lam);
  double eC = (-ost) * rx + (oct) * ry;
  double nC = (-sphi * oct) * rx + (-sphi * ost) * ry + (cphi) * rz;
  double uC = ( cphi * oct) * rx + ( cphi * ost) * ry + (sphi) * rz;
  el = (float)(atan2(uC, sqrt(eC * eC + nC * nC)) / DEG);
  double a = atan2(eC, nC) / DEG; if (a < 0) a += 360.0;
  az = (float)a;

  // Sub-point: satellite TEME -> ECEF (Rz(-theta)) -> geodetic (iterative).
  double gx =  r[0] * ct + r[1] * st;
  double gy = -r[0] * st + r[1] * ct;
  double gz =  r[2];
  double lon = atan2(gy, gx) / DEG;
  double pd  = sqrt(gx * gx + gy * gy);
  double lat = atan2(gz, pd * (1.0 - e2));
  double Nl = RE_KM;
  for (int i = 0; i < 4; ++i) {
    double sl = sin(lat);
    Nl  = RE_KM / sqrt(1.0 - e2 * sl * sl);
    lat = atan2(gz + e2 * Nl * sl, pd);
  }
  subLat = (float)(lat / DEG);
  subLon = (float)lon;
  altKm  = (float)(pd / cos(lat) - Nl);

  // Cylindrical shadow, evaluated directly in TEME (a Z-rotation of the ECEF test:
  // dot products and norms are invariant, so the result is identical to look()'s).
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);
  double proj = r[0] * sx + r[1] * sy + r[2] * sz;
  double rm2  = r[0] * r[0] + r[1] * r[1] + r[2] * r[2];
  double perp = sqrt(fmax(0.0, rm2 - proj * proj));
  sunlit = !(proj < 0.0 && perp < RE_KM);
  return true;
}

// Range rate from the SGP4 velocity vector at a fractional instant -- the
// method Gpredict uses (sgp4sdp4 converts ECI position+velocity straight to
// observer-centred range rate). Far cleaner near TCA than differencing slant
// range, and evaluated at the exact time rather than the nearest whole second.
// This Hopperpop build uses the older Vallado propagator signature
// sgp4(whichconst, satrec, tsince_min, r[3], v[3]); pass WGS72 (the constant set
// the elements are fit to) -> TEME position (km) and velocity (km/s).
double Predictor::rangeRateAt(double unixSec) {
  if (!_haveSat) return 0.0;

  // Propagate to the exact instant. tsince is MINUTES since the element epoch;
  // measure it from the stored Unix epoch so we don't depend on satrec's epoch
  // field layout.
  double tsince = (unixSec - _epochUnix) / 60.0;
  double r[3] = {0, 0, 0}, v[3] = {0, 0, 0};
  sgp4(wgs72, _sat.satrec, tsince, r, v);       // TEME position/velocity (WGS72)

  // Observer in the same TEME frame: geodetic -> ECEF -> rotate by GMST.
  double jd  = unixSec / 86400.0 + 2440587.5;
  double th  = gmstRad(jd);
  double ct = cos(th), st = sin(th);
  double phi = _o.lat * DEG, lam = _o.lon * DEG, hKm = _o.altM / 1000.0;
  double e2  = 6.694318e-3;                     // WGS72 first eccentricity^2 (f = 1/298.26)
  double sphi = sin(phi), cphi = cos(phi);
  double N   = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double xe = (N + hKm) * cphi * cos(lam);      // ECEF
  double ye = (N + hKm) * cphi * sin(lam);
  double ze = (N * (1.0 - e2) + hKm) * sphi;
  double ox = xe * ct - ye * st;                // ECEF -> TEME  (Rz(+theta))
  double oy = xe * st + ye * ct;
  double oz = ze;

  // Observer velocity in TEME from Earth rotation: omega_earth x r_obs.
  const double we = 7.2921150e-5;               // rad/s (sidereal)
  double ovx = -we * oy, ovy = we * ox, ovz = 0.0;

  // Range rate = (r_rel . v_rel) / |r_rel|, +ve when the range is increasing.
  double rx = r[0] - ox,  ry = r[1] - oy,  rz = r[2] - oz;
  double vx = v[0] - ovx, vy = v[1] - ovy, vz = v[2] - ovz;
  double rmag = sqrt(rx * rx + ry * ry + rz * rz);
  if (rmag <= 0.0) return 0.0;
  return (rx * vx + ry * vy + rz * vz) / rmag;
}

LiveLook Predictor::look(time_t t) {
  LiveLook L;
  if (!_haveSat) return L;

  // Current sample (az/el/range/sub-point) from the propagator.
  _sat.findsat((unsigned long)t);
  L.az       = _sat.satAz;
  L.el       = _sat.satEl;
  L.rangeKm  = _sat.satDist;
  L.subLat   = _sat.satLat;
  L.subLon   = _sat.satLon;
  L.satAltKm = _sat.satAlt;
  L.visible  = (_sat.satEl > 0.0);

  // Range rate from the SGP4 velocity vector (exact; no finite-difference
  // truncation), at this same instant -- see rangeRateAt().
  L.rangeRate = rangeRateAt((double)t);

  // ---- Sun geometry: satellite illumination + Sun look-angle --------------
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);    // Sun unit vector (ECI)
  double th = gmstRad(jd);
  double ct = cos(th), st = sin(th);

  // Satellite ECEF position from its geodetic sub-point (lat/lon/alt).
  double phi = L.subLat * DEG, lam = L.subLon * DEG, h = L.satAltKm;
  double e2 = 6.694318e-3;                           // WGS72 first ecc^2 (f = 1/298.26)
  double sphi = sin(phi), cphi = cos(phi);
  double Nlat = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double rx = (Nlat + h) * cphi * cos(lam);
  double ry = (Nlat + h) * cphi * sin(lam);
  double rz = (Nlat * (1.0 - e2) + h) * sphi;

  // Sun unit vector rotated ECI -> ECEF (Rz(-theta)).
  double ux =  sx * ct + sy * st;
  double uy = -sx * st + sy * ct;
  double uz =  sz;

  // Cylindrical-shadow test: in eclipse if on the anti-solar side and the
  // perpendicular distance to the Earth-Sun axis is less than Earth's radius.
  double proj = rx * ux + ry * uy + rz * uz;         // km along Sun direction
  double rmag2 = rx * rx + ry * ry + rz * rz;
  double perp  = sqrt(fmax(0.0, rmag2 - proj * proj));
  L.sunlit = !(proj < 0.0 && perp < RE_KM);

  // Sun az/el for the observer (topocentric ENU; solar parallax negligible).
  double olat = _o.lat * DEG;
  double ost = sin(th + _o.lon * DEG), oct = cos(th + _o.lon * DEG);
  double slat = sin(olat), clat = cos(olat);
  // East, North, Up (ECI) dotted with Sun unit vector:
  double eComp = (-ost) * sx + (oct) * sy;
  double nComp = (-slat * oct) * sx + (-slat * ost) * sy + (clat) * sz;
  double uComp = ( clat * oct) * sx + ( clat * ost) * sy + (slat) * sz;
  L.sunEl = atan2(uComp, sqrt(eComp * eComp + nComp * nComp)) / DEG;
  double az = atan2(eComp, nComp) / DEG; if (az < 0) az += 360.0;
  L.sunAz = az;
  return L;
}

// Eclipse-only test (cylindrical Earth shadow), mirroring look()'s geometry but
// skipping all observer-relative work. Used by the 60-day illumination screen.
bool Predictor::sunlitAt(time_t t) {
  if (!_haveSat) return true;
  _sat.findsat((unsigned long)t);
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);
  double th = gmstRad(jd);
  double ct = cos(th), st = sin(th);
  double phi = _sat.satLat * DEG, lam = _sat.satLon * DEG, h = _sat.satAlt;
  double e2 = 6.694318e-3;
  double sphi = sin(phi), cphi = cos(phi);
  double Nlat = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double rx = (Nlat + h) * cphi * cos(lam);
  double ry = (Nlat + h) * cphi * sin(lam);
  double rz = (Nlat * (1.0 - e2) + h) * sphi;
  double ux =  sx * ct + sy * st;
  double uy = -sx * st + sy * ct;
  double uz =  sz;
  double proj  = rx * ux + ry * uy + rz * uz;
  double rmag2 = rx * rx + ry * ry + rz * rz;
  double perp  = sqrt(fmax(0.0, rmag2 - proj * proj));
  return !(proj < 0.0 && perp < RE_KM);
}

// Eclipse depth in degrees (PREDICT/Clarke convention). Reuses the same Sun and
// satellite geometry as sunlitAt: with r the geocentric satellite distance, the
// Earth subtends a half-angle sd_earth = asin(Re/r) at the satellite. The angle
// of the satellite off the anti-solar axis is delta = asin(perp / r). The depth
// is sd_earth - delta on the anti-sun side: positive when the satellite is
// inside the umbral cone (eclipsed), negative (a clearance margin) in sunlight.
double Predictor::eclipseDepthDeg(time_t t) {
  if (!_haveSat) return -90.0;
  _sat.findsat((unsigned long)t);
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);
  double th = gmstRad(jd);
  double ct = cos(th), st = sin(th);
  double phi = _sat.satLat * DEG, lam = _sat.satLon * DEG, h = _sat.satAlt;
  double e2 = 6.694318e-3;
  double sphi = sin(phi), cphi = cos(phi);
  double Nlat = RE_KM / sqrt(1.0 - e2 * sphi * sphi);
  double rx = (Nlat + h) * cphi * cos(lam);
  double ry = (Nlat + h) * cphi * sin(lam);
  double rz = (Nlat * (1.0 - e2) + h) * sphi;
  double ux =  sx * ct + sy * st;
  double uy = -sx * st + sy * ct;
  double uz =  sz;
  double proj  = rx * ux + ry * uy + rz * uz;          // along Sun axis (+ = sunward)
  double rmag2 = rx * rx + ry * ry + rz * rz;
  double r     = sqrt(rmag2);
  double perp  = sqrt(fmax(0.0, rmag2 - proj * proj));  // distance from Sun axis
  double sdEarth = asin(fmin(1.0, RE_KM / r)) / DEG;     // Earth angular radius (deg)
  double delta   = asin(fmin(1.0, perp / r)) / DEG;      // off anti-sun axis (deg)
  // On the sunward side (proj >= 0) the satellite cannot be eclipsed; report the
  // depth as negative (well outside the shadow) using the supplement of delta.
  double depth = (proj < 0.0) ? (sdEarth - delta) : (sdEarth - (180.0 - delta));
  return depth;
}

double Predictor::betaAngleDeg(time_t t, double inclDeg, double raanDeg) {
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);          // Sun unit vector (ECI)
  double i = inclDeg * DEG, O = raanDeg * DEG;
  // Orbit-normal unit vector in ECI from inclination + RAAN.
  double nx =  sin(i) * sin(O);
  double ny = -sin(i) * cos(O);
  double nz =  cos(i);
  double d = nx * sx + ny * sy + nz * sz;                 // = cos(angle to Sun)
  if (d >  1.0) d =  1.0;
  if (d < -1.0) d = -1.0;
  return asin(d) / DEG;                                   // beta = 90 - angle(n,Sun)
}

void Predictor::dopplerFreqs(freq_t dlNominal, freq_t ulNominal,
                             double rangeRateKmS,
                             int32_t calDlHz, int32_t calUlHz,
                             freq_t& rxHz, freq_t& txHz) {
  double rr = rangeRateKmS * 1000.0;       // m/s, +ve receding
  double beta = rr / C_LIGHT;

  // Downlink: observer receives dl*(1 - beta) -> tune RX there.
  double rx = (double)dlNominal * (1.0 - beta) + (double)calDlHz;
  // Uplink: transmit so the satellite hears ul nominal -> ul/(1 - beta).
  double tx = (ulNominal ? ((double)ulNominal / (1.0 - beta)) : 0.0);
  if (ulNominal) tx += (double)calUlHz;

  rxHz = (freq_t)llround(rx);
  txHz = (freq_t)llround(tx);
}

freq_t Predictor::uplinkForFixedDownlink(freq_t dlOp, freq_t ulOp,
                                           bool invert, double rangeRateKmS,
                                           int32_t calDlHz, int32_t calUlHz) {
  if (ulOp == 0) return 0;
  double beta = rangeRateKmS * 1000.0 / C_LIGHT;   // +ve receding
  double oneMinusBeta = 1.0 - beta;
  if (oneMinusBeta == 0.0) oneMinusBeta = 1e-12;    // guard (never physical)

  // The operator parks RX at the ground frequency dlOp+calDl. For the ground to
  // hear that, the bird must EMIT a downlink of Fdl_sat = (dlOp+calDl)/(1-beta).
  // delta is how far that emit sits above the nominal operating downlink.
  double parkedGround = (double)dlOp + (double)calDlHz;
  double fdlSat = parkedGround / oneMinusBeta;
  double delta  = fdlSat - (double)dlOp;

  // Map the shifted emit back to the uplink the bird must HEAR, using the same
  // inversion sense as passbandFreqs: inverting -> uplink moves opposite the
  // downlink; non-inverting -> it tracks. Then Doppler-compensate that uplink so
  // the bird actually hears it, and add the uplink calibration.
  double fulSat = invert ? ((double)ulOp - delta) : ((double)ulOp + delta);
  double tx = fulSat / oneMinusBeta + (double)calUlHz;
  if (tx < 0) tx = 0;
  return (freq_t)llround(tx);
}

freq_t Predictor::downlinkForFixedUplink(freq_t dlOp, freq_t ulOp,
                                           bool invert, double rangeRateKmS,
                                           int32_t calDlHz, int32_t calUlHz) {
  double beta = rangeRateKmS * 1000.0 / C_LIGHT;   // +ve receding
  double oneMinusBeta = 1.0 - beta;
  if (oneMinusBeta == 0.0) oneMinusBeta = 1e-12;    // guard (never physical)
  // No uplink (downlink-only bird): just the plain Doppler-shifted downlink.
  if (ulOp == 0)
    return (freq_t)llround((double)dlOp * oneMinusBeta + (double)calDlHz);

  // The operator parks TX at the ground frequency ulOp+calUl; the bird hears that
  // Doppler-shifted to Ful_sat.
  double parkedTxGround = (double)ulOp + (double)calUlHz;
  double fulSat = parkedTxGround * oneMinusBeta;
  // Translate the heard uplink to the emitted downlink with the same inversion
  // sense as passbandFreqs (inverting -> downlink moves opposite the uplink).
  double fdlSat = invert ? ((double)dlOp - (fulSat - (double)ulOp))
                         : ((double)dlOp + (fulSat - (double)ulOp));
  // That emit is Doppler-shifted again on the way down; add downlink calibration.
  double rx = fdlSat * oneMinusBeta + (double)calDlHz;
  if (rx < 0) rx = 0;
  return (freq_t)llround(rx);
}

void Predictor::passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                              freq_t& dlOp, freq_t& ulOp) {
  // No tunable downlink passband -> single channel; ignore the offset.
  uint32_t dlBw = t.bandwidth();
  if (!t.isLinear || dlBw == 0) {
    dlOp = t.downlink;
    ulOp = t.uplink;
    return;
  }

  // Clamp the tuning offset into [0, downlink bandwidth].
  int32_t off = pbOffsetHz;
  if (off < 0) off = 0;
  if ((uint32_t)off > dlBw) off = (int32_t)dlBw;

  dlOp = t.downlink + (uint32_t)off;

  if (t.uplink == 0) { ulOp = 0; return; }

  // Assume equal up/down passband width when the uplink top edge is missing.
  uint32_t ulBw = (t.uplinkHigh > t.uplink) ? (t.uplinkHigh - t.uplink) : dlBw;
  if (t.invert) {
    // Inverting: bottom of uplink maps to top of downlink. As the downlink
    // tunes up by `off`, the uplink tunes down by the same amount.
    ulOp = t.uplink + ulBw - (uint32_t)off;
  } else {
    ulOp = t.uplink + (uint32_t)off;
  }
}

time_t Predictor::jdToUnix(double jd) {
  return (time_t)llround((jd - 2440587.5) * 86400.0);
}

bool Predictor::azelAt(time_t t, double& az, double& el) {
  if (!_haveSat) { az = el = 0; return false; }
  _sat.findsat((unsigned long)t);
  az = _sat.satAz;
  el = _sat.satEl;
  return true;
}

double Predictor::elevationFromSubpoint(double obsLatDeg, double obsLonDeg,
                                        double obsAltM,
                                        double satLatDeg, double satLonDeg,
                                        double satAltKm) {
  const double D = M_PI / 180.0, RE = 6378.135, e2 = 6.694318e-3;
  auto ecef = [&](double latD, double lonD, double hKm,
                  double& x, double& y, double& z) {
    double phi = latD * D, lam = lonD * D, s = sin(phi), c = cos(phi);
    double N = RE / sqrt(1.0 - e2 * s * s);
    x = (N + hKm) * c * cos(lam);
    y = (N + hKm) * c * sin(lam);
    z = (N * (1.0 - e2) + hKm) * s;
  };
  double ox, oy, oz, sx, sy, sz;
  ecef(obsLatDeg, obsLonDeg, obsAltM / 1000.0, ox, oy, oz);
  ecef(satLatDeg, satLonDeg, satAltKm,          sx, sy, sz);
  double dx = sx - ox, dy = sy - oy, dz = sz - oz;
  double dn = sqrt(dx * dx + dy * dy + dz * dz);
  if (dn <= 0) return -90.0;
  double phi = obsLatDeg * D, lam = obsLonDeg * D;          // ellipsoidal up
  double ux = cos(phi) * cos(lam), uy = cos(phi) * sin(lam), uz = sin(phi);
  return asin((dx * ux + dy * uy + dz * uz) / dn) / D;
}

int Predictor::mutualWindows(time_t from, const Observer& dx, float minEl,
                             MutualWindow* out, int maxN) {
  if (!_haveSat) return 0;
  // My passes bound the search (a mutual window can only occur while I can see
  // the bird), so scan inside each of my passes out to the day horizon. Heap-
  // allocate the pass buffer: 10 days of a busy LEO is too large for the stack.
  PassPredict* mine = new PassPredict[MUTUAL_PASS_SCAN];
  int np = predictPasses(from, minEl, mine, MUTUAL_PASS_SCAN,
                         from + (time_t)MUTUAL_HORIZON_DAYS * 86400);

  const time_t dt = 10;                 // scan step (s)
  int n = 0;
  for (int p = 0; p < np && n < maxN; ++p) {
    bool inWin = false;
    MutualWindow w;
    for (time_t t = mine[p].aos; t <= mine[p].los; t += dt) {
      _sat.findsat((unsigned long)t);
      double myEl = _sat.satEl;
      double dxEl = elevationFromSubpoint(dx.lat, dx.lon, dx.altM,
                                          _sat.satLat, _sat.satLon, _sat.satAlt);
      bool both = (myEl >= minEl && dxEl >= minEl);
      if (both) {
        if (!inWin) { inWin = true; w = MutualWindow();
                      w.start = t; w.end = t;
                      w.myMaxEl = (float)myEl; w.dxMaxEl = (float)dxEl; }
        else { w.end = t;
               if (myEl > w.myMaxEl) w.myMaxEl = (float)myEl;
               if (dxEl > w.dxMaxEl) w.dxMaxEl = (float)dxEl; }
      } else if (inWin) {
        out[n++] = w; inWin = false;
        if (n >= maxN) break;
      }
    }
    if (inWin && n < maxN) out[n++] = w;   // window open at pass end
  }
  delete[] mine;
  return n;
}

int Predictor::predictPasses(time_t from, float minEl, PassPredict* out, int maxN,
                             time_t horizonEnd) {
  if (!_haveSat) return 0;

  // ---- High-orbit finder (period >= ~225 min; 0.9.59) --------------------------------
  // The library's nextpass() hops one revolution at a time and Brent-brackets a rise
  // and a set around each elevation maximum -- an excellent LEO strategy (harness:
  // 7/7 vs skyfield) that demonstrably fails for Molniya-class orbits and cannot
  // represent a continuously-visible geosynchronous bird at all (there is no rise to
  // bracket). Deep-space SDP4 propagation itself is present and healthy in this
  // build's sgp4unit (harness look(): Molniya el/range spot-on), so for slow orbits
  // we scan elevation ourselves and bisect the threshold crossings: step = period/96
  // clamped to [120 s, 900 s], AOS/LOS refined to ~1 s, max-elevation sampled inside
  // the pass at <= 240 points. A bird above minEl for the whole horizon yields one
  // horizon-long pass (aos = from, los = horizon) -- the honest answer for a GEO
  // stationed in view. Verified against skyfield in tools/host_orbit_audit (HIORBIT).
  if (_mmRevDay > 0 && _mmRevDay <= 6.4) {
    time_t hEnd = horizonEnd ? horizonEnd : from + 86400;
    if (hEnd <= from || maxN <= 0) return 0;
    double periodS = 86400.0 / _mmRevDay;
    time_t step = (time_t)fmax(120.0, fmin(900.0, periodS / 96.0));
    auto elAt = [&](time_t t) { return (double)look(t).el; };
    auto refine = [&](time_t lo, time_t hi, bool rising) -> time_t {
      // invariant: el(lo) and el(hi) straddle minEl in the stated direction
      for (int i = 0; i < 20 && hi - lo > 1; ++i) {
        time_t mid = lo + (hi - lo) / 2;
        bool up = elAt(mid) >= minEl;
        if (up == rising) hi = mid; else lo = mid;
      }
      return hi;
    };
    int found = 0;
    bool  prevUp = elAt(from) >= minEl;
    time_t prevT = from, aos = prevUp ? from : 0;
    for (time_t t = from + step; found < maxN; t += step) {
      if (t > hEnd) t = hEnd;
      bool up = elAt(t) >= minEl;
      if (up && !prevUp)      aos = refine(prevT, t, true);
      else if (!up && prevUp && aos) {
        time_t los = refine(prevT, t, false);
        PassPredict& p = out[found];
        p.aos = aos; p.los = los;
        time_t ps = (los - aos) / 240; if (ps < 60) ps = 60;
        double mE = -90; time_t tM = aos; float aA = 0, aL = 0;
        { LiveLook L = look(aos); aA = L.az; }
        for (time_t q = aos; q <= los; q += ps) {
          LiveLook L = look(q);
          if (L.el > mE) { mE = L.el; tM = q; }
        }
        { LiveLook L = look(los); aL = L.az; }
        p.tca = tM; p.maxEl = (float)mE; p.azAos = aA; p.azLos = aL;
        found++; aos = 0;
      }
      prevUp = up; prevT = t;
      if (t >= hEnd) break;
    }
    if (aos && found < maxN) {         // still above minEl at the horizon's edge
      PassPredict& p = out[found];
      p.aos = aos; p.los = hEnd;
      time_t ps = (hEnd - aos) / 240; if (ps < 60) ps = 60;
      double mE = -90; time_t tM = aos; float aA = 0, aL = 0;
      { LiveLook L = look(aos); aA = L.az; }
      for (time_t q = aos; q <= hEnd; q += ps) {
        LiveLook L = look(q);
        if (L.el > mE) { mE = L.el; tM = q; }
      }
      { LiveLook L = look(hEnd); aL = L.az; }
      p.tca = tM; p.maxEl = (float)mE; p.azAos = aA; p.azLos = aL;
      found++;
    }
    return found;
  }

  passinfo overpass;
  _sat.initpredpoint((unsigned long)from, (double)minEl);

  int found = 0;
  for (int i = 0; i < maxN; ++i) {
    // search up to ~ a number of iterations for the next pass
    bool ok = _sat.nextpass(&overpass, 200);
    if (!ok) break;
    time_t aos = jdToUnix(overpass.jdstart);
    if (horizonEnd && aos > horizonEnd) break;     // stop past the time horizon
    PassPredict& p = out[found];
    p.aos   = aos;
    p.los   = jdToUnix(overpass.jdstop);
    p.tca   = jdToUnix(overpass.jdmax);
    p.maxEl = (float)overpass.maxelevation;
    p.azAos = (float)overpass.azstart;
    p.azLos = (float)overpass.azstop;
    found++;
  }

  // Belt-and-braces for the LIBRARY path only (slow orbits never reach here --
  // the scan finder above owns them, always-up case included): if nextpass()
  // found nothing but the bird is above minEl right now, sample the horizon and
  // report continuous visibility as one horizon-long pass. Costs a single extra
  // look() in the common no-pass case.
  if (found == 0 && maxN > 0) {
    LiveLook L0 = look(from);
    if (L0.el >= minEl) {
      time_t hEnd = horizonEnd ? horizonEnd : from + 86400;
      if (hEnd > from) {
        const int NS = 49;                         // ~30 min grid over 24 h
        double maxE = L0.el; time_t tMax = from;
        float azA = L0.az, azL = L0.az;
        bool allUp = true;
        for (int k = 1; k < NS; ++k) {
          time_t t = from + (time_t)(((int64_t)(hEnd - from) * k) / (NS - 1));
          LiveLook L = look(t);
          azL = L.az;
          if (L.el < minEl) { allUp = false; break; }
          if (L.el > maxE) { maxE = L.el; tMax = t; }
        }
        if (allUp) {
          PassPredict& p = out[0];
          p.aos = from; p.los = hEnd; p.tca = tMax;
          p.maxEl = (float)maxE; p.azAos = azA; p.azLos = azL;
          found = 1;
        }
      }
    }
  }
  return found;
}
