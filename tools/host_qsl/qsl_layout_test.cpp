// ===========================================================================
//  qsl_layout_test.cpp -- does the QSL card fit the paper?
// ===========================================================================
//
//  printQsl() lays out through Printer::center() / kv() / rule() / title(), all
//  of which are width-dependent. There is no way to see the result on a 240x135
//  screen and `audit_screen_geometry` only covers the DISPLAY, so an over-wide
//  card would first be discovered on paper -- after a hamfest, on a receipt with
//  a callsign shorn off the right edge.
//
//  This reimplements the three primitives with print.cpp's exact semantics and
//  replays the same call sequence printQsl() makes, at every supported sink
//  width. It asserts that nothing overflows, and prints the rendered card so the
//  layout can be read rather than merely measured.
//
//  It is a LAYOUT check, not a unit test of printQsl(): App is not linkable on a
//  host. The call sequence below must be kept in step with printQsl() by hand --
//  which is cheap, because that function is a flat list of emissions with one
//  conditional per field.
//
//  Build + run: tools/host_qsl/qsl_layout_test.sh
// ===========================================================================
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static int g_fails = 0, g_checks = 0;

// ---- the primitives, mirroring src/print.cpp ------------------------------
struct Sink {
    int width;
    std::vector<std::string> out;

    // print.cpp centreTo(): pad left only, pass through when it does not fit.
    static std::string centreTo(const std::string& s, int w) {
        if (w <= 0 || (int)s.size() >= w) return s;
        return std::string((w - (int)s.size()) / 2, ' ') + s;
    }
    void line(const std::string& s)   { out.push_back(s); }
    void center(const std::string& s) { out.push_back(centreTo(s, width)); }
    void blank()                      { out.push_back(""); }
    void rule()                       { out.push_back(std::string(width, '-')); }
    void title(const std::string& s)  { out.push_back(s); rule(); }   // + emphasis on paper
    // print.cpp kv(): one line if "label value" fits, else label then indented value.
    void kv(const std::string& label, const std::string& value) {
        if ((int)(label.size() + 1 + value.size()) <= width) out.push_back(label + " " + value);
        else { out.push_back(label); out.push_back("  " + value); }
    }
    bool narrow() const { return width > 0 && width <= 32; }
};

// ---- the QSO under test ---------------------------------------------------
struct Qso {
    const char* myCall; const char* myGrid; const char* opName;
    const char* dxcc; const char* cqz; const char* ituz; const char* state;
    const char* email;
    const char* call; const char* grid;
    const char* date; const char* utc;
    const char* sat;  const char* mode; const char* rstS;
    const char* up;   const char* down; const char* path;
    bool lotw;
};

// Mirrors printQsl()'s emission order exactly.
static void renderCard(Sink& p, const Qso& q) {
    const bool wide = !p.narrow();
    p.title("RADIO QSL CONFIRMATION");
    p.blank();
    p.center(q.myCall);
    p.center(q.myGrid);
    if (*q.opName) p.center(q.opName);
    {
        std::string ids;
        if (*q.dxcc)  ids += std::string("DXCC ") + q.dxcc;
        if (*q.cqz)   ids += (ids.size() ? "  CQ " : "CQ ")   + std::string(q.cqz);
        if (*q.ituz)  ids += (ids.size() ? "  ITU " : "ITU ") + std::string(q.ituz);
        if (*q.state) ids += (ids.size() ? "  " : "")         + std::string(q.state);
        if (ids.size()) p.center(ids);
    }
    if (*q.email) p.center(q.email);
    p.rule();
    p.center("CONFIRMING TWO-WAY CONTACT");
    p.center("WITH");
    p.center(q.call);
    p.rule();
    p.kv("DATE ", q.date);
    p.kv("UTC  ", q.utc);
    if (*q.sat) { p.kv("SAT  ", q.sat); p.kv("PROP ", "SAT"); }
    if (*q.mode) p.kv("MODE ", q.mode);
    if (*q.up)   p.kv("UP   ", q.up);
    if (*q.down) p.kv("DOWN ", q.down);
    if (*q.rstS) p.kv("RST SENT", q.rstS);
    if (*q.grid) p.kv("UR GRID ", q.grid);
    if (*q.path) p.kv("PATH ", q.path);
    p.rule();
    if (*q.sat) p.center("worked via amateur satellite");
    p.center("PSE QSL  /  TNX FOR THE QSO");
    if (q.lotw) p.center("Uploaded to LoTW");
    if (wide) { p.blank(); p.line("Signed __________________  Date ________"); }
    p.blank();
    p.center(std::string("73 de ") + q.myCall);
}

static void check(Sink& p, const char* what, bool show) {
    if (show) {
        std::printf("\n--- %s (%d columns) ---\n", what, p.width);
        for (const auto& l : p.out) std::printf("|%s|\n", l.c_str());
    }
    for (const auto& l : p.out) {
        ++g_checks;
        if ((int)l.size() > p.width) {
            std::printf("  OVERFLOW at %d cols (%d chars): \"%s\"\n",
                        p.width, (int)l.size(), l.c_str());
            ++g_fails;
        }
    }
}

int main() {
    // A realistic satellite QSO, with every optional field populated -- the worst
    // case for width, since anything absent only makes the card narrower.
    const Qso full = {
        "N8HM", "FM18lu", "Paul Stoetzer",
        "291", "5", "8", "VA",
        "n8hm@example.org",
        "W1ABC", "FN31pr",
        "2026-08-04", "1432Z",
        "AO-7", "SSB", "59",
        "145.9250 MHz  2m", "432.1750 MHz  70cm", "565 km  brg 041",
        true
    };
    // The other end of the range: a minimal QSO with almost nothing set. Checks
    // that no line collapses to something nonsensical rather than merely short.
    const Qso bare = {
        "N8HM", "FM18lu", "",
        "", "", "", "",
        "",
        "W1ABC", "",
        "2026-08-04", "1432Z",
        "", "", "",
        "", "", "",
        false
    };
    // A long-callsign case: contest/special-event calls are the realistic way a
    // centred line gets wide.
    const Qso longcall = {
        "VK9/W1ABC/P", "FM18lu", "Paul Stoetzer",
        "291", "5", "8", "VA", "",
        "SV2/DJ5AA/QRP", "KM17ux",
        "2026-08-04", "1432Z",
        "GREENCUBE", "DATA", "599",
        "435.3100 MHz  70cm", "435.3100 MHz  70cm", "8241 km  brg 093",
        false
    };

    // Every width a sink can present: 32 = 58 mm receipt, 42/48 = 80 mm receipt
    // (Font A / Font B), 64 = ESC/POS Font B narrow, 80 = the file sink.
    const int widths[] = { 32, 42, 48, 64, 80 };

    std::printf("QSL card layout check\n");
    for (int w : widths) {
        Sink p{w, {}};
        renderCard(p, full);
        check(p, "full QSO", w == 32 || w == 42);
    }
    for (int w : widths) { Sink p{w, {}}; renderCard(p, bare);     check(p, "bare QSO", false); }
    for (int w : widths) { Sink p{w, {}}; renderCard(p, longcall); check(p, "long calls", w == 32); }

    std::printf("\n%s (%d lines checked, %d overflows)\n",
                g_fails ? "FAIL" : "PASS", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
