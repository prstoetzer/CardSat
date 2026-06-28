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

#include <miniz.h>           // ESP32 ROM miniz: tdefl_* raw DEFLATE + mz_crc32

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
  char d[9], t[7];
  strftime(d, sizeof(d), "%Y%m%d", g);
  strftime(t, sizeof(t), "%H%M%S", g);
  date = d; time = t;
}

// ---------------------------------------------------------------------------
// SIGNDATA: normalized ADIF fields in the EXACT order from developer-tq8,
// optional fields skipped when empty, no trailing spaces. For a satellite QSO
// CardSat maps uplink->BAND/FREQ (TX) and downlink->BAND_RX/FREQ_RX.
// ---------------------------------------------------------------------------
String Lotw::signData(const PendingQso& q) {
  double dlM = q.dlHz / 1e6, ulM = q.ulHz / 1e6;
  String date, tm; utcToAdif(q.utc, date, tm);
  char satnm[7] = ""; lotwSatResolveExt(q.sat, satnm);

  String s;
  adifT(s, "BAND",      bandUpper(ulM));
  if (dlM > 0) adifT(s, "BAND_RX", bandUpper(dlM));
  if (ulM > 0) adifT(s, "FREQ",    String(ulM, 4));
  if (dlM > 0) adifT(s, "FREQ_RX", String(dlM, 4));
  adifT(s, "MODE",      q.mode);
  adifT(s, "PROP_MODE", "SAT");
  adifT(s, "QSO_DATE",  date);
  adifT(s, "QSO_TIME",  tm);
  if (satnm[0]) adifT(s, "SAT_NAME", String(satnm));
  return s;
}

// One tCONTACT record (spaced outer fields) given the precomputed signature.
static String contactRec(const PendingQso& q, const String& sd,
                         const String& sigB64) {
  double dlM = q.dlHz / 1e6, ulM = q.ulHz / 1e6;
  String date, tm; utcToAdif(q.utc, date, tm);
  char satnm[7] = ""; lotwSatResolveExt(q.sat, satnm);

  String r;
  adifT(r, "Rec_Type", "tCONTACT");
  adifSp(r, "CALL",      q.call);
  adifSp(r, "BAND",      bandUpper(ulM));
  if (dlM > 0) adifSp(r, "BAND_RX", bandUpper(dlM));
  if (ulM > 0) adifSp(r, "FREQ",    String(ulM, 4));
  if (dlM > 0) adifSp(r, "FREQ_RX", String(dlM, 4));
  adifSp(r, "MODE",      q.mode);
  adifSp(r, "PROP_MODE", "SAT");
  adifSp(r, "QSO_DATE",  date);
  adifSp(r, "TIME_ON",   tm);
  if (satnm[0]) adifSp(r, "SAT_NAME", String(satnm));
  adifT(r, "SIGNDATA",      sd);
  adifT(r, "SIGN_LOTW_1.0", sigB64);
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
// gzip framing: raw DEFLATE (miniz tdefl) wrapped in a 10-byte gzip header and
// an 8-byte CRC32+ISIZE trailer. Validated against gunzip in docs/proto/lotw/.
// Streams the deflate output directly to the open file to avoid a second large
// RAM buffer on the no-PSRAM heap.
// ---------------------------------------------------------------------------
struct GzSink { File* f; bool err; };
static mz_bool gzPut(const void* buf, int len, void* user) {
  GzSink* s = (GzSink*)user;
  if (s->err) return MZ_FALSE;
  if (s->f->write((const uint8_t*)buf, len) != (size_t)len) { s->err = true; return MZ_FALSE; }
  return MZ_TRUE;
}

static bool gzipToFile(const String& text, const char* path, size_t* outBytes) {
  File f = Store::fs().open(path, "w");
  if (!f) return false;

  // gzip header: magic, method=deflate, flags=0, mtime=0, xfl=0, os=255(unknown).
  static const uint8_t hdr[10] = {0x1f,0x8b,0x08,0x00,0,0,0,0,0,0xff};
  if (f.write(hdr, sizeof(hdr)) != sizeof(hdr)) { f.close(); return false; }

  // raw DEFLATE (no zlib header) at the default probe depth. The ESP32 ROM miniz
  // omits tdefl_create_comp_flags_from_zip_params (it's gated behind the zlib API
  // macros), so we build the flags directly: TDEFL_DEFAULT_MAX_PROBES gives the
  // standard 128-probe search, and omitting TDEFL_WRITE_ZLIB_HEADER yields raw
  // deflate -- which is what our manual gzip framing wraps.
  GzSink sink{&f, false};
  tdefl_compressor* c = (tdefl_compressor*)malloc(sizeof(tdefl_compressor));
  if (!c) { f.close(); return false; }
  mz_uint flags = TDEFL_DEFAULT_MAX_PROBES;   // raw deflate, no zlib header/Adler-32
  tdefl_init(c, gzPut, &sink, flags);
  tdefl_status st = tdefl_compress_buffer(c, text.c_str(), text.length(), TDEFL_FINISH);
  free(c);
  if (st != TDEFL_STATUS_DONE || sink.err) { f.close(); return false; }

  // trailer: CRC32 of the uncompressed text, then ISIZE (mod 2^32), little-endian.
  // mz_crc32 (ROM miniz) is the standard gzip CRC-32 -- using it avoids the
  // bit-convention ambiguity of esp_rom_crc32_le and needs no extra dependency.
  uint32_t crc = (uint32_t)mz_crc32(MZ_CRC32_INIT, (const uint8_t*)text.c_str(), text.length());
  uint32_t isize = (uint32_t)text.length();
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
  String text;
  adifT(text, "TQSL_IDENT",
        String("TQSL CardSat ") + FW_VERSION + " Lib(CardSat) Config()");
  text += "\n";
  adifT(text, "Rec_Type", "tCERT");
  adifT(text, "CERTIFICATE", certB64);
  text += "<eor>\n";

  adifT(text, "Rec_Type", "tSTATION");
  adifSp(text, "CALL",       st.call);
  adifSp(text, "DXCC",       st.dxcc);
  adifSp(text, "GRIDSQUARE", st.grid);
  adifSp(text, "STATE",      st.state);
  adifSp(text, "CNTY",       st.cnty);
  adifSp(text, "CQZ",        st.cqz);
  adifSp(text, "ITUZ",       st.ituz);
  text += "<eor>\n";

  int signedN = 0;
  for (int i = 0; i < n; ++i) {
    const PendingQso& q = qsos[i];
    String sd = signData(q);

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

    text += contactRec(q, sd, sigB64);
    signedN++;
  }

  mbedtls_pk_free(&pk); mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
  if (err.length()) return false;
  if (signedN == 0) { err = "no QSOs to sign"; return false; }

  // --- gzip to the staged .tq8 file ---
  if (!gzipToFile(text, LOTW_TQ8_OUT, gzippedBytes)) { err = "gzip/write failed"; return false; }
  return true;
}
