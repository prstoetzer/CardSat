// Host harness for CardSat's IGRF-14 field model and the field-line shell
// geometry behind the Van Allen belt zones.
//
// Reference values come from ppigrf (the IGRF14.shc distribution), an
// independent implementation -- so this checks OUR spherical-harmonic recursion
// and Schmidt normalization, not just the coefficient table. Shell cases encode
// the physical result the zone model depends on: a high-latitude LEO sits on a
// belt shell but at a huge B/B0, which is what separates it from the belt.
#include <cstdio>
#include <cmath>
#include <cstdint>
#include <cstring>

#include "/tmp/igrf_extract.inc"

static int fails = 0;

static void checkField(float r, float colat, float lon, float yrs,
                       float wBr, float wBt, float wBp, const char* what) {
  float Br, Bt, Bp;
  igrfField(r, colat, lon, yrs, Br, Bt, Bp);
  float dn = sqrtf((Br-wBr)*(Br-wBr) + (Bt-wBt)*(Bt-wBt) + (Bp-wBp)*(Bp-wBp));
  float rn = sqrtf(wBr*wBr + wBt*wBt + wBp*wBp);
  float rel = dn / rn;
  if (rel < 2e-4f) { printf("ok   %-28s rel=%.2e\n", what, rel); return; }
  printf("FAIL %-28s rel=%.2e\n     got  %.2f %.2f %.2f\n     want %.2f %.2f %.2f\n",
         what, rel, Br, Bt, Bp, wBr, wBt, wBp);
  fails++;
}

// Mirror of App::shellAt()'s walk, using the extracted evaluator.
static void igrfVecL(const float p[3], float yrs, float b[3]) {
  float r = sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
  float colat = acosf(p[2]/r)*57.2957795f, lon = atan2f(p[1],p[0])*57.2957795f;
  float Br,Bt,Bp; igrfField(r,colat,lon,yrs,Br,Bt,Bp);
  float th=colat/57.2957795f, ph=lon/57.2957795f;
  float st=sinf(th),ct=cosf(th),sp=sinf(ph),cp=cosf(ph);
  b[0]=Br*st*cp+Bt*ct*cp-Bp*sp; b[1]=Br*st*sp+Bt*ct*sp+Bp*cp; b[2]=Br*ct-Bt*st;
}
static void shell(float lat, float lonE, float alt, float yrs,
                  float* L, float* ratio, int* steps) {
  const float RE = 6371.0f;
  float r = RE+alt, colat=(90.0f-lat)/57.2957795f, ph=lonE/57.2957795f;
  float p[3]={r*sinf(colat)*cosf(ph), r*sinf(colat)*sinf(ph), r*cosf(colat)};
  float b[3]; igrfVecL(p,yrs,b);
  float bm=sqrtf(b[0]*b[0]+b[1]*b[1]+b[2]*b[2]);
  float bSat=bm, b0=bm, sh=r/RE;
  int sign=+1;
  { float q[3],t[3]; for(int i=0;i<3;i++) q[i]=p[i]+50.0f*b[i]/bm;
    igrfVecL(q,yrs,t); if (sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2])>bm) sign=-1; }
  float prev=bm; int rising=0, n=0;
  for (; n<3000; ++n) {
    float rr=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if (rr>12.0f*RE || rr<RE+80.0f) break;
    float h=0.02f*rr; if(h<15.0f)h=15.0f; if(h>500.0f)h=500.0f;
    float k1[3],k2[3],k3[3],k4[3],q[3],t[3],m;
    igrfVecL(p,yrs,t); m=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
    for(int i=0;i<3;i++) k1[i]=sign*t[i]/m;
    for(int i=0;i<3;i++) q[i]=p[i]+0.5f*h*k1[i];
    igrfVecL(q,yrs,t); m=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); for(int i=0;i<3;i++) k2[i]=sign*t[i]/m;
    for(int i=0;i<3;i++) q[i]=p[i]+0.5f*h*k2[i];
    igrfVecL(q,yrs,t); m=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); for(int i=0;i<3;i++) k3[i]=sign*t[i]/m;
    for(int i=0;i<3;i++) q[i]=p[i]+h*k3[i];
    igrfVecL(q,yrs,t); m=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); for(int i=0;i<3;i++) k4[i]=sign*t[i]/m;
    for(int i=0;i<3;i++) p[i]+=(h/6.0f)*(k1[i]+2*k2[i]+2*k3[i]+k4[i]);
    igrfVecL(p,yrs,t); float bn=sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
    float rn=sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if (bn<b0){b0=bn; sh=rn/RE;}
    if (bn>prev){ if(++rising>=2) break; } else rising=0;
    prev=bn;
  }
  *L=sh; *ratio=bSat/b0; *steps=n;
}
static void checkShell(const char* what, float lat, float lon, float alt,
                       float wL, float wRatio, bool wantInBelt) {
  float L, ratio; int steps;
  shell(lat, lon, alt, 1.574f, &L, &ratio, &steps);
  bool inBelt = (ratio <= 3.0f) &&
                ((L >= 1.2f && L <= 2.5f) || (L >= 3.0f && L <= 7.0f)) && alt >= 300.0f;
  bool ok = fabsf(L - wL) < 0.05f * wL &&
            fabsf(ratio - wRatio) < 0.10f * wRatio + 0.02f &&
            inBelt == wantInBelt;
  printf("%s %-26s L=%5.2f B/B0=%8.2f %s (%d steps)\n",
         ok ? "ok  " : "FAIL", what, L, ratio,
         inBelt ? "IN BELT" : "not in belt", steps);
  if (!ok) { printf("     want L=%.2f B/B0=%.2f %s\n", wL, wRatio,
                    wantInBelt ? "IN BELT" : "not in belt"); fails++; }
}

int main() {
  // --- IGRF field vs ppigrf (IGRF14.shc), 2026-07-28 = 1.574 yr past epoch ---
  const float Y = 1.574f;
  checkField(6771.0f,  45.0f, -75.0f, Y, -40763.92f, -15107.90f, -3163.39f, "400 km, colat 45, 75W");
  checkField(6371.2f,  90.0f,   0.0f, Y,  16073.56f, -27516.31f, -1835.84f, "surface, equator, 0E");
  checkField(7571.0f, 117.0f, -53.0f, Y,   8663.29f, -11323.57f, -2594.74f, "1200 km, SAA region");
  checkField(26371.0f, 90.0f,  20.0f, Y,     30.17f,   -404.06f,   -54.87f, "20000 km, equator");
  checkField(42164.0f, 90.0f, -75.0f, Y,    -31.96f,    -98.07f,    -0.89f, "GEO");
  checkField(6871.0f,  25.0f,  20.0f, Y, -41700.17f, -10236.95f,  1429.35f, "500 km, colat 25");

  // --- shell geometry: the cases the belt model must get right ---------------
  printf("\n");
  checkShell("polar LEO 1200 @65N", 65.0f,  20.0f,  1200.0f,  5.49f, 183.8f, false);
  checkShell("polar LEO 1200 @50N", 50.0f,  20.0f,  1200.0f,  2.47f,  15.6f, false);
  checkShell("MEO 20000 equator",    0.0f,  20.0f, 20000.0f,  4.14f,   1.00f, true);
  checkShell("inner belt 3000 eq",   0.0f,  20.0f,  3000.0f,  1.49f,   1.03f, true);
  checkShell("ISS in the SAA",     -27.0f, -53.0f,   420.0f,  1.23f,   1.44f, true);
  checkShell("ISS equatorial",       0.0f,-150.0f,   420.0f,  1.07f,   1.00f, false);

  printf(fails ? "\n%d FAILURE(S)\n" : "\nall geomagnetic vectors pass\n", fails);
  return fails ? 1 : 0;
}
