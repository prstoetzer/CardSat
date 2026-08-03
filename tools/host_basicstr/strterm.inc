String strTerm() {
      ws();
      if (*p == '"') { ++p; String s; while (*p && *p != '"') s += *p++; if (*p == '"') ++p; return s; }
      if (kw("LEFT$"))  { String s; double n; if (!str2(s, n)) return ""; 
                          int k = (int)n; if (k < 0) k = 0;
                          return k >= (int)s.length() ? s : s.substring(0, k); }
      if (kw("RIGHT$")) { String s; double n; if (!str2(s, n)) return "";
                          int k = (int)n; if (k < 0) k = 0;
                          return k >= (int)s.length() ? s : s.substring(s.length() - k); }
      if (kw("MID$"))   { // MID$(s, start [, len]) -- start is 1-BASED, as in every
                          // Microsoft BASIC. Off-by-one here would be silently wrong
                          // in every program a user ports.
                          ws(); if (*p != '(') { err = "MID$ ("; return ""; } ++p;
                          String s = strExpr(); ws(); if (*p != ',') { err = "MID$ ,"; return ""; } ++p;
                          double st = expr(); double ln = -1; ws();
                          if (*p == ',') { ++p; ln = expr(); ws(); }
                          if (*p == ')') ++p; else { err = "MID$ )"; return ""; }
                          int i = (int)st - 1; if (i < 0) i = 0;
                          if (i >= (int)s.length()) return "";
                          if (ln < 0) return s.substring(i);
                          int k = (int)ln; if (k < 0) k = 0;
                          return s.substring(i, min((int)s.length(), i + k)); }
      if (kw("CHR$"))   { double v = paren1(); char b[2] = { (char)(int)v, 0 }; return String(b); }
      if (kw("STR$"))   { double v = paren1(); return fmtNum(v); }
      if (kw("UCASE$")) { String s = paren1s(); s.toUpperCase(); return s; }
      if (kw("LCASE$")) { String s = paren1s(); s.toLowerCase(); return s; }
      if (kw("TRIM$"))  { String s = paren1s(); s.trim(); return s; }
      // GRID$(lat, lon) -- Maidenhead, via the firmware's own converter rather than a
      // second implementation. A program that computes a grid differently from the
      // tracker it runs on is worse than one that cannot compute it at all.
      if (kw("GRID$"))  { ws(); if (*p != '(') { err = "GRID$ ("; return ""; } ++p;
                          double la = expr(); ws();
                          if (*p != ',') { err = "GRID$ ,"; return ""; } ++p;
                          double lo = expr(); ws();
                          if (*p == ')') ++p; else { err = "GRID$ )"; return ""; }
                          return Location::toGrid(la, lo); }
      // DXCC$(code) -- entity name for an ARRL code, from the table already on board.
      if (kw("DXCC$"))  { double v = paren1();
                          int code = (int)v;
                          for (size_t i = 0; i < sizeof(DXCC_LK) / sizeof(DXCC_LK[0]); ++i)
                            if (DXCC_LK[i].code == code) return String(DXCC_LK[i].name);
                          return String(""); }
      // TIME$ / DATE$ -- from the SAME snapshot the numeric fields use, so a program
      // cannot print a timestamp that disagrees with its own UTCH/UTCM readings.
      if (kw("TIME$"))  { if (!sys.timeOk) { err = "no time"; return ""; }
                          char b[12]; snprintf(b, sizeof(b), "%02d:%02d:%02d",
                            (int)sys.utcH, (int)sys.utcM, (int)sys.utcS); return String(b); }
      if (kw("DATE$"))  { if (!sys.timeOk) { err = "no time"; return ""; }
                          char b[12]; snprintf(b, sizeof(b), "%04d-%02d-%02d",
                            (int)sys.utcYr, (int)sys.utcMon, (int)sys.utcDay); return String(b); }
      if (isalpha((unsigned char)*p) && *(p + 1) == '$') {          // A$ .. Z$
        int i = toupper((unsigned char)*p) - 'A'; p += 2;
        strUsed = true;
        String* t = sv(); if (!t) { err = "out of memory"; return ""; }
        return t[i];
      }
      err = "string expected"; return "";
    }
    