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
  // Frequencies are freq_t (64-bit) Hz (0.9.62), so microwave transponders above the
  // old ~4.294 GHz uint32 ceiling -- QO-100 10489.75 MHz, 5.7/10/24 GHz -- are
  // representable, stored, and displayed. High-edge fields stay absolute (not deltas)
  // so all readers need only a type change. See docs/design/HIGH_FREQ_SCOPE.md.
  char     desc[40];
  freq_t   downlink     = 0; // Hz (downlink_low;  0 if none)
  freq_t   downlinkHigh = 0; // Hz (downlink_high; 0 if single-channel)
  freq_t   uplink       = 0; // Hz (uplink_low;    0 if none / beacon)
  freq_t   uplinkHigh   = 0; // Hz (uplink_high;   0 if single-channel)
  char     mode[12] = {0};   // e.g. "FM", "USB", "DATA"
  bool     invert   = false; // inverting linear transponder
  bool     isLinear = false; // true => has a tunable passband (do passband tracking)
  bool     active   = true;  // SatNOGS status==active / alive==true (false => decommissioned/off)
  uint32_t baud     = 0;     // SatNOGS baud (data rate; CW = WPM). 0 = not specified
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
  static bool freqIsAmateur(freq_t hz) {
    if (hz == 0) return false;
    struct Band { freq_t lo, hi; };
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
  // FIELD ORDER IS DELIBERATE (0.9.59): members are grouped largest-alignment-first
  // -- 8-byte doubles, then 4-byte words, then 2-byte, then the char arrays, then the
  // 1-byte flags last -- so the compiler inserts no internal alignment padding. The
  // previous source-logical order left four holes (a 4-byte gap before meanMotion,
  // tail pad after name/intlDes, trailing struct pad) totalling 8 bytes/entry; at
  // MAX_SATS that was ~1.2 KB of dead RAM in the single largest resident block on a
  // no-PSRAM part. Reordering is purely a memory-layout change: every access is by
  // name and nothing here depends on declaration order. Keep new fields in the right
  // size bucket when adding them. (Verified against the DWARF layout in the 0.9.59
  // compiler-output audit; see docs/design/RAM_AUDIT_0_9_59.md.)

  // -- 8-byte: doubles (MUST stay double; see notes) --
  double   epochUnix = 0;     // EPOCH as Unix UTC seconds (FRACTIONAL) -- MUST stay double.
                              //   Not just for magnitude (~1.7e9) but for the sub-second
                              //   fraction: gpToTle() renders it into the TLE epoch as
                              //   %012.8f day-of-year (~0.86 ms), and tsince is measured
                              //   from this instant. float (~128 s steps) OR a whole-second
                              //   integer would corrupt the epoch SGP4 re-parses.
  double   meanMotion = 0;    // MEAN_MOTION       (rev/day) -- MUST stay double
                              //   (%11.8f needs ~10 sig figs; float resolves only ~7)

  // -- 4-byte: identifiers, counters, and the float mean elements --
  uint32_t norad = 0;         // NORAD_CAT_ID (identity / display)
  uint32_t revAtEpoch = 0;    // REV_AT_EPOCH
  uint32_t amsatHeardEpoch = 0; // UTC epoch of the winning report's latest_reported_time (0 = none)
  // --- BEGIN 0.9.41 float-elements optimisation (REVERSIBLE) -------------------
  // These eight mean elements are stored as float instead of double to shrink the
  // resident SatEntry and so leave more contiguous heap for the TLS handshake on this
  // no-PSRAM part (mbedTLS when this was written; BearSSL since 0.9.43 -- the contiguity
  // motive is unchanged). This is SAFE because the elements are never fed to SGP4 as raw
  // numbers: gpToTle() formats them into a fixed-width TLE text string (which SGP4
  // re-parses), and float's ~7 significant digits exceed every field's precision --
  // 4-decimal angles, 7-digit eccentricity, ~5-digit bstar/ndot. (meanMotion is
  // deliberately NOT here: its %11.8f field wants ~10 sig figs, beyond float, so it
  // stays double above.)
  //
  // TO REVERT to all-double: change the eight `float` lines below back to `double` AND
  // move them into the 8-byte group above (to keep the no-padding invariant). Every
  // read/write site relies on implicit float<->double conversion, and the TLE
  // formatting promotes float to double for %f automatically. (See
  // docs/design/HEAP_FLOAT_ELEMENTS.md.)
  float    incl = 0;          // INCLINATION       (deg)    [float: 4-dp TLE field]
  float    ecc = 0;           // ECCENTRICITY      (dimensionless) [float: 7-digit field]
  float    raan = 0;          // RA_OF_ASC_NODE    (deg)    [float: 4-dp TLE field]
  float    argp = 0;          // ARG_OF_PERICENTER (deg)    [float: 4-dp TLE field]
  float    ma = 0;            // MEAN_ANOMALY      (deg)    [float: 4-dp TLE field]
  float    bstar = 0;         // BSTAR             (1/earth radii) [float: ~5-digit field]
  float    ndot = 0;          // MEAN_MOTION_DOT   (rev/day^2, = ndot/2)  [float]
  float    nddot = 0;         // MEAN_MOTION_DDOT  (rev/day^3, = nddot/6) [float]
  // --- END 0.9.41 float-elements optimisation ---------------------------------

  // -- char arrays (byte-aligned; grouped so no field forces re-alignment after) --
  char     name[26];          // AMSAT_NAME
  char     amsatName[28] = ""; // the AMSAT API name of the matched status row (for reports.php)
  char     intlDes[12] = {0}; // OBJECT_ID, e.g. "1974-089B"

  // -- 2-byte then 1-byte: keeps the trailing pad minimal --
  uint16_t elsetNum = 0;      // ELEMENT_SET_NO
  bool     txLoaded = false;  // have we fetched transponders this session?
  uint8_t  amsatStatus = 0;   // AMSAT: 0 none, 1 heard, 2 not heard, 3 telemetry only
  uint8_t  amsatReports = 0;  // report_count of the winning row (how many stations reported)
};

class SatDb {
public:
  bool begin();                  // mount LittleFS
  int  count() const { return _n; }
  int  seenCount() const { return _seen; }     // total objects in the last-parsed file (>= count())
  bool wasTruncated() const { return _seen > _n; }
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
  bool loadGpFromFsPreferring(const uint32_t* favs, int favN);  // ...keeping favorites (see .cpp)
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
  // Resolve an external service's satellite name (AMSAT status, hams.at, LoTW) to a
  // catalog index, source-independently: parenthesised-designator equality first
  // (bridges CelesTrak's "FOX-1B (AO-91)" to a service's "AO-91"), then whole-name
  // equality, then delimited-token containment -- the same normalized primitives the
  // AMSAT status matcher uses. Returns -1 when nothing matches.
  int  findByServiceName(const char* svc) const;
  int  amsMapCount() const { return _amsMapN; }
  int  loadGpFromFile(const char* path);       // stream-parse a GP file (low RAM)
  // Favorites-first priority load: guarantees every NORAD in favs[] is loaded even if it
  // sits past the MAX_SATS-th object in the file, then fills remaining slots in file order.
  // Also records seenCount() so the caller can report "loaded X of Y". favs may be null.
  int  loadGpFromFilePreferring(const char* path, const uint32_t* favs, int favN);
  bool saveGpJson(const String& json);         // cache the downloaded blob

  // ---- CelesTrak extra favorites (FILE_CTX) -----------------------------------------
  // Objects the user added from the CelesTrak search screen that are NOT covered by the
  // primary catalog source. Persisted one OMM object per line like FILE_MGP, but with a
  // crucial difference in lifecycle: on every GP update the firmware re-fetches each of
  // these from CelesTrak (courtesy-throttled), so their elements stay fresh. Hand-entered
  // FILE_MGP satellites (state-vector fits, pre-launch objects) are deliberately NEVER
  // auto-fetched -- many aren't in the public catalog at all.
  static void writeGpLine(File& f, const SatEntry& s);   // one compact OMM object + newline
  bool addCtExtra(const SatEntry& s);          // append to FILE_CTX (no-op if norad present)
  bool isCtExtra(uint32_t norad);              // has a line in FILE_CTX
  bool removeCtExtra(uint32_t norad);          // drop its line from FILE_CTX
  // Merge FILE_CTX into the loaded catalog. Skip-if-present (a primary-catalog entry is
  // fresher than our line); when the DB is full, evict the last non-favorite to make room
  // (extras are explicit user picks and outrank file-order fills, same philosophy as the
  // favorites-preferring loader). Returns the number merged.
  int  loadCtExtraFile(const uint32_t* favs, int favN);
  // List the NORAD ids present in an NDJSON GP file (one object per line). Returns count
  // written (capped at maxN; out may be null with maxN 0 to just count); *total
  // (optional) receives the number of lines seen.
  static int listNdjsonNorads(const char* path, uint32_t* out, int maxN, int* total);
  // NORAD id from one raw NDJSON GP line WITHOUT a JsonDocument -- the per-line
  // alloc/free of a document is exactly the no-PSRAM fragmentation pattern the bulk
  // parser avoids (see gpFindValue), so every norad-only file walk uses this instead.
  static uint32_t gpLineNorad(const char* line, size_t len);

  // Stream a GP/OMM JSON file object-by-object into a caller sink WITHOUT touching the
  // resident catalog -- for transient screens (e.g. the debris-group tool). Same flat-RAM
  // streaming and allocation-free field parse as scanGpFile; sink(entry, ctx) is called for
  // each parsed object. Returns the number of objects parsed. GP JSON, not TLE: the legacy
  // TLE format's 5-digit catalog field can't represent newer objects.
  static int streamGpFileEntries(const char* path,
                                 void (*sink)(const SatEntry&, void*), void* ctx);

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
  int      _seen = 0;         // total objects seen in the last parse (for truncation reporting)
  // Shared streaming scanner behind loadGpFromFile / loadGpFromFilePreferring. accept(norad,ctx)
  // returns true to keep an object; null accept means "take in file order until full".
  int      scanGpFile(const char* path, bool (*accept)(uint32_t, void*), void* ctx, int* loaded);
};
