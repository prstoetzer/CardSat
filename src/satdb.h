#pragma once
// ===========================================================================
//  satdb.h  -  in-memory satellite catalog (slim) + transponder parsing
// ===========================================================================
//  Orbital data is GP (General Perturbations / OMM) element sets, sourced from
//  AMSAT's JSON distribution. The legacy TLE *text* format is being retired as
//  the 5-digit NORAD catalog field runs out; GP/OMM carries the same SGP4 mean
//  elements in named fields with no width limit. We store the elements here and
//  reconstruct a TLE line-pair on demand only to feed the SGP4 propagator (the
//  Hopperpop library ingests elements via twoline2rv); see gpToTle().
//
//  RAM note: the StampS3A has ~512 KB internal SRAM and no PSRAM, so we keep
//  SatEntry small (no embedded transponder array). Transponders are parsed on
//  demand into a caller-supplied buffer for the *active* satellite only.
// ===========================================================================
#include <Arduino.h>
#include "config.h"

struct Transponder {
  char     desc[40];
  uint32_t downlink     = 0; // Hz (downlink_low;  0 if none)
  uint32_t downlinkHigh = 0; // Hz (downlink_high; 0 if single-channel)
  uint32_t uplink       = 0; // Hz (uplink_low;    0 if none / beacon)
  uint32_t uplinkHigh   = 0; // Hz (uplink_high;   0 if single-channel)
  char     mode[12] = {0};   // e.g. "FM", "USB", "DATA"
  bool     invert   = false; // inverting linear transponder
  bool     isLinear = false; // true => has a tunable passband (do passband tracking)
  float    toneHz   = 0.0f;  // required FM uplink CTCSS/PL tone (0 = none)

  // Downlink passband width in Hz (0 for single-channel / FM).
  uint32_t bandwidth() const {
    return (downlinkHigh > downlink) ? (downlinkHigh - downlink) : 0;
  }
};

// One satellite's GP mean elements (the SGP4 inputs) plus identity.
struct SatEntry {
  char     name[26];          // AMSAT_NAME
  uint32_t norad = 0;         // NORAD_CAT_ID (identity / display)
  char     intlDes[12] = {0}; // OBJECT_ID, e.g. "1974-089B"
  double   epochUnix = 0;     // EPOCH as Unix UTC seconds (fractional)
  double   incl = 0;          // INCLINATION       (deg)
  double   ecc = 0;           // ECCENTRICITY      (dimensionless)
  double   raan = 0;          // RA_OF_ASC_NODE    (deg)
  double   argp = 0;          // ARG_OF_PERICENTER (deg)
  double   ma = 0;            // MEAN_ANOMALY      (deg)
  double   meanMotion = 0;    // MEAN_MOTION       (rev/day)
  double   bstar = 0;         // BSTAR             (1/earth radii)
  double   ndot = 0;          // MEAN_MOTION_DOT   (rev/day^2, = ndot/2)
  double   nddot = 0;         // MEAN_MOTION_DDOT  (rev/day^3, = nddot/6)
  uint32_t revAtEpoch = 0;    // REV_AT_EPOCH
  uint16_t elsetNum = 0;      // ELEMENT_SET_NO
  bool     txLoaded = false;  // have we fetched transponders this session?
};

class SatDb {
public:
  bool begin();                  // mount LittleFS
  int  count() const { return _n; }
  SatEntry& at(int i) { return _sats[i]; }
  int  indexOfNorad(uint32_t norad) const;

  // Parse AMSAT's GP JSON (array of OMM objects) into the catalog.
  int  loadGpFromJson(const String& json);    // replace DB
  int  appendGpFromJson(const String& json);  // append (dedup/replace by norad)
  bool addGp(const SatEntry& s);               // one manual sat (+persist NDJSON)
  bool loadManualGpFile();                     // merge FILE_MGP into the DB
  bool loadGpFromFs();                         // reload cached GP JSON at boot
  int  loadGpFromFile(const char* path);       // stream-parse a GP file (low RAM)
  bool saveGpJson(const String& json);         // cache the downloaded blob

  // Reconstruct a TLE line-pair from a satellite's GP elements (69 chars each,
  // checksummed). Only used to initialise the SGP4 propagator. Returns false
  // on a malformed entry.
  static bool gpToTle(const SatEntry& s, char l1[72], char l2[72]);

  // Parse an OMM EPOCH string ("YYYY-MM-DD HH:MM:SS.ffffff") to Unix UTC
  // seconds (fractional). TZ-independent. Exposed for manual entry.
  static double gpEpochToUnix(const char* s);

  // Parse a SatNOGS /api/transmitters/ JSON array into out[0..maxN-1].
  // Returns number of (active) transponders parsed.
  static int parseTransmittersJson(const String& json,
                                   Transponder* out, int maxN);

  // Per-satellite transponder cache on LittleFS.
  static bool saveTxCache(uint32_t norad, const String& json);
  static int  loadTxCache(uint32_t norad, Transponder* out, int maxN);

  // Required FM uplink CTCSS (PL) tone in Hz for well-known FM satellites
  // (SatNOGS carries no structured tone field), or 0 if none/unknown.
  static float knownCtcssHz(uint32_t norad);

private:
  SatEntry _sats[MAX_SATS];
  int      _n = 0;
};
