#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstring>
static time_t T(int y,int mo,int d,int h,int mi){struct tm t{};t.tm_year=y-1900;t.tm_mon=mo-1;t.tm_mday=d;t.tm_hour=h;t.tm_min=mi;return timegm(&t);}
struct Obs{time_t t;int m;int neg;};
static Obs obs[400]; static int NOBS=0;
static void add(time_t t,int m,int neg){obs[NOBS++]={t,m,neg};}
const double W_POS=1.0,W_NEG=0.35;
static double score(double P,double t0,int flip){
  double sc=0;
  for(int i=0;i<NOBS;i++){
    double k=floor(((double)obs[i].t-t0)/P);
    int pm=(((long)k)&1)^flip;
    if(obs[i].neg){ if(pm!=obs[i].m) sc+=W_NEG; } else { if(pm==obs[i].m) sc+=W_POS; }
  }
  return sc;
}
int main(int argc,char**argv){
  bool useNeg = !(argc>1 && !strcmp(argv[1],"--noneg"));
  // Heard reports (mode 0=A/[V/a], 1=B/[U/v]) from the live 2026-07-24 AMSAT fetch,
  // times refined to the 15-min period centre where the period field was known.
  add(T(2026,7,21,13,7),1,0); add(T(2026,7,21,13,7),1,0); add(T(2026,7,21,13,7),1,0);
  add(T(2026,7,21,19,30),0,0);
  add(T(2026,7,21,21,30),0,0);
  add(T(2026,7,22,0,30),0,0); add(T(2026,7,22,0,30),0,0);
  add(T(2026,7,22,9,30),0,0);
  add(T(2026,7,22,12,30),0,0);
  add(T(2026,7,22,15,37),1,0); add(T(2026,7,22,15,37),1,0);
  add(T(2026,7,22,17,52),1,0);
  add(T(2026,7,22,19,37),1,0); add(T(2026,7,22,19,22),1,0); add(T(2026,7,22,19,22),1,0);
  add(T(2026,7,22,21,22),1,0);
  add(T(2026,7,22,23,7),1,0); add(T(2026,7,22,23,7),1,0);
  add(T(2026,7,23,0,52),1,0);
  add(T(2026,7,23,1,7),1,0);
  add(T(2026,7,23,13,30),0,0);
  add(T(2026,7,23,18,30),0,0);
  add(T(2026,7,23,20,30),0,0);
  add(T(2026,7,23,22,30),0,0);
  add(T(2026,7,24,0,30),0,0); add(T(2026,7,24,0,30),0,0);
  add(T(2026,7,24,2,30),0,0);
  add(T(2026,7,24,5,22),1,0);
  add(T(2026,7,24,6,22),1,0);
  add(T(2026,7,24,10,7),1,0);
  add(T(2026,7,24,11,52),1,0);
  int nPos=NOBS;
  if(useNeg){ // horizon-gated "Not Heard" for mode B during the long 01:30->13:30 gap
    add(T(2026,7,23,5,7),1,1); add(T(2026,7,23,10,37),1,1);
    add(T(2026,7,23,11,22),1,1); add(T(2026,7,23,11,7),1,1);
  }
  const double PMIN=12*3600.0,PMAX=30*3600.0,PSC=300.0,TSC=1800.0,PSF=30.0,TSF=60.0;
  double tRef=(double)obs[0].t,bP=PMIN,bT=tRef,bS=-1;int bF=0;
  for(double P=PMIN;P<=PMAX;P+=PSC)for(double off=0;off<P;off+=TSC)for(int f=0;f<2;f++){
    double s=score(P,tRef+off,f); if(s>bS){bS=s;bP=P;bT=tRef+off;bF=f;} }
  double p0=bP,s0=bT;
  for(double P=p0-PSC;P<=p0+PSC;P+=PSF){ if(P<PMIN||P>PMAX)continue;
    for(double t0=s0-TSC;t0<=s0+TSC;t0+=TSF){ double s=score(P,t0,bF); if(s>bS){bS=s;bP=P;bT=t0;} } }
  double wTot=0; for(int i=0;i<NOBS;i++) wTot+=obs[i].neg?W_NEG:W_POS;
  double lo=0,hi=0;
  for(double d=TSF;d<=bP/2;d+=TSF){ if(score(bP,bT-d,bF)<bS-W_POS)break; lo=d; }
  for(double d=TSF;d<=bP/2;d+=TSF){ if(score(bP,bT+d,bF)<bS-W_POS)break; hi=d; }
  printf("%s: N=%d (pos=%d neg=%d)\n", useNeg?"WITH negatives":"positives only", NOBS,nPos,NOBS-nPos);
  printf("  P=%.2fh  agreement=%.1f%%  phase-unc=+/-%.0f min\n", bP/3600,100*bS/wTot,0.5*(lo+hi)/60);
  time_t now=T(2026,7,24,13,0);
  double kN=floor(((double)now-bT)/bP);
  time_t nx=(time_t)(bT+(kN+1)*bP); struct tm tv; gmtime_r(&nx,&tv);
  printf("  mode now=%s  next switch=%02d-%02d %02d:%02dZ\n",
    ((((long)kN)&1)^bF)==0?"A":"B", tv.tm_mon+1,tv.tm_mday,tv.tm_hour,tv.tm_min);
  // Does the fit put a switch inside a confirmed single-mode run? (the old fit's blind spot)
  int viol=0;
  for(int i=1;i<nPos;i++){
    if(obs[i].m!=obs[i-1].m||obs[i].neg||obs[i-1].neg) continue;
    double k1=floor(((double)obs[i-1].t-bT)/bP), k2=floor(((double)obs[i].t-bT)/bP);
    if(k1!=k2) viol++;
  }
  printf("  predicted switches inside confirmed same-mode runs: %d %s\n",viol,viol==0?"(none - good)":"");
  int fail=0;
  // The mode-agreement objective must explain essentially all the positive evidence --
  // the old boundary-midpoint fit could only ever speak for the 4 derived midpoints.
  if(100*bS/wTot < 95.0){ printf("FAIL: agreement %.1f%% < 95%%\n",100*bS/wTot); fail=1; }
  // It must never place a switch inside a stretch where the mode was repeatedly
  // confirmed unchanged. This is the failure the old objective was structurally blind to.
  if(viol!=0){ printf("FAIL: %d switches inside confirmed runs\n",viol); fail=1; }
  // Period must stay in the domain-plausible band (never the old aliased ~24h assumption).
  if(!(bP/3600.0>15.0&&bP/3600.0<24.0)){ printf("FAIL: period %.2fh outside (15,24)h\n",bP/3600.0); fail=1; }
  // Mode B was observed on the live status page at this instant.
  if((((((long)kN)&1))^bF)!=1){ printf("FAIL: expected mode B\n"); fail=1; }
  if(!fail) printf("  PASS\n");
  return fail;
}
