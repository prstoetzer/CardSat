// LoTW (Logbook of the World) direct upload for CardSat.
//
// Builds a cryptographically-signed .tq8 from logged satellite QSOs and uploads
// it to ARRL's self-authenticating LoTW web service. REQUIRES a microSD card:
// the user's callsign-certificate private key + cert live there as PEM files,
// and the staged .tq8 is written there before upload.
//
// The .tq8 byte format, the SIGNDATA normalization order, and the
// RSA-PKCS1v15-over-SHA1 signature were validated host-side against ARRL's
// developer-tq8 spec and OpenSSL's verifier (see docs/proto/lotw/). The signing
// path uses mbedtls_pk_sign(SHA1) -- proven byte-identical to OpenSSL.
//
// This is an UPLOAD feature, not enrollment: first-time certificate issuance is
// gated by ARRL behind TQSL + a mailed postcard and cannot happen on-device. The
// user exports their existing credential to the SD card once (see the manual).
#pragma once
#include <Arduino.h>
#include "app.h"     // PendingQso

// SD-card paths for the user-supplied credential (PEM, extracted once from a .p12).
#define LOTW_KEY_PEM   "/CardSat/lotw_key.pem"
#define LOTW_CERT_PEM  "/CardSat/lotw_cert.pem"
#define LOTW_TQ8_OUT   "/CardSat/lotw_upload.tq8"

// Station-location fields LoTW needs beyond the per-QSO data. Grid + call already
// live in CardSat config; DXCC and zones come from the LoTW settings group.
struct LotwStation {
  String call;       // STATION_CALLSIGN / cert call
  String dxcc;       // DXCC entity number (e.g. "291" = USA)
  String grid;       // Maidenhead grid
  String cqz;        // CQ zone
  String ituz;       // ITU zone
  String state;      // US/AK/HI: STATE (2-letter); required by LoTW for those DXCCs
  String cnty;       // US: county as "STATE,County" (ADIF MY_CNTY form); optional
};

// Result of a build+upload attempt.
struct LotwResult {
  bool   ok = false;        // overall success (built, signed, uploaded, accepted)
  int    signed_n = 0;      // QSOs signed into the .tq8
  int    accepted = 0;      // QSOs LoTW reported accepted (parsed from response)
  int    dupes = 0;         // QSOs LoTW reported as duplicates
  String message;           // human-readable status / error
};

class Lotw {
public:
  // Build a .tq8 at LOTW_TQ8_OUT from the given QSOs, signing each with the
  // SD-card key (unlocked with keyPass, "" if the key is unencrypted). Returns
  // false and sets err on any failure (missing card/key, parse, sign, gzip).
  // gzippedBytes receives the on-disk .tq8 size on success.
  static bool buildTq8(const PendingQso* qsos, int n,
                       const LotwStation& st, const String& keyPass,
                       String& err, size_t* gzippedBytes = nullptr);

  // Build the normalized SIGNDATA for one QSO (exact developer-tq8 field order,
  // no trailing spaces). Public so it can be unit-checked.
  static String signData(const PendingQso& q);

  // Convenience: are the credential files present on the SD card?
  static bool credentialPresent();
};
