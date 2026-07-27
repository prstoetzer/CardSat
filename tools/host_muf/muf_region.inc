static double minimufMHz(double L1, double W1, double L2, double W2,
                         int M0, int D6, double T5, double S9) {
  const double PIC = 3.141593, P0C = 1.570796;
  double K1,K6,K8,K9,G0,G8,J9,M9,P,Q,A,C0,T,T4,T9,Y1,Y2,Y3,ft,gt,dist;
  auto SGN = [](double x){ return x>0?1.0:(x<0?-1.0:0.0); };
  auto CLAMP1 = [](double x){ return x<-1?-1.0:(x>1?1.0:x); };

  ft = sin(L1)*sin(L2)+cos(L1)*cos(L2)*cos(W2-W1);
  dist = acos(CLAMP1(ft));
  K6 = 1.59*dist; if (K6 < 1) K6 = 1;
  P = sin(L2); Q = cos(L2);
  A = (sin(L1)-P*cos(dist))/(Q*sin(dist));
  Y1 = 0.0172*(10+(M0-1)*30.4+D6);
  Y2 = 0.409*cos(Y1);
  ft = 2.5*dist/K6; if (ft > P0C) ft = P0C; ft = sin(ft);
  M9 = 1+2.5*ft*sqrt(ft);
  J9 = 100;
  for (K1 = 1.0/(2*K6); K1 <= 1-1.0/(2*K6)+1e-12; K1 += fabs(0.9999-1.0/K6)) {
    gt = dist*K1;
    ft = P*cos(gt)+Q*sin(gt)*A;
    ft = CLAMP1(ft);
    Y3 = P0C-acos(ft);
    ft = (cos(gt)-ft*P)/(Q*sqrt(1-ft*ft));
    ft = CLAMP1(ft);
    ft = W2+SGN(sin(W1-W2))*acos(ft);
    if (ft < 0) ft += 2*PIC;
    if (ft >= 2*PIC) ft -= 2*PIC;
    ft = 3.82*ft+12+0.13*(sin(Y1)+1.2*sin(2*Y1));
    K8 = ft-12*(1+SGN(ft-24))*SGN(fabs(ft-24));
    if (cos(Y3+Y2) <= -0.26) {
      K9 = 0; G0 = 0;
    } else {
      ft = (-0.26+sin(Y2)*sin(Y3))/(cos(Y2)*cos(Y3)+0.001);
      K9 = 12-atan(ft/sqrt(fabs(1-ft*ft)))*7.639437;
      T  = K8-K9/2+12*(1-SGN(K8-K9/2))*SGN(fabs(K8-K9/2));
      T4 = K8+K9/2-12*(1+SGN(K8+K9/2-24))*SGN(fabs(K8+K9/2-24));
      C0 = fabs(cos(Y3+Y2));
      T9 = 9.7*pow(C0,9.6); if (T9 < 0.1) T9 = 0.1;
      G8 = PIC*T9/K9;
      if ((T4<T && (T5-T4)*(T-T5)>0.0) || (T4>=T && (T5-T)*(T4-T5)<=0)) {
        ft = T5+12*(1+SGN(T4-T5))*SGN(fabs(T4-T5));
        ft = (T4-ft)/2;
        G0 = C0*(G8*(exp(-K9/T9)+1))*exp(ft)/(1+G8*G8);
      } else {
        ft = T5+12*(1+SGN(T-T5))*SGN(fabs(T-T5));
        gt = PIC*(ft-T)/K9;
        ft = (T-ft)/T9;
        G0 = C0*(sin(gt)+G8*(exp(ft)-cos(gt)))/(1+G8*G8);
        ft = C0*(G8*(exp(-K9/T9)+1))*exp((K9-24)/2)/(1+G8*G8);
        if (G0 < ft) G0 = ft;
      }
    }
    ft = (1+S9/250)*M9*sqrt(6+58*sqrt(G0));
    ft *= 1-0.1*exp((K9-24)/3);
    ft *= 1+0.1*(1-SGN(L1)*SGN(L2));
    ft *= 1-0.1*(1+SGN(fabs(sin(Y3))-cos(Y3)));
    if (ft < J9) J9 = ft;
  }
  return J9 < 2 ? 2 : (J9 > 32 ? 32 : J9);
}