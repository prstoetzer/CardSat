#include <cstdio>
#include <cmath>
#include <ctime>
struct Fit{double P,t0;int nsw,modeNow;long toSwitch;double rms;};
// Mirrors App::ao7Estimate's core fit exactly: MAXGAP=20h bracket rule, inverse-span
// weighted grid search over a 12-30h domain range (replaces the old fixed-24h-guess
// iterative rounding scheme, which was found to alias catastrophically against real
// AO-7 data -- see ao7_realdata_test.cpp).
Fit estimate(time_t*T,unsigned char*M,int N,time_t now){
  Fit f{0,0,0,-1,0,0};if(N==0)return f;
  for(int i=1;i<N;i++){time_t kt=T[i];unsigned char km=M[i];int j=i-1;while(j>=0&&T[j]>kt){T[j+1]=T[j];M[j+1]=M[j];j--;}T[j+1]=kt;M[j+1]=km;}
  const double MAXGAP=20.0*3600.0;
  static double sw[500],span[500],wt[500]; int ns=0;
  for(int i=1;i<N&&ns<500;i++)if(M[i]!=M[i-1]){double g=(double)(T[i]-T[i-1]);if(g<=MAXGAP){sw[ns]=0.5*((double)T[i]+(double)T[i-1]);span[ns]=g;ns++;}}
  f.nsw=ns;if(ns==0){f.modeNow=M[N-1];return f;}
  for(int i=0;i<ns;i++){double s=span[i]>60?span[i]:60; wt[i]=1.0/s;}

  const double PMIN=12*3600.0, PMAX=30*3600.0, PSTEP=300.0;
  double bestP=PMIN,bestT0=sw[0],bestSse=-1,bestWsum=1;
  for(double cand=PMIN; cand<=PMAX; cand+=PSTEP){
    double wsum=0,wadjsum=0;
    for(int i=0;i<ns;i++){ double k=floor((sw[i]-sw[0])/cand+0.5); double adj=sw[i]-k*cand; wsum+=wt[i]; wadjsum+=wt[i]*adj; }
    double cT0=wadjsum/wsum; double sse=0;
    for(int i=0;i<ns;i++){ double k=floor((sw[i]-cT0)/cand+0.5); double r=sw[i]-(cT0+k*cand); sse+=wt[i]*r*r; }
    if(bestSse<0||sse<bestSse){bestSse=sse;bestP=cand;bestT0=cT0;bestWsum=wsum;}
  }
  double P=bestP,t0=bestT0;
  f.P=P;f.t0=t0; f.rms=(bestWsum>0)?sqrt(bestSse/bestWsum):0;
  double kNow=floor(((double)now-t0)/P);
  f.toSwitch=(long)((t0+(kNow+1)*P)-now);
  int last=N-1; double kObs=floor(((double)T[last]-t0)/P);
  int steps=(int)fabs(kNow-kObs); f.modeNow=M[last]^(steps&1);
  return f;
}
int main(){double Ptrue=24.05*3600,t0true=1000000;int pass=0;
  for(int trial=0;trial<12;trial++){time_t now=(time_t)(t0true+(20+trial)*86400+trial*7000);
    time_t T[400];unsigned char M[400];int N=0;
    for(int k=0;k<30;k++){double s0=t0true+k*Ptrue,s1=t0true+(k+1)*Ptrue;int mode=k&1;for(int r=0;r<4;r++){double frac=0.1+0.75*r/4.0;time_t tt=(time_t)(s0+frac*(s1-s0));if(tt<=now){T[N]=tt;M[N]=mode;N++;}}}
    Fit f=estimate(T,M,N,now);double kNowTrue=floor((now-t0true)/Ptrue);int trueMode=((int)kNowTrue)&1;
    bool ok=f.modeNow==trueMode;pass+=ok;
    printf("trial %2d: modeNow fit=%d true=%d %s  (P=%.2fh rms=%.1fmin)\n",trial,f.modeNow,trueMode,ok?"PASS":"FAIL",f.P/3600,f.rms/60);}
  printf("== %d/12 pass ==\n",pass);return 0;}
