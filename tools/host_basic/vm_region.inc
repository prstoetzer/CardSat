static const int  BASIC_MAX_LINES = 200;      // program lines
static const int  BASIC_GOSUB_MAX = 16;       // GOSUB nesting depth
static const int  BASIC_FOR_MAX   = 8;        // FOR nesting depth
static const long BASIC_STMT_BUDGET = 500000; // statements/run before "loop?" halt
// Hard cap on console output. basicRun() reserves exactly this much for both the VM's
// buffer and the surviving copy, so neither ever reallocs -- see the ordering note there.
static const int  BASIC_OUT_MAX = 6000;       // bytes of output per run

namespace {
  // Read-only system data, snapshotted ONCE per run in basicRun() and carried inside the
  // VM -- which is heap-allocated per run and deleted at the end, so this costs zero
  // permanent RAM and is freed automatically when the program stops.
  //
  // Why a snapshot rather than live reads: basicRun() executes to completion inside a key
  // handler and never yields to loop(), while the statement budget is 500,000. If a bare
  // name could trigger SGP4 (~0.3-1 ms), "10 PRINT SATEL / 20 GOTO 10" would call it half a
  // million times and trip the task watchdog. Snapshotting costs exactly one look() per run
  // no matter what the program does. The values are therefore frozen for the run -- which is
  // no loss, since a run lasts milliseconds -- and mean "the sky when you pressed Fn+r".
  struct BasicSys {
    // Availability flags. Readable as SATOK/TIMEOK/... so a program can branch instead of halt.
    bool satOk = false, timeOk = false, posOk = false, wxOk = false, spwxOk = false, passOk = false;
    double satAz = 0, satEl = 0, satRng = 0, satRR = 0, satLat = 0, satLon = 0, satAlt = 0, satSun = 0;
    double satInc = 0, satEcc = 0, satRaan = 0, satMM = 0, satNor = 0;
    double aosIn = 0, losIn = 0, passEl = 0, passVis = 0;
    double sunAz = 0, sunEl = 0, moonAz = 0, moonEl = 0;
    double myLat = 0, myLon = 0, myAlt = 0;
    double utcH = 0, utcM = 0, utcS = 0, utcDay = 0, utcMon = 0, utcYr = 0;
    double sfi = 0, kp = 0, aIdx = 0;
    // 0.9.61 expanded space weather
    double ssn = 0, flare = 0, bz = 0, swSpeed = 0, muf = 0;
    double fcKp1 = 0, fcKp2 = 0, fcKp3 = 0;
    double magDecl = 0;   // magnetic declination at QTH (deg, E+)
    double wxTemp = 0, wxWind = 0, wxDir = 0, wxHum = 0;
    double batt = 0, gpAge = 0, nfav = 0;
    // 0.9.59 additions
    double lstHr = 0;                                     // local sidereal time, hours
    bool   gpsOk = false;  double gpsSats = 0, gpsSpd = 0, gpsLat = 0, gpsLon = 0, gpsAlt = 0;
    double heapFree = 0, upTime = 0, nSat = 0, nTx = 0;
    bool   txOk = false;   double txDl = 0, txUl = 0, txBw = 0, txInv = 0, txLin = 0;
    double pAos[8], pLos[8], pMax[8]; int pN = 0;         // up to 8 upcoming passes
    // 0.9.70 additions -- data the firmware gained this cycle and already computes.
    // All are for the satellite currently selected (SATSEL or the tracked one).
    double lshell = 0, bratio = 0, bfield = 0;   // IGRF-14 McIlwain L, B/B0, |B| nT
    double inBelt = 0, inSaa = 0;                // zone membership of the subpoint
    double decayD = 0, decaySrc = 0;             // days to re-entry; 1 = measured n-dot, 2 = B*
    double battMv = 0, charging = 0;             // battery millivolts + inferred charge state
    double heapBlk = 0;                          // largest free block (fragmentation, not total)
    double doppRx = 0, doppTx = 0;               // Doppler-corrected RX/TX for the current pair
  };

  // System-name table. Read-only; every entry is data the firmware already has.
  // NOTE ON SAFETY: the app signals "no data" with -1 (spaceKp/spaceA/wxWindDirNow/
  // wxHumidNow) and -999 (wxTempNow/wxWindNow). Those are NUMBERS, and BASIC has no NULL,
  // so letting them through would mean "IF WXTEMP < 0 THEN PRINT FREEZING" printing
  // FREEZING when no weather was ever fetched -- confidently wrong. Worse, an unset SATAZ
  // reading 0 says "point due North", a perfectly plausible bearing. So a read of missing
  // data HALTS the run with a real error; the *OK flags exist for programs that would
  // rather branch than stop.
  struct BasicSysEnt { const char* name; uint8_t id; };
  static const BasicSysEnt BASIC_SYS[] = {
    {"SATAZ",1},{"SATEL",2},{"SATRNG",3},{"SATRR",4},{"SATLAT",5},{"SATLON",6},
    {"SATALT",7},{"SATSUN",8},{"SATINC",9},{"SATECC",10},{"SATRAAN",11},{"SATMM",12},
    {"SATNOR",13},{"AOSIN",14},{"LOSIN",15},{"PASSEL",16},{"PASSVIS",17},
    {"SUNAZ",18},{"SUNEL",19},{"MOONAZ",20},{"MOONEL",21},
    {"MYLAT",22},{"MYLON",23},{"MYALT",24},
    {"UTCH",25},{"UTCM",26},{"UTCS",27},{"UTCDAY",28},{"UTCMON",29},{"UTCYR",30},
    {"SFI",31},{"KP",32},{"AINDEX",33},
    {"WXTEMP",34},{"WXWIND",35},{"WXDIR",36},{"WXHUM",37},
    {"BATT",38},{"GPAGE",39},{"NFAV",40},
    {"SATOK",41},{"TIMEOK",42},{"POSOK",43},{"WXOK",44},{"SPWXOK",45},{"PASSOK",46},
    {"LSTHR",47},{"GPSOK",48},{"GPSSATS",49},{"GPSSPD",50},
    {"GPSLAT",51},{"GPSLON",52},{"GPSALT",53},
    {"HEAPFREE",54},{"UPTIME",55},{"NSAT",56},{"NTX",57},{"PASSN",58},
    {"TXDL",59},{"TXUL",60},{"TXBW",61},{"TXINV",62},{"TXLIN",63},
    // 0.9.61 space-weather additions
    {"SSN",64},{"FLARE",65},{"BZ",66},{"SWSPEED",67},{"MUF",69},
    {"FCKP1",70},{"FCKP2",71},{"FCKP3",72},{"MAGDECL",73},
    // 0.9.70: the geomagnetic / decay / power data added this cycle. Ids continue
    // from 74; 68 stays unused (it always has).
    {"LSHELL",74},{"BRATIO",75},{"BFIELD",76},
    {"INBELT",77},{"INSAA",78},
    {"DECAYD",79},{"DECAYSRC",80},
    {"BATTMV",81},{"CHARGING",82},{"HEAPBLK",83},
    {"DOPPRX",84},{"DOPPTX",85},
    // TXOK was missing: sys.txOk has always been SET by TXSEL but never readable, so a
    // program could not tell whether a transponder snapshot succeeded. That matters
    // more now -- SATSEL deliberately clears it, so TXOK=0 means "no transponder
    // chosen for this satellite yet". Found by tools/host_examples.
    {"TXOK",86},
  };
  static const int BASIC_SYS_N = (int)(sizeof(BASIC_SYS) / sizeof(BASIC_SYS[0]));

  // Longest match at p, rejecting a name followed by another alnum so "KP1" stays an
  // error rather than silently parsing as KP then 1 (the single-letter path has the same
  // guard). Returns the id and sets len; 0 = no match.
  static uint8_t basicSysMatch(const char* p, int& len) {
    uint8_t best = 0; int bestLen = 0;
    for (int i = 0; i < BASIC_SYS_N; ++i) {
      const char* w = BASIC_SYS[i].name;
      int n = (int)strlen(w);
      bool ok = true;
      for (int k = 0; k < n; ++k)
        if (toupper((unsigned char)p[k]) != w[k]) { ok = false; break; }
      if (!ok) continue;
      if (isalnum((unsigned char)p[n])) continue;      // trailing-alnum guard
      if (n > bestLen) { best = BASIC_SYS[i].id; bestLen = n; }
    }
    len = bestLen; return best;
  }

  struct BasicVM {
    struct Line { int num; const char* text; };
    Line   lines[BASIC_MAX_LINES];
    int    nLines = 0;
    double vars[26];
    BasicSys sys;                               // snapshot; freed with the VM
    String out;
    String err;
    int    gosubStack[BASIC_GOSUB_MAX]; int gosubSP = 0;
    struct ForRec { int var; double limit, step; int lineIdx; const char* bodyP; };
    ForRec forStack[BASIC_FOR_MAX]; int forSP = 0;
    long   stmts = 0;
    int    depth = 0;                           // expression recursion depth (see atom)
    const char* p = nullptr;                    // expression cursor
    const char* forResumeP = nullptr;           // NEXT -> resume same-line FOR body here
    // The FOR header is followed either by ':' (more statements on this line) or by end
    // of line. Return a pointer past a single leading ':' + spaces so the caller can test
    // "is there really a body on this line".
    static const char* skipToColonBody(const char* q) {
      while (*q == ' ' || *q == '\t') ++q;
      if (*q == ':') { ++q; while (*q == ' ' || *q == '\t') ++q; }
      return q;
    }
    double* arr = nullptr; int arrN = 0;        // the one @() array (DIM @(n), n<=256)
    int    dataLineIdx = 0;                     // DATA read cursor: line index...
    const char* dataP = nullptr;                //   ...and position inside its value list
    // Host hooks (set by basicRun; null = feature unavailable). The VM stays ignorant of
    // App: everything crosses these five function pointers. satsel/txsel re-snapshot the
    // sat/transponder groups; lpr streams a finished line to the report sinks; gfx draws
    // on the canvas; file is the gated /CardSat/basic/ writer + FILES lister.
    void* host = nullptr;
    // out[13] is the transponder COUNT for the satellite just selected: SATSEL now
    // carries the transponder context with it, so TXSEL/NTX refer to the satellite
    // the program chose rather than whatever the operator happened to be tracking.
    bool (*satselCb)(void*, int, double out[14]) = nullptr;
    bool (*txselCb)(void*, int, double out[5])   = nullptr;
    bool (*lprCb)(void*, const char*, int op)    = nullptr;   // 1 = line, 2 = close
    void (*gfxCb)(void*, int op, double a, double b, double c2, double d, double e, const char* s) = nullptr;
    int  (*fileCb)(void*, int op, const char* a, String* out) = nullptr;
    int  satselLeft = 2000;                     // SGP4-per-run budget for SATSEL
    bool gfxUsed = false, lprUsed = false, fileUsed = false;
    ~BasicVM() { delete[] arr; }

    void ws() { while (*p == ' ' || *p == '\t') ++p; }
    // Boundary-checked keyword match for WORD OPERATORS (MOD/AND/OR/NOT...): unlike kw(),
    // refuses when the next char is alphanumeric, so a variable named M followed by "OD"
    // can never be swallowed as MOD.
    bool kwb(const char* w) {
      const char* q = p; const char* w0 = w;
      while (*w0) { if (toupper((unsigned char)*q) != *w0) return false; ++q; ++w0; }
      if (isalnum((unsigned char)*q)) return false;
      p = q; return true;
    }

    // Resolve a matched system name to its value. The *OK flags never error (that is the
    // point of them); every other group errors when its data is missing rather than
    // returning a plausible-looking zero.
    double sysValue(uint8_t id) {
      switch (id) {                                   // availability flags: always safe
        case 41: return sys.satOk  ? 1 : 0;  case 42: return sys.timeOk ? 1 : 0;
        case 43: return sys.posOk  ? 1 : 0;  case 44: return sys.wxOk   ? 1 : 0;
        case 45: return sys.spwxOk ? 1 : 0;  case 46: return sys.passOk ? 1 : 0;
        case 48: return sys.gpsOk  ? 1 : 0;
        case 49: return sys.gpsSats;         case 50: return sys.gpsSpd;
        case 54: return sys.heapFree;        case 55: return sys.upTime;
        case 56: return sys.nSat;            case 57: return sys.nTx;
        case 58: return sys.pN;
      }
      if (id >= 1  && id <= 13 && !sys.satOk)  { err = "no active satellite"; return 0; }
      if (id >= 14 && id <= 17 && !sys.passOk) { err = "no pass predicted";   return 0; }
      if (id >= 18 && id <= 21 && (!sys.posOk || !sys.timeOk))
                                               { err = "no position/clock";   return 0; }
      if (id >= 22 && id <= 24 && !sys.posOk)  { err = "no position";         return 0; }
      if (id >= 25 && id <= 30 && !sys.timeOk) { err = "clock not set";       return 0; }
      if (id >= 31 && id <= 33 && !sys.spwxOk) { err = "no space wx data";    return 0; }
      if (id >= 34 && id <= 37 && !sys.wxOk)   { err = "no weather data";     return 0; }
      if (id == 47 && (!sys.timeOk || !sys.posOk)) { err = "no position/clock"; return 0; }
      if (id >= 51 && id <= 53 && !sys.gpsOk)  { err = "no GPS fix";        return 0; }
      if (id >= 59 && id <= 63 && !sys.txOk)   { err = "no transponder (TXSEL)"; return 0; }
      switch (id) {
        case 1:  return sys.satAz;   case 2:  return sys.satEl;   case 3:  return sys.satRng;
        case 4:  return sys.satRR;   case 5:  return sys.satLat;  case 6:  return sys.satLon;
        case 7:  return sys.satAlt;  case 8:  return sys.satSun;  case 9:  return sys.satInc;
        case 10: return sys.satEcc;  case 11: return sys.satRaan; case 12: return sys.satMM;
        case 13: return sys.satNor;  case 14: return sys.aosIn;   case 15: return sys.losIn;
        case 16: return sys.passEl;  case 17: return sys.passVis; case 18: return sys.sunAz;
        case 19: return sys.sunEl;   case 20: return sys.moonAz;  case 21: return sys.moonEl;
        case 22: return sys.myLat;   case 23: return sys.myLon;   case 24: return sys.myAlt;
        case 25: return sys.utcH;    case 26: return sys.utcM;    case 27: return sys.utcS;
        case 28: return sys.utcDay;  case 29: return sys.utcMon;  case 30: return sys.utcYr;
        case 31: return sys.sfi;     case 32: return sys.kp;      case 33: return sys.aIdx;
        case 34: return sys.wxTemp;  case 35: return sys.wxWind;  case 36: return sys.wxDir;
        case 37: return sys.wxHum;   case 38: return sys.batt;    case 39: return sys.gpAge;
        case 40: return sys.nfav;
        case 64: return sys.ssn;     case 65: return sys.flare;   case 66: return sys.bz;
        case 67: return sys.swSpeed; case 69: return sys.muf;
        case 70: return sys.fcKp1;   case 71: return sys.fcKp2;   case 72: return sys.fcKp3;
        case 73: return sys.magDecl;
        case 47: return sys.lstHr;
        case 51: return sys.gpsLat;  case 52: return sys.gpsLon;  case 53: return sys.gpsAlt;
        case 59: return sys.txDl;    case 60: return sys.txUl;    case 61: return sys.txBw;
        case 62: return sys.txInv;   case 63: return sys.txLin;
        case 74: return sys.lshell;  case 75: return sys.bratio;  case 76: return sys.bfield;
        case 77: return sys.inBelt;  case 78: return sys.inSaa;
        case 79: return sys.decayD;  case 80: return sys.decaySrc;
        case 81: return sys.battMv;  case 82: return sys.charging;
        case 83: return sys.heapBlk;
        case 84: return sys.doppRx;  case 85: return sys.doppTx;
        case 86: return sys.txOk ? 1 : 0;
      }
      return 0;
    }
    bool kw(const char* w) {                     // match keyword (case-insensitive), not mid-identifier
      const char* q = p; while (*w) { if (toupper((unsigned char)*q) != *w) return false; ++q; ++w; }
      p = q; return true;
    }
    double expr() { double v = term(); for (;;) { ws();
      if (*p == '+') { ++p; v += term(); } else if (*p == '-') { ++p; v -= term(); } else break; } return v; }
    double term() { double v = power(); for (;;) { ws();
      if (*p == '*') { ++p; v *= power(); }
      else if (*p == '/') { ++p; double d = power(); v = (d != 0) ? v / d : 0; }
      else if (kwb("MOD")) { double d = power(); v = (d != 0) ? fmod(v, d) : 0; } else break; } return v; }
    double power() { double v = unary(); ws(); if (*p == '^') { ++p; v = pow(v, power()); } return v; }
    double unary() { DepthGuard dg(this); if (!dg.ok) return 0;
      ws(); if (*p == '-') { ++p; return -unary(); } if (*p == '+') { ++p; return unary(); } return atom(); }
    // Depth-guarded: every path that can recurse (parenthes, unary sign, NOT, and every
    // function argument) funnels through atom() or unary(), so counting here bounds the
    // whole expression grammar. 48 nested terms is far beyond any real program and
    // costs ~15 KB of stack at ~5 frames x ~64 B per level -- the margin that matters,
    // since a 4 KB line of '(' would otherwise demand megabytes against the 16 KB loop
    // task stack and hard-fault the device.
    struct DepthGuard {
      BasicVM* v; bool ok;
      DepthGuard(BasicVM* vm) : v(vm), ok(++vm->depth <= 48) {
        if (!ok && vm->err.isEmpty()) vm->err = "expression too deep";
      }
      ~DepthGuard() { --v->depth; }
    };
    double atom() { DepthGuard dg(this); if (!dg.ok) return 0;
      ws();
      const double D2R = 0.017453292519943295;
      if (*p == '(') { ++p; double v = expr(); ws(); if (*p == ')') ++p; else err = "paren"; return v; }
      if (*p == '@') {                            // the one array: @(index)
        ++p; ws(); if (*p != '(') { err = "@ needs ()"; return 0; }
        ++p; double iv = expr(); ws(); if (*p == ')') ++p; else { err = "paren"; return 0; }
        int i = (int)iv;
        if (!arr) { err = "no DIM @"; return 0; }
        if (i < 0 || i >= arrN) { err = "@ index"; return 0; }
        return arr[i];
      }
      if (isalpha((unsigned char)*p)) {
        if (kw("ABS")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return fabs(v); } }
        else if (kw("INT")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return floor(v); } }
        else if (kw("SQR")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return v>0?sqrt(v):0; } }
        else if (kw("SIN")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return sin(v*D2R); } }
        else if (kw("COS")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return cos(v*D2R); } }
        else if (kw("RND")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p;
              long m = (long)(v < 1 ? 1 : v); return (double)((unsigned)rand() % (m > 0 ? m : 1)); } }
        else if (kw("TAN")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return tan(v*D2R); } }
        else if (kw("ATN")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return atan(v)/D2R; } }
        else if (kw("LOG")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return v>0?log(v):0; } }
        else if (kw("EXP")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return exp(v); } }
        else if (kw("SGN")) { ws(); if (*p=='(') { ++p; double v=expr(); ws(); if(*p==')')++p; return (v>0)-(v<0)+0.0; } }
        else if (kw("MIN")) { ws(); if (*p=='(') { ++p; double a2=expr(); ws(); if(*p==',')++p;
              double b2=expr(); ws(); if(*p==')')++p; return a2<b2?a2:b2; } }
        else if (kw("MAX")) { ws(); if (*p=='(') { ++p; double a2=expr(); ws(); if(*p==',')++p;
              double b2=expr(); ws(); if(*p==')')++p; return a2>b2?a2:b2; } }
        // Pass lookahead: PASSAOS(k)/PASSLOS(k)/PASSMAX(k), k = 1..PASSN. Function
        // style (they take an index), so they're matched here, before the bare-name
        // table -- which also keeps its PASSEL/PASSOK prefixes unambiguous.
        else if (kw("PASSAOS") || kw("PASSLOS") || kw("PASSMAX")) {
          // p-3..p-1 are the final 3 chars of whichever keyword matched: AOS/LOS/MAX.
          char w3 = toupper((unsigned char)*(p - 3));
          ws(); if (*p != '(') { err = "needs (k)"; return 0; }
          ++p; double kv = expr(); ws(); if (*p == ')') ++p; else { err = "paren"; return 0; }
          int k = (int)kv;
          if (k < 1 || k > sys.pN) { err = "pass index"; return 0; }
          if (w3 == 'A') return sys.pAos[k - 1];            // ...AOS
          if (w3 == 'L') return sys.pLos[k - 1];            // ...LOS
          return sys.pMax[k - 1];                           // ...MAX
        }
        // System data (read-only). Checked before the single-letter fallback; every
        // multi-letter identifier was already an error before these existed, so no
        // previously-valid program changes meaning.
        { int slen = 0; uint8_t sid = basicSysMatch(p, slen);
          if (sid) { p += slen; return sysValue(sid); } }
        // single-letter variable. Re-check isalpha: the guard on entry to this block
        // held at the identifier's first char, but a function keyword matched without
        // its '(' (ABS 5 / a bare trailing SIN) has advanced p since -- possibly onto
        // a digit or the NUL, where v-'A' would index far outside vars[] and ++p on
        // NUL would walk out of the line entirely.
        if (!isalpha((unsigned char)*p)) { err = "syntax"; return 0; }
        char v = toupper((unsigned char)*p); ++p;
        // guard: a two-letter identifier we don't know is an error, not a var
        if (isalpha((unsigned char)*p)) { err = "unknown name"; return 0; }
        return vars[v - 'A'];
      }
      char* e = nullptr; double v = strtod(p, &e);
      if (e == p) { err = "syntax"; return 0; }
      p = e; return v;
    }
    // IF-condition grammar: OR-of ANDs of (optionally NOT-ed) relations. Word operators
    // are boundary-checked (kwb) so single-letter variables can't be eaten. Both sides
    // are always evaluated (no short-circuit) -- simple, and side-effect-free here.
    double lnot() { DepthGuard dg(this); if (!dg.ok) return 0;
      ws(); if (kwb("NOT")) return (lnot() == 0) ? 1 : 0; return relation(); }
    double land() { double v = lnot(); for (;;) { ws();
      if (kwb("AND")) { double r = lnot(); v = (v != 0 && r != 0) ? 1 : 0; } else break; } return v; }
    double lor()  { double v = land(); for (;;) { ws();
      if (kwb("OR"))  { double r = land(); v = (v != 0 || r != 0) ? 1 : 0; } else break; } return v; }

    double relation() {
      double l = expr(); ws();
      char a = *p, b = *(p + 1);
      if (a == '<' && b == '=') { p += 2; return l <= expr(); }
      if (a == '>' && b == '=') { p += 2; return l >= expr(); }
      if (a == '<' && b == '>') { p += 2; return l != expr(); }
      if (a == '=') { ++p; return l == expr(); }
      if (a == '<') { ++p; return l < expr(); }
      if (a == '>') { ++p; return l > expr(); }
      return l;
    }
    int findLine(int num) { for (int i = 0; i < nLines; ++i) if (lines[i].num == num) return i; return -1; }

    void printNum(double v) {
      char b[32];
      if (v == floor(v) && fabs(v) < 1e15) snprintf(b, sizeof(b), "%ld", (long)v);
      else snprintf(b, sizeof(b), "%g", v);
      out += b;
    }
    // PRINT's item grammar rendered into a caller String -- one finished line, no
    // trailing newline. Shared by LPRINT (report sinks) and FPRINT (gated file).
    void emitLine(String& dst) {
      for (;;) { ws();
        if (*p == 0 || *p == ':') break;
        if (*p == '"') { ++p; while (*p && *p != '"') dst += *p++; if (*p == '"') ++p; }
        else if (*p == ',') { dst += "  "; ++p; }
        else if (*p == ';') { ++p; }
        else { double v = expr(); if (!err.isEmpty()) return;
          char b[32];
          if (v == floor(v) && fabs(v) < 1e15) snprintf(b, sizeof(b), "%ld", (long)v);
          else snprintf(b, sizeof(b), "%g", v);
          dst += b; }
      }
    }

    void printStmt() {
      // suppressNL is set by a trailing ',' or ';' and cleared when an item follows it.
      // emitted tracks whether any item was printed at all: an empty PRINT (no items,
      // no trailing separator) still prints a blank line.
      bool suppressNL = false, emitted = false, sawSep = false;
      for (;;) { ws();
        if (*p == 0 || *p == ':') break;
        if (*p == '"') { ++p; while (*p && *p != '"') out += *p++; if (*p == '"') ++p; emitted = true; suppressNL = false; }
        else if (*p == ',') { out += "  "; ++p; suppressNL = true; sawSep = true; }
        else if (*p == ';') { ++p; suppressNL = true; sawSep = true; }
        else { double v = expr(); if (!err.isEmpty()) return;   // check BEFORE printing --
               // emitLine() already does; printing first appended a bogus 0 to the output
               printNum(v); emitted = true; suppressNL = false; }
      }
      // Newline unless the statement ended with a ',' or ';'. A bare PRINT (nothing at
      // all -- no items and no separator) always emits a blank line.
      if (!suppressNL || (!emitted && !sawSep)) out += '\n';
    }
    int assign() { ws();
      if (*p == '@') {                            // @(index) = expr
        ++p; ws(); if (*p != '(') { err = "@ needs ()"; return -1; }
        ++p; double iv = expr(); ws(); if (*p == ')') ++p; else { err = "paren"; return -1; }
        ws(); if (*p == '=') ++p; else { err = "="; return -1; }
        double val = expr();
        int i = (int)iv;
        if (!arr) { err = "no DIM @"; return -1; }
        if (i < 0 || i >= arrN) { err = "@ index"; return -1; }
        arr[i] = val; return -1;
      }
      if (!isalpha((unsigned char)*p)) { err = "var A-Z"; return -1; }   // LET 5=1 must not index vars[-12]
      char v = toupper((unsigned char)*p); ++p; ws(); if (*p == '=') ++p; vars[v-'A'] = expr(); return -1; }

    // Pull the next DATA value (numbers only, comma-separated, optional sign) scanning
    // forward from the read cursor across the program's DATA lines. false = out of DATA.
    bool nextData(double& out) {
      for (;;) {
        if (dataP) {                              // continue inside the current DATA line
          const char* q = dataP;
          while (*q == ' ' || *q == '\t' || *q == ',') ++q;
          if (*q) {
            char* e = nullptr; double v = strtod(q, &e);
            if (e == q) { dataP = nullptr; ++dataLineIdx; continue; }   // junk: next line
            dataP = e; out = v; return true;
          }
          dataP = nullptr; ++dataLineIdx;         // line exhausted
        }
        // find the next DATA line at/after dataLineIdx
        while (dataLineIdx < nLines) {
          const char* q = lines[dataLineIdx].text;
          while (*q == ' ' || *q == '\t') ++q;
          if (isdigit((unsigned char)*q)) { strtol(q, (char**)&q, 10);
                                            while (*q == ' ' || *q == '\t') ++q; }
          bool isData = true; const char* w = "DATA";
          for (int k = 0; k < 4; ++k)
            if (toupper((unsigned char)q[k]) != w[k]) { isData = false; break; }
          if (isData && !isalnum((unsigned char)q[4])) { dataP = q + 4; break; }
          ++dataLineIdx;
        }
        if (dataLineIdx >= nLines) return false;  // no more DATA anywhere
      }
    }

    // Execute the ONE statement at p (line number already skipped by execLine).
    // -1 fall-through, >=0 jump to line index, -999 END. Does not touch ':'.
    int execStmt(int curIdx) {
      ws();
      if (*p == 0) return -1;
      if (kw("REM")) return -1;
      if (kw("END")) return -999;
      if (kw("PRINT")) { printStmt(); return -1; }
      if (*p == '?') { ++p; printStmt(); return -1; }
      if (kw("LET")) return assign();
      if (kw("IF")) {
        double cond = lor(); ws();
        kw("THEN"); ws();
        if (cond != 0) {
          // Bare line number after THEN is an implicit GOTO (IF X THEN 90). Every other
          // consequent is a full statement, so delegate to execStmt rather than keeping a
          // hand-picked subset here -- the old subset omitted TEXT/LINE/CIRCLE/GOSUB/CLS,
          // so "IF C=3 THEN TEXT ..." fell through to assign() and reported "unknown name".
          if (isdigit((unsigned char)*p)) { int ln = (int)strtol(p, (char**)&p, 10);
            int idx = findLine(ln); if (idx < 0) { err = "no line " + String(ln); return -1; } return idx; }
          return execStmt(curIdx);
        }
        return -1;
      }
      if (kw("GOTO")) { ws(); int ln = (int)strtol(p, (char**)&p, 10);
        int idx = findLine(ln); if (idx < 0) { err = "no line " + String(ln); return -1; } return idx; }
      if (kw("GOSUB")) { ws(); int ln = (int)strtol(p, (char**)&p, 10);
        int idx = findLine(ln); if (idx < 0) { err = "no line " + String(ln); return -1; }
        if (gosubSP >= BASIC_GOSUB_MAX) { err = "GOSUB too deep"; return -1; }
        gosubStack[gosubSP++] = curIdx + 1; return idx; }
      if (kw("RETURN")) { if (gosubSP <= 0) { err = "RETURN w/o GOSUB"; return -1; } return gosubStack[--gosubSP]; }
      if (kw("FOR")) { ws();
        if (!isalpha((unsigned char)*p)) { err = "FOR var A-Z"; return -1; }
        char v = toupper((unsigned char)*p); ++p; ws(); if (*p == '=') ++p;
        double start = expr(); ws(); kw("TO"); double lim = expr(); ws(); double step = 1;
        if (kw("STEP")) step = expr();
        vars[v-'A'] = start;
        if (forSP >= BASIC_FOR_MAX) { err = "FOR too deep"; return -1; }
        forStack[forSP++] = { v - 'A', lim, step, curIdx, p }; return -1; }
      if (kw("NEXT")) { ws();
        if (forSP <= 0) { err = "NEXT w/o FOR"; return -1; }
        ForRec& f = forStack[forSP - 1];
        vars[f.var] += f.step;
        bool cont = (f.step >= 0) ? (vars[f.var] <= f.limit) : (vars[f.var] >= f.limit);
        if (!cont) { forSP--; return -1; }
        // Resume the loop body. If the body began mid-line (a same-line
        // "FOR..:..:NEXT"), the next statement after the FOR header is on THIS line, so
        // hand the cursor back to the line runner rather than jumping to the following
        // line and skipping it. forResumeP carries the position; -998 asks execLine to
        // continue from it.
        ws();
        bool bodyOnThisLine = (f.bodyP && *f.bodyP && *skipToColonBody(f.bodyP) != 0);
        if (bodyOnThisLine) { forResumeP = f.bodyP; return -998; }
        return f.lineIdx + 1;
      }
      if (kw("DIM")) {                           // DIM @(n) -- the one numeric array
        ws(); if (*p == '@') ++p; else { err = "DIM @(n)"; return -1; }
        ws(); if (*p != '(') { err = "DIM @(n)"; return -1; }
        ++p; double nv = expr(); ws(); if (*p == ')') ++p; else { err = "paren"; return -1; }
        int n = (int)nv;
        if (n < 1 || n > 256) { err = "@ size 1..256"; return -1; }
        delete[] arr; arr = new (std::nothrow) double[n];
        if (!arr) { err = "out of memory"; arrN = 0; return -1; }
        arrN = n; for (int i = 0; i < n; ++i) arr[i] = 0;
        return -1; }
      if (kwb("DATA")) return -1;                 // data lines execute as no-ops
      if (kw("RESTORE")) { dataLineIdx = 0; dataP = nullptr; return -1; }
      if (kw("READ")) {                           // READ X[,Y,@(I),...]
        for (;;) { ws();
          double v;
          if (!nextData(v)) { err = "out of DATA"; return -1; }
          if (*p == '@') {                        // array target
            ++p; ws(); if (*p != '(') { err = "@ needs ()"; return -1; }
            ++p; double iv = expr(); ws(); if (*p == ')') ++p; else { err = "paren"; return -1; }
            int i = (int)iv;
            if (!arr || i < 0 || i >= arrN) { err = "@ index"; return -1; }
            arr[i] = v;
          } else if (isalpha((unsigned char)*p)) {
            char vc = toupper((unsigned char)*p); ++p;
            if (isalnum((unsigned char)*p)) { err = "READ var"; return -1; }
            vars[vc - 'A'] = v;
          } else { err = "READ var"; return -1; }
          ws(); if (*p == ',') { ++p; continue; }
          break;
        }
        return -1; }
      if (kw("ON")) {                             // ON expr GOTO l1,l2,... (1-based)
        double sel = expr(); ws();
        if (!kw("GOTO")) { err = "ON..GOTO"; return -1; }
        int want = (int)sel, i = 1, target = -1;
        for (;;) { ws();
          if (!isdigit((unsigned char)*p)) break;
          int ln = (int)strtol(p, (char**)&p, 10);
          if (i == want) target = ln;
          ++i; ws();
          if (*p == ',') { ++p; continue; }
          break;
        }
        if (target < 0) return -1;                // out of range: fall through (classic)
        int idx = findLine(target);
        if (idx < 0) { err = "no line " + String(target); return -1; }
        return idx; }
      if (kw("SATSEL")) {                        // re-snapshot the SAT* names for sat #expr
        double iv = expr(); if (!err.isEmpty()) return -1;
        if (!satselCb) { err = "SATSEL unavailable"; return -1; }
        if (--satselLeft < 0) { err = "SATSEL limit (2000/run)"; return -1; }
        double o[14];
        o[13] = 0;
        if (!satselCb(host, (int)iv, o)) { err = "bad sat index"; return -1; }  // bad INDEX only
        // The transponder context follows the selection even when the satellite
        // itself cannot be propagated: a program may legitimately want to read a
        // decayed bird's transponders. Any previously TXSELed values are stale now,
        // so clear TXOK -- silently keeping them was the old trap.
        sys.nTx  = o[13];
        sys.txOk = false;
        sys.txDl = sys.txUl = sys.txBw = sys.txInv = sys.txLin = 0;
        if (o[0] <= -998.0) { sys.satOk = false; return -1; }   // valid index, no fix/dead TLE:
                                                                //   SATOK=0 so the program branches
        sys.satAz = o[0]; sys.satEl = o[1]; sys.satRng = o[2]; sys.satRR = o[3];
        sys.satLat = o[4]; sys.satLon = o[5]; sys.satAlt = o[6]; sys.satSun = o[7];
        sys.satInc = o[8]; sys.satEcc = o[9]; sys.satRaan = o[10]; sys.satMM = o[11];
        sys.satNor = o[12]; sys.satOk = true;
        return -1; }
      if (kw("TXSEL")) {                         // snapshot transponder #expr of the
                                                 // satellite SATSEL chose (or the
                                                 // tracked one if SATSEL was not used)
        double iv = expr(); if (!err.isEmpty()) return -1;
        if (!txselCb) { err = "TXSEL unavailable"; return -1; }
        double o[5];
        if (!txselCb(host, (int)iv, o)) { err = "bad tx index"; return -1; }
        sys.txDl = o[0]; sys.txUl = o[1]; sys.txBw = o[2]; sys.txInv = o[3];
        sys.txLin = o[4]; sys.txOk = true;
        return -1; }
      if (kw("LPRINT")) {                        // PRINT, but through the report sinks
        String ln2; emitLine(ln2); if (!err.isEmpty()) return -1;
        if (!lprCb || !lprCb(host, ln2.c_str(), 1)) { err = "no print output"; return -1; }
        lprUsed = true; return -1; }
      if (kw("CLS"))  { if (gfxCb) { gfxCb(host, 0, 0,0,0,0,0, nullptr); gfxUsed = true; } return -1; }
      if (kw("SHOW")) { if (gfxCb) { gfxCb(host, 5, 0,0,0,0,0, nullptr); gfxUsed = true; } return -1; }
      if (kw("PSET")) {
        double x = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double y = expr(); double col = 1; ws();
        if (*p == ',') { ++p; col = expr(); }
        if (gfxCb) { gfxCb(host, 1, x, y, col, 0, 0, nullptr); gfxUsed = true; }
        return -1; }
      if (kw("LINE")) {
        double x1 = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double y1 = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double x2 = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double y2 = expr(); double col = 1; ws();
        if (*p == ',') { ++p; col = expr(); }
        if (gfxCb) { gfxCb(host, 2, x1, y1, x2, y2, col, nullptr); gfxUsed = true; }
        return -1; }
      if (kw("CIRCLE")) {
        double x = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double y = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double r = expr(); double col = 1; ws();
        if (*p == ',') { ++p; col = expr(); }
        if (gfxCb) { gfxCb(host, 3, x, y, r, col, 0, nullptr); gfxUsed = true; }
        return -1; }
      if (kw("TEXT")) {
        double x = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        double y = expr(); ws(); if (*p == ',') ++p; else { err = ","; return -1; }
        ws();
        char tb[64]; tb[0] = 0;
        if (*p == '"') { ++p; int i = 0;
          while (*p && *p != '"' && i < 63) tb[i++] = *p++;
          if (*p == '"') ++p; tb[i] = 0;
        } else { double v = expr(); if (!err.isEmpty()) return -1;
          if (v == floor(v) && fabs(v) < 1e15) snprintf(tb, sizeof(tb), "%ld", (long)v);
          else snprintf(tb, sizeof(tb), "%g", v); }
        if (gfxCb) { gfxCb(host, 4, x, y, 0, 0, 0, tb); gfxUsed = true; }
        return -1; }
      if (kw("FOPEN")) {                         // FOPEN "name" (append, /CardSat/basic/)
        ws(); if (*p != '"') { err = "FOPEN \"name\""; return -1; }
        ++p; char nb[40]; int i = 0;
        while (*p && *p != '"' && i < 39) nb[i++] = *p++;
        if (*p == '"') ++p; nb[i] = 0;
        int rc = fileCb ? fileCb(host, 0, nb, nullptr) : 1;
        if (rc == 1) { err = "file write off (Settings)"; return -1; }
        if (rc == 2) { err = "bad file name"; return -1; }
        if (rc != 0) { err = "open failed"; return -1; }
        fileUsed = true; return -1; }
      if (kw("FPRINT")) {
        String ln2; emitLine(ln2); if (!err.isEmpty()) return -1;
        int rc = fileCb ? fileCb(host, 1, ln2.c_str(), nullptr) : 4;
        if (rc != 0) { err = "no file open (FOPEN)"; return -1; }
        return -1; }
      if (kw("FCLOSE")) { if (fileCb) fileCb(host, 2, nullptr, nullptr); return -1; }
      if (kw("FILES")) {                         // list /CardSat/basic/ to the console
        String ls;
        if (fileCb && fileCb(host, 3, nullptr, &ls) == 0) out += ls;
        else out += "(no files)\n";
        return -1; }
      if (*p == '@') return assign();             // bare @(i) = expr
      if (isalpha((unsigned char)*p)) return assign();     // bare VAR = expr
      err = "syntax"; return -1;
    }

    // Execute a whole line, honoring ':' as a statement separator (the tutorial and the
    // manual both document it, e.g. "40 FOR I=1 TO 3: PRINT I: NEXT"). Only a fall-through
    // statement continues to the next: a GOTO/GOSUB/IF-jump (>=0), END (-999) or error
    // consumes the remainder of the line, so "10 GOTO 90 : PRINT X" never prints and a
    // taken IF does not run statements meant for the not-taken path. A colon inside a
    // string or after REM is never seen here -- REM returns before we look, and printStmt/
    // emitLine consume their own quoted text.
    int execLine(const char* text, int curIdx) {
      p = text; ws();
      if (isdigit((unsigned char)*p)) { strtol(p, (char**)&p, 10); ws(); }   // skip line number
      for (;;) {
        int r = execStmt(curIdx);
        if (r == -998) {                           // same-line FOR body resume (NEXT looped)
          p = skipToColonBody(forResumeP); forResumeP = nullptr; continue;
        }
        if (r != -1 || !err.isEmpty()) return r;   // jump/END/error: abandon rest of line
        ws();
        if (*p != ':') return -1;                  // end of line
        ++p;                                       // consume ':' and run the next statement
      }
    }

    void run() {
      out = ""; err = ""; gosubSP = forSP = 0; stmts = 0; depth = 0;
      for (int i = 0; i < 26; ++i) vars[i] = 0;
      int pc = 0;
      while (pc >= 0 && pc < nLines) {
        if (++stmts > BASIC_STMT_BUDGET) { err = "stopped (loop? >500k stmts)"; return; }
        if ((stmts & 0x3FF) == 0) yield();          // keep the watchdog happy on long runs
        if ((int)out.length() > BASIC_OUT_MAX) { err = "output too large (>6KB)"; return; }
        int next = execLine(lines[pc].text, pc);
        if (!err.isEmpty()) { err = "line " + String(lines[pc].num) + ": " + err; return; }
        if (next == -999) return;
        pc = (next >= 0) ? next : pc + 1;
      }
    }
  };

  // Parse basicBuf into the VM line table (sorted by line number). Lines without a
  // leading number are skipped (BASIC requires line numbers here). Returns false + sets
  // err if there are too many lines. The VM points into a persistent copy of the source.
  // Inside the anonymous namespace so it can name BasicVM.
  // `work` is the caller-owned, tokenized copy of the source; the VM's line pointers
  // reference into it, so it must outlive the run (see App::basicRun). Passed in rather
  // than held static so nothing here occupies .bss at boot.
  static bool basicParse(BasicVM& vm, const String& src, String& work, String& perr) {
  work = src;
  vm.nLines = 0;
  char* base = (char*)work.c_str();   // mutable; we NUL-terminate each line in place
  char* s = base;
  while (*s) {
    char* lineStart = s;
    while (*s && *s != '\n') ++s;
    if (*s == '\n') { *s = 0; ++s; }
    // trim leading spaces
    char* q = lineStart; while (*q == ' ' || *q == '\t') ++q;
    if (isdigit((unsigned char)*q)) {
      if (vm.nLines >= BASIC_MAX_LINES) { perr = "too many lines (>200)"; return false; }
      vm.lines[vm.nLines].num = atoi(q);
      vm.lines[vm.nLines].text = q;
      vm.nLines++;
    }
  }
  // insertion sort by line number (stable, small n)
  for (int i = 1; i < vm.nLines; ++i) {
    BasicVM::Line key = vm.lines[i]; int j = i - 1;
    while (j >= 0 && vm.lines[j].num > key.num) { vm.lines[j+1] = vm.lines[j]; j--; }
    vm.lines[j+1] = key;
  }
  return true;
  }
}