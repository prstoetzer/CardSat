#include "print.h"
#include "storage.h"
#include "font16x32.h"
#include <new>              // std::nothrow raster buffers (heap-on-demand)

// Three-sink report emitter with a selectable page language on the network sink.
// Per-sink hard wrapping keeps a narrow printer inside its paper while a wider
// file/serial sink shows the full-width layout.
namespace {
  WiFiClient s_cli;
  bool       s_pOK  = false;            // printer sink connected (this-session, cleared by end())
  bool       s_pConnected = false;      // LATCHED: printer connected at least once (survives end())
  int        s_pCols = 32;              // printer sink width (columns)
  int        s_fmt  = Printer::FMT_ESCPOS;
  int        s_transport = Printer::RAW9100;   // RAW9100 or IPP
  int        s_paper = 0;                      // raster paper: 0 Letter, 1 A4
  bool       s_chunkOpen = false;       // IPP: HTTP chunked body is open
  bool       s_ippAccepted = false;     // IPP: printer returned HTTP 2xx for the job
  bool       s_writeOk = true;          // all socket writes completed (no partial/failed write)
  // Raster mode (FMT_PWG_RASTER): report lines are COLLECTED, then rendered as a
  // page at end()/feedCut() rather than streamed as text.
  static const int RAS_MAX_LINES = 400;   // ~10 letter pages of report text
  // Heap-on-demand (RAM Tier 1): 400 String objects are 6,400 B of .bss -- and
  // after a raster job their CONTENTS used to stay allocated until the next job
  // overwrote them. Allocated in begin() only for raster jobs, freed (objects AND
  // contents) in end(). ESC/POS receipts -- the field path -- never pay for this.
  String*    s_rasLines = nullptr;
  int        s_rasN = 0;
  bool       s_ser  = false;            // serial sink active
  bool       s_file = false;            // file sink active
  int        s_fCols = Printer::FILE_COLS;
  File       s_f;
  String     s_fpath;

  // ---- PostScript page state (FMT_POSTSCRIPT only) ----
  // Courier at 10 pt on US Letter: 72 lines fit in a 720 pt text column from a
  // 750 pt top; 12 pt leading. We stream show-commands and eject as we fill.
  int   s_psY   = 0;                    // current baseline (pt from bottom); 0 = need new page
  int   s_psPage = 0;                   // pages started
  const int PS_TOP = 750, PS_BOT = 40, PS_LEAD = 12, PS_LEFT = 36;

  // ---- ZPL label state (FMT_ZPL only) ----
  // Each report line is one ^FO(x,y)^A0N,h,w^FD(text)^FS field. y advances down
  // the label; when it passes ZPL_MAX we close the label (^XZ) and open a new one.
  int   s_zplY  = 0;                    // current y (dots from top); -1 = no label open
  const int ZPL_TOP = 20, ZPL_MAX = 1180, ZPL_LEAD = 26, ZPL_LEFT = 20;
  const int ZPL_FH = 22, ZPL_FW = 12;   // font cell height/width (dots) ~ small monospace

  template <typename W>
  void wrapTo(const String& s, int width, W writeLine) {
    // Word-aware wrapping: break at the last space that fits; if a single word is
    // longer than the width, hard-break it so nothing overflows the column. Leading
    // spaces created by a break are consumed. Blank lines are preserved.
    int n = s.length();
    if (n == 0) { writeLine(String("")); return; }
    if (width < 1) width = 1;
    int i = 0;
    while (i < n) {
      if (n - i <= width) { writeLine(s.substring(i)); break; }
      // find last space in s[i .. i+width] (inclusive of the boundary char)
      int brk = -1;
      for (int k = i + width; k > i; --k) { if (s.charAt(k) == ' ') { brk = k; break; } }
      if (brk <= i) {                       // no space: hard-break a long word
        writeLine(s.substring(i, i + width));
        i += width;
      } else {
        writeLine(s.substring(i, brk));     // break at the space
        i = brk + 1;                        // consume it
      }
    }
  }

  // ---- HTTP chunked-transfer writer (IPP transport) ----
  // Each write becomes one HTTP chunk: <hex-length>CRLF <bytes> CRLF. This lets us
  // stream a document of unknown total length (the report is built line-by-line),
  // keeping the zero-buffer model. RAW9100 writes go straight to the socket.
  // Write every byte or record a failure. WiFiClient::write returns bytes written;
  // a short write (full TCP buffer, disconnect, timeout) is a real error we must not
  // ignore -- otherwise a job that dies mid-transmission still reports success.
  void wrChecked(const uint8_t* b, size_t n) {
    size_t off = 0;
    while (off < n && s_writeOk) {
      size_t w = s_cli.write(b + off, n - off);
      if (w == 0) {                            // could not place any bytes
        if (!s_cli.connected()) { s_writeOk = false; return; }
        delay(2);                              // transient full buffer: brief retry
        w = s_cli.write(b + off, n - off);
        if (w == 0) { s_writeOk = false; return; }
      }
      off += w;
    }
  }
  void sockWrite(const uint8_t* b, size_t n) {
    if (!s_pOK || n == 0) return;
    if (s_transport == Printer::IPP && s_chunkOpen) {
      char hdr[12];
      int hn = snprintf(hdr, sizeof(hdr), "%X\r\n", (unsigned)n);
      wrChecked((const uint8_t*)hdr, hn);
      wrChecked(b, n);
      wrChecked((const uint8_t*)"\r\n", 2);
    } else {
      wrChecked(b, n);
    }
  }
  void pRaw(const uint8_t* b, size_t n) { if (s_pOK) sockWrite(b, n); }
  void pStr(const String& s)            { if (s_pOK && s.length()) sockWrite((const uint8_t*)s.c_str(), s.length()); }
  void pNL()                            { if (s_pOK) { uint8_t c = '\n'; sockWrite(&c, 1); } }

  // Append one IPP attribute: value-tag(1) name-len(2) name value-len(2) value.
  void ippAttr(String& b, uint8_t tag, const char* name, const String& val) {
    b += (char)tag;
    int nl = strlen(name);   b += (char)(nl >> 8); b += (char)(nl & 0xFF); b += name;
    int vl = val.length();   b += (char)(vl >> 8); b += (char)(vl & 0xFF); b += val;
  }

  // Build the IPP Print-Job operation header (everything before the document data).
  String ippHeader(const char* host, const char* resource, const String& docFormat) {
    String uri = String("ipp://") + host + resource;
    String b;
    b += (char)0x01; b += (char)0x01;              // version 1.1
    b += (char)0x00; b += (char)0x02;              // operation-id = Print-Job
    b += (char)0x00; b += (char)0x00; b += (char)0x00; b += (char)0x01;  // request-id
    b += (char)0x01;                               // operation-attributes-tag
    ippAttr(b, 0x47, "attributes-charset", "utf-8");
    ippAttr(b, 0x48, "attributes-natural-language", "en");
    ippAttr(b, 0x45, "printer-uri", uri);
    ippAttr(b, 0x42, "requesting-user-name", "cardsat");
    ippAttr(b, 0x42, "job-name", "CardSat report");
    ippAttr(b, 0x49, "document-format", docFormat);
    b += (char)0x03;                               // end-of-attributes-tag
    return b;
  }

  // Escape a line for PostScript string literals: \ ( ) and non-ASCII.
  String psEscape(const String& s) {
    String o; o.reserve(s.length() + 4);
    for (size_t i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c == '\\' || c == '(' || c == ')') { o += '\\'; o += c; }
      else if ((uint8_t)c < 32 || (uint8_t)c > 126) o += ' ';
      else o += c;
    }
    return o;
  }

  void psBeginPage() {
    s_psPage++;
    pStr("%%Page: "); pStr(String(s_psPage)); pStr(" "); pStr(String(s_psPage)); pNL();
    pStr("/Courier findfont 10 scalefont setfont\n");
    s_psY = PS_TOP;
  }
  void psShowLine(const String& s) {
    if (!s_pOK) return;
    if (s_psY <= PS_BOT) { pStr("showpage\n"); psBeginPage(); }
    pStr(String(PS_LEFT)); pStr(" "); pStr(String(s_psY)); pStr(" moveto (");
    pStr(psEscape(s)); pStr(") show\n");
    s_psY -= PS_LEAD;
  }

  // Escape a line for a ZPL ^FD field: ^ and ~ are ZPL control chars; drop non-ASCII.
  String zplEscape(const String& s) {
    String o; o.reserve(s.length() + 2);
    for (size_t i = 0; i < s.length(); ++i) {
      char c = s[i];
      if (c == '^' || c == '~') o += ' ';                 // neutralize ZPL command chars
      else if ((uint8_t)c < 32 || (uint8_t)c > 126) o += ' ';
      else o += c;
    }
    return o;
  }
  void zplBeginLabel() {
    if (!s_pOK) return;
    pStr("^XA\n");                                        // start label
    pStr("^CI28\n");                                      // UTF-8 input (ASCII-safe)
    s_zplY = ZPL_TOP;
  }
  void zplEndLabel() {
    if (!s_pOK || s_zplY < 0) return;
    pStr("^XZ\n");                                        // end + print label
    s_zplY = -1;
  }
  void zplShowLine(const String& s) {
    if (!s_pOK) return;
    if (s_zplY < 0) zplBeginLabel();
    if (s_zplY > ZPL_MAX) { zplEndLabel(); zplBeginLabel(); }   // overflow -> next label
    pStr("^FO"); pStr(String(ZPL_LEFT)); pStr(","); pStr(String(s_zplY));
    pStr("^A0N,"); pStr(String(ZPL_FH)); pStr(","); pStr(String(ZPL_FW));
    pStr("^FD"); pStr(zplEscape(s)); pStr("^FS\n");
    s_zplY += ZPL_LEAD;
  }

  // Emit one text line to the PRINTER sink, honoring the page language.
  void printerLine(const String& s) {
    if (!s_pOK) return;
    if (s_fmt == Printer::FMT_POSTSCRIPT) {
      wrapTo(s, s_pCols, [](const String& ln){ psShowLine(ln); });
    } else if (s_fmt == Printer::FMT_ZPL) {
      wrapTo(s, s_pCols, [](const String& ln){ zplShowLine(ln); });
    } else {
      // ESC/POS, TEXT, PCL, ESC/P2 and Star are all plain text at the line level.
      wrapTo(s, s_pCols, [](const String& ln){ pStr(ln); pNL(); });
    }
  }

  void serialFileLine(const String& s) {
    int w = s_fCols;
    if (s_ser)  wrapTo(s, w, [](const String& ln){ Serial.println(ln); });
    if (s_file) wrapTo(s, w, [](const String& ln){ s_f.print(ln); s_f.print('\n'); });
  }
}


// ===========================================================================
//  PWG Raster generation (FMT_PWG_RASTER): render report text into a raster
//  page, scanline-by-scanline, and stream it through the current sink (over the
//  IPP chunked transport for AirPrint/driverless printers). Validated
//  byte-identical to ppm2pwg; ~4 KB working RAM, no full-page buffer.
// ===========================================================================
namespace RasterGen {
  // These write through the printer sink via the file-scope pRaw() above.
  static uint32_t s_rw = 0, s_rh = 0, s_rdpi = 300;
  // Media geometry for the PWG page header (set by renderReport per paper choice).
  static uint32_t s_pagePtsW = 612, s_pagePtsH = 792;
  static const char* s_pageName = "na_letter_8.5x11in";
  static bool     s_rvalid = false;
  static uint8_t  s_rreps  = 0;
  static const uint32_t RAS_SCAN_BYTES = 2560;   // max scanline width we support
  // Heap-on-demand (RAM Tier 1): 5,120 B of permanent .bss for buffers only alive
  // while a raster page renders. One block, allocated in Printer::begin() for
  // raster jobs, freed in Printer::end(); s_rprev = s_rscan + RAS_SCAN_BYTES.
  static uint8_t* s_rprev = nullptr;    // one scanline hold
  static uint8_t* s_rscan = nullptr;    // scanline compose buffer

  static void rU32(uint32_t v) { uint8_t b[4]={(uint8_t)(v>>24),(uint8_t)(v>>16),(uint8_t)(v>>8),(uint8_t)v}; pRaw(b,4); }
  static void rI32(int32_t v)  { rU32((uint32_t)v); }
  static void rStr(const char* s, int n) {
    uint8_t buf[64]; memset(buf,0,sizeof(buf));
    if (n>64) n=64; size_t k = s?strlen(s):0; if((int)k>n-1)k=n-1; if(k)memcpy(buf,s,k);
    pRaw(buf,n);
  }
  static void rPad(int n){ uint8_t z[64]={0}; while(n>0){ int c=n<64?n:64; pRaw(z,c); n-=c; } }

  static void beginDoc(){ pRaw((const uint8_t*)"RaS2",4); }
  static void beginPage(uint32_t w, uint32_t h, uint32_t dpi){
    s_rw=w; s_rh=h; s_rdpi=dpi; s_rvalid=false; s_rreps=0;
    rStr("PwgRaster",64); rStr("",64); rStr("",64); rStr("",64);   // sync + 3 strings
    rPad(12);
    rU32(0); rU32(0);                 // CutMedia, Duplex
    rU32(dpi); rU32(dpi);             // HWResolution X/Y
    rPad(16);
    rU32(0); rU32(0); rU32(0);        // InsertSheet, Jog, LeadingEdge
    rPad(12);
    rU32(0); rU32(0);                 // MediaPosition, MediaWeightMetric
    rPad(8);
    rU32(1); rU32(0);                 // NumCopies, Orientation
    rPad(4);
    rU32(s_pagePtsW); rU32(s_pagePtsH);  // PageSize X/Y (points)
    rPad(8);
    rU32(0); rU32(w); rU32(h);        // Tumble, Width, Height
    rPad(4);
    rU32(8); rU32(8); rU32(w);        // BitsPerColor, BitsPerPixel, BytesPerLine
    rU32(0); rU32(18);                // ColorOrder=Chunky, ColorSpace=sGray
    rPad(16);
    rU32(1);                          // NumColors
    rPad(28);
    rU32(0); rI32(1); rI32(1);        // TotalPageCount, CrossFeedTransform, FeedTransform
    rU32(0); rU32(0); rU32(0); rU32(0); // ImageBox L/T/R/B
    rU32(0x00FFFFFF); rU32(0);        // AlternatePrimary, PrintQuality
    rPad(20);
    rU32(0); rU32(0);                 // VendorIdentifier, VendorLength
    rPad(1088); rPad(64);
    rStr("",64); rStr(s_pageName,64);   // RenderingIntent, PageSizeName
  }
  // PWG PackBits (exact ppm2pwg compress_line, oneChunk=1).
  static void compress(const uint8_t* raw, size_t len){
    size_t pos=0;
    while(pos!=len){
      size_t start=pos; uint8_t cur=raw[pos]; pos++;
      if(pos==len || raw[pos]==cur){
        int rep=0;
        while(pos!=len && raw[pos]==cur){ pos++; rep++; if(rep==127)break; }
        uint8_t rb=(uint8_t)rep; pRaw(&rb,1); pRaw(&cur,1);
      } else {
        size_t verb=1;
        for(;;){ cur=raw[pos]; pos++; verb++; if(verb==127)break; if(!(pos!=len && raw[pos]!=cur))break; }
        if(pos!=len) verb--;
        if(verb==1){ pos=start+1; uint8_t z=0; pRaw(&z,1); pRaw(&raw[start],1); }
        else { pos=start+verb; uint8_t vb=(uint8_t)(257-verb); pRaw(&vb,1); pRaw(&raw[start],verb); }
      }
    }
  }
  static void flush(){ if(!s_rvalid)return; pRaw(&s_rreps,1); compress(s_rprev,s_rw); s_rvalid=false; s_rreps=0; }
  static void row(const uint8_t* r){
    if(s_rvalid && s_rreps<255 && memcmp(r,s_rprev,s_rw)==0){ s_rreps++; return; }
    flush(); memcpy(s_rprev,r, s_rw<=RAS_SCAN_BYTES?s_rw:RAS_SCAN_BYTES); s_rvalid=true; s_rreps=0;
  }
  static void endDoc(){ flush(); }

  // ---- URF (Apple Raster, image/urf) container ----
  // Same RLE row data as PWG; only the file magic + a compact 32-byte page header
  // differ. Validated byte-identical to ppm2pwg -f urf.
  static void beginDocUrf(uint32_t pages){
    pRaw((const uint8_t*)"UNIRAST\0", 8);   // 8-byte magic incl. trailing NUL
    rU32(pages);                             // page count (0 = unspecified/stream)
  }
  static void beginPageUrf(uint32_t w, uint32_t h, uint32_t dpi){
    s_rw=w; s_rh=h; s_rdpi=dpi; s_rvalid=false; s_rreps=0;
    uint8_t ph[32]; memset(ph,0,32);
    ph[0]=8;   // BitsPerPixel = 8
    ph[1]=0;   // ColorSpace = sGray
    ph[2]=1;   // Duplex = OneSided
    ph[3]=0;   // Quality = Default
    ph[4]=0;   // MediaType = auto
    ph[5]=0;   // MediaPosition = auto
    ph[12]=(uint8_t)(w>>24); ph[13]=(uint8_t)(w>>16); ph[14]=(uint8_t)(w>>8); ph[15]=(uint8_t)w;
    ph[16]=(uint8_t)(h>>24); ph[17]=(uint8_t)(h>>16); ph[18]=(uint8_t)(h>>8); ph[19]=(uint8_t)h;
    ph[20]=(uint8_t)(dpi>>24); ph[21]=(uint8_t)(dpi>>16); ph[22]=(uint8_t)(dpi>>8); ph[23]=(uint8_t)dpi;
    pRaw(ph, 32);
  }
}

// Render an array of monospace text lines as a full raster page and stream it.
// scale integer-scales the 16x32 font. Called by Printer when FMT_PWG_RASTER.
namespace RasterGen {
  // Render ONE page from lines[0..n-1] (already sliced to fit) into the current
  // open raster document. Does NOT emit the file magic or endDoc -- the caller
  // brackets the whole document so multiple pages share one job.
  static void renderOnePage(const char* const* lines, int n,
                            uint32_t W, uint32_t H, uint32_t dpi,
                            int scale, int marginX, int marginY, int lineGap, bool urf) {
    if (urf) beginPageUrf(W,H,dpi); else beginPage(W,H,dpi);
    const int GW = FONT_W*scale, GH = FONT_H*scale, pitch = GH + lineGap;
    for(uint32_t y=0; y<H; ++y){
      memset(s_rscan, 0xFF, W);                 // white background
      int ty=(int)y - marginY;
      if(ty>=0){
        int rr=ty/pitch, gy=ty%pitch;
        if(rr<n && gy<GH){
          int srcY=gy/scale; const char* s=lines[rr]; int x=marginX;
          for(const char* p=s; *p && x+GW<=(int)W; ++p, x+=GW){
            unsigned char c=(unsigned char)*p;
            if(c<FONT_FIRST||c>FONT_LAST) continue;
            uint16_t bits=FONT16x32[c-FONT_FIRST][srcY];
            for(int gx=0; gx<FONT_W; ++gx){
              if(bits & (1<<(FONT_W-1-gx))){
                int px0=x+gx*scale;
                for(int sx=0; sx<scale; ++sx){ int px=px0+sx; if(px>=0&&px<(int)W) s_rscan[px]=0x00; }
              }
            }
          }
        }
      }
      row(s_rscan);
    }
    flush();   // emit the last page's pending (coalesced) row before the next page
  }

  // Render a whole report as one raster document, paginating across as many pages
  // as the line count needs (PWG/URF both support multiple pages in one job). The
  // report text never overflows a single sheet now -- long reports simply span pages.
  static void renderReport(const char* const* lines, int n,
                           uint32_t W, uint32_t H, uint32_t dpi,
                           int scale, int marginX, int marginY, int lineGap, bool urf) {
    // W/H are the pixel dimensions the caller computed from the paper choice; the
    // point size and PWG media name are set alongside them (see feedCut).
    if (W > RAS_SCAN_BYTES) W = RAS_SCAN_BYTES;
    const int GH = FONT_H*scale, pitch = GH + lineGap;
    int usableH = (int)H - 2*marginY;
    int perPage = usableH / pitch;
    if (perPage < 1) perPage = 1;
    int pages = (n + perPage - 1) / perPage;
    if (pages < 1) pages = 1;                    // always emit at least one page
    if (urf) beginDocUrf((uint32_t)pages); else beginDoc();
    for (int pg = 0; pg < pages; ++pg) {
      int start = pg * perPage;
      int cnt = n - start; if (cnt > perPage) cnt = perPage;
      renderOnePage(lines + start, cnt, W, H, dpi, scale, marginX, marginY, lineGap, urf);
    }
    endDoc();
  }
}


namespace Printer {

// True when the active format is a raster container (PWG or URF): both collect
// report lines and render a page rather than streaming text.
static inline bool isRaster() { return s_fmt == FMT_PWG_RASTER || s_fmt == FMT_URF_RASTER; }

bool begin(const Sinks& s) {
  s_pOK = false; s_pConnected = false; s_ser = s.toSerial; s_file = false; s_fpath = "";
  s_pCols = (s.printerCols >= 24 && s.printerCols <= 72) ? s.printerCols : 32;
  s_fmt   = (s.format >= FMT_ESCPOS && s.format <= FMT_URF_RASTER) ? s.format : FMT_ESCPOS;
  s_paper = (s.paper == 1) ? 1 : 0;
  s_fCols = FILE_COLS;
  s_psY = 0; s_psPage = 0;

  // Printer sink (optional; failure is non-fatal to the others).
  s_transport = (s.transport == Printer::IPP) ? Printer::IPP : Printer::RAW9100;
  // PWG raster is only meaningful over IPP (driverless printers); force it.
  if (s.format == FMT_PWG_RASTER || s.format == FMT_URF_RASTER) s_transport = Printer::IPP;
  s_chunkOpen = false; s_ippAccepted = false; s_rasN = 0; s_writeOk = true;
  // Raster jobs rent their buffers for the life of the job (see the notes at the
  // declarations). Free any stale set first -- an aborted job that never reached
  // end() must not leak into this one.
  if (s_rasLines) { delete[] s_rasLines; s_rasLines = nullptr; }
  if (RasterGen::s_rscan) {
    free(RasterGen::s_rscan);
    RasterGen::s_rscan = nullptr; RasterGen::s_rprev = nullptr;
  }
  if (isRaster()) {
    s_rasLines = new (std::nothrow) String[RAS_MAX_LINES];
    RasterGen::s_rscan = (uint8_t*)malloc(2 * RasterGen::RAS_SCAN_BYTES);
    if (!s_rasLines || !RasterGen::s_rscan) {
      if (s_rasLines) { delete[] s_rasLines; s_rasLines = nullptr; }
      if (RasterGen::s_rscan) { free(RasterGen::s_rscan); RasterGen::s_rscan = nullptr; }
      return false;                     // caller reports begin() failure as usual
    }
    RasterGen::s_rprev = RasterGen::s_rscan + RasterGen::RAS_SCAN_BYTES;
  }
  if (s.host && s.host[0]) {
    s_cli.setTimeout(4000);                       // socket read/write timeout
    // IPP rides HTTP on 631 unless the caller overrode the port; raw uses 9100.
    uint16_t pport = s.port ? s.port
                    : (s_transport == Printer::IPP ? 631 : 9100);
    // Raster over IPP: if the port is still the raw-9100 default, use IPP's 631.
    if ((s.format == FMT_PWG_RASTER || s.format == FMT_URF_RASTER) && pport == 9100) pport = 631;
    if (s_cli.connect(s.host, pport)) {           // 2-arg form (matches net.cpp; the
                                                  // 3-arg timeout overload is unreliable
                                                  // on several ESP32 core versions)
      s_pOK = true;
      s_pConnected = true;   // latch success so status survives end()

      // IPP transport: open a chunked HTTP POST and send the IPP operation header
      // as the first body bytes, BEFORE the page-language preamble streams in.
      if (s_transport == Printer::IPP) {
        // Document format is the page language IPP will carry.
        String docFmt = (s_fmt == FMT_POSTSCRIPT)  ? "application/postscript"
                      : (s_fmt == FMT_PCL)          ? "application/vnd.hp-PCL"
                      : (s_fmt == FMT_PWG_RASTER)   ? "image/pwg-raster"
                      : (s_fmt == FMT_URF_RASTER)   ? "image/urf"
                      :                               "application/octet-stream";
        const char* res = s.ippResource && s.ippResource[0] ? s.ippResource : "/ipp/print";
        String req  = String("POST ") + res + " HTTP/1.1\r\n";
        req += "Host: " + String(s.host) + "\r\n";
        req += "Content-Type: application/ipp\r\n";
        req += "Transfer-Encoding: chunked\r\n";
        req += "Connection: close\r\n\r\n";
        s_cli.print(req);                         // HTTP request line + headers (unchunked)
        s_chunkOpen = true;                       // from here, body writes are chunked
        String hdr = ippHeader(s.host, res, docFmt);
        pRaw((const uint8_t*)hdr.c_str(), hdr.length());   // IPP header = first chunk(s)
      }
      // Per-format document preamble.
      if (s_fmt == FMT_ESCPOS) {
        uint8_t reset[2] = {0x1B, 0x40}; pRaw(reset, 2);          // ESC @
      } else if (s_fmt == FMT_PCL) {
        // PJL-wrapped PCL so office HPs reliably interpret the raw job. The UEL
        // (ESC %-12345X) enters PJL; @PJL ENTER LANGUAGE=PCL selects PCL; ESC E
        // resets; ESC(s0p16.67h8.5v0s0b0T selects a fixed-pitch (Courier) font so
        // columns line up. A matching UEL is sent at end() to close the job.
        pStr("\x1B%-12345X@PJL\r\n");
        pStr("@PJL ENTER LANGUAGE=PCL\r\n");
        uint8_t reset[2] = {0x1B, 'E'};  pRaw(reset, 2);          // ESC E : reset
        pStr("\x1B(s0p16.67h8.5v0s0b0T");                        // fixed-pitch Courier ~16.67 cpi
      } else if (s_fmt == FMT_POSTSCRIPT) {
        pStr("%!PS-Adobe-3.0\n");
        pStr("%%Creator: CardSat\n%%Pages: (atend)\n%%EndComments\n");
        psBeginPage();
      } else if (s_fmt == FMT_ESCP2) {
        uint8_t reset[2] = {0x1B, 0x40}; pRaw(reset, 2);          // ESC @ : ESC/P2 reset
      } else if (s_fmt == FMT_STAR) {
        uint8_t reset[2] = {0x1B, 0x40}; pRaw(reset, 2);          // ESC @ : Star reset
      } else if (s_fmt == FMT_ZPL) {
        s_zplY = -1;                                              // no label open yet
      }
      // FMT_TEXT: no preamble at all.
    }
  }

  // File sink (optional): /CardSat/Reports/<title>-<uptime>.txt. All app data lives
  // under /CardSat (matching RovePlans, workable, search, etc.); mkdir is idempotent.
  if (s.toFile && Store::ready()) {
    if (!Store::fs().exists("/CardSat")) Store::fs().mkdir("/CardSat");
    if (!Store::fs().exists("/CardSat/Reports")) Store::fs().mkdir("/CardSat/Reports");
    String stem = s.fileTitle ? String(s.fileTitle) : String("report");
    stem.replace(' ', '_'); stem.replace('/', '_');
    s_fpath = "/CardSat/Reports/" + stem + "-" + String((unsigned long)millis()) + ".txt";
    s_f = Store::fs().open(s_fpath, "w");
    s_file = (bool)s_f;
    if (!s_file) s_fpath = "";
  }

  if (s_ser) Serial.println(F("---- CardSat report ----"));
  return s_pOK || s_ser || s_file;
}


// ---------------------------------------------------------------------------
//  Printer capability probe: Get-Printer-Attributes over IPP, report which
//  document formats the printer accepts. Lets a user check whether their printer
//  supports the raster formats CardSat generates (image/pwg-raster, image/urf)
//  before relying on it. Returns a short human summary; empty on failure.
// ---------------------------------------------------------------------------
String probeCapabilities(const char* host, uint16_t port) {
  if (!host || !host[0]) return String("");
  uint16_t pport = port ? port : 631;
  WiFiClient cli;
  cli.setTimeout(5000);
  if (!cli.connect(host, pport)) return String("unreachable");

  // Build a Get-Printer-Attributes (operation 0x000B) request.
  String uri = String("ipp://") + host + "/ipp/print";
  String b;
  b += (char)0x01; b += (char)0x01;              // IPP 1.1
  b += (char)0x00; b += (char)0x0B;              // op = Get-Printer-Attributes
  b += (char)0x00; b += (char)0x00; b += (char)0x00; b += (char)0x01;  // request-id
  b += (char)0x01;                               // operation-attributes-tag
  ippAttr(b, 0x47, "attributes-charset", "utf-8");
  ippAttr(b, 0x48, "attributes-natural-language", "en");
  ippAttr(b, 0x45, "printer-uri", uri);
  ippAttr(b, 0x44, "requested-attributes", "document-format-supported");
  b += (char)0x03;                               // end-of-attributes-tag

  String req = String("POST /ipp/print HTTP/1.1\r\n");
  req += "Host: " + String(host) + "\r\n";
  req += "Content-Type: application/ipp\r\n";
  req += "Content-Length: " + String(b.length()) + "\r\n";
  req += "Connection: close\r\n\r\n";
  cli.print(req);
  cli.write((const uint8_t*)b.c_str(), b.length());

  // Read the whole response into a RAW byte buffer. IPP responses are binary and
  // contain many null bytes (2-byte length prefixes), so an Arduino String would be
  // truncated at the first '\0' and miss the format tokens further in. Use a plain
  // buffer and scan it with memory search.
  // One-shot heap buffer: the raster workspace this used to borrow is now itself
  // heap-on-demand (allocated only inside a raster job), so the probe rents its
  // own 2,560 B for the few seconds it runs.
  const size_t bufCap = 2560;
  uint8_t* buf = (uint8_t*)malloc(bufCap);
  if (!buf) { cli.stop(); return String("no RAM for probe"); }
  size_t blen = 0;
  uint32_t t0 = millis();
  while (cli.connected() && (millis() - t0) < 6000 && blen < bufCap) {
    while (cli.available() && blen < bufCap) { buf[blen++] = (uint8_t)cli.read(); t0 = millis(); }
    delay(5);
  }
  cli.stop();

  // Search the raw bytes for each format token (binary-safe substring search).
  auto has = [&](const char* tok) -> bool {
    size_t tl = strlen(tok);
    if (tl == 0 || tl > blen) return false;
    for (size_t i = 0; i + tl <= blen; ++i) {
      if (memcmp(buf + i, tok, tl) == 0) return true;
    }
    return false;
  };
  String out;
  if (has("image/pwg-raster")) out += "PWG ";
  if (has("image/urf"))        out += "URF ";
  if (has("application/pdf"))   out += "PDF ";
  if (has("image/jpeg"))        out += "JPEG ";
  if (has("application/postscript")) out += "PS ";
  if (has("PCL") || has("pcl")) out += "PCL ";
  free(buf);                                     // token scan done; buf unused below
  if (out.length() == 0) {
    // Connected but no recognizable formats found (or empty reply).
    return blen ? String("connected; formats unknown") : String("no reply");
  }
  out.trim();
  return out;
}

bool printerOk() { return s_pConnected; }   // did the printer connect this print? (survives end())
bool ippAccepted() { return s_ippAccepted; }
bool documentSent() { return s_writeOk; }   // did the whole document transmit without a write error?
bool anySink()   { return s_pOK || s_ser || s_file; }
String lastFile(){ return s_fpath; }

int cols() {
  int w = 0;
  if (s_ser || s_file) w = s_fCols;
  if (s_pOK && s_pCols > w) w = s_pCols;
  return w > 0 ? w : s_pCols;
}

// Collect one report line for the raster page, wrapping to s_pCols so long lines
// don't run off the printable width (mirrors what the text formats do via wrapTo).
// Append one already-wrapped line to the raster page buffer. On overflow, stamp a
// visible truncation marker in the last slot instead of silently dropping lines.
static void rasterPush(const String& ln) {
  if (!s_rasLines) return;               // raster job without buffers (OOM guard)
  if (s_rasN < RAS_MAX_LINES - 1) { s_rasLines[s_rasN++] = ln; }
  else if (s_rasN == RAS_MAX_LINES - 1) { s_rasLines[s_rasN++] = String("-- report truncated --"); }
  // once full (s_rasN == RAS_MAX_LINES) further lines are dropped, marker already set
}

void rasterCollect(const String& s) {
  wrapTo(s, s_pCols, [](const String& ln){ rasterPush(ln); });
}

void line(const String& s) {
  if (s_pOK && isRaster()) {
    rasterCollect(s);                                       // wrap + collect for the page
    serialFileLine(s);                                      // serial/file still get text
    return;
  }
  printerLine(s);
  serialFileLine(s);
}

void wrap(const String& s) { line(s); }   // line() already per-sink wraps

void blank() {
  if (s_pOK) {
    if (isRaster()) { rasterPush(String("")); }
    else if (s_fmt == FMT_POSTSCRIPT) psShowLine(String(""));
    else if (s_fmt == FMT_ZPL)   zplShowLine(String(""));
    else pNL();
  }
  if (s_ser)  Serial.println();
  if (s_file) s_f.print('\n');
}

void rule() {
  auto dash = [](int w){ String r; r.reserve(w); for (int i=0;i<w;++i) r += '-'; return r; };
  if (s_pOK && isRaster()) {
    rasterPush(dash(s_pCols));
    if (s_ser)  Serial.println(dash(s_fCols));
    if (s_file) { s_f.print(dash(s_fCols)); s_f.print('\n'); }
    return;
  }
  if (s_pOK)  printerLine(dash(s_pCols));
  if (s_ser)  Serial.println(dash(s_fCols));
  if (s_file) { s_f.print(dash(s_fCols)); s_f.print('\n'); }
}

void title(const String& s) {
  if (s_pOK && isRaster()) {
    rasterCollect(s);
    if (s_ser)  Serial.println(s);
    if (s_file) { s_f.print(s); s_f.print('\n'); }
    rule();
    return;
  }
  if (s_pOK) {
    if (s_fmt == FMT_ESCPOS) {
      uint8_t c1[3] = {0x1B, 0x61, 0x01}; pRaw(c1, 3);   // ESC a 1  center
      uint8_t c2[3] = {0x1B, 0x45, 0x01}; pRaw(c2, 3);   // ESC E 1  emphasis on
      pStr(s); pNL();
      uint8_t c3[3] = {0x1B, 0x45, 0x00}; pRaw(c3, 3);
      uint8_t c4[3] = {0x1B, 0x61, 0x00}; pRaw(c4, 3);
    } else if (s_fmt == FMT_ESCP2) {
      uint8_t on[2]  = {0x1B, 0x45}; pRaw(on, 2);        // ESC E : bold on
      pStr(s); pNL();
      uint8_t off[2] = {0x1B, 0x46}; pRaw(off, 2);       // ESC F : bold off
    } else {
      // TEXT / PCL / PostScript / Star / ZPL: emit the title as an ordinary line.
      printerLine(s);
    }
  }
  if (s_ser)  Serial.println(s);
  if (s_file) { s_f.print(s); s_f.print('\n'); }
  rule();
}

void feedCut() {
  if (s_pOK) {
    if (s_fmt == FMT_ESCPOS) {
      uint8_t fd[3] = {0x1B, 0x64, 0x04}; pRaw(fd, 3);   // ESC d 4  feed 4 lines
      uint8_t cut[3] = {0x1D, 0x56, 0x01}; pRaw(cut, 3); // GS V 1   partial cut
    } else if (s_fmt == FMT_PCL) {
      pNL(); { uint8_t ff=0x0C; pRaw(&ff,1); }          // form feed: eject the page
    } else if (s_fmt == FMT_POSTSCRIPT) {
      pStr("showpage\n");
    } else if (s_fmt == FMT_ESCP2) {
      { uint8_t ff=0x0C; pRaw(&ff,1); }                  // form feed: eject the page
    } else if (s_fmt == FMT_STAR) {
      uint8_t fd[3]  = {0x1B, 0x64, 0x03}; pRaw(fd, 3);  // ESC d 3 : Star feed 3 lines
      uint8_t cut[3] = {0x1B, 0x64, 0x02}; pRaw(cut, 3); // ESC d 2 : Star partial cut
    } else if (s_fmt == FMT_ZPL) {
      zplEndLabel();                                     // ^XZ : print the label
    } else if (isRaster() && s_rasLines && RasterGen::s_rscan) {
      // Render the collected report lines as a raster page and stream it now.
      const char* ptrs[RAS_MAX_LINES];
      for (int i = 0; i < s_rasN; ++i) ptrs[i] = s_rasLines[i].c_str();
      // Letter @ 300 DPI. Auto-size the font so s_pCols columns fit inside the
      // printable width (printers have an unprintable border ~1/4in; use a safe
      // 2400px imageable area of the 2550px sheet). Pick the largest integer scale
      // whose text block fits, so wider column settings shrink the glyphs to fit.
      // Paper geometry at 300 DPI: Letter 2550x3300 / A4 2480x3508.
      uint32_t pxW, pxH, ptW, ptH; const char* pname;
      if (s_paper == 1) { pxW=2480; pxH=3508; ptW=595; ptH=842; pname="iso_a4_210x297mm"; }
      else              { pxW=2550; pxH=3300; ptW=612; ptH=792; pname="na_letter_8.5x11in"; }
      RasterGen::s_pagePtsW = ptW; RasterGen::s_pagePtsH = ptH; RasterGen::s_pageName = pname;
      const int marginX = 60;
      int safeW = (int)pxW - 150;      // ~1/4in unprintable border each side
      int cols = (s_pCols > 0) ? s_pCols : 32;
      int scale = 3;
      while (scale > 1 && (cols * FONT_W * scale + 2 * marginX) > safeW) scale--;
      RasterGen::renderReport(ptrs, s_rasN, pxW, pxH, 300, scale, marginX, 100, 12,
                              /*urf=*/ s_fmt == FMT_URF_RASTER);
    }
    // FMT_TEXT: nothing; some printers need a manual eject, noted in the docs.
  }
  if (s_ser)  Serial.println(F("---- end ----"));
  if (s_file) { s_f.print("\n"); }
}

void end() {
  if (s_pOK) {
    if (s_fmt == FMT_POSTSCRIPT) {
      pStr("%%Trailer\n%%Pages: "); pStr(String(s_psPage)); pStr("\n%%EOF\n");
    } else if (s_fmt == FMT_PCL) {
      uint8_t reset[2] = {0x1B, 'E'}; pRaw(reset, 2);             // ESC E : reset
      pStr("\x1B%-12345X");                                      // UEL : end the PJL job
    } else if (s_fmt == FMT_ZPL) {
      zplEndLabel();                                             // close any open label
    }
    // IPP: terminate the chunked body (0-length chunk) and read the HTTP status so
    // the connection closes cleanly and we can tell whether the job was accepted.
    if (s_transport == Printer::IPP && s_chunkOpen) {
      s_cli.print("0\r\n\r\n");                                 // last-chunk marker
      s_chunkOpen = false;
      s_cli.flush();
      // Read the status line (e.g. "HTTP/1.1 200 OK"); a 2xx means accepted.
      String status = s_cli.readStringUntil('\n');
      s_ippAccepted = (status.indexOf(" 200") >= 0 || status.indexOf(" 202") >= 0);
    }
    s_cli.flush(); s_cli.stop(); s_pOK = false;
  }
  if (s_file) { s_f.close(); s_file = false; }
  // Return the raster job's rented buffers (String objects AND their contents).
  if (s_rasLines) { delete[] s_rasLines; s_rasLines = nullptr; }
  s_rasN = 0;
  if (RasterGen::s_rscan) {
    free(RasterGen::s_rscan);
    RasterGen::s_rscan = nullptr; RasterGen::s_rprev = nullptr;
  }
  s_ser = false;
}

}  // namespace Printer
