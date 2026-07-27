#include <cstdio>
#include <cmath>
#include <ctime>
#include <cstdlib>
struct FitR { double P, t0, rms; };
// Grid search over a 12-30h domain range (mirrors App::ao7Estimate's real fit; unweighted
// here since this test's synthetic switch instants have no per-bracket span/uncertainty
// model -- the recency-preference and illumination-filter logic under test is independent
// of whether the underlying point-fit is weighted or grid-searched vs iterative).
void fitRange(double* sw, int a, int b, double& P, double& t0, double& rms) {
  const double PMIN = 12*3600.0, PMAX = 30*3600.0, PSTEP = 300.0;
  int n = b - a;
  double bestP = PMIN, bestT0 = sw[a], bestSse = -1;
  for (double cand = PMIN; cand <= PMAX; cand += PSTEP) {
    double sum=0;
    for (int i=a;i<b;++i){ double k=floor((sw[i]-sw[a])/cand+0.5); sum += sw[i]-k*cand; }
    double cT0 = sum/n;
    double sse=0;
    for (int i=a;i<b;++i){ double k=floor((sw[i]-cT0)/cand+0.5); double r=sw[i]-(cT0+k*cand); sse+=r*r; }
    if (bestSse<0 || sse<bestSse) { bestSse=sse; bestP=cand; bestT0=cT0; }
  }
  P = bestP; t0 = bestT0;
  rms = (n>0) ? sqrt(bestSse/n) : 0;
}

// TEST 1: consistent single-period data throughout -- recent fit should NOT be preferred
// (residuals near-equal, ratio not <=0.5).
void test1() {
  double Ptrue = 86000.0, t0true = 1000000;
  double sw[20]; int ns=10;
  for (int i=0;i<ns;++i) sw[i] = t0true + i*Ptrue;
  double Pall,t0all,rmsAll; fitRange(sw,0,ns,Pall,t0all,rmsAll);
  int recentStart = ns - ns/2;
  double Prec,t0rec,rmsRec; fitRange(sw,recentStart,ns,Prec,t0rec,rmsRec);
  bool preferRecent = (ns-recentStart>=3) && (rmsAll>300.0) && (rmsRec <= 0.5*rmsAll);
  printf("TEST1 consistent-data: rmsAll=%.3f rmsRec=%.3f preferRecent=%s (expect false) %s\n",
    rmsAll, rmsRec, preferRecent?"true":"false", !preferRecent?"PASS":"FAIL");
}

// TEST 2: early boundaries contaminated by irregular spacing (simulating leftover
// eclipse-season jitter), later boundaries clean and consistent -- recent fit SHOULD win.
void test2() {
  double Ptrue = 86000.0, t0true = 1000000;
  double sw[20]; int ns=10;
  // first 5 boundaries: irregular (jittered heavily, simulating a still-transitioning period)
  srand(42);
  for (int i=0;i<5;++i) sw[i] = t0true + i*Ptrue + (rand()%20000 - 10000);
  // last 5 boundaries: clean, continuing the true period from where the clean run starts
  double cleanBase = t0true + 5*Ptrue;
  for (int i=5;i<10;++i) sw[i] = cleanBase + (i-5)*Ptrue;
  double Pall,t0all,rmsAll; fitRange(sw,0,ns,Pall,t0all,rmsAll);
  int recentStart = ns - ns/2;
  double Prec,t0rec,rmsRec; fitRange(sw,recentStart,ns,Prec,t0rec,rmsRec);
  bool preferRecent = (ns-recentStart>=3) && (rmsAll>300.0) && (rmsRec <= 0.5*rmsAll);
  printf("TEST2 contaminated-old: rmsAll=%.1fs rmsRec=%.1fs preferRecent=%s (expect true) %s\n",
    rmsAll, rmsRec, preferRecent?"true":"false", preferRecent?"PASS":"FAIL");
  printf("       recovered period from recent fit: %.1fh (true %.1fh) %s\n",
    Prec/3600, Ptrue/3600, fabs(Prec-Ptrue)<200?"PASS":"FAIL");
}

// TEST 3: illumination-cutoff filtering -- observations before sinceT should be dropped
// before bracket-finding even begins, so contamination never reaches fitRange at all.
void test3() {
  time_t now = 2000000;
  time_t sinceT = 1500000;   // illumination began here
  time_t obsT[10] = {1000000,1100000,1200000,1300000,1400000, 1600000,1700000,1800000,1900000,2000000};
  int mode[10]    = {0,1,0,1,0,  0,1,0,1,0};   // junk pattern before cutoff, clean after
  int startIdx = 0;
  while (startIdx < 10 && obsT[startIdx] < sinceT) ++startIdx;
  printf("TEST3 illumination-filter: startIdx=%d (expect 5) %s, excluded=%d\n",
    startIdx, startIdx==5?"PASS":"FAIL", startIdx);
}


// TEST 4: recent fit only modestly better (not dramatically) -- should NOT flip, guards
// against over-triggering on noise that happens to favor a smaller sample.
void test4() {
  double Ptrue = 86000.0, t0true = 1000000;
  double sw[20]; int ns=10;
  srand(7);
  for (int i=0;i<10;++i) sw[i] = t0true + i*Ptrue + (rand()%3000 - 1500);  // mild jitter throughout
  double Pall,t0all,rmsAll; fitRange(sw,0,ns,Pall,t0all,rmsAll);
  int recentStart = ns - ns/2;
  double Prec,t0rec,rmsRec; fitRange(sw,recentStart,ns,Prec,t0rec,rmsRec);
  bool preferRecent = (ns-recentStart>=3) && (rmsAll>300.0) && (rmsRec <= 0.5*rmsAll);
  printf("TEST4 mild-uniform-jitter: rmsAll=%.1fs rmsRec=%.1fs preferRecent=%s\n",
    rmsAll, rmsRec, preferRecent?"true":"false");
}
int main(){ test1(); test2(); test3(); test4(); return 0; }
