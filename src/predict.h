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

  // Compute az/el/range/range-rate at unix time `t` (UTC seconds).
  LiveLook look(time_t t);

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

  // Linear-transponder passband tracking. Given a tuning offset measured in Hz
  // up from the downlink passband bottom, return the *operating* downlink and
  // uplink centre frequencies (before Doppler). For an inverting transponder
  // the uplink moves opposite to the downlink; for non-inverting it tracks it.
  // Single-channel transponders ignore the offset (dlOp=downlink, ulOp=uplink).
  static void passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                            uint32_t& dlOp, uint32_t& ulOp);

  // Fill up to `maxN` upcoming passes starting from `from` (unix UTC).
  int  predictPasses(time_t from, float minEl, PassPredict* out, int maxN);

  static time_t jdToUnix(double jd);

private:
  Sgp4   _sat;
  Observer _o;
  bool   _haveSat = false;
  double _epochUnix = 0;        // element-set epoch (Unix UTC s), for tsince
  char   _name[26], _l1[72], _l2[72];
};
