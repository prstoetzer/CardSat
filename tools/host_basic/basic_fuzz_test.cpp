// Host harness for the Tiny BASIC VM. The VM region is extracted from the LIVE
// src/app.cpp by extract_vm.py (see basic_fuzz_test.sh), so this always exercises the
// code that ships -- not a copy that can drift. No device, no network.
//
// Two classes of check:
//   * SECURITY: adversarial inputs that, before the 0.9.66 hardening, caused an
//     out-of-bounds vars[] write (LET/FOR with a non-alpha "variable"), an OOB read +
//     NUL-walk (a bare function keyword like SIN with no arg), or an unbounded
//     expression recursion that stack-overflowed the device. Each must now be a clean
//     error string, never a crash.
//   * REGRESSION: ordinary programs must still run and produce exact output.
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <string>
struct String : std::string {
  String() {}
  String(const char* s) : std::string(s ? s : "") {}
  String(const std::string& s) : std::string(s) {}
  String(int v) : std::string(std::to_string(v)) {}
  bool isEmpty() const { return empty(); }
  const char* c_str() const { return std::string::c_str(); }
};
static String operator+(const String& a, const String& b){ String r(a); r+=b; return r; }
static String operator+(const char* a, const String& b){ String r(a); r+=b; return r; }
static String operator+(const String& a, const char* b){ String r(a); r+=b; return r; }
static void yield() {}
#include "vm_region.inc"

static int failures = 0;

// Run a program; return err (empty if ok) and capture output.
static void run(const char* prog, String& err, String& out) {
  BasicVM* vm = new BasicVM();
  String work, perr;
  if (!basicParse(*vm, String(prog), work, perr)) { err = perr; out = ""; delete vm; return; }
  vm->run();
  err = vm->err; out = vm->out;
  delete vm;
}

// A security case: must produce a NON-EMPTY error and must not crash (ASan enforces
// the no-crash half by aborting the process).
static void mustError(const char* name, const std::string& prog) {
  String err, out; run(prog.c_str(), err, out);
  if (err.isEmpty()) { printf("  FAIL %-16s: expected an error, got out=[%s]\n", name, out.c_str()); failures++; }
  else               printf("  ok   %-16s: %s\n", name, err.c_str());
}

// A regression case: must succeed with exactly this output.
static void mustEqual(const char* name, const char* prog, const char* want) {
  String err, out; run(prog, err, out);
  if (!err.isEmpty())      { printf("  FAIL %-16s: unexpected error %s\n", name, err.c_str()); failures++; }
  else if (out != want)    { printf("  FAIL %-16s: out=[%s] want=[%s]\n", name, out.c_str(), want); failures++; }
  else                     printf("  ok   %-16s\n", name);
}

int main() {
  printf("Tiny BASIC VM -- security + regression (live src/app.cpp)\n");

  // --- security: OOB vars[] write via a non-alpha "variable" ---
  mustError("LET non-alpha", "10 LET 5=1\n");
  mustError("bare non-alpha", "10 9=1\n");
  mustError("FOR non-alpha",  "10 FOR 5=1 TO 3\n20 NEXT\n");
  // --- security: OOB read + NUL-walk via a bare function keyword ---
  mustError("bare SIN",  "10 PRINT SIN\n");
  mustError("ABS no ()",  "10 PRINT ABS 5\n");
  mustError("trailing fn", "10 A=SItesting\n");   // 2-letter unknown after S
  // --- security: unbounded expression recursion ---
  { std::string p="10 A="; for(int i=0;i<20000;i++)p+='('; p+="1"; for(int i=0;i<20000;i++)p+=')'; p+="\n";
    mustError("deep parens", p); }
  { std::string p="10 A="; for(int i=0;i<20000;i++)p+='-'; p+="1\n";
    mustError("deep unary", p); }
  { std::string p="10 IF "; for(int i=0;i<20000;i++)p+="NOT "; p+="1 THEN END\n";
    mustError("deep NOT", p); }

  // --- regression: ordinary programs unchanged ---
  mustEqual("FOR loop",   "10 FOR I=1 TO 3\n20 PRINT I\n30 NEXT\n", "1\n2\n3\n");
  mustEqual("nested expr","10 PRINT ((1+2)*(3+4))^2\n", "441\n");
  mustEqual("functions",  "10 PRINT ABS(-5)+INT(2.9)+MAX(1,2)\n", "9\n");
  mustEqual("GOSUB/IF",   "10 GOSUB 100\n20 IF A>2 THEN PRINT 99\n30 END\n100 A=3\n110 RETURN\n", "99\n");
  mustEqual("DATA/READ",  "10 DATA 5,7\n20 READ X,Y\n30 PRINT X+Y\n", "12\n");
  mustEqual("legal nest", "10 PRINT (((((((1)))))))\n", "1\n");
  mustEqual("MOD/AND",    "10 IF 7 MOD 3 = 1 AND 2 > 1 THEN PRINT 1\n", "1\n");

  // --- regression: ':' statement separator (tutorial + manual both document it) ---
  mustEqual("colon join",   "10 PRINT 1: PRINT 2: PRINT 3\n", "1\n2\n3\n");
  mustEqual("colon assign", "10 A=5: B=A*2: PRINT B\n", "10\n");
  mustEqual("colon FOR",    "10 FOR I=1 TO 3: PRINT I: NEXT\n", "1\n2\n3\n");
  mustEqual("colon FOR body","10 FOR I=1 TO 2: PRINT I: PRINT I*10: NEXT\n", "1\n10\n2\n20\n");
  mustEqual("colon nest FOR","10 FOR I=1 TO 2: FOR J=1 TO 2: PRINT I*J: NEXT: NEXT\n", "1\n2\n2\n4\n");
  mustEqual("colon GOTO",   "10 GOTO 30: PRINT 99\n20 END\n30 PRINT 7\n", "7\n");
  mustEqual("colon REM",    "10 PRINT 5: REM ignored\n", "5\n");

  printf("\nbasic fuzz: %s\n", failures ? "FAIL" : "PASS");
  return failures ? 1 : 0;
}
