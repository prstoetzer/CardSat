# CardSat v0.9.39 — release notes

Focused bug-fix release: the LoRa self-echo fix that came out of real two-way on-air
testing against the [CardSat Pager](https://github.com/prstoetzer/CardSatPager) on a
LilyGo T-LoRa device.

## LoRa messaging

- **Fixed: a sent message echoed back to you.** After transmitting on the LoRa Messages
  screen, your own message would reappear as if it had been received — often with leftover
  text from a previous message appended (e.g. sending "Hi" produced "Hi in FM18", reusing
  the tail of an earlier "59 in FM18"). It happened even with no other radio in range,
  because nothing was actually being received over the air.

  The cause: the SX1262's DIO1 interrupt signals **both** transmit-complete and
  receive-complete through a single flag. After a transmit, that flag was left set, so the
  next receive poll mistook the transmit-complete event for an incoming packet and read
  stale bytes out of the radio's buffer (which still held the frame you'd just sent). The
  flag is now cleared after each transmit, so only genuine receptions are processed.

- **LoRa messaging is now hardware-verified.** With this fix, two-way messaging between
  CardSat and a LilyGo T-LoRa unit running the companion CardSat Pager firmware is
  confirmed working on real hardware — the on-air frame format, sync word, and CRC all
  interoperate. The path is no longer marked experimental.

## Notes

- No orbital-engine, CAT, rotator, or settings changes; existing station settings, logs,
  and cached data carry over untouched.
