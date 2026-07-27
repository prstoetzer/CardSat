// Host harness for MINIMUF-3.5, extracted from the live src/app.cpp by extract_muf.py.
// Pins the model to NOSC TD-201's own Figure-1 verification table (TX 21N/156W,
// RX 38N/122W, 17 Oct, SSN 110). A regression that breaks the transcription moves
// multiple hours by MHz and fails immediately; the 0.8 MHz tolerance only accommodates
// the one arc-transition hour that rounds against a table printed to 0.1 MHz.
#include <cstdio>
#include <cmath>
#include "muf_region.inc"

int main() {
  const double D2R = 3.141593/180.0;
  double L1=21*D2R, W1=156*D2R, L2=38*D2R, W2=122*D2R;
  // TD-201 Figure 1, hours 0..23:
  double ref[24]={32.0,32.0,32.0,29.9,25.0,22.0,20.9,19.3,18.0,16.9,16.0,15.2,
                  14.6,14.1,13.7,21.0,27.6,31.5,32.0,32.0,32.0,32.0,32.0,32.0};
  const double TOL=0.8;
  int fails=0; double ss=0, maxd=0;
  printf("MINIMUF-3.5 vs NOSC TD-201 Figure-1 (24-hour verification table)\n");
  for (int h=0;h<24;h++){
    double m=minimufMHz(L1,W1,L2,W2,10,17,(double)h,110);
    double d=m-ref[h]; ss+=d*d; if(fabs(d)>maxd)maxd=fabs(d);
    if(fabs(d)>TOL){ fails++; printf("  FAIL h%02d: %.1f vs %.1f (d=%+.2f)\n",h,m,ref[h],d); }
  }
  printf("  RMS=%.3f MHz  max=%.2f MHz\n", sqrt(ss/24), maxd);
  printf("minimuf verify: %s\n", fails ? "FAIL" : "PASS (24/24 within 0.8 MHz)");
  return fails ? 1 : 0;
}
