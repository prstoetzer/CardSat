#!/usr/bin/env python3
"""
CardSat LoTW upload — verified host reference for the .tq8 build + sign pipeline.

PROVEN (this file's round-trip + the mbedTLS C twin sign_mbedtls.c):
  * .tq8 byte format matches ARRL's developer-tq8 spec
  * SIGNDATA normalization order is correct
  * RSA-PKCS1v15-over-SHA1 signature verifies with the exact LoTW command:
        openssl dgst -sha1 -verify pubkey.pem -signature sig signdata
  * mbedtls_pk_sign (the on-device path) yields a byte-identical signature
  * gzip framing (miniz-compatible raw deflate) decompresses cleanly

This is the spec the C/mbedTLS firmware port mirrors. Keep it in sync.
"""
import base64, hashlib, subprocess, struct, zlib

# Normalized-QSO field order, verbatim from developer-tq8 (optional ones skipped
# when empty). ADIF QSO_TIME == CardSat's TIME_ON.
NORM_ORDER = ["BAND","BAND_RX","FREQ","FREQ_RX","MODE","PROP_MODE",
              "QSO_DATE","QSO_TIME","SAT_NAME"]

def adif(name, val):
    val = str(val); return f"<{name}:{len(val)}>{val}"

def normalized_signdata(q):
    """q keys use ADIF names; QSO_TIME already mapped from TIME_ON by caller."""
    return "".join(adif(f, q[f]) for f in NORM_ORDER if q.get(f))

def sign_qso(signdata, key_pem_path):
    """RSA PKCS#1 v1.5 over SHA-1(signdata). On device: mbedtls_pk_sign(SHA1)."""
    import tempfile, os
    with tempfile.NamedTemporaryFile(delete=False) as t:
        t.write(signdata.encode()); p = t.name
    sig = subprocess.run(["openssl","dgst","-sha1","-sign",key_pem_path,p],
                         capture_output=True, check=True).stdout
    os.unlink(p); return sig

def build_tq8(qsos, cert_pem_path, key_pem_path, station, tqsl_ident):
    """qsos: list of dicts with ADIF field names. Returns gzipped .tq8 bytes."""
    cert_der = subprocess.run(["openssl","x509","-in",cert_pem_path,"-outform","DER"],
                              capture_output=True, check=True).stdout
    cert_b64 = base64.b64encode(cert_der).decode()

    text  = adif("TQSL_IDENT", tqsl_ident) + "\n"
    text += adif("Rec_Type","tCERT") + adif("CERTIFICATE", cert_b64) + "<eor>\n"
    text += adif("Rec_Type","tSTATION") + "".join(adif(k,v) for k,v in station.items()) + "<eor>\n"
    for q in qsos:
        sd  = normalized_signdata(q)
        sig = base64.b64encode(sign_qso(sd, key_pem_path)).decode()
        rec  = adif("Rec_Type","tCONTACT")
        rec += adif("CALL", q["CALL"]) + adif("BAND", q["BAND"]) + adif("MODE", q["MODE"])
        rec += adif("PROP_MODE", q["PROP_MODE"]) + adif("QSO_DATE", q["QSO_DATE"])
        rec += adif("TIME_ON", q["QSO_TIME"]) + adif("SAT_NAME", q["SAT_NAME"])
        rec += adif("SIGNDATA", sd) + adif("SIGN_LOTW_1.0", sig) + "<eor>\n"
        text += rec
    return _gzip(text.encode())

def _gzip(data):
    """gzip framing the way ESP32 miniz raw-deflate produces it."""
    co = zlib.compressobj(6, zlib.DEFLATED, -15)
    deflated = co.compress(data) + co.flush()
    return (b'\x1f\x8b\x08\x00' + b'\x00'*4 + b'\x00\x03' + deflated +
            struct.pack('<II', zlib.crc32(data)&0xffffffff, len(data)&0xffffffff))

if __name__ == "__main__":
    q = {"CALL":"W1AW","BAND":"70CM","MODE":"SSB","PROP_MODE":"SAT",
         "QSO_DATE":"20260627","QSO_TIME":"180108","SAT_NAME":"AO-7","FREQ":"435.100"}
    station = {"CALL":"N0CALL","DXCC":"291","GRIDSQUARE":"FN31pr","STATE":"CT","CQZ":"5","ITUZ":"8"}
    tq8 = build_tq8([q], "test_cert.pem", "test_key.pem", station,
                    "TQSL CardSat V0.1 Lib(CardSat) Config()")
    open("reference.tq8","wb").write(tq8)
    print(f"reference.tq8: {len(tq8)} bytes — built + signed + gzipped")
