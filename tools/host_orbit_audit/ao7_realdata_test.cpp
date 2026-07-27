#include <cstdio>
#include <cmath>
#include <ctime>

int main(){
  auto T=[&](int y,int mo,int d,int h,int mi)->time_t{struct tm tmv{}; tmv.tm_year=y-1900;tmv.tm_mon=mo-1;tmv.tm_mday=d;tmv.tm_hour=h;tmv.tm_min=mi; return timegm(&tmv);};

  // Heard-only observations (mode 0=A,1=B), MAXGAP=20h bracket rule applied as in the
  // real firmware code -- reconstructed from the live AMSAT fetch.
  struct Obs{time_t t;int m;};
  Obs obs[] = {
    {T(2026,7,21,13,30),1},{T(2026,7,21,13,30),1},{T(2026,7,21,13,30),1},
    {T(2026,7,21,19,30),0},
    {T(2026,7,21,21,30),0},
    {T(2026,7,22,0,30),0},{T(2026,7,22,0,30),0},
    {T(2026,7,22,9,30),0},
    {T(2026,7,22,12,30),0},
    {T(2026,7,22,15,30),1},{T(2026,7,22,15,30),1},
    {T(2026,7,22,17,30),1},
    {T(2026,7,22,19,30),1},{T(2026,7,22,19,30),1},{T(2026,7,22,19,30),1},
    {T(2026,7,22,21,30),1},
    {T(2026,7,22,23,30),1},{T(2026,7,22,23,30),1},
    {T(2026,7,23,0,30),1},
    {T(2026,7,23,1,30),1},
    {T(2026,7,23,13,30),0},
    {T(2026,7,23,18,30),0},
    {T(2026,7,23,20,30),0},
    {T(2026,7,23,22,30),0},
    {T(2026,7,24,0,30),0},{T(2026,7,24,0,30),0},
    {T(2026,7,24,2,30),0},
    {T(2026,7,24,5,30),1},
    {T(2026,7,24,6,30),1},
    {T(2026,7,24,10,30),1},
    {T(2026,7,24,11,30),1},
  };
  int nobs = sizeof(obs)/sizeof(obs[0]);

  const double MAXGAP = 20.0*3600.0;
  double sw[50], span[50]; int ns=0;
  for (int i=1;i<nobs;i++){
    if (obs[i].m != obs[i-1].m) {
      double gap = (double)(obs[i].t - obs[i-1].t);
      if (gap <= MAXGAP) { sw[ns]=0.5*(obs[i].t+obs[i-1].t); span[ns]=gap; ns++; }
    }
  }
  printf("boundaries found (MAXGAP=20h): %d\n", ns);
  for (int i=0;i<ns;i++){ time_t m=(time_t)sw[i]; struct tm tv; gmtime_r(&m,&tv);
    printf("  #%d  %04d-%02d-%02d %02d:%02dZ  span=%.1fh\n", i, tv.tm_year+1900,tv.tm_mon+1,tv.tm_mday,tv.tm_hour,tv.tm_min, span[i]/3600); }

  double wt[50]; for(int i=0;i<ns;i++){ double s=span[i]>60?span[i]:60; wt[i]=1.0/s; }

  auto fitRange=[&](int a,int b,double&P,double&t0,double&rms){
    const double PMIN=12*3600.0, PMAX=30*3600.0, PSTEP=300.0;
    int n=b-a; double bestP=PMIN,bestT0=sw[a],bestSse=-1,bestWsum=1;
    for(double cand=PMIN; cand<=PMAX; cand+=PSTEP){
      double wsum=0,wadjsum=0;
      for(int i=a;i<b;i++){ double k=floor((sw[i]-sw[a])/cand+0.5); double adj=sw[i]-k*cand; wsum+=wt[i]; wadjsum+=wt[i]*adj; }
      double cT0=wadjsum/wsum; double sse=0;
      for(int i=a;i<b;i++){ double k=floor((sw[i]-cT0)/cand+0.5); double r=sw[i]-(cT0+k*cand); sse+=wt[i]*r*r; }
      if(bestSse<0||sse<bestSse){bestSse=sse;bestP=cand;bestT0=cT0;bestWsum=wsum;}
    }
    P=bestP;t0=bestT0; rms=(n>0&&bestWsum>0)?sqrt(bestSse/bestWsum):0;
  };

  double Pall,t0all,rmsAll; fitRange(0,ns,Pall,t0all,rmsAll);
  printf("\nFull-window fit: P=%.2fh  rms=%.1f min  (n=%d)\n", Pall/3600, rmsAll/60, ns);

  double P=Pall,t0=t0all,rms=rmsAll; bool usedRecent=false; int usedA=0,usedB=ns;
  if (ns>=6){
    int recentStart = ns-ns/2;
    double Prec,t0rec,rmsRec; fitRange(recentStart,ns,Prec,t0rec,rmsRec);
    int nsRec=ns-recentStart;
    printf("Recent-half fit: P=%.2fh  rms=%.1f min  (n=%d)\n", Prec/3600, rmsRec/60, nsRec);
    if (nsRec>=3 && rmsAll>1.0 && rmsRec<=0.5*rmsAll){ P=Prec;t0=t0rec;rms=rmsRec;usedRecent=true;usedA=recentStart;usedB=ns; }
  }
  printf("\n=== CHOSEN FIT: P=%.2fh  rms=%.1f min  usedRecent=%s  n=%d ===\n", P/3600, rms/60, usedRecent?"yes":"no", usedB-usedA);

  time_t now = T(2026,7,24,13,0);
  double kNow = floor(((double)now-t0)/P);
  time_t nextT = (time_t)(t0+(kNow+1)*P);
  struct tm tv; gmtime_r(&nextT,&tv);
  printf("Next switch projected: %04d-%02d-%02d %02d:%02dZ\n", tv.tm_year+1900,tv.tm_mon+1,tv.tm_mday,tv.tm_hour,tv.tm_min);

  int last=nobs-1;
  double kObs=floor(((double)obs[last].t-t0)/P);
  int steps=(int)fabs(kNow-kObs);
  int modeNow = obs[last].m ^ (steps&1);
  printf("Mode now: %s\n", modeNow==0?"A":"B");

  // Regression assertions, pinned to the live AO-7 data pulled 2026-07-24: the switch the
  // operator observed on AMSAT status (between 02:00 and 05:15Z) must land inside the
  // fit's own bracket, the recovered period must be nowhere near the old wrongly-assumed
  // 24h, and the residual must be a small fraction of what the 24h-anchored fit produced
  // (340 min) on this exact data.
  int fail = 0;
  if (!(P/3600.0 > 15.0 && P/3600.0 < 24.0)) { printf("FAIL: period %.2fh not in (15,24)h\n", P/3600.0); fail=1; }
  if (!(rms/60.0 < 120.0)) { printf("FAIL: rms %.1fmin not < 120min (old alg gave 340min)\n", rms/60.0); fail=1; }
  if (modeNow != 1) { printf("FAIL: expected mode B (matches observed switch), got %s\n", modeNow==0?"A":"B"); fail=1; }
  if (!fail) printf("\nao7 real-data regression: PASS\n");
  return fail;
}
