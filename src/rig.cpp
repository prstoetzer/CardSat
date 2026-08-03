// ===========================================================================
//  rig.cpp  -  Rig factory + shared helpers
// ===========================================================================
#include "rig.h"
#include "settings.h"   // CatType enum (avoid raw catType magic numbers)
#include "civ.h"
#include "icomnet.h"
#include "yaesu.h"
#include "kenwood.h"

// CAT serial trace sink (see rig.h). Null unless the serial-terminal screen sets
// it; catTrace() is the null-safe wrapper the backends call on every frame.
CatTraceFn catTraceSink = nullptr;
void catTrace(const char* dir, const uint8_t* b, size_t n) {
  if (catTraceSink) catTraceSink(dir, b, n);
}

RigMode Rig::modeFromString(const String& s) {
  String u = s; u.toUpperCase();
  if (u.indexOf("FM")  >= 0) return RM_FM;
  if (u.indexOf("USB") >= 0) return RM_USB;
  if (u.indexOf("LSB") >= 0) return RM_LSB;
  if (u.indexOf("CW")  >= 0) return RM_CW;
  if (u.indexOf("AM")  >= 0) return RM_AM;
  if (u.indexOf("FSK") >= 0 || u.indexOf("RTTY") >= 0 ||
      u.indexOf("DATA") >= 0 || u.indexOf("DIG") >= 0) return RM_DATA;
  // Linear transponders are most often operated USB up / USB down.
  return RM_USB;
}

Rig* makeRig(RadioModel model, uint8_t catType, const char* host,
             uint16_t port, const char* user, const char* pass,
             uint32_t groveBaud) {
  if (model == RIG_NONE) return nullptr;   // no radio: CardSat runs as a tracker only
  // M24: nothrow allocation throughout. On low contiguous heap, return nullptr so the
  // caller (which already null-checks rig) reports "radio not ready" instead of crashing.
  if (catType == CAT_RIGCTL) {            // rigctld client (TCP): model-agnostic
    (void)user; (void)pass;
    return new (std::nothrow) RigctlRig(host, port);
  }
  if (catType == CAT_RIGCTL_GROVE) {      // rigctld client over the Grove UART (no Wi-Fi)
    (void)host; (void)port; (void)user; (void)pass;
    return new (std::nothrow) RigctlGroveRig(groveBaud ? groveBaud : 115200);   // C2: dedicated 32-bit baud
  }
  // Icom LAN (RS-BA1 UDP) network CAT: only for CI-V models.
  if (catType == CAT_NET && RADIOS[model].proto == PROTO_CIV)
    return new (std::nothrow) IcomNetRig(model, host, port, user, pass);
  switch (RADIOS[model].proto) {
    case PROTO_YAESU:   return new (std::nothrow) YaesuRig(model);
    case PROTO_KENWOOD: return new (std::nothrow) KenwoodRig(model);
    case PROTO_CIV:
    default:            return new (std::nothrow) CivRig(model);
  }
}

// Standard 39 EIA CTCSS tones in tenths of Hz, ascending. This exact order is
// shared with Hamlib's ft847_ctcss_list[] and the Kenwood tone list, so the
// index doubles as the Kenwood tone number (index+1) and the row into the
// FT-847 CAT code table. Icom encodes the frequency in BCD instead.
static const uint16_t CTCSS_TENTHS[39] = {
  670, 693, 719, 744, 770, 797, 825, 854, 885, 915,
  948, 974, 1000,1035,1072,1109,1148,1188,1230,1273,
  1318,1365,1413,1462,1514,1567,1622,1679,1738,1799,
  1862,1928,2035,2107,2181,2257,2336,2418,2503
};

int ctcssToneIndex(float hz) {
  if (hz <= 0) return -1;
  int target = (int)lroundf(hz * 10.0f);   // tenths of Hz
  int best = -1, bestErr = 9999;
  for (int i = 0; i < 39; ++i) {
    int e = abs((int)CTCSS_TENTHS[i] - target);
    if (e < bestErr) { bestErr = e; best = i; }
  }
  // Reject if the nearest standard tone is more than ~1 Hz away (bad input).
  return (bestErr <= 10) ? best : -1;
}

float ctcssToneHz(int index) {
  if (index < 0 || index >= 39) return 0.0f;
  return CTCSS_TENTHS[index] / 10.0f;
}

// ---------------------------------------------------------------------------
//  Dual-rig legs (CAT_DUAL): dialect frame builders, PlainCatRig, DualRig
// ---------------------------------------------------------------------------
//  Ported from companion/CardSatDualRig (the bench-validated encoders): CI-V
//  plain set (cmd 05/06/03), Yaesu 5-byte binary (opcode 01/07/03), Yaesu ASCII
//  (FA/MD0x), Kenwood TH-D7x (FQ/MD on Band B -- the handheld's all-mode SSB/CW
//  receiver lives on Band B only, Band A is FM/DV). The builders are PURE so
//  tools/host_dualrig can byte-verify them without hardware.

// -- BCD helpers (leg-local; civ.cpp has its own file-static copies) ----------
// CI-V frequency: little-endian BCD, two digits per byte, 1 Hz resolution. Five
// bytes (ten digits) is the universal form; the IC-905 uses six above 5.85 GHz.
static void legCivPackFreqN(uint64_t hz, uint8_t* out, int nBytes) {
  for (int i = 0; i < nBytes; i++) {
    uint8_t lo = hz % 10; hz /= 10;
    uint8_t hi = hz % 10; hz /= 10;
    out[i] = (uint8_t)((hi << 4) | lo);
  }
}
static void legCivPackFreq(uint64_t hz, uint8_t out[5]) { legCivPackFreqN(hz, out, 5); }
// Above this, the IC-905 expects the six-byte field (Hamlib icom.c).
static const uint64_t LEG_CIV_WIDE_HZ = 5850000000ULL;
static uint64_t legCivUnpackFreq(const uint8_t* b) {
  uint64_t hz = 0;
  for (int i = 4; i >= 0; i--) hz = hz * 100 + (b[i] >> 4) * 10 + (b[i] & 0x0F);
  return hz;
}
// FT-100 (LEGF_Y100) uses LITTLE-endian BCD -- Hamlib's to_bcd(), where the FT-817
// family uses to_bcd_be(). Same four bytes, opposite order.
static void legY100PackFreq(uint64_t hz, uint8_t out[4]) {
  uint32_t f = (uint32_t)((hz + 5) / 10);
  for (int i = 0; i < 4; ++i) {
    out[i] = (uint8_t)(((f / 10) % 10) << 4 | (f % 10));
    f /= 100;
  }
}
static uint64_t legY100UnpackFreq(const uint8_t* b) {
  uint64_t f = 0, mul = 1;
  for (int i = 0; i < 4; ++i) {
    f += ((b[i] & 0x0F) + (uint64_t)(b[i] >> 4) * 10) * mul;
    mul *= 100;
  }
  return f * 10ULL;
}
// FT-100 mode values, in data[3] with opcode 0x0C (Hamlib ft100.c):
//   00 LSB  01 USB  02 CW  03 CWR  04 AM  05 DIG  06 FM
static uint8_t legY100ModeByte(RigMode m) {
  switch (m) { case RM_LSB: return 0x00; case RM_USB: return 0x01;
               case RM_CW:  return 0x02; case RM_AM:  return 0x04;
               case RM_DATA: return 0x05; case RM_FM: return 0x06;
               default: return 0x01; }
}
// VR-5000 (LEGF_YVR5): FT-817 framing, but FM is 0x88 (MODE_FMN) -- plain 0x08 does
// not appear in this receiver's table at all (Hamlib vr5000.c).
static uint8_t legYVr5ModeByte(RigMode m) {
  switch (m) { case RM_LSB: return 0x00; case RM_USB: return 0x01;
               case RM_CW:  return 0x02; case RM_AM:  return 0x04;
               case RM_FM:  return 0x88; case RM_DATA: return 0x01;
               default: return 0x01; }
}

static void legYBinPackFreq(uint64_t hz, uint8_t out[4]) {
  // +5 before the divide: round to the nearest 10 Hz rather than truncating, which
  // is what Hamlib's ft857/ft897 backends do.
  uint32_t f = (uint32_t)((hz + 5) / 10);                  // Yaesu binary is 10 Hz units
  out[0] = (uint8_t)((((f/10000000)%10)<<4) | ((f/1000000)%10));
  out[1] = (uint8_t)((((f/100000)%10)<<4)   | ((f/10000)%10));
  out[2] = (uint8_t)((((f/1000)%10)<<4)     | ((f/100)%10));
  out[3] = (uint8_t)((((f/10)%10)<<4)       | (f%10));
}
static uint64_t legYBinUnpackFreq(const uint8_t* b) {
  uint64_t f = 0;
  for (int i = 0; i < 4; i++) f = f * 100 + (b[i] >> 4) * 10 + (b[i] & 0x0F);
  return f * 10ULL;
}
// -- per-family mode bytes (RigMode -> wire). CardSat has no CWR mode, so the
//    companion's CWR rows are unreachable here and intentionally not carried. --
static uint8_t legCivModeByte(RigMode m) {
  switch (m) { case RM_LSB: return 0x00; case RM_USB: return 0x01;
               case RM_AM:  return 0x02; case RM_CW:  return 0x03;
               case RM_FM:  return 0x05; case RM_DATA: return 0x01;
               default: return 0x01; }
}
static uint8_t legYBinModeByte(RigMode m) {
  switch (m) { case RM_LSB: return 0x00; case RM_USB: return 0x01;
               case RM_CW:  return 0x02; case RM_AM:  return 0x04;
               case RM_FM:  return 0x08; case RM_DATA: return 0x0A;
               default: return 0x01; }
}
static char legYTxtModeDigit(RigMode m) {
  switch (m) { case RM_LSB: return '1'; case RM_USB: return '2';
               case RM_CW:  return '3'; case RM_FM:  return '4';
               case RM_AM:  return '5'; case RM_DATA: return 'C';
               default: return '2'; }
}
// Band/VFO selector character: '0' = VFO A, '1' = VFO B. Band B carries the
// all-mode (SSB/CW/AM) receiver on the TH-D74/D75, which is what we drive.
// Kenwood all-mode BASE stations (TS-711/TS-811): the generic Kenwood ASCII CAT.
// Identical encoding to this firmware's TS-790/TS-2000 backend, verified against
// Hamlib's kenwood.c mode table: 1 LSB, 2 USB, 3 CW, 4 FM, 5 AM, 6 FSK/RTTY.
static char legKwTsModeDigit(RigMode m) {
  switch (m) { case RM_LSB: return '1'; case RM_USB: return '2';
               case RM_CW:  return '3'; case RM_FM:  return '4';
               case RM_AM:  return '5'; case RM_DATA: return '6';   // FSK
               default: return '2'; }
}

static const char LEG_KWHT_BAND = '1';
// Mode digits for the TH-D74/D75:
//   0 FM  1 DV  2 AM  3 LSB  4 USB  5 CW  6 NFM  7 DR  8 WFM  9 R-CW
// MEASURED on a TH-D75 (tools/thd75_verify.py), not taken from a table -- because
// the two available references disagree and one of them is self-contradictory:
//   * LA3QMA/TH-D74-Kenwood tables/mode.md says 1 = DV, 2 = AM.
//   * Hamlib rigs/kenwood/thd74.c has BOTH: its thd74_mode_table[] says [2] = AM,
//     while its set_mode() switch sends '1' for AM. CardSat copied the switch.
// The sweep decides it: on band B code 2 is ACCEPTED and takes fine mode, code 1 is
// REFUSED -- which is exactly right for a band with an airband AM receiver and no
// D-STAR. So AM is '2'. (An earlier comment here claimed 0.9.68/0.9.69 had AM and DV
// transposed and "fixed" them; that change WAS the transposition.)
//
// RM_FM maps to NFM ('6'), NOT FM ('0'). CardSat drives band B -- the all-mode
// receiver, the only band that can cover linear birds AND FM -- and band B REFUSES
// "MD 1,0" outright ("N"). NFM is what this band calls narrow FM, and it is accepted.
// Consequence worth knowing: NFM does not support fine mode, so an FM bird tunes on
// the 5 kHz grid while linear birds get 20 Hz.
//
// RM_DATA maps to NFM as well. DV ('1') is refused on band B, and satellite "DATA"
// transponders are overwhelmingly FM packet, so narrow FM is the useful answer
// rather than a mode the radio will reject.
static char legKwHtModeDigit(RigMode m) {
  switch (m) { case RM_FM:   return '6';   // NFM: band B refuses plain FM
               case RM_DATA: return '6';   // DV is refused here; packet sats are FM
               case RM_AM:   return '2';   // measured; see above
               case RM_LSB:  return '3'; case RM_USB: return '4';
               case RM_CW:   return '5';
               default: return '4'; }
}

size_t legBuildFreqFrame(LegFamily fam, uint8_t civAddr, uint64_t hz,
                         uint8_t* out, size_t cap, bool wideFreq) {
  switch (fam) {
    case LEGF_CIV: {
      if (wideFreq && hz > LEG_CIV_WIDE_HZ) {     // IC-905 above 5.85 GHz
        if (cap < 12) return 0;
        uint8_t f6[6]; legCivPackFreqN(hz, f6, 6);
        uint8_t fr6[12] = { 0xFE,0xFE, civAddr, 0xE0, 0x05,
                            f6[0],f6[1],f6[2],f6[3],f6[4],f6[5], 0xFD };
        memcpy(out, fr6, 12); return 12;
      }
      if (cap < 11) return 0;
      uint8_t f[5]; legCivPackFreq(hz, f);
      const uint8_t fr[11] = { 0xFE,0xFE, civAddr, 0xE0, 0x05,
                               f[0],f[1],f[2],f[3],f[4], 0xFD };
      memcpy(out, fr, 11); return 11;
    }
    case LEGF_YBIN:
    case LEGF_YVR5: {                       // same frame and opcode as the FT-817
      if (cap < 5) return 0;
      uint8_t f[4]; legYBinPackFreq(hz, f);
      out[0]=f[0]; out[1]=f[1]; out[2]=f[2]; out[3]=f[3]; out[4]=0x01; return 5;
    }
    case LEGF_Y100: {                       // opcode 0x0A, little-endian BCD
      if (cap < 5) return 0;
      uint8_t f[4]; legY100PackFreq(hz, f);
      out[0]=f[0]; out[1]=f[1]; out[2]=f[2]; out[3]=f[3]; out[4]=0x0A; return 5;
    }
    case LEGF_YTXT: {
      int n = snprintf((char*)out, cap, "FA%09llu;", (unsigned long long)hz);
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
    case LEGF_KWTS: {                     // Kenwood base: VFO A, ELEVEN digits
      int n = snprintf((char*)out, cap, "FA%011llu;", (unsigned long long)hz);
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
    case LEGF_KWHT: {
      // "FQ <band>,<10 digits>" -- a SINGLE-FRAME set, measured working on a real
      // TH-D75 (tools/thd75_probe.py: "FQ 0,0144430000" accepted, readback matched).
      //
      // This replaces the FO read-modify-write that shipped through 0.9.70. FO is a
      // valid QUERY on this radio but its WRITE was refused with "N" on both bands,
      // in both VFO and memory mode, and even when the payload was byte-identical to
      // what the radio had just emitted. FQ needs no round trip at all, which also
      // removes the read latency, the reply-buffer sizing and the record patching
      // from the hot path.
      //
      // The frequency MUST be on the radio's current step grid: an off-grid write is
      // refused and the old frequency echoed back (probe: 10/10 -- every accepted
      // write was a 5 kHz multiple, every refused one was not). Callers round before
      // getting here; see PlainCatRig::sendFreq().
      int n = snprintf((char*)out, cap, "FQ %c,%010llu\r",
                       LEG_KWHT_BAND, (unsigned long long)hz);
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
  }
  return 0;
}

size_t legBuildModeFrame(LegFamily fam, uint8_t civAddr, RigMode m,
                         uint8_t* out, size_t cap, bool withFilter) {
  switch (fam) {
    case LEGF_CIV: {
      if (!withFilter) {                 // "06 <mode>" -- see LegProfile::modeFilter
        if (cap < 7) return 0;
        const uint8_t fr[7] = { 0xFE,0xFE, civAddr, 0xE0, 0x06,
                                legCivModeByte(m), 0xFD };
        memcpy(out, fr, 7); return 7;
      }
      if (cap < 8) return 0;
      const uint8_t fr[8] = { 0xFE,0xFE, civAddr, 0xE0, 0x06,
                              legCivModeByte(m), 0x01, 0xFD };
      memcpy(out, fr, 8); return 8;
    }
    case LEGF_YBIN: {
      if (cap < 5) return 0;
      out[0]=legYBinModeByte(m); out[1]=0; out[2]=0; out[3]=0; out[4]=0x07; return 5;
    }
    case LEGF_YVR5: {                       // FT-817 form, VR-5000 mode values
      if (cap < 5) return 0;
      out[0]=legYVr5ModeByte(m); out[1]=0; out[2]=0; out[3]=0; out[4]=0x07; return 5;
    }
    case LEGF_Y100: {                       // mode in data[3], opcode 0x0C
      if (cap < 5) return 0;
      out[0]=0; out[1]=0; out[2]=0; out[3]=legY100ModeByte(m); out[4]=0x0C; return 5;
    }
    case LEGF_YTXT: {
      int n = snprintf((char*)out, cap, "MD0%c;", legYTxtModeDigit(m));
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
    case LEGF_KWTS: {                     // no VFO digit on these: "MD<mode>;"
      int n = snprintf((char*)out, cap, "MD%c;", legKwTsModeDigit(m));
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
    case LEGF_KWHT: {
      // "MD <band>,<mode>" -- note the SPACE. Kenwood handheld CAT separates the
      // verb from its parameters with one; 0.9.68/0.9.69 emitted "MD1,4" and the
      // radio simply ignored it.
      int n = snprintf((char*)out, cap, "MD %c,%c\r", LEG_KWHT_BAND, legKwHtModeDigit(m));
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
  }
  return 0;
}

size_t legBuildReadFreqFrame(LegFamily fam, uint8_t civAddr,
                             uint8_t* out, size_t cap) {
  switch (fam) {
    case LEGF_CIV: {
      if (cap < 6) return 0;
      const uint8_t q[6] = { 0xFE,0xFE, civAddr, 0xE0, 0x03, 0xFD };
      memcpy(out, q, 6); return 6;
    }
    case LEGF_YBIN:
    case LEGF_YVR5: {                       // (the VR-5000 will not answer -- see canRead)
      if (cap < 5) return 0;
      out[0]=0; out[1]=0; out[2]=0; out[3]=0; out[4]=0x03; return 5;
    }
    case LEGF_Y100: {                       // "get FREQ and MODE status", opcode 0x10
      if (cap < 5) return 0;
      out[0]=0; out[1]=0; out[2]=0; out[3]=0; out[4]=0x10; return 5;
    }
    case LEGF_YTXT:
    case LEGF_KWTS: {
      if (cap < 4) return 0;
      memcpy(out, "FA;", 3); return 3;
    }
    case LEGF_KWHT: {
      // "FO <band>" -- the frequency OBJECT query. There is no "FQ" command on
      // this family; that was the single biggest error in the 0.9.68 encoder and
      // is why a TH-D75 enumerated but never answered. The reply is one long
      // record: "FO <band>,<10-digit Hz>,<step>,<shift>,..." (about 73 bytes).
      int n = snprintf((char*)out, cap, "FO %c\r", LEG_KWHT_BAND);
      return (n > 0 && (size_t)n < cap) ? (size_t)n : 0;
    }
  }
  return 0;
}


bool legParseFreqReply(LegFamily fam, uint8_t civAddr,
                       const uint8_t* buf, size_t n, uint64_t& hz) {
  switch (fam) {
    case LEGF_CIV:
      // Reply: FE FE E0 <addr> 03 <5 BCD> FD. The 6-byte query echo a CI-V
      // interface commonly returns can't match this 11-byte pattern (H6).
      for (size_t i = 0; i + 11 <= n; i++) {
        if (!(buf[i]==0xFE && buf[i+1]==0xFE && buf[i+2]==0xE0 &&
              buf[i+3]==civAddr && buf[i+4]==0x03)) continue;
        // Five-byte field is universal; the IC-905 answers with SIX above
        // 5.85 GHz, so accept either length by looking for the terminator.
        if (buf[i+10] == 0xFD) { hz = legCivUnpackFreq(&buf[i+5]); return hz > 0; }
        if (i + 12 <= n && buf[i+11] == 0xFD) {
          uint64_t v = 0;
          for (int k = 5; k >= 0; --k)
            v = v * 100 + (buf[i+5+k] >> 4) * 10 + (buf[i+5+k] & 0x0F);
          hz = v; return hz > 0;
        }
      }
      return false;
    case LEGF_YBIN:
    case LEGF_YVR5:
      // 4 BCD bytes + mode; take the first 5 bytes after an RX clear (this family's
      // adapters do not echo the binary command).
      if (n < 5) return false;
      hz = legYBinUnpackFreq(buf);
      return hz > 0;
    case LEGF_Y100:
      // FT-100 status block: band_no, freq[4] (LITTLE-endian BCD), mode, ...
      // The frequency starts at offset 1, not 0 (Hamlib ft100.c ft100_status_data).
      if (n < 6) return false;
      hz = legY100UnpackFreq(buf + 1);
      return hz > 0;
    case LEGF_YTXT:
    case LEGF_KWTS: {
      // Same "FA<digits>;" answer, different width: 9 digits on the Yaesus,
      // 11 on the Kenwood base stations.
      const int nd = (fam == LEGF_KWTS) ? 11 : 9;
      for (size_t i = 0; i + (size_t)nd + 3 <= n; i++) {
        if (buf[i]=='F' && buf[i+1]=='A') {
          uint64_t v = 0; bool ok = true;
          for (int k = 2; k < 2 + nd; k++) {
            char c = (char)buf[i+k];
            if (c < '0' || c > '9') { ok = false; break; }
            v = v*10 + (uint64_t)(c - '0');
          }
          if (ok) { hz = v; return hz > 0; }
        }
      }
      return false;
    }
    case LEGF_KWHT:
      // "FO <band>,<10-digit Hz>,..." -- the frequency is a fixed ten digits at
      // offset 5 of the record, the same offset Hamlib's thd74 backend reads.
      for (size_t i = 0; i + 15 <= n; i++) {
        if (buf[i]=='F' && buf[i+1]=='O' && buf[i+2]==' ' && buf[i+4]==',') {
          uint64_t v = 0; bool ok = true;
          for (int k = 5; k < 15; k++) {
            char c = (char)buf[i+k];
            if (c < '0' || c > '9') { ok = false; break; }
            v = v*10 + (uint64_t)(c - '0');
          }
          if (ok) { hz = v; return hz > 0; }
        }
      }
      return false;
  }
  return false;
}

// ---------------------------------------------------------------------------
//  PlainCatRig
// ---------------------------------------------------------------------------
PlainCatRig::PlainCatRig(LegModel m, uint8_t civAddr, uint32_t baud)
  : _model(m),
    _addr(civAddr ? civAddr : LEG_RADIOS[m].civAddr),
    _baud(baud ? baud : LEG_RADIOS[m].baud) {}

void PlainCatRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  if (baud) _baud = baud;
  if (extStream) { _stream = extStream; return; }   // USB leg: adapter already open
  // Grove leg: go through the SHARED CI-V UART opener, so a leg honors the CI-V
  // wiring mode just like the wired path -- including SINGLE-WIRE CI-V, which is
  // how nearly every half-duplex Icom in the leg catalog presents the bus (a 3.5 mm
  // jack carrying both directions). Before 0.9.69 this called Serial1.begin(rx,tx)
  // directly, which is two-wire only: a one-wire radio simply never answered.
  // Yaesu/Kenwood legs pass pinMode 0 and get the ordinary two-wire setup.
  // (The CAT_DUAL conflict guard has already ensured we are the only Grove claimant.)
  _stream = &civUartOpen(_pinMode, _baud, uartNum, rxPin, txPin);
}

bool PlainCatRig::canReadFreq() const {
  // Per-model, from the catalog. Every dialect here implements a read EXCEPT the
  // VR-5000, whose CAT has no read command at all -- claiming otherwise would make
  // knob-follow poll a radio that can never answer, once per CAT cycle, and burn
  // the whole read budget waiting for it.
  return LEG_RADIOS[_model].canRead;
}

bool PlainCatRig::sendFrame(const uint8_t* b, size_t n) {
  if (!_stream || !n) return false;
  catTrace("TX", b, n);
  size_t w = _stream->write(b, n);
  _stream->flush();
  if (cmdDelayMs) delay(cmdDelayMs);
  return w == n;
}

bool PlainCatRig::sendRaw(const uint8_t* b, size_t n) { return sendFrame(b, n); }

// TH-D74/D75 session preconditions. Sent ONCE per attached stream, not per set:
// they are radio state, not per-frequency parameters, and the probe showed each
// costs ~70 ms (BC) which is far too slow for the Doppler loop.
//
// Both were measured necessary. With band A in VFO mode but band B as the control
// band, every FO/FQ write to band A was refused; issuing "BC 0" made the identical
// write succeed. With band A in MEMORY mode, being the control band was not enough.
void PlainCatRig::kwEnsureSession() {
  if (_kwSession || !_stream) return;
  char b[16];
  int n = snprintf(b, sizeof(b), "VM %c,0\r", LEG_KWHT_BAND);   // (1) VFO, not memory
  if (n > 0) { _stream->write((const uint8_t*)b, n); _stream->flush(); delay(20); }
  n = snprintf(b, sizeof(b), "BC %c\r", LEG_KWHT_BAND);         // (2) control band
  if (n > 0) { _stream->write((const uint8_t*)b, n); _stream->flush(); delay(80); }
  _kwSession = true;                 // one attempt per stream; a failure here shows
                                     // up as refused writes, which the caller reports
}

// Fine mode is what buys a usable Doppler step: 20 Hz instead of 5 kHz. It is only
// valid in SSB/CW (bench-reported; FM does not support it), so this is a whitelist.
// FT/FS are standalone commands -- no record patching involved.
void PlainCatRig::kwApplyStepForMode(RigMode m) {
  if (!_stream) return;
  // AM is included on measurement, not assumption: the sweep showed band B accepts
  // FT 1 in AM and reaches the same 20 Hz grid as SSB/CW. NFM refuses fine mode and
  // stays on 5 kHz, which is where RM_FM and RM_DATA land.
  _kwMode = m;                          // remembered so a band change can re-apply it
  const bool fine = (m == RM_USB || m == RM_LSB || m == RM_CW || m == RM_AM);
  char b[16];
  int n = snprintf(b, sizeof(b), "FT %c\r", fine ? '1' : '0');
  if (n > 0) { _stream->write((const uint8_t*)b, n); _stream->flush(); delay(20); }
  if (fine) {                                   // 0 = 20 Hz, the finest available
    n = snprintf(b, sizeof(b), "FS 0\r");
    if (n > 0) { _stream->write((const uint8_t*)b, n); _stream->flush(); delay(20); }
  }
  _kwFine = fine;
}


bool PlainCatRig::sendFreq(freq_t hz) {
  const LegFamily fam = LEG_RADIOS[_model].family;
  if (fam == LEGF_KWHT) {
    // The radio REFUSES an off-grid frequency (it echoes the old one back rather
    // than rounding), so round here. Measured 10/10 on a TH-D75: every accepted
    // write sat on the 5 kHz grid, every refused one did not. Grid is 20 Hz when
    // fine mode is on (SSB/CW) and 5 kHz otherwise.
    kwEnsureSession();                       // VFO mode + control band, once
    const uint32_t g = kwGrid();
    const freq_t want = hz;
    hz = (freq_t)(((hz + g / 2) / g) * g);   // nearest, not truncated
    // WHICH SIDE ROUNDS? The bench sees 5 kHz quantisation on an AO-7 mode A downlink
    // while the radio itself demonstrably does 20 Hz there (verified at 29.4 MHz), and
    // the band-change theory was measured and refuted. So the number CardSat actually
    // puts on the wire, and the grid it chose, have to be visible -- reading the code
    // has now failed twice. Logged only when rounding actually moved the frequency.
    if (want != hz)
      Serial.printf("[KWHT] want %llu -> sent %llu (grid %lu, fine=%d)\n",
                    (unsigned long long)want, (unsigned long long)hz,
                    (unsigned long)g, (int)_kwFine);
  }
  uint8_t fr[24];
  size_t n = legBuildFreqFrame(fam, _addr, hz, fr, sizeof(fr),
                               LEG_RADIOS[_model].wideFreq);
  bool ok = sendFrame(fr, n);
  if (ok) _lastSetMs = millis();
  // RE-APPLY THE STEP AFTER A BAND CHANGE.
  //
  // The TH-D75 holds its tuning step PER BAND. CardSat sends MD/FT/FS first and the
  // frequency second, so on a bird whose downlink is in a different band from wherever
  // the radio was sitting -- AO-7 mode A, downlink 29.4 MHz, is the case that exposed
  // it -- the fine step is set on the old band and then thrown away by the move to the
  // new one. Everything downstream still BELIEVES fine mode is on: kwGrid() keeps
  // returning 20 Hz and CardSat keeps sending exact frequencies, while the radio
  // quantises them to the band's 5 kHz step. The symptom is "rounding on HF downlinks
  // only", with VHF/UHF perfect.
  //
  // Measured, not assumed: thd75_verify.py --freq 29400000 shows USB/LSB/CW/AM all
  // accepting fine mode with a 20 Hz grid at 29.4 MHz, so the radio is not the limit.
  if (ok && fam == LEGF_KWHT) {
    const uint8_t band = kwBandOf(hz);
    if (band != _kwBand) {
      _kwBand = band;
      kwApplyStepForMode(_kwMode);        // re-assert FT/FS on the band we just entered
    }
  }
  return ok;
}

bool PlainCatRig::sendMode(RigMode m) {
  uint8_t fr[16];
  size_t n = legBuildModeFrame(LEG_RADIOS[_model].family, _addr, m, fr, sizeof(fr),
                               LEG_RADIOS[_model].modeFilter);
  bool ok = sendFrame(fr, n);
  if (ok && LEG_RADIOS[_model].family == LEGF_KWHT) {
    // The usable Doppler step follows the MODE on this family: fine mode (20 Hz) is
    // valid in SSB/CW only, so it has to be re-applied whenever the mode changes --
    // which is exactly when CardSat switches between a linear and an FM bird.
    kwEnsureSession();
    kwApplyStepForMode(m);
  }
  if (ok) _lastSetMs = millis();
  return ok;
}

bool PlainCatRig::readFreq(freq_t& hzOut) {
  if (!_stream) return false;
  const LegFamily fam = LEG_RADIOS[_model].family;
  // H8: let a just-sent set's echo/ACK settle before clearing RX for the read.
  if (_lastSetMs) {
    uint32_t frameMs = _baud ? (uint32_t)((16UL * 10UL * 1000UL) / _baud) + 3 : 8;
    if (frameMs > 40) frameMs = 40;
    while ((millis() - _lastSetMs) < frameMs) delay(1);
  }
  while (_stream->available() > 0) _stream->read();  // clear stale RX (>0: closed CDC returns -1)
  uint8_t q[8];
  size_t qn = legBuildReadFreqFrame(fam, _addr, q, sizeof(q));
  if (!qn) return false;
  catTrace("TX", q, qn);
  if (_stream->write(q, qn) != qn) return false;
  _stream->flush();
  // Collect until a stop byte / quiet interval / deadline, then parse. The CIV
  // family needs the quiet-interval collect (H6: interface echo shares the 0xFD
  // terminator with the reply); the ASCII families stop on their terminator.
  uint32_t deadline = readBudgetMs ? readBudgetMs : 220;
  int stopByte = (fam == LEGF_YTXT || fam == LEGF_KWTS) ? ';'
               : (fam == LEGF_KWHT) ? '\r' : -1;
  // NOTE: a 300 ms floor used to sit here, on the theory that the FO record was slow
  // to produce. The Mac probe measured that read at 1 ms, 5/5 -- the floor was fixing
  // a problem that does not exist, so it is gone. Reads on this family are fast; it
  // was the WRITE that was being refused, for reasons that had nothing to do with time.
  uint8_t buf[96]; size_t n = 0;
  uint32_t t0 = millis(), lastRx = millis();
  while ((millis() - t0) < deadline && n < sizeof(buf)) {
    int c = (_stream->available() > 0) ? _stream->read() : -1;
    if (c < 0) {
      if (n > 0 && fam == LEGF_CIV && (millis() - lastRx) > 20) break;
      if (n >= 5 && fam == LEGF_YBIN && (millis() - lastRx) > 20) break;
      delay(1); continue;
    }
    buf[n++] = (uint8_t)c; lastRx = millis();
    if (stopByte >= 0 && c == stopByte && n > 3) break;
  }
  if (n) catTrace("RX", buf, n);
  uint64_t hz = 0;
  if (!legParseFreqReply(fam, _addr, buf, n, hz)) return false;
  hzOut = (freq_t)hz;
  return true;
}

// ---------------------------------------------------------------------------
//  DualRig
// ---------------------------------------------------------------------------
DualRig::DualRig(Rig* down, Rig* up, int usbLeg, uint32_t downBaud, uint32_t upBaud)
  : _down(down), _up(up), _usbLeg(usbLeg) {
  _baud[0] = downBaud; _baud[1] = upBaud;
  // Name says what is actually driven, so a one-legged setup is never mistaken for
  // a broken two-legged one.
  if (_down && _up)      snprintf(_nameBuf, sizeof(_nameBuf), "Dual %s+%s", _down->name(), _up->name());
  else if (_down)        snprintf(_nameBuf, sizeof(_nameBuf), "%s (DL only)", _down->name());
  else if (_up)          snprintf(_nameBuf, sizeof(_nameBuf), "%s (UL only)", _up->name());
  else                   snprintf(_nameBuf, sizeof(_nameBuf), "Dual (no legs)");
}

DualRig::~DualRig() {
  // The legs cache extStream copies (see the fix31 note on setExternalStream in
  // rig.h); the engage teardown clears the stream through us BEFORE UsbSerial
  // dies, exactly as for a single CAT_USB rig, so plain delete is safe here.
  delete _down; delete _up;
}

void DualRig::begin(uint32_t baud, int uartNum, int rxPin, int txPin) {
  (void)baud;
  // Begin every leg except the USB one(s) -- a USB leg starts when the reconciler
  // attaches its CDC stream (setExternalStream / setLegExternalStream below),
  // mirroring the single-rig CAT_USB lifecycle. LAN legs ignore the UART args.
  if (_down && !legIsUsb(0)) _down->begin(_baud[0], uartNum, rxPin, txPin);
  if (_up   && !legIsUsb(1)) _up->begin(_baud[1], uartNum, rxPin, txPin);
}

// Ready when every leg that EXISTS is ready. A "None" leg is not a missing leg --
// it is a deliberate choice that this half of the link is not CAT-controlled, so
// it must not hold the composite permanently un-ready.
bool DualRig::ready() const {
  if (!_down && !_up) return false;
  if (_down && !_down->ready()) return false;
  if (_up   && !_up->ready())   return false;
  return true;
}

void DualRig::service() {
  if (_down) _down->service();
  if (_up)   _up->service();
}

void DualRig::setSessionWanted(bool want) {
  if (_down) _down->setSessionWanted(want);
  if (_up)   _up->setSessionWanted(want);
}

void DualRig::setCmdDelay(uint16_t ms) {
  Rig::setCmdDelay(ms);
  if (_down) _down->setCmdDelay(ms);
  if (_up)   _up->setCmdDelay(ms);
}

void DualRig::setReadBudgetMs(uint16_t ms) {
  Rig::setReadBudgetMs(ms);
  if (_down) _down->setReadBudgetMs(ms);
  if (_up)   _up->setReadBudgetMs(ms);
}

// Only a GROVE leg can use a wiring mode; the call is harmless on the others
// (USB legs take the external-stream path, LAN legs ignore it entirely).
void DualRig::setPinMode(uint8_t mode) {
  if (_down) _down->setPinMode(mode);
  if (_up)   _up->setPinMode(mode);
}

// Per-leg attach: the dual-USB reconciler binds each leg to its OWN CDC stream
// (CAT-A for the downlink, CAT-B for the uplink). First attach begins the leg;
// detach (nullptr) resets so the next attach begins it again.
void DualRig::setLegExternalStream(int leg, Stream* s) {
  if (leg < 0 || leg > 1 || !legIsUsb(leg)) return;
  Rig* L = (leg == 0) ? _down : _up;
  if (!L) return;
  L->setExternalStream(s);
  if (s && !_usbBegun[leg]) {
    L->begin(_baud[leg], -1, -1, -1);
    _usbBegun[leg] = true;
  }
  if (!s) _usbBegun[leg] = false;
}

void DualRig::setExternalStream(Stream* s) {
  Rig::setExternalStream(s);
  if (_usbLeg < 0) return;
  if (s) {
    // A single stream can only serve a SINGLE USB leg. With two, the reconciler
    // must use setLegExternalStream() per leg; a blanket non-null attach here
    // would put both radios on one wire, so it is deliberately ignored.
    if (_usbLeg == 0 || _usbLeg == 1) setLegExternalStream(_usbLeg, s);
    return;
  }
  // nullptr = teardown: detach EVERY USB leg (fix31 rule -- clear each backend's
  // cached copy before the Stream dies), whether there are one or two.
  if (legIsUsb(0)) setLegExternalStream(0, nullptr);
  if (legIsUsb(1)) setLegExternalStream(1, nullptr);
}

bool DualRig::setMainFreq(freq_t hz) { return _up   ? _up->setMainFreq(hz)   : false; }
bool DualRig::setSubFreq (freq_t hz) { return _down ? _down->setSubFreq(hz)  : false; }
bool DualRig::setMainMode(RigMode m) { return _up   ? _up->setMainMode(m)    : false; }
bool DualRig::setSubMode (RigMode m) { return _down ? _down->setSubMode(m)   : false; }
bool DualRig::readSubFreq (freq_t& hzOut) { return _down ? _down->readSubFreq(hzOut)  : false; }
bool DualRig::readMainFreq(freq_t& hzOut) { return _up   ? _up->readMainFreq(hzOut)   : false; }
bool DualRig::readPtt(bool& tx) { return _up ? _up->readPtt(tx) : false; }
// Knob-follow reads the DOWNLINK. With no downlink leg there is nothing to follow.
bool DualRig::canReadFreq() const { return _down && _down->canReadFreq(); }

// ---------------------------------------------------------------------------
//  Leg + composite factories
// ---------------------------------------------------------------------------
Rig* makeLegRig(uint8_t legModel, uint8_t bus, uint8_t civAddr, uint32_t baud,
                const char* host, uint16_t port, const char* user, const char* pass) {
  if (legModel >= LEG_NONE) return nullptr;   // LEG_NONE is last before LEG_COUNT
  const LegProfile& lp = LEG_RADIOS[legModel];
  const uint8_t  addr = civAddr ? civAddr : lp.civAddr;
  const uint32_t bd   = baud    ? baud    : lp.baud;
  if (bus == LEGBUS_LAN) {
    // Icom network CAT leg (the IC-705 over its own Wi-Fi being the flagship).
    // Only the CI-V family has this transport, and only LAN-capable models offer
    // it in the UI; both are re-checked here so a stale config can't build a
    // nonsense backend.
    if (lp.family != LEGF_CIV || !lp.hasLan || !host || !host[0]) return nullptr;
    return new (std::nothrow) IcomNetRig(addr, lp.name, host, port ? port : 50001,
                                         user ? user : "", pass ? pass : "");
  }
  // Grove serial or USB adapter leg: the plain-CAT backend over a Stream. For a
  // USB leg the stream is attached later by the reconciler (extStream), exactly
  // like single-rig CAT_USB.
  return new (std::nothrow) PlainCatRig((LegModel)legModel, addr, bd);
}

Rig* makeDualRig(const uint8_t model[2], const uint8_t bus[2], const uint8_t civ[2],
                 const uint32_t baud[2], const char host[2][40], const uint16_t port[2],
                 const char user[2][24], const char pass[2][24]) {
  // Physical-bus conflicts (two Grove legs, two USB legs) are refused by the
  // settings UI and re-checked by the engage path; this factory only builds.
  // A leg set to "None" is DELIBERATELY absent: that half of the link simply is not
  // CAT-controlled, and Doppler for it goes nowhere. That covers the common station
  // where only one radio speaks CAT -- an SSB downlink rig plus a hand-tuned HT on
  // the uplink, or a CAT receiver alongside a transmitter with no computer port.
  // Only a leg that was ASKED for and could not be built is an error.
  const bool wantDown = (model[0] != LEG_NONE), wantUp = (model[1] != LEG_NONE);
  if (!wantDown && !wantUp) return nullptr;             // nothing configured at all
  Rig* down = wantDown ? makeLegRig(model[0], bus[0], civ[0], baud[0],
                                    host[0], port[0], user[0], pass[0]) : nullptr;
  Rig* up   = wantUp   ? makeLegRig(model[1], bus[1], civ[1], baud[1],
                                    host[1], port[1], user[1], pass[1]) : nullptr;
  if ((wantDown && !down) || (wantUp && !up)) { delete down; delete up; return nullptr; }
  int usbLeg = -1;
  const bool uD = wantDown && bus[0] == LEGBUS_USB, uU = wantUp && bus[1] == LEGBUS_USB;
  if (uD && uU) usbLeg = 2;                            // dual-USB CAT
  else if (uD)  usbLeg = 0;
  else if (uU)  usbLeg = 1;
  const LegProfile& d = LEG_RADIOS[model[0]];
  const LegProfile& u = LEG_RADIOS[model[1]];
  DualRig* dr = new (std::nothrow) DualRig(down, up, usbLeg,
      baud[0] ? baud[0] : d.baud, baud[1] ? baud[1] : u.baud);
  if (!dr) { delete down; delete up; return nullptr; }
  return dr;
}

// ---------------------------------------------------------------------------
//  RigctlRig - rigctld (Hamlib NET rigctl) TCP client backend
// ---------------------------------------------------------------------------
// ---- base (TCP) transport primitives --------------------------------------
bool RigctlRig::linkOpen() {
  if (_c.connected()) { _ok = true; return true; }
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < 3000) { _ok = false; return false; }   // throttle retries
  _lastTry = now; _c.stop();
  if (WiFi.status() != WL_CONNECTED || _host.length() == 0) { _ok = false; return false; }
  _ok = _c.connect(_host.c_str(), _port, 1500);
  if (_ok) { _probed = false; _failStreak = 0; }   // fresh connection -> re-probe, clear streak
  return _ok;
}
void   RigctlRig::linkClose() { _c.stop(); }
size_t RigctlRig::linkWrite(const uint8_t* d, size_t n) { return _c.write(d, n); }
int    RigctlRig::linkRead() { return _c.read(); }

// ---- shared protocol (transport-agnostic) ---------------------------------
// Read one non-empty reply line via the transport's linkRead(). "" on timeout.
String RigctlRig::readLine(uint32_t timeoutMs) {
  String line; uint32_t t = millis();
  while ((millis() - t) < timeoutMs) {
    int ch = linkRead();
    if (ch < 0) { delay(2); continue; }
    if (ch == '\n' || ch == '\r') { if (line.length()) break; else continue; }
    line += (char)ch;
  }
  return line;
}

// One-shot VFO-mode handshake, shared by every transport. We steer the two legs by
// selecting a VFO and then issuing plain set_freq/set_mode on it (downlink = VFOA,
// uplink = VFOB) -- what gpredict and mainstream Hamlib backends expect for a duplex
// sat rig, and far more portable than set_split_freq (which makes Hamlib tune the
// wrong VFO on Icoms). Works against any rigctld, including CardSat's own server and
// the CardSatDualRig companion. Probe \chk_vfo: a server started with --vfo answers
// "CHKVFO 1" and then wants the VFO inline on every command; otherwise we pre-select
// with V and send bare commands on currVFO.
void RigctlRig::probeVfoMode() {
  _vfo = -1; _vfoMode = false;
  const char* q = "\\chk_vfo\n";
  linkWrite((const uint8_t*)q, strlen(q));
  String r = readLine(300);
  if (r.indexOf("CHKVFO 1") >= 0) _vfoMode = true;
  _probed = true;
}

bool RigctlRig::ensure() {
  if (!linkOpen()) return false;
  if (!_probed) probeVfoMode();
  return _ok;
}

const char* RigctlRig::modeName(RigMode m) {
  switch (m) {
    case RM_LSB: return "LSB";  case RM_USB:  return "USB";
    case RM_CW:  return "CW";   case RM_FM:   return "FM";
    case RM_AM:  return "AM";   case RM_DATA: return "PKTUSB";
  }
  return "USB";
}

// Send one command line; return the first non-empty reply line ("" on failure).
String RigctlRig::xchg(const String& tx, uint32_t replyMs) {
  if (!ensure()) return "";
  uint32_t t0 = millis();
  while (linkRead() >= 0 && (millis() - t0) < 20) { }           // drain stale reply
  if (linkWrite((const uint8_t*)tx.c_str(), tx.length()) != tx.length()) {
    _ok = false; linkClose(); return "";
  }
  String r = readLine(replyMs);
  // M22: a silent peer returns "" here. Left unchecked, _ok stays true and every Doppler
  // tick pays the full 400 ms timeout while the UI still shows the rig engaged. Count
  // consecutive empty replies and, past a small threshold, mark not-ready and close the
  // link so ready() tells the truth and ensure() must re-establish (and re-probe) it.
  if (r.length() == 0) {
    if (_failStreak < 255) _failStreak++;
    if (_failStreak >= 3) { _ok = false; linkClose(); }
  } else {
    _failStreak = 0;                          // a real reply: the peer is alive
  }
  return r;
}

// ===========================================================================
//  RigctlGroveRig - the same VFO-mode protocol over the Grove UART (G1/G2).
//  Serial1 is shared with wired CI-V / Grove GPS / Grove rotator; the caller's
//  mutual-exclusion rules guarantee only one owner at a time.
// ===========================================================================
void RigctlGroveRig::begin(uint32_t, int /*uartNum*/, int rxPin, int txPin) {
  // C1: the base Rig::begin contract is (baud, uartNum, rxPin, txPin). Capture the pins
  // from args 3 and 4 -- an earlier signature named args 2/3 rx/tx, which stored uartNum
  // as _rx and rxPin as _tx (both GPIO 1) and dropped the real txPin, so the Grove cable
  // ran RX and TX on the same pin.
  _rx = rxPin; _tx = txPin; _lastTry = 0; _probed = false; _open = false;
  linkOpen();
}
bool RigctlGroveRig::linkOpen() {
  // M7: opening Serial1 always "succeeds" at the driver level, so an ABSENT companion used
  // to be marked ready forever -- each command then paid a full timeout and the UART was
  // never closed. Now: open the UART once, probe for a live companion, and only stay ready
  // if it answered. On no answer, close and back off so we don't hammer the bus / hitch the
  // UI. ensure() re-probes on the next attempt after the backoff.
  if (_open) return _ok;                        // already up: keep whatever readiness we have
  uint32_t now = millis();
  if (_lastTry && (now - _lastTry) < 3000) { _ok = false; return false; }   // backoff after a failed probe
  _lastTry = now;
  if (!_serial) _serial = &Serial1;
  _serial->begin(_baud, SERIAL_8N1, _rx, _tx);
  _open = true; _probed = false;
  // Lightweight liveness probe: a rigctld/companion answers \dump_state or \chk_vfo. If we
  // get any reply line, treat the link as up; otherwise close and let the backoff apply.
  uint32_t t0 = millis();
  while (linkRead() >= 0 && (millis() - t0) < 20) { }   // drain
  const char* q = "\\chk_vfo\n";
  _serial->write((const uint8_t*)q, strlen(q));
  String r = readLine(400);
  if (r.length() == 0) {
    // No companion on the Grove UART: don't pretend to be ready.
    _serial->end(); _open = false; _ok = false;
    return false;
  }
  if (r.indexOf("CHKVFO 1") >= 0) _vfoMode = true;
  _probed = true;                               // probe already done here
  _ok = true; _failStreak = 0;
  return true;
}
void RigctlGroveRig::linkClose() {
  if (_serial && _open) _serial->end();
  _open = false; _ok = false;
}
size_t RigctlGroveRig::linkWrite(const uint8_t* d, size_t n) {
  if (!_serial || !_open) return 0;
  return _serial->write(d, n);
}
int RigctlGroveRig::linkRead() {
  if (!_serial || !_open) return -1;
  return _serial->read();
}

// Downlink is VFOA, uplink is VFOB. (The operator picks which band each VFO holds
// in the rig's own sat/duplex setup; we only need two consistent VFOs to steer.)
const char* RigctlRig::vfoTok(bool sub) { return sub ? "VFOA" : "VFOB"; }

// Make sub's VFO the target of the next bare command. In --vfo servers the VFO
// travels inline on each command instead (see cmd()), so this is a no-op there.
void RigctlRig::selectVfo(bool sub) {
  if (_vfoMode) return;
  int want = sub ? 0 : 1;
  if (_vfo == want) return;
  if (xchg(String("V ") + vfoTok(sub) + "\n") == "RPRT 0") _vfo = want;
  else _vfo = -1;
}

// Assemble a command line, inserting the VFO token inline when the server runs in
// --vfo mode. body is the value(s) after the command letter, if any.
String RigctlRig::cmd(char c, bool sub, const String& body) {
  String s; s += c;
  if (_vfoMode)      { s += ' '; s += vfoTok(sub); }
  if (body.length()) { s += ' '; s += body; }
  s += '\n';
  return s;
}

bool RigctlRig::setSubFreq (freq_t hz) { selectVfo(true);  return xchg(cmd('F', true,  String((unsigned long long)hz))) == "RPRT 0"; }
bool RigctlRig::setMainFreq(freq_t hz) { selectVfo(false); return xchg(cmd('F', false, String((unsigned long long)hz))) == "RPRT 0"; }
bool RigctlRig::setSubMode (RigMode m) { selectVfo(true);  return xchg(cmd('M', true,  String(modeName(m)) + " 0")) == "RPRT 0"; }
bool RigctlRig::setMainMode(RigMode m) { selectVfo(false); return xchg(cmd('M', false, String(modeName(m)) + " 0")) == "RPRT 0"; }

bool RigctlRig::readSubFreq(freq_t& hzOut) {
  selectVfo(true);
  String r = xchg(cmd('f', true));
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (freq_t)strtoull(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readMainFreq(freq_t& hzOut) {
  selectVfo(false);
  String r = xchg(cmd('f', false));
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  hzOut = (freq_t)strtoull(r.c_str(), nullptr, 10);
  return hzOut > 0;
}
bool RigctlRig::readPtt(bool& tx) {
  String r = xchg("t\n");
  if (r.length() == 0 || r.startsWith("RPRT")) return false;
  tx = (r.toInt() != 0);
  return true;
}
