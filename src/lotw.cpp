// LoTW .tq8 builder + signer. See lotw.h for the design and the host-verified
// provenance (docs/proto/lotw/). REQUIRES a microSD card.
#include "lotw.h"
#include "storage.h"
#include "config.h"

#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <mbedtls/base64.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/version.h>   // MBEDTLS_VERSION_MAJOR for the SHA-1 API guard

#include <miniz.h>           // ESP32 ROM miniz: mz_crc32 (gzip CRC-32; no allocation)

// ---------------------------------------------------------------------------
// ADIF field emitters. The OUTER record uses spaced fields (<NAME:len>val<sp>);
// SIGNDATA and structural tags are TIGHT (no trailing space). This split is
// load-bearing: LoTW's SIGNDATA must have no inter-field spaces or the signature
// won't verify (proven in docs/proto/lotw/).
// ---------------------------------------------------------------------------
static void adifSp(String& o, const char* name, const String& v) {
  if (!v.length()) return;
  o += "<"; o += name; o += ":"; o += String(v.length()); o += ">"; o += v; o += " ";
}
static void adifT(String& o, const char* name, const String& v) {
  o += "<"; o += name; o += ":"; o += String(v.length()); o += ">"; o += v;
}
// ADIF field with a type annotation: <NAME:LEN:TYPE>VALUE. tqsl emits the signature
// field this way via tqsl_adifMakeField(name, '6', ...), i.e. <SIGN_LOTW_V2.0:LEN:6>.
static void adifTy(String& o, const char* name, char type, const String& v) {
  o += "<"; o += name; o += ":"; o += String(v.length());
  o += ":"; o += type; o += ">"; o += v;
}

// SHA-1 one-shot, version-portable: mbedTLS 2.x exposes the *_ret forms (the
// un-suffixed ones are deprecated there); mbedTLS 3.x (ESP32) dropped the suffix.
// Selecting by version keeps both builds warning-clean.
static void sha1Hash(const uint8_t* in, size_t len, uint8_t out[20]) {
  mbedtls_sha1_context c; mbedtls_sha1_init(&c);
#if MBEDTLS_VERSION_MAJOR >= 3
  mbedtls_sha1_starts(&c); mbedtls_sha1_update(&c, in, len); mbedtls_sha1_finish(&c, out);
#else
  mbedtls_sha1_starts_ret(&c); mbedtls_sha1_update_ret(&c, in, len); mbedtls_sha1_finish_ret(&c, out);
#endif
  mbedtls_sha1_free(&c);
}

// ADIF 3.1.7 band enumeration, UPPER-CASE to match LoTW's documented examples
// (<BAND:4>70CM). Mirrors app.cpp's bandFor() ranges.
static String bandUpper(double mhz) {
  if (mhz >= 28    && mhz <= 29.7)  return "10M";
  if (mhz >= 50    && mhz <= 54)    return "6M";
  if (mhz >= 144   && mhz <= 148)   return "2M";
  if (mhz >= 222   && mhz <= 225)   return "1.25M";
  if (mhz >= 420   && mhz <= 450)   return "70CM";
  if (mhz >= 902   && mhz <= 928)   return "33CM";
  if (mhz >= 1240  && mhz <= 1300)  return "23CM";
  if (mhz >= 2300  && mhz <= 2450)  return "13CM";
  if (mhz >= 3300  && mhz <= 3500)  return "9CM";
  if (mhz >= 5650  && mhz <= 5925)  return "6CM";
  if (mhz >= 10000 && mhz <= 10500) return "3CM";
  return "";
}

// Resolve CardSat's sat name to a LoTW SAT_NAME (file map, then built-in table).
// Declared in app.cpp; reused here so the .tq8 and the ADIF export agree.
int lotwSatResolveExt(const char* amsat, char out[7]);

// Date/time helpers (UTC seconds -> ADIF YYYYMMDD / HHMMSS).
static void utcToAdif(uint32_t utc, String& date, String& time) {
  date = ""; time = "";
  if (!utc) return;
  time_t tt = (time_t)utc; struct tm* g = gmtime(&tt);
  if (!g) return;
  // TQSL's .tq8 uses TEXT date/time, not the compact ADIF forms: the date is
  // YYYY-MM-DD and the time is HH:MM:SSZ (tqsl_convertDateToText/TimeToText). LoTW's
  // parser rejects the compact 20260628 / 011800 forms as "Invalid Date/Time". The
  // SAME formatted strings go into both the tCONTACT record and the signed data (TQSL
  // signs the converted-to-text values), so producing them here fixes both at once.
  char d[12], t[12];
  strftime(d, sizeof(d), "%Y-%m-%d", g);
  strftime(t, sizeof(t), "%H:%M:%SZ", g);
  date = d; time = t;
}

// ---------------------------------------------------------------------------
// SIGNDATA for LoTW signature spec "LOTW V2.0" (reverse-engineered from tqsl
// 2.8.6 src/location.cpp make_sign_data + tqsl_getGABBItCONTACTData and the
// <sigspecs> in src/config.xml; the public developer-tq8 page documents the OLD
// 1.0 scheme and is wrong about what is signed -- see docs/design/LOTW_TQ8_FORMAT.md).
//
// The signed string is the concatenation of station field VALUES then contact
// field VALUES -- VALUES ONLY, no <adif:tags> -- in the exact sigspec order, with
// the whole result UPPERCASED. LoTW re-derives this same string and verifies the
// signature against it; any mismatch (tags, case, order, missing station data or
// worked CALL) makes LoTW silently drop the QSO even though the file is accepted.
//
//   tSTATION order (non-empty only): AU_STATE, CA_PROVINCE, CA_US_PARK, CN_PROVINCE,
//     CQZ, DX_US_PARK, FI_KUNTA, GRIDSQUARE, IOTA, ITUZ, JA_CITY_GUN_KU,
//     JA_PREFECTURE, RU_OBLAST, US_COUNTY, US_PARK, US_STATE
//     (CardSat populates only CQZ, GRIDSQUARE, ITUZ, US_COUNTY=cnty, US_STATE=state;
//      CALL and DXCC are deliberately NOT signed.)
//   tCONTACT order (non-empty only): BAND, BAND_RX, CALL, FREQ, FREQ_RX, MODE,
//     PROP_MODE, QSO_DATE, QSO_TIME, SAT_NAME   (the worked CALL IS included)
// ---------------------------------------------------------------------------
String Lotw::signData(const PendingQso& q, const LotwStation& st) {
  double dlM = q.dlHz / 1e6, ulM = q.ulHz / 1e6;
  String date, tm; utcToAdif(q.utc, date, tm);
  char satnm[7] = ""; lotwSatResolveExt(q.sat, satnm);

  String s;
  // --- station values, in sigspec order (only the fields CardSat can populate) ---
  if (st.cqz.length())   s += st.cqz;        // CQZ
  if (st.grid.length())  s += st.grid;       // GRIDSQUARE
  if (st.ituz.length())  s += st.ituz;       // ITUZ
  // US_COUNTY is signed as the county NAME ALONE (TQSL keeps state and county in
  // separate location fields, so its signed value is just "Arlington", not the
  // "ST,County" CardSat stores). Must match the value emitted in the tSTATION record.
  if (st.cnty.length()) {
    String cnty = st.cnty;
    int comma = cnty.indexOf(',');
    if (comma >= 0) cnty = cnty.substring(comma + 1);
    cnty.trim();
    s += cnty;                                // US_COUNTY
  }
  if (st.state.length()) s += st.state;      // US_STATE
  // --- contact values, in sigspec (alphabetical) order ---
  s += bandUpper(ulM);                        // BAND (uplink)
  if (dlM > 0) s += bandUpper(dlM);           // BAND_RX (downlink)
  s += q.call;                                // CALL (worked station)
  if (ulM > 0) s += String(ulM, 4);           // FREQ
  if (dlM > 0) s += String(dlM, 4);           // FREQ_RX
  s += q.mode;                                // MODE
  s += "SAT";                                 // PROP_MODE
  s += date;                                  // QSO_DATE
  s += tm;                                    // QSO_TIME
  if (satnm[0]) s += satnm;                   // SAT_NAME

  s.toUpperCase();                            // entire string is uppercased
  return s;
}

// One tCONTACT record given the precomputed signature. stationUid links the QSO to
// the tSTATION (and thus the certificate); LoTW drops the QSO without it. The
// SIGNDATA field stores the exact normalized string that was signed (V2.0: the
// uppercased station+contact values from signData), and the signature is emitted as
// <SIGN_LOTW_V2.0:LEN:6> (the ":6" is tqsl's ADIF type annotation).
static String contactRec(const PendingQso& q, const String& sd,
                         const String& sigB64, int stationUid) {
  double dlM = q.dlHz / 1e6, ulM = q.ulHz / 1e6;
  String date, tm; utcToAdif(q.utc, date, tm);
  char satnm[7] = ""; lotwSatResolveExt(q.sat, satnm);

  String r;
  adifT(r, "Rec_Type", "tCONTACT");
  adifSp(r, "STATION_UID", String(stationUid));
  adifSp(r, "CALL",      q.call);
  adifSp(r, "BAND",      bandUpper(ulM));
  if (dlM > 0) adifSp(r, "BAND_RX", bandUpper(dlM));
  if (ulM > 0) adifSp(r, "FREQ",    String(ulM, 4));
  if (dlM > 0) adifSp(r, "FREQ_RX", String(dlM, 4));
  adifSp(r, "MODE",      q.mode);
  adifSp(r, "PROP_MODE", "SAT");
  adifSp(r, "QSO_DATE",  date);
  adifSp(r, "QSO_TIME",  tm);          // TQSL uses QSO_TIME in the record, not TIME_ON
  if (satnm[0]) adifSp(r, "SAT_NAME", String(satnm));
  adifTy(r, "SIGN_LOTW_V2.0", '6', sigB64);
  adifT(r, "SIGNDATA",      sd);
  r += "<eor>\n";
  return r;
}

// base64-encode bytes via mbedTLS.
static String b64(const uint8_t* data, size_t len) {  size_t olen = 0;
  mbedtls_base64_encode(nullptr, 0, &olen, data, len);   // size query
  String out; out.reserve(olen + 1);
  uint8_t* buf = (uint8_t*)malloc(olen + 1);
  if (!buf) return String();
  if (mbedtls_base64_encode(buf, olen + 1, &olen, data, len) == 0) {
    buf[olen] = 0; out = (char*)buf;
  }
  free(buf);
  return out;
}

// Read an SD file fully into a String (small files: key/cert PEM are ~1-2 KB).
static bool readFile(const char* path, String& out) {
  File f = Store::fs().open(path, "r");
  if (!f) return false;
  out = ""; out.reserve(f.size() + 1);
  while (f.available()) out += (char)f.read();
  f.close();
  return true;
}

// ---------------------------------------------------------------------------
// gzip framing, STORED (uncompressed) DEFLATE blocks. A .tq8 is gzipped ADIF;
// TQSL compresses it, but a *stored* gzip stream is equally valid and LoTW accepts
// it. We use stored blocks deliberately: the miniz tdefl_compressor is ~160 KB+
// (a 32 KB LZ dictionary plus 64 KB m_next/m_hash tables), which cannot be
// allocated as one contiguous block on the no-PSRAM ESP32-S3 -- that was the
// "gzip/write failed" / "low memory (gzip)" failure. Stored framing needs ZERO
// working memory: a 5-byte header per <=64 KB block, no dictionary, no allocation.
// The upload is a few KB for a typical batch, so the lost compression is moot.
// Validated against gunzip host-side. CRC32 uses ROM miniz mz_crc32 (a function,
// no allocation). Streamed directly to the file -- no second RAM buffer.
// ---------------------------------------------------------------------------
static bool gzipToFile(const String& text, const char* path, size_t* outBytes) {
  File f = Store::fs().open(path, "w");
  if (!f) return false;

  // gzip header: magic, CM=8(deflate), FLG=0, MTIME=0, XFL=0, OS=255(unknown).
  static const uint8_t hdr[10] = {0x1f,0x8b,0x08,0x00,0,0,0,0,0,0xff};
  if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) { f.close(); return false; }

  // DEFLATE stored blocks. Each: 1 byte (BFINAL|BTYPE=00) + LEN(2,LE) + NLEN(2,LE)
  // + raw bytes. LEN <= 65535, so split long text across blocks; BFINAL set on the
  // last. An empty payload still emits one final zero-length block.
  const uint8_t* data = (const uint8_t*)text.c_str();
  size_t len = text.length(), off = 0;
  do {
    size_t chunk = len - off;
    if (chunk > 65535) chunk = 65535;
    uint8_t bfinal = (off + chunk >= len) ? 0x01 : 0x00;   // BFINAL, BTYPE=00 stored
    if (f.write(&bfinal, 1) != 1) { f.close(); return false; }
    uint16_t L = (uint16_t)chunk, N = (uint16_t)~L;
    uint8_t ln[4] = { (uint8_t)(L & 0xff), (uint8_t)(L >> 8),
                      (uint8_t)(N & 0xff), (uint8_t)(N >> 8) };
    if (f.write(ln, 4) != 4) { f.close(); return false; }
    if (chunk && f.write(data + off, chunk) != chunk) { f.close(); return false; }
    off += chunk;
  } while (off < len);

  // trailer: CRC32 of the uncompressed text, then ISIZE (mod 2^32), little-endian.
  // mz_crc32 (ROM miniz) is the standard gzip CRC-32 and allocates nothing.
  uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, data, len);
  uint32_t isize = (uint32_t)len;
  uint8_t tr[8] = {
    (uint8_t)(crc), (uint8_t)(crc>>8), (uint8_t)(crc>>16), (uint8_t)(crc>>24),
    (uint8_t)(isize), (uint8_t)(isize>>8), (uint8_t)(isize>>16), (uint8_t)(isize>>24)
  };
  if (f.write(tr, sizeof(tr)) != sizeof(tr)) { f.close(); return false; }

  if (outBytes) *outBytes = f.size();
  f.close();
  return true;
}

// ---------------------------------------------------------------------------
bool Lotw::credentialPresent() {
  return Store::fs().exists(LOTW_KEY_PEM) && Store::fs().exists(LOTW_CERT_PEM);
}

bool Lotw::buildTq8(const PendingQso* qsos, int n, const LotwStation& st,
                    const String& keyPass, String& err, size_t* gzippedBytes) {
  err = "";
  if (!Store::ready()) { err = "no SD card"; return false; }
  if (!credentialPresent()) { err = "LoTW key/cert not on SD"; return false; }

  // --- load + parse the private key (PEM, optionally password-protected) ---
  String keyPem, certPem;
  if (!readFile(LOTW_KEY_PEM, keyPem))  { err = "cannot read key";  return false; }
  if (!readFile(LOTW_CERT_PEM, certPem)){ err = "cannot read cert"; return false; }

  mbedtls_pk_context pk; mbedtls_pk_init(&pk);
  mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
  const char* pers = "cardsat-lotw";
  mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent,
                        (const uint8_t*)pers, strlen(pers));

  // mbedTLS 3.x parse_key takes an RNG for blinding; key buffer must be NUL-term'd
  // and the length INCLUDES the terminator for PEM.
  int rc = mbedtls_pk_parse_key(
      &pk, (const uint8_t*)keyPem.c_str(), keyPem.length() + 1,
      keyPass.length() ? (const uint8_t*)keyPass.c_str() : nullptr,
      keyPass.length(), mbedtls_ctr_drbg_random, &drbg);
  if (rc != 0) {
    err = (keyPass.length() ? "key unlock failed" : "key needs password");
    mbedtls_pk_free(&pk); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
    return false;
  }

  // --- cert PEM -> DER -> base64 (for the tCERT section) ---
  mbedtls_x509_crt crt; mbedtls_x509_crt_init(&crt);
  rc = mbedtls_x509_crt_parse(&crt, (const uint8_t*)certPem.c_str(),
                              certPem.length() + 1);
  if (rc != 0) {
    err = "cert parse failed";
    mbedtls_x509_crt_free(&crt); mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
    return false;
  }
  String certB64 = b64(crt.raw.p, crt.raw.len);     // crt.raw is the DER
  mbedtls_x509_crt_free(&crt);
  if (!certB64.length()) {
    err = "cert encode failed";
    mbedtls_pk_free(&pk); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
    return false;
  }

  // --- assemble the .tq8 text ---
  // A single certificate (CERT_UID 1) and single station location (STATION_UID 1)
  // per file; every tCONTACT references STATION_UID 1, which references CERT_UID 1.
  const int CERT_UID = 1, STATION_UID = 1;

  String text;
  adifT(text, "TQSL_IDENT",
        String("TQSL CardSat ") + FW_VERSION + " Lib(CardSat) Config()");
  text += "\n";
  adifT(text, "Rec_Type", "tCERT");
  adifT(text, "CERT_UID", String(CERT_UID));
  adifT(text, "CERTIFICATE", certB64);
  text += "<eor>\n";

  adifT(text, "Rec_Type", "tSTATION");
  adifSp(text, "STATION_UID", String(STATION_UID));
  adifSp(text, "CERT_UID",    String(CERT_UID));
  adifSp(text, "CALL",       st.call);
  adifSp(text, "DXCC",       st.dxcc);
  adifSp(text, "GRIDSQUARE", st.grid);
  // The tSTATION record uses TQSL's internal (gabbi) field names, which for the US
  // state/county are US_STATE / US_COUNTY -- NOT the ADIF names STATE / CNTY. LoTW's
  // parser doesn't recognize a bare CNTY/STATE here and rejects the whole tSTATION
  // (which then orphans every tCONTACT's STATION_UID). US_COUNTY also has a length
  // limit of 30, vs the tiny default applied to an unrecognized field.
  adifSp(text, "US_STATE",   st.state);
  // CardSat stores the county as "ST,County" (one field), but the US_COUNTY value LoTW
  // validates against its enumeration is the county NAME ALONE (e.g. "Arlington"); the
  // state is already carried in US_STATE. Sending "VA,Arlington" is rejected as an
  // invalid value, so strip everything up to and including the comma.
  {
    String cnty = st.cnty;
    int comma = cnty.indexOf(',');
    if (comma >= 0) cnty = cnty.substring(comma + 1);
    cnty.trim();
    adifSp(text, "US_COUNTY", cnty);
  }
  adifSp(text, "CQZ",        st.cqz);
  adifSp(text, "ITUZ",       st.ituz);
  text += "<eor>\n";

  int signedN = 0;
  for (int i = 0; i < n; ++i) {
    const PendingQso& q = qsos[i];
    String sd = signData(q, st);

    // SHA-1(SIGNDATA) -> RSA PKCS#1 v1.5 sign (mbedtls_pk_sign).
    uint8_t hash[20];
    sha1Hash((const uint8_t*)sd.c_str(), sd.length(), hash);
    uint8_t sig[512]; size_t siglen = 0;
    rc = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA1, hash, sizeof(hash),
                         sig, sizeof(sig), &siglen,
                         mbedtls_ctr_drbg_random, &drbg);
    if (rc != 0) { err = "sign failed"; break; }

    String sigB64 = b64(sig, siglen);
    if (!sigB64.length()) { err = "sig encode failed"; break; }

    text += contactRec(q, sd, sigB64, STATION_UID);
    signedN++;
  }

  mbedtls_pk_free(&pk); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
  if (err.length()) return false;
  if (signedN == 0) { err = "no QSOs to sign"; return false; }

  // --- gzip (stored/uncompressed framing -> zero working memory) to the .tq8 ---
  if (!gzipToFile(text, LOTW_TQ8_OUT, gzippedBytes)) { err = "gzip/write failed"; return false; }
  return true;
}
