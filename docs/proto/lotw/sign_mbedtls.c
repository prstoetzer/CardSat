/* Prove CardSat's on-device signing path: load the private key (as the firmware
 * would after unlocking a .p12), SHA-1 the normalized SIGNDATA, RSA-PKCS1v15
 * sign it with mbedtls_pk_sign -- the exact primitives available on the ESP32-S3.
 * Output the raw signature so OpenSSL can verify it matches the spec. */
#include <stdio.h>
#include <string.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha1.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>

int main(void) {
    /* SIGNDATA exactly as the Python prototype produced it */
    const char* signdata =
      "<BAND:4>70CM<FREQ:7>435.100<MODE:3>SSB<PROP_MODE:3>SAT"
      "<QSO_DATE:8>20260627<QSO_TIME:6>180108<SAT_NAME:4>AO-7";

    /* 1) SHA-1 of SIGNDATA */
    unsigned char hash[20];
    mbedtls_sha1((const unsigned char*)signdata, strlen(signdata), hash);

    /* 2) load the private key (PEM here; on device, from the unlocked .p12) */
    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int rc = mbedtls_pk_parse_keyfile(&pk, "test_key.pem", NULL);
    if (rc) { printf("key parse failed: -0x%04x\n", -rc); return 1; }

    /* 3) RSA PKCS#1 v1.5 sign over the SHA-1 digest */
    mbedtls_entropy_context ent; mbedtls_ctr_drbg_context drbg;
    mbedtls_entropy_init(&ent); mbedtls_ctr_drbg_init(&drbg);
    const char* pers = "cardsat-lotw";
    mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent,
                          (const unsigned char*)pers, strlen(pers));

    unsigned char sig[512]; size_t siglen = 0;
    rc = mbedtls_pk_sign(&pk, MBEDTLS_MD_SHA1, hash, sizeof(hash),
                         sig, &siglen,
                         mbedtls_ctr_drbg_random, &drbg);
    if (rc) { printf("sign failed: -0x%04x\n", -rc); return 1; }

    FILE* f = fopen("sig_mbedtls.bin","wb");
    fwrite(sig, 1, siglen, f); fclose(f);
    printf("mbedtls signed OK: %zu bytes\n", siglen);

    mbedtls_pk_free(&pk);
    mbedtls_ctr_drbg_free(&drbg); mbedtls_entropy_free(&ent);
    return 0;
}
