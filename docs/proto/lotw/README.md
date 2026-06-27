# LoTW upload prototype

Verified host-side proof that CardSat can build and sign a LoTW `.tq8`.
No private keys are shipped here — generate a throwaway test cert to reproduce:

```sh
openssl req -x509 -newkey rsa:2048 -keyout test_key.pem -out test_cert.pem \
  -days 3650 -nodes -subj "/CN=W1AW"

python3 cardsat_lotw_reference.py        # builds + signs + gzips reference.tq8
gcc -o sign_mbedtls sign_mbedtls.c -lmbedcrypto && ./sign_mbedtls   # on-device path

# LoTW-style verification (must print "Verified OK"):
openssl x509 -in test_cert.pem -noout -pubkey > pubkey.pem
printf '%s' '<BAND:4>70CM<FREQ:7>435.100<MODE:3>SSB<PROP_MODE:3>SAT<QSO_DATE:8>20260627<QSO_TIME:6>180108<SAT_NAME:4>AO-7' > signdata
openssl dgst -sha1 -verify pubkey.pem -signature sig_mbedtls.bin signdata
```

See `INTEGRATION_PLAN.md` for the full results and the firmware integration plan.
