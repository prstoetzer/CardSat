// Semantics test for CardSat BASIC's text functions.
//
// These reimplement the exact index arithmetic used in BasicVM::strTerm(), because an
// off-by-one in MID$ or INSTR is SILENT: the program runs, prints something plausible,
// and is wrong. Anyone porting a Microsoft BASIC program would hit it and blame their
// own code. The values below are what MS BASIC produces.
#include <cstdio>
#include <string>
#include <algorithm>
using std::string;

static string LEFTs (const string& s, int k){ if(k<0)k=0; return k>=(int)s.size()?s:s.substr(0,k); }
static string RIGHTs(const string& s, int k){ if(k<0)k=0; return k>=(int)s.size()?s:s.substr(s.size()-k); }
static string MIDs  (const string& s, int st, int ln){        // st is 1-BASED
  int i=st-1; if(i<0)i=0; if(i>=(int)s.size()) return "";
  if(ln<0) return s.substr(i);
  int k=ln; if(k<0)k=0; return s.substr(i, std::min((int)s.size()-i, k));
}
static int INSTRb(const string& h, const string& n){          // 1-based, 0 = absent
  auto i=h.find(n); return i==string::npos?0:(int)i+1;
}

static int fails=0;
static void eq(const string& got, const string& want, const char* what){
  bool ok = got==want; printf("%s %-28s got \"%s\"%s\n", ok?"ok  ":"FAIL", what, got.c_str(),
                              ok?"":(" want \""+want+"\"").c_str());
  if(!ok) fails++;
}
static void eqi(int got, int want, const char* what){
  bool ok = got==want; printf("%s %-28s got %d%s\n", ok?"ok  ":"FAIL", what, got,
                              ok?"":(" want "+std::to_string(want)).c_str());
  if(!ok) fails++;
}

int main(){
  const string s = "CARDSAT";
  eq (LEFTs(s,4),      "CARD",    "LEFT$(\"CARDSAT\",4)");
  eq (LEFTs(s,99),     "CARDSAT", "LEFT$ beyond end clamps");
  eq (LEFTs(s,0),      "",        "LEFT$ zero");
  eq (LEFTs(s,-3),     "",        "LEFT$ negative clamps to 0");
  eq (RIGHTs(s,3),     "SAT",     "RIGHT$(\"CARDSAT\",3)");
  eq (RIGHTs(s,99),    "CARDSAT", "RIGHT$ beyond end clamps");
  eq (MIDs(s,5,3),     "SAT",     "MID$ 1-based start");
  eq (MIDs(s,1,4),     "CARD",    "MID$ from position 1");
  eq (MIDs(s,5,-1),    "SAT",     "MID$ without length");
  eq (MIDs(s,99,2),    "",        "MID$ past end");
  eq (MIDs(s,5,99),    "SAT",     "MID$ length beyond end clamps");
  eq (MIDs(s,0,3),     "CAR",     "MID$ start 0 treated as 1");
  eqi(INSTRb(s,"SAT"), 5,         "INSTR 1-based hit");
  eqi(INSTRb(s,"XYZ"), 0,         "INSTR miss is 0");
  eqi(INSTRb(s,"C"),   1,         "INSTR at first char is 1");
  eqi((int)s.size(),   7,         "LEN");
  printf(fails ? "\n%d FAILURE(S)\n" : "\nall text-function semantics match MS BASIC\n", fails);
  return fails?1:0;
}
