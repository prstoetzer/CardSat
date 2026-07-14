#pragma once
#include <Arduino.h>
#include <WiFiClient.h>

// ---------------------------------------------------------------------------
//  Printer -- emit plain-text reports to up to THREE sinks at once:
//    * a network printer over raw TCP port 9100 (JetDirect standard),
//    * the USB serial console (copy/paste when there is no printer),
//    * a text file under /CardSat/Reports (an 80-column .txt to pull off the SD).
//
//  The NETWORK printer sink can speak one of four page languages, chosen by the
//  operator to match their hardware (Sinks.format):
//    FMT_ESCPOS  - ESC/POS control codes for thermal RECEIPT printers (default;
//                  e.g. Epson TM-P20II, GZM8022). Center/bold titles, feed+cut.
//    FMT_TEXT    - pure plain text, no control bytes at all. The universal
//                  fallback: most raw-9100 printers print it as a basic job.
//    FMT_PCL     - HP PCL5 (PJL-wrapped): reset + fixed-pitch font + form-feed.
//                  For office LaserJet / OfficeJet and other PCL printers.
//    FMT_POSTSCRIPT - a minimal PostScript document (Courier monospace) with
//                  automatic page breaks. For PostScript office printers.
//    FMT_ESCP2   - Epson ESC/P2 for Epson page/inkjet/dot-matrix printers:
//                  reset, plain text, form-feed to eject.
//    FMT_STAR    - Star Line Mode for networked Star thermal printers (TSP-series):
//                  reset, plain text, Star feed+cut.
//    FMT_ZPL     - Zebra ZPL for network label printers: each line becomes a
//                  positioned ^FO/^FD field inside an ^XA...^XZ label; a new
//                  label starts when one fills. Also drives many Godex/compatibles.
//  The serial and file sinks are ALWAYS plain text regardless of format.
//
//  Design for a no-PSRAM ESP32-S3: a THIN STREAMING transport. Nothing buffers a
//  whole report; PostScript is emitted incrementally with a running y-cursor.
// ---------------------------------------------------------------------------
namespace Printer {

  static const int FILE_COLS = 80;     // the /CardSat/Reports/*.txt sink is always 80 columns

  enum Format { FMT_ESCPOS = 0, FMT_TEXT = 1, FMT_PCL = 2, FMT_POSTSCRIPT = 3,
                FMT_ESCP2 = 4, FMT_STAR = 5, FMT_ZPL = 6, FMT_PWG_RASTER = 7,
                FMT_URF_RASTER = 8 };

  // Transport for the network printer sink. RAW9100 streams the page language
  // straight to a socket (the default for all the formats above). IPP wraps the
  // same page-language bytes in an HTTP/IPP Print-Job to port 631 -- for office
  // printers that expose IPP but not raw 9100. IPP carries PCL or PostScript as
  // its document payload; it CANNOT rasterize, so raster-only (AirPrint-only)
  // printers that advertise only image/urf or image/pwg-raster are NOT supported.
  enum Transport { RAW9100 = 0, IPP = 1 };

  struct Sinks {
    const char* host = nullptr;        // printer IP ("" / null = no printer sink)
    uint16_t    port = 9100;
    int         printerCols = 32;       // 32 (58mm), 42/48 (80mm), 64 (Font B)
    int         format = FMT_ESCPOS;    // page language for the network printer sink
    bool        toSerial = false;       // echo the report to the USB serial console
    bool        toFile   = false;       // also write /CardSat/Reports/<fileTitle>-<stamp>.txt
    const char* fileTitle = "report";   // filename stem for the file sink
    int         transport = RAW9100;    // RAW9100 (port 9100) or IPP (HTTP POST :631)
    const char* ippResource = "/ipp/print"; // IPP resource path on the printer
  };

  bool begin(const Sinks& s);
  bool printerOk();                    // did the TCP printer sink connect?
  String probeCapabilities(const char* host, uint16_t port);  // IPP Get-Printer-Attributes -> formats
  bool ippAccepted();                  // IPP: did the printer return HTTP 2xx? (RAW9100: false)
  bool anySink();                      // is at least one sink active?
  String lastFile();                   // path of the file sink written ("" if none)

  int  cols();                         // widest active sink's column count (for layout)

  void line(const String& s);          // one line to every active sink
  void wrap(const String& s);          // hard-wrap at each sink's width
  void blank();
  void title(const String& s);         // emphasized/centered where the format allows
  void rule();                         // a row of '-' at each sink's width
  void feedCut();                      // finish the page (cut / form-feed / showpage)

  void end();                          // flush + close all sinks
}
