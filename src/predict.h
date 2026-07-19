#pragma once
// ===========================================================================
//  predict.h  -  SGP4 wrapper: live look-angles, Doppler range-rate, passes
// ===========================================================================
#include <Arduino.h>
#include <Sgp4.h>
#include "satdb.h"
#include "location.h"

struct PassPredict {
  time_t aos = 0;        // unix UTC of acquisition of signal
  time_t los = 0;        // unix UTC of loss of signal
  time_t tca = 0;        // unix UTC of time of closest approach (max elev)
  float  maxEl = 0;      // degrees
  float  azAos = 0;
  float  azLos = 0;
};

struct LiveLook {
  double az = 0, el = 0;     // degrees
  double rangeKm = 0;        // slant range
  double rangeRate = 0;      // km/s, +ve = receding
  double subLat = 0, subLon = 0, satAltKm = 0;
  bool   visible = false;    // el > 0
  bool   sunlit = true;      // satellite illuminated (not in Earth's shadow)
  double sunAz = 0, sunEl = 0;   // Sun position from the observer (degrees)
};

// One co-visibility (mutual) window: both my station and a remote (DX) station
// see the satellite above their horizons at the same time.
struct MutualWindow {
  time_t start = 0, end = 0; // unix UTC bounds of co-visibility
  float  myMaxEl = 0;        // peak elevation here during the window
  float  dxMaxEl = 0;        // peak elevation at the remote site during the window
};

class Predictor {
public:
  void setSite(const Observer& o);
  // Point the propagator at a satellite (renders its GP elements for SGP4).
  bool setSat(SatEntry& s);

  // Propagate an EXPLICIT satellite's GP elements to unix time `t` and return the
  // raw TEME position (km) and velocity (km/s). This is the forward model used by the
  // state-vector -> GP-element fitter. Returns false if the elements don't initialise.
  bool temeStateAt(SatEntry& s, double unixSec, double r[3], double v[3]);

  // Full topocentric look for an ARBITRARY satellite entry at time t, computed with a
  // local propagator (temeStateAt) so the live tracking state (_sat) is never disturbed.
  // Built for BASIC's SATSEL: one call = one SGP4 init+prop. Outputs az/el/range (deg,
  // km), range rate (km/s, +receding), the geodetic sub-point and altitude, and the
  // cylindrical-shadow sunlit flag. The observer/ENU/shadow math mirrors rangeRateAt()
  // and look() expression-for-expression; the host audit harness compares lookFor()
  // against look() sample-by-sample to hold them together.
  bool lookFor(SatEntry& s, time_t t, float& az, float& el, float& rangeKm,
               float& rrKmS, float& subLat, float& subLon, float& altKm, bool& sunlit);

  // Compute az/el/range/range-rate at unix time `t` (UTC seconds).
  LiveLook look(time_t t);
  double mmRevDay() const { return _mmRevDay; }   // 0 unknown; <=6.4 => high orbit

  // True if the satellite is in sunlight (not in Earth's cylindrical shadow) at
  // time t. Lightweight: propagate + shadow test only, no observer geometry.
  bool sunlitAt(time_t t);

  // Eclipse depth (deg), PREDICT/Clarke convention: how far the satellite is
  // inside (positive) or outside (negative) Earth's umbral shadow cone, as the
  // difference between Earth's angular radius seen from the satellite and the
  // satellite's angular distance from the anti-solar axis. >0 = eclipsed.
  double eclipseDepthDeg(time_t t);

  // Solar beta angle (deg) for an orbit of the given inclination and RAAN at
  // time t: the angle between the orbit plane and the Sun direction. Near 0 the
  // orbit plane contains the Sun (long eclipses each rev); near +/-90 the plane
  // faces the Sun (often continuous sunlight). Independent of the satellite's
  // position in the orbit, so it needs no propagation.
  double betaAngleDeg(time_t t, double inclDeg, double raanDeg);

  // Range rate (km/s, +ve receding) at a FRACTIONAL unix time, taken from the
  // SGP4 velocity vector (the method Gpredict/sgp4sdp4 use) rather than by
  // differencing slant range. Exact and not quantised to whole seconds.
  double rangeRateAt(double unixSec);

  // Lightweight: just az/el (degrees) for the current site at time t.
  bool azelAt(time_t t, double& az, double& el);

  // Topocentric elevation (deg) of a satellite at sub-point (satLat/Lon, altKm)
  // as seen from an arbitrary observer. Used for the remote leg of a mutual
  // (co-visibility) window without a second propagator.
  static double elevationFromSubpoint(double obsLatDeg, double obsLonDeg,
                                      double obsAltM,
                                      double satLatDeg, double satLonDeg,
                                      double satAltKm);

  // Find co-visibility windows where this site and `dx` both see the satellite
  // above `minEl` at once, searching forward from `from`. Returns count (<=maxN).
  int mutualWindows(time_t from, const Observer& dx, float minEl,
                    MutualWindow* out, int maxN);

  // Doppler-corrected radio frequencies for the current geometry.
  //   rxHz: tune the receiver here to hear a downlink of dlNominal
  //   txHz: transmit here so the satellite receives ulNominal
  static void dopplerFreqs(uint32_t dlNominal, uint32_t ulNominal,
                           double rangeRateKmS,
                           int32_t calDlHz, int32_t calUlHz,
                           uint32_t& rxHz, uint32_t& txHz);

  // Full-duplex uplink when the operator HOLDS THE DOWNLINK on a fixed receive
  // frequency (so they keep hearing their own signal at the same spot) instead of
  // holding a fixed point in the satellite passband. This compensates the *round
  // trip*: the uplink must counter both the uplink Doppler and the downlink
  // Doppler, because where your downlink lands depends on where the bird heard
  // your uplink. dlOp/ulOp are the satellite-frame operating pair from
  // passbandFreqs (so `invert` matches the transponder). Returns the transmit
  // frequency (incl. calUl) that parks the ground downlink at dlOp+calDl.
  static uint32_t uplinkForFixedDownlink(uint32_t dlOp, uint32_t ulOp,
                                         bool invert, double rangeRateKmS,
                                         int32_t calDlHz, int32_t calUlHz);

  // Symmetric case: the operator HOLDS THE UPLINK on a fixed transmit frequency
  // and tunes only the downlink to follow. Returns the receive frequency (incl.
  // calDl) to hear their own signal, compensating the round trip (the bird hears
  // the fixed uplink Doppler-shifted, then its emitted downlink is Doppler-shifted
  // again on the way down). dlOp/ulOp are the satellite-frame pair from
  // passbandFreqs so `invert` matches the transponder.
  static uint32_t downlinkForFixedUplink(uint32_t dlOp, uint32_t ulOp,
                                         bool invert, double rangeRateKmS,
                                         int32_t calDlHz, int32_t calUlHz);

  // Linear-transponder passband tracking. Given a tuning offset measured in Hz
  // up from the downlink passband bottom, return the *operating* downlink and
  // uplink centre frequencies (before Doppler). For an inverting transponder
  // the uplink moves opposite to the downlink; for non-inverting it tracks it.
  // Single-channel transponders ignore the offset (dlOp=downlink, ulOp=uplink).
  static void passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                            uint32_t& dlOp, uint32_t& ulOp);

  // Fill up to `maxN` upcoming passes starting from `from` (unix UTC).
  int  predictPasses(time_t from, float minEl, PassPredict* out, int maxN,
                     time_t horizonEnd = 0);

  static time_t jdToUnix(double jd);

private:
  Sgp4   _sat;
  Observer _o;
  bool   _haveSat = false;
  double _mmRevDay = 0;      // mean motion (rev/day); <= 6.4 selects the scan pass finder
  double _epochUnix = 0;        // element-set epoch (Unix UTC s), for tsince
  char   _name[26], _l1[72], _l2[72];
};
