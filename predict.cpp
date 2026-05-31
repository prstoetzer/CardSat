// ===========================================================================
//  predict.cpp
// ===========================================================================
#include "predict.h"
#include "config.h"
#include <math.h>

static const double DEG = M_PI / 180.0;
static const double RE_KM = 6378.137;          // WGS84 equatorial radius

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
  strncpy(_name, s.name,  sizeof(_name)-1); _name[sizeof(_name)-1]=0;
  strncpy(_l1,   s.line1, sizeof(_l1)-1);   _l1[sizeof(_l1)-1]=0;
  strncpy(_l2,   s.line2, sizeof(_l2)-1);   _l2[sizeof(_l2)-1]=0;
  _sat.init(_name, _l1, _l2);
  _haveSat = (_sat.satrec.error == 0);
  return _haveSat;
}

LiveLook Predictor::look(time_t t) {
  LiveLook L;
  if (!_haveSat) return L;

  // Range rate via central finite difference of slant range (2 s baseline).
  _sat.findsat((unsigned long)(t - 1));
  double d0 = _sat.satDist;
  _sat.findsat((unsigned long)(t + 1));
  double d1 = _sat.satDist;
  L.rangeRate = (d1 - d0) / 2.0;          // km/s

  // Current sample.
  _sat.findsat((unsigned long)t);
  L.az       = _sat.satAz;
  L.el       = _sat.satEl;
  L.rangeKm  = _sat.satDist;
  L.subLat   = _sat.satLat;
  L.subLon   = _sat.satLon;
  L.satAltKm = _sat.satAlt;
  L.visible  = (_sat.satEl > 0.0);

  // ---- Sun geometry: satellite illumination + Sun look-angle --------------
  double jd = (double)t / 86400.0 + 2440587.5;
  double sx, sy, sz; sunEciUnit(jd, sx, sy, sz);    // Sun unit vector (ECI)
  double th = gmstRad(jd);
  double ct = cos(th), st = sin(th);

  // Satellite ECEF position from its geodetic sub-point (lat/lon/alt).
  double phi = L.subLat * DEG, lam = L.subLon * DEG, h = L.satAltKm;
  double e2 = 6.69437999014e-3;                      // WGS84 first ecc^2
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

void Predictor::dopplerFreqs(uint32_t dlNominal, uint32_t ulNominal,
                             double rangeRateKmS,
                             int32_t calDlHz, int32_t calUlHz,
                             uint32_t& rxHz, uint32_t& txHz) {
  double rr = rangeRateKmS * 1000.0;       // m/s, +ve receding
  double beta = rr / C_LIGHT;

  // Downlink: observer receives dl*(1 - beta) -> tune RX there.
  double rx = (double)dlNominal * (1.0 - beta) + (double)calDlHz;
  // Uplink: transmit so the satellite hears ul nominal -> ul/(1 - beta).
  double tx = (ulNominal ? ((double)ulNominal / (1.0 - beta)) : 0.0);
  if (ulNominal) tx += (double)calUlHz;

  rxHz = (uint32_t)llround(rx);
  txHz = (uint32_t)llround(tx);
}

void Predictor::passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                              uint32_t& dlOp, uint32_t& ulOp) {
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

int Predictor::predictPasses(time_t from, float minEl, PassPredict* out, int maxN) {
  if (!_haveSat) return 0;
  passinfo overpass;
  _sat.initpredpoint((unsigned long)from, (double)minEl);

  int found = 0;
  for (int i = 0; i < maxN; ++i) {
    // search up to ~ a number of iterations for the next pass
    bool ok = _sat.nextpass(&overpass, 200);
    if (!ok) break;
    PassPredict& p = out[found];
    p.aos   = jdToUnix(overpass.jdstart);
    p.los   = jdToUnix(overpass.jdstop);
    p.tca   = jdToUnix(overpass.jdmax);
    p.maxEl = (float)overpass.maxelevation;
    p.azAos = (float)overpass.azstart;
    p.azLos = (float)overpass.azstop;
    found++;
  }
  return found;
}
