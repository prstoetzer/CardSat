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
  bool     active   = true;  // SatNOGS status==active / alive==true (false => decommissioned/off)
  float    toneHz   = 0.0f;  // required FM uplink CTCSS/PL tone (0 = none)

  // Downlink passband width in Hz (0 for single-channel / FM).
  uint32_t bandwidth() const {
    return (downlinkHigh > downlink) ? (downlinkHigh - downlink) : 0;
  }

  // Two-way: has both an uplink and a downlink (transponder/transceiver), so it can
  // actually be worked -- as opposed to a one-way beacon/telemetry-only downlink.
  bool isTwoWay() const { return uplink != 0 && downlink != 0; }

  // True if a frequency (Hz) falls in an amateur-satellite allocation. Used to sort
  // non-amateur transmitters (e.g. Soyuz VHF, S-band TT&C) to the end of the list.
  // Note: downlink/uplink are uint32 Hz, so they top out at ~4.29 GHz; the higher
  // amateur microwave bands (5/3 cm) cannot be represented in these fields and are
  // therefore not tested here (no bird CardSat tunes reports them in this field).
  static bool freqIsAmateur(uint32_t hz) {
    if (hz == 0) return false;
    struct Band { uint32_t lo, hi; };
    static const Band AB[] = {
      {  28000000u,   29700000u},   // 10 m (HF sat downlinks ~29.3-29.5)
      { 144000000u,  148000000u},   // 2 m
      { 220000000u,  225000000u},   // 1.25 m
      { 420000000u,  450000000u},   // 70 cm
      { 902000000u,  928000000u},   // 33 cm
      {1240000000u, 1300000000u},   // 23 cm
      {2300000000u, 2450000000u},   // 13 cm
      {3300000000u, 3500000000u},   // 9 cm
    };
    for (const Band& b : AB) if (hz >= b.lo && hz <= b.hi) return true;
    return false;
  }

  // A transmitter is "amateur" if either leg is in an amateur allocation. SatNOGS's
  // own `service` field is unreliable here (often "Unknown" for clearly-amateur
  // transponders), so we judge by frequency.
  bool isAmateur() const { return freqIsAmateur(downlink) || freqIsAmateur(uplink); }
};

// One satellite's GP mean elements (the SGP4 inputs) plus identity.
struct SatEntry {
  char     name[26];          // AMSAT_NAME
  uint32_t norad = 0;         // NORAD_CAT_ID (identity / display)
  char     intlDes[12] = {0}; // OBJECT_ID, e.g. "1974-089B"
  double   epochUnix = 0;     // EPOCH as Unix UTC seconds (fractional) -- MUST stay double
                              //   (a ~1.7e9 value; float would round it to ~128 s steps)
  // --- BEGIN 0.9.41 float-elements optimisation (REVERSIBLE) -------------------
  // These eight mean elements are stored as float instead of double to shrink the
  // resident SatEntry (~32 bytes/entry, ~4-5 KB across MAX_SATS) and so leave more
  // contiguous heap for the mbedTLS handshake on this no-PSRAM part. This is SAFE
  // because the elements are never fed to SGP4 as raw numbers: gpToTle() formats
  // them into a fixed-width TLE text string (which SGP4 re-parses), and float's ~7
  // significant digits exceed every field's precision -- 4-decimal angles, 7-digit
  // eccentricity, ~5-digit bstar/ndot. (meanMotion is deliberately NOT here: its
  // %11.8f field wants ~10 sig figs, beyond float, so it stays double below.)
  //
  // TO REVERT to all-double: change the eight `float` lines below back to `double`.
  // No other code changes are needed -- every read/write site relies on implicit
  // float<->double conversion, and the TLE formatting promotes float to double for
  // %f automatically. (See docs/design/HEAP_FLOAT_ELEMENTS.md.)
  float    incl = 0;          // INCLINATION       (deg)    [float: 4-dp TLE field]
  float    ecc = 0;           // ECCENTRICITY      (dimensionless) [float: 7-digit field]
  float    raan = 0;          // RA_OF_ASC_NODE    (deg)    [float: 4-dp TLE field]
  float    argp = 0;          // ARG_OF_PERICENTER (deg)    [float: 4-dp TLE field]
  float    ma = 0;            // MEAN_ANOMALY      (deg)    [float: 4-dp TLE field]
  // --- END 0.9.41 float-elements optimisation ---------------------------------
  double   meanMotion = 0;    // MEAN_MOTION       (rev/day) -- MUST stay double
                              //   (%11.8f needs ~10 sig figs; float resolves only ~7)
  // --- 0.9.41 float-elements optimisation, continued (REVERSIBLE: -> double) ---
  float    bstar = 0;         // BSTAR             (1/earth radii) [float: ~5-digit field]
  float    ndot = 0;          // MEAN_MOTION_DOT   (rev/day^2, = ndot/2)  [float]
  float    nddot = 0;         // MEAN_MOTION_DDOT  (rev/day^3, = nddot/6) [float]
  // ----------------------------------------------------------------------------
  uint32_t revAtEpoch = 0;    // REV_AT_EPOCH
  uint16_t elsetNum = 0;      // ELEMENT_SET_NO
  bool     txLoaded = false;  // have we fetched transponders this session?
  uint8_t  amsatStatus = 0;   // AMSAT: 0 none, 1 heard, 2 not heard, 3 telemetry only
  uint32_t amsatHeardEpoch = 0; // UTC epoch of the winning report's latest_reported_time (0 = none)
  uint8_t  amsatReports = 0;  // report_count of the winning row (how many stations reported)
  char     amsatName[28] = ""; // the AMSAT API name of the matched status row (for reports.php)
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
  bool isManualGp(uint32_t norad);             // true if norad has a line in FILE_MGP
  bool removeManualGp(uint32_t norad);         // delete a hand-entered sat from FILE_MGP
  bool loadGpFromFs();                         // reload cached GP JSON at boot
  void applyAmsatStatusFile(const char* path); // set amsatStatus from a cached summary.php

  // AMSAT catalog name map: every entry of the status API's catalog.php, matched
  // to a catalog satellite. One sat can carry several entries (AO-7_[U/v] and
  // AO-7_[V/a]); the map is what makes reports and submissions mode-aware.
  static const int AMS_MAP_MAX = 96;
  static const int AMS_NAME_LEN = 28;
  struct AmsMapEnt { int16_t sat; char name[AMS_NAME_LEN]; };
  void applyAmsatCatalogFile(const char* path);   // (re)build the map from cached catalog.php
  int  amsFindByName(const char* apiName) const;  // exact API-name -> sat index, or -1
  int  amsNamesFor(int satIdx, const char* out[], int maxN) const; // this sat's API names
  int  amsMapCount() const { return _amsMapN; }
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
  // Streaming variant: parse straight from a File/Stream (e.g. the cache file)
  // so a large body never has to be held in one contiguous RAM String.
  static int parseTransmittersStream(Stream& src, Transponder* out, int maxN);

  // Per-satellite transponder cache on LittleFS.
  static bool saveTxCache(uint32_t norad, const String& json);
  static int  loadTxCache(uint32_t norad, Transponder* out, int maxN);
  static String txCachePath(uint32_t norad);   // path of the per-sat tx cache file

  // Required FM uplink CTCSS (PL) tone in Hz for well-known FM satellites
  // (SatNOGS carries no structured tone field), or 0 if none/unknown.
  static float knownCtcssHz(uint32_t norad);

private:
  AmsMapEnt _amsMap[AMS_MAP_MAX];
  int      _amsMapN = 0;
  SatEntry _sats[MAX_SATS];
  int      _n = 0;
};
