#pragma once
// ===========================================================================
//  csuh_proto.h  --  CardSat USB Helper (CSUH) wire protocol, v1
// ===========================================================================
//
//  ONE FILE, TWO COPIES. This header is shared verbatim between CardSat
//  (src/csuh_proto.h) and the companion firmware
//  (companion/CardSatUsbHelper/csuh_proto.h). The two MUST be byte-identical:
//  a protocol constant that drifts between the ends produces frames that decode
//  cleanly and mean the wrong thing, which is far worse than a link that fails.
//  tools/check_csuh_parity.py is the gate; run it before any delivery.
//
//  ---- WHAT THIS IS FOR --------------------------------------------------------
//
//  The ESP32-S3 has OTG_NUM_HOST_CHAN = 8 host channels for the WHOLE bus, one
//  per open pipe including each device's default control pipe. A hub costs 2, a
//  CDC radio 3, a vendor-serial adapter 4. The IC-705 contains its own internal
//  TI TUSB2046 hub, so it costs 5 on its own. That makes
//
//      hub + TH-D75 + IC-705 = 10        -- not solvable in software
//
//  because usb_host_interface_claim() allocates a pipe for EVERY endpoint of an
//  interface, all-or-nothing. A second MCU brings its own 8 channels, which is
//  the entire reason this protocol exists: CardSat keeps one USB device on the
//  Cardputer and hands the other to an M5StickS3 over the Grove UART.
//
//  ---- WHAT THE HELPER IS NOT --------------------------------------------------
//
//  It is a BYTE PIPE, not a radio. It speaks no CAT dialect, holds no radio
//  model, and knows nothing about Doppler, VFOs or rotator grammar. CardSat owns
//  every protocol decision exactly as it does for a local USB adapter; the helper
//  moves bytes, manages the CDC line state, and reports what is plugged in.
//  That is deliberate: the retired CardSatDualRig companion owned CAT state, and
//  keeping two independent radio catalogues honest across releases was a standing
//  cost that bought nothing once CAT_DUAL landed natively.
//
//  ---- FRAMING -----------------------------------------------------------------
//
//  Every frame is COBS-encoded and terminated by a single 0x00 delimiter, so 0x00
//  never appears inside a frame and a receiver can always resynchronise at the
//  next delimiter. That property is the reason for COBS rather than a magic byte
//  plus escapes: the helper is stateless and may reboot mid-session, and whatever
//  garbage its UART emits while its bootloader runs must not be able to desync the
//  host permanently.
//
//  Decoded frame layout:
//
//      [0]        TYPE          one of the CSUH_T_* codes below
//      [1]        PORT          reserved; always 0 in v1 (see CSUH_MAX_PORTS)
//      [2..n-3]   PAYLOAD       type-specific, 0..CSUH_MAX_PAYLOAD bytes
//      [n-2..n-1] CRC16         CCITT-FALSE over [0..n-3], LITTLE-endian
//
//  Minimum decoded frame is 4 bytes (TYPE, PORT, CRC lo, CRC hi).
//
//  Host->helper types are 0x01..0x7F, helper->host types are 0x80..0xFF. The
//  direction is implicit in who received the frame, so the split buys nothing at
//  runtime -- it buys a byte trace you can read without knowing which end dumped
//  it, which is worth one bit of type space.
//
//  ---- FLOW CONTROL ------------------------------------------------------------
//
//  Credit-based, symmetric, counted in FRAMES. Each side may have at most
//  CSUH_CREDIT_INIT DATA frames outstanding; the receiver returns credit as it
//  moves payload into its sink. The dangerous direction is helper->host: an Icom
//  in CI-V transceive mode emits unsolicited frames, and CardSat's main loop can
//  stall for tens of milliseconds inside a screen redraw. Under credit that
//  becomes back-pressure into the helper's ring (counted, reported via
//  CSUH_EV_OVERRUN if it ever overflows) instead of bytes vanishing off the wire.
//  A dropped CI-V byte does not look like a link fault -- it looks like a radio
//  fault, and would be chased as one.
//
//  ---- LINK BAUD AND AUTO-BAUD -------------------------------------------------
//
//  The helper keeps NO persistent configuration, so it cannot remember the link
//  rate. It scans CSUH_BAUDS[] until a frame passes CRC, then locks; after
//  CSUH_BAUD_RELOCK_MS with no valid frame it unlocks and scans again. CRC-checked
//  framing is what makes this safe -- at the wrong rate the decoder sees noise and
//  noise does not pass a CRC.
//
//  Default is 230400. The wire is a short 3.3 V Grove cable, but it runs beside a
//  transmitting radio, and the failure mode of pushing it faster is corrupted CAT
//  rather than a clean error. CSUH_T_STAT reports CRC and framing error counts so
//  "is the link clean at this rate" is a number the operator can read rather than
//  a thing to guess at.
// ===========================================================================
#include <stdint.h>
#include <stddef.h>

// ---- version + sizing -----------------------------------------------------
#define CSUH_PROTO_VER      1
// DATA payload bytes per frame. 128 keeps the whole encoded frame under 140
// bytes, which is ~6 ms at 230400 -- short enough that a frame in flight never
// dominates a CAT command's round trip.
#define CSUH_MAX_PAYLOAD    128
// Largest decoded frame: TYPE + PORT + payload + CRC16.
#define CSUH_MAX_FRAME      (2 + CSUH_MAX_PAYLOAD + 2)
// COBS worst case adds one overhead byte per 254, plus the leading code byte,
// plus the 0x00 delimiter.
#define CSUH_MAX_ENCODED    (CSUH_MAX_FRAME + (CSUH_MAX_FRAME / 254) + 2)
// v1 carries exactly one device. The PORT byte exists so a later revision can
// carry more without a framing change; v1 receivers reject a non-zero PORT
// rather than silently treating it as 0.
#define CSUH_MAX_PORTS      1
// DATA frames each side may have outstanding before it must wait for credit.
#define CSUH_CREDIT_INIT    8

// Longest device key ("vvvv:pppp/serial" or "vvvv:pppp@addr") and label the
// protocol will carry. Both match CardSat's own adapter-registry limits so a key
// never has to be truncated on the way across.
#define CSUH_MAX_KEY        40
#define CSUH_MAX_LABEL      48

// ---- link baud ------------------------------------------------------------
// Scanned in order by the helper's auto-baud, and offered in this order in
// CardSat's settings. Index 0 is the default.
#define CSUH_BAUD_N         3
static const uint32_t CSUH_BAUDS[CSUH_BAUD_N] = { 230400UL, 115200UL, 460800UL };
// How long the helper listens at one candidate rate before trying the next.
#define CSUH_BAUD_TRY_MS    400
// No valid frame for this long -> unlock and rescan.
#define CSUH_BAUD_RELOCK_MS 5000

// ---- frame types: host -> helper ------------------------------------------
#define CSUH_T_HELLO_REQ    0x01   // ()                      -> CSUH_T_HELLO
#define CSUH_T_ENUM_REQ     0x02   // ()                      -> 0..n CSUH_T_ENUM
#define CSUH_T_OPEN         0x03   // baud32,bits,par,stop,dtr,rts,keylen,key[]
#define CSUH_T_CLOSE        0x04   // ()
#define CSUH_T_DATA_OUT     0x05   // bytes -> the USB device
#define CSUH_T_MODEM        0x06   // dtr,rts
#define CSUH_T_PING         0x07   // token16
#define CSUH_T_CREDIT_OUT   0x08   // frames8  (host returns credit to helper)
#define CSUH_T_STAT_REQ     0x09   // ()                      -> CSUH_T_STAT
// Force a fresh USB enumeration pass. The helper implements this by REBOOTING
// itself, which is only sane because it is stateless: there is no configuration to
// lose, and a reboot is the one recovery that always works when the USB host stack
// has wedged (usb_host_install() returning 259 for the rest of a boot is a real
// failure mode -- see third_party/EspUsbHost/PATCHES.md). Tearing the host down
// in place would be the elegant version and is exactly the path with a history of
// getting stuck. The host detects the new epoch in CSUH_T_HELLO and re-OPENs.
#define CSUH_T_RESCAN       0x0A   // ()  reboot the helper and re-enumerate
#define CSUH_T_LINE         0x0B   // baud32,bits,par,stop  (re-coding an open port)
#define CSUH_T_WAKE         0x0C   // on8  ask the helper to light its screen

// ---- frame types: helper -> host ------------------------------------------
#define CSUH_T_HELLO        0x81   // ver,epoch32,maxPayload,credits,fwlen,fw[]
#define CSUH_T_ENUM         0x82   // index,count,flags,keylen,key[],lablen,label[]
#define CSUH_T_OPENED       0x83   // ok8,err8,namelen,name[]
#define CSUH_T_DATA_IN      0x84   // bytes <- from the USB device
#define CSUH_T_EVENT        0x85   // code8,detlen,detail[]
#define CSUH_T_PONG         0x86   // token16
#define CSUH_T_CREDIT_IN    0x87   // frames8  (helper returns credit to host)
#define CSUH_T_STAT         0x88   // see CsuhStat below

// ---- OPEN result codes ----------------------------------------------------
#define CSUH_ERR_NONE       0
#define CSUH_ERR_NODEV      1   // no device matches the requested key
#define CSUH_ERR_AMBIG      2   // key matched by VID:PID but >1 candidate; never guess
#define CSUH_ERR_BUSY       3   // reserved for a multi-port revision; v1 never sends it.
                                //   A v1 OPEN REPLACES any port already open: that is
                                //   what the host means when it re-OPENs after a settings
                                //   change, and making it say CLOSE first would add a
                                //   state the two ends could disagree about for nothing.
#define CSUH_ERR_HOST       4   // the USB host stack is not running
#define CSUH_ERR_BADARG     5   // malformed OPEN (bad baud/bits/parity/stop)
#define CSUH_ERR_NOTCDC     6   // device present but exposes no usable serial interface

// ---- EVENT codes ----------------------------------------------------------
#define CSUH_EV_ATTACH      1   // detail = device key
#define CSUH_EV_DETACH      2   // detail = device key
#define CSUH_EV_PORTLOST    3   // the OPEN device went away; host must re-OPEN
#define CSUH_EV_USBERR      4   // detail = human-readable host error
#define CSUH_EV_OVERRUN     5   // detail = decimal bytes lost from the USB->link ring
#define CSUH_EV_HOSTDOWN    6   // USB host failed to start; detail = reason
#define CSUH_EV_REBIND      7   // stale binding dropped and re-bound; detail = key
#define CSUH_EV_RESTART     8   // helper is rebooting now (CSUH_T_RESCAN); expect a new epoch

// ---- ENUM device flags ----------------------------------------------------
#define CSUH_DEV_LIVE       0x01   // still present (a tombstone reports 0)
#define CSUH_DEV_OPEN       0x02   // this device is the currently open port

// ---- parity encoding (matches HardwareSerial's convention) ----------------
#define CSUH_PAR_NONE       0
#define CSUH_PAR_ODD        1
#define CSUH_PAR_EVEN       2

// ---- CSUH_T_STAT payload (packed, little-endian) --------------------------
// Fixed layout rather than a struct memcpy: both ends are little-endian Xtensa
// today, but a wire format that depends on that is a trap for any future port.
// Offsets are explicit so the encoder and decoder can be read against each other.
// 0.9.73 bench extension: three trailing diagnostic bytes. This is a
// V1-COMPATIBLE TRAILING EXTENSION -- both receivers read fixed offsets and
// ignore bytes past what they know, so a 36-byte STAT from an older helper and a
// 39-byte STAT read by an older host both parse fine. Added because the first
// hardware session could not distinguish "no attach interrupt ever fired" from
// "devices enumerated but the serial-capability filter hid them" -- the screen
// showed zero either way, and those two faults have completely different causes.
#define CSUH_STAT_LEN       39
#define CSUH_STAT_O_SEEN    36   // u8: raw registry entries (incl. non-serial; hubs excluded)
#define CSUH_STAT_O_USABLE  37   // u8: entries passing the serial-capability filter
#define CSUH_STAT_O_HOSTUP  38   // u8: 1 = usb_host stack installed OK at boot
#define CSUH_STAT_O_FRAMESRX   0   // u32 frames decoded OK
#define CSUH_STAT_O_FRAMESTX   4   // u32 frames sent
#define CSUH_STAT_O_CRCERR     8   // u32 frames failed CRC
#define CSUH_STAT_O_COBSERR   12   // u32 frames failed COBS / length checks
#define CSUH_STAT_O_USBRX     16   // u32 bytes received from the USB device
#define CSUH_STAT_O_USBTX     20   // u32 bytes sent to the USB device
#define CSUH_STAT_O_OVERRUN   24   // u32 bytes lost to ring overflow
#define CSUH_STAT_O_HEAP      28   // u32 free heap
#define CSUH_STAT_O_UPTIME    32   // u32 seconds since helper boot

// ===========================================================================
//  CRC-16/CCITT-FALSE  (poly 0x1021, init 0xFFFF, no reflection, no final xor)
// ===========================================================================
static inline uint16_t csuhCrc16(const uint8_t* d, size_t n) {
  uint16_t c = 0xFFFF;
  for (size_t i = 0; i < n; ++i) {
    c ^= (uint16_t)d[i] << 8;
    for (int b = 0; b < 8; ++b)
      c = (c & 0x8000) ? (uint16_t)((c << 1) ^ 0x1021) : (uint16_t)(c << 1);
  }
  return c;
}

// ===========================================================================
//  COBS  (Consistent Overhead Byte Stuffing)
// ===========================================================================
//  Encodes src so the result contains no 0x00, letting a single 0x00 delimit
//  frames. Returns the encoded length, or 0 if dst is too small. The caller
//  appends the 0x00 delimiter; keeping it out of the encoder means the same
//  function can be tested against the reference vectors, which do not include it.
static inline size_t csuhCobsEncode(const uint8_t* src, size_t n,
                                    uint8_t* dst, size_t dstMax) {
  if (dstMax < n + (n / 254) + 1) return 0;
  size_t rd = 0, wr = 1, code_at = 0;
  uint8_t code = 1;
  while (rd < n) {
    if (src[rd] == 0) {
      dst[code_at] = code; code = 1; code_at = wr++; rd++;
    } else {
      dst[wr++] = src[rd++];
      if (++code == 0xFF && rd < n) { dst[code_at] = code; code = 1; code_at = wr++; }
    }
  }
  dst[code_at] = code;
  return wr;
}

//  Decodes one COBS block (the bytes between delimiters). Returns the decoded
//  length, or 0 on a malformed block. Zero is safe as the error value because a
//  zero-length CSUH frame is invalid anyway (the minimum is 4 bytes).
static inline size_t csuhCobsDecode(const uint8_t* src, size_t n,
                                    uint8_t* dst, size_t dstMax) {
  size_t rd = 0, wr = 0;
  while (rd < n) {
    uint8_t code = src[rd++];
    if (code == 0) return 0;                       // 0x00 cannot appear inside a block
    for (uint8_t i = 1; i < code; ++i) {
      if (rd >= n || wr >= dstMax) return 0;
      dst[wr++] = src[rd++];
    }
    // A run shorter than 0xFF implies a zero byte, EXCEPT at the very end of the
    // block where the trailing zero is the delimiter we already stripped.
    if (code != 0xFF && rd < n) {
      if (wr >= dstMax) return 0;
      dst[wr++] = 0;
    }
  }
  return wr;
}

// ===========================================================================
//  Frame build / verify helpers (shared by both ends)
// ===========================================================================
//  Build one encoded frame INCLUDING the trailing 0x00 delimiter. Returns the
//  number of bytes written to out, or 0 if it will not fit.
static inline size_t csuhBuildFrame(uint8_t type, uint8_t port,
                                    const uint8_t* payload, size_t plen,
                                    uint8_t* out, size_t outMax) {
  if (plen > CSUH_MAX_PAYLOAD) return 0;
  uint8_t raw[CSUH_MAX_FRAME];
  raw[0] = type; raw[1] = port;
  for (size_t i = 0; i < plen; ++i) raw[2 + i] = payload[i];
  const uint16_t crc = csuhCrc16(raw, 2 + plen);
  raw[2 + plen]     = (uint8_t)(crc & 0xFF);
  raw[2 + plen + 1] = (uint8_t)(crc >> 8);
  const size_t rawLen = 2 + plen + 2;
  if (outMax < 1) return 0;
  const size_t enc = csuhCobsEncode(raw, rawLen, out, outMax - 1);
  if (!enc) return 0;
  out[enc] = 0x00;
  return enc + 1;
}

//  Verify and split a DECODED frame. Returns true and fills type/port/payload on
//  success. `n` is the COBS-decoded length (delimiter already removed).
static inline bool csuhParseFrame(const uint8_t* raw, size_t n,
                                  uint8_t* type, uint8_t* port,
                                  const uint8_t** payload, size_t* plen) {
  if (n < 4 || n > CSUH_MAX_FRAME) return false;
  const uint16_t want = (uint16_t)raw[n - 2] | ((uint16_t)raw[n - 1] << 8);
  if (csuhCrc16(raw, n - 2) != want) return false;
  if (raw[1] >= CSUH_MAX_PORTS) return false;      // reserved field must be 0 in v1
  *type = raw[0]; *port = raw[1];
  *payload = raw + 2; *plen = n - 4;
  return true;
}
