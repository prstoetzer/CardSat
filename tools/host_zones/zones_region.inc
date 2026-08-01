// AUTO-EXTRACTED from src/app.cpp by extract_zones.py -- do not edit.
static double g_testYears = 0.0;      // SAA ellipse drift (years past 2025.0)
static double g_testIgrfYears = 1.5;  // IGRF secular-variation epoch offset
static constexpr float ZONE_BRATIO_MAX = 3.0f;
struct ShellInfo { float bSat=0, b0=0, shellL=0, bRatio=1; };
static void igrfVec(const float p[3], float yrs, float b[3]);

static const uint8_t IGRF_NMAX = 13;
static const uint8_t IGRF_SVMAX = 8;
static const float IGRF_EPOCH = 2025.0f;
static const float IGRF_G[105] = {
        0.00f,  -29350.00f,   -1410.30f,   -2556.20f,    2950.90f,    1648.70f,
     1360.90f,   -2404.20f,    1243.80f,     453.40f,     894.70f,     799.60f,
       55.80f,    -281.10f,      12.00f,    -232.90f,     369.00f,     187.20f,
     -138.70f,    -141.90f,      20.90f,      64.30f,      63.80f,      76.70f,
     -115.70f,     -40.90f,      14.90f,     -60.80f,      79.60f,     -76.90f,
       -8.80f,      59.30f,      15.80f,       2.50f,     -11.20f,      14.30f,
       23.10f,      10.90f,     -17.50f,       2.00f,     -21.80f,      16.90f,
       14.90f,     -16.80f,       1.00f,       4.70f,       8.00f,       3.00f,
       -0.20f,      -2.50f,     -13.10f,       2.40f,       8.60f,      -8.70f,
      -12.80f,      -1.30f,      -6.40f,       0.20f,       2.00f,      -1.00f,
       -0.50f,      -0.90f,       1.50f,       0.90f,      -2.60f,      -3.90f,
        3.00f,      -1.40f,      -2.50f,       2.40f,      -0.60f,       0.00f,
       -0.60f,      -0.10f,       1.10f,      -1.00f,      -0.10f,       2.60f,
       -2.00f,      -0.10f,       0.40f,       1.20f,      -1.20f,       0.60f,
        0.50f,       0.50f,      -0.10f,      -0.50f,      -0.20f,      -1.20f,
       -0.70f,       0.20f,      -0.90f,       0.60f,       0.70f,      -0.20f,
        0.50f,       0.10f,       0.70f,       0.00f,       0.30f,       0.20f,
        0.40f,      -0.50f,      -0.40f,
};
static const float IGRF_H[105] = {
        0.00f,       0.00f,    4545.50f,       0.00f,   -3133.60f,    -814.20f,
        0.00f,     -56.90f,     237.60f,    -549.60f,       0.00f,     278.60f,
     -134.00f,     212.00f,    -375.40f,       0.00f,      45.30f,     220.00f,
     -122.90f,      42.90f,     106.20f,       0.00f,     -18.40f,      16.80f,
       48.90f,     -59.80f,      10.90f,      72.80f,       0.00f,     -48.90f,
      -14.40f,      -1.00f,      23.50f,      -7.40f,     -25.10f,      -2.20f,
        0.00f,       7.20f,     -12.60f,      11.50f,      -9.70f,      12.70f,
        0.70f,      -5.20f,       3.90f,       0.00f,     -24.80f,      12.10f,
        8.30f,      -3.40f,      -5.30f,       7.20f,      -0.60f,       0.80f,
        9.80f,       0.00f,       3.30f,       0.10f,       2.50f,       5.40f,
       -9.00f,       0.40f,      -4.20f,      -3.80f,       0.90f,      -9.00f,
        0.00f,       0.00f,       2.80f,      -0.60f,       0.10f,       0.50f,
       -0.30f,      -1.20f,      -1.70f,      -2.90f,      -1.80f,      -2.30f,
        0.00f,      -1.20f,       0.60f,       1.00f,      -1.50f,       0.00f,
        0.60f,      -0.20f,       0.80f,       0.10f,      -0.90f,       0.10f,
        0.20f,       0.00f,      -0.90f,       0.70f,       1.20f,      -0.30f,
       -1.30f,      -0.10f,       0.20f,      -0.20f,       0.50f,       0.60f,
       -0.60f,      -0.30f,      -0.50f,
};
static const float IGRF_GSV[45] = {
        0.00f,      12.60f,      10.00f,     -11.20f,      -5.30f,      -8.30f,
       -1.50f,      -4.40f,       0.40f,     -15.60f,      -1.70f,      -2.30f,
       -5.80f,       5.40f,      -6.80f,       0.60f,       1.30f,       0.00f,
        0.70f,       2.30f,       1.00f,      -0.20f,      -0.30f,       0.80f,
        1.20f,      -0.80f,       0.40f,       0.90f,      -0.10f,      -0.10f,
       -0.10f,       0.50f,      -0.10f,      -0.80f,      -0.80f,       0.90f,
       -0.10f,       0.20f,       0.00f,       0.40f,      -0.10f,       0.30f,
        0.10f,       0.00f,       0.30f,
};
static const float IGRF_HSV[45] = {
        0.00f,       0.00f,     -21.50f,       0.00f,     -27.30f,     -11.10f,
        0.00f,       3.80f,      -0.20f,      -3.90f,       0.00f,      -1.30f,
        4.10f,       1.60f,      -4.10f,       0.00f,      -0.50f,       2.10f,
        0.50f,       1.70f,       1.90f,       0.00f,       0.30f,      -1.60f,
       -0.40f,       0.80f,       0.70f,       0.90f,       0.00f,       0.60f,
        0.50f,      -0.70f,       0.00f,      -0.90f,       0.50f,      -0.30f,
        0.00f,      -0.30f,       0.40f,      -0.30f,       0.40f,      -0.50f,
       -0.60f,       0.30f,       0.20f,
};
#define IGRF_IDX(n,m) ((n)*((n)+1)/2 + (m))
static const float IGRF_A = 6371.2f;      // IGRF reference radius, km
static const int   SHELL_MAX_STEPS = 3000;// hard bound on a field-line walk

// ---- IGRF-14 evaluation ------------------------------------------------------
// Geocentric spherical harmonic sum, Schmidt semi-normalized, the standard
// formulation. Returns Br (outward), Bt (colatitude/south), Bp (east) in nT.
// Validated against an independent implementation (ppigrf, IGRF14.shc) to 3e-5
// relative from the surface to GEO -- see tools/host_geomag.
void igrfField(float rKm, float colatDeg, float lonEDeg, float yrs,
                    float& Br, float& Bt, float& Bp) {
  const float D2R = 0.01745329252f;
  float th = colatDeg * D2R, ph = lonEDeg * D2R;
  float ct = cosf(th), st = sinf(th);
  if (fabsf(st) < 1e-7f) st = (st < 0) ? -1e-7f : 1e-7f;   // magnetic-pole guard
  const int NM = IGRF_NMAX;
  float P[IGRF_IDX(13,13)+1], dP[IGRF_IDX(13,13)+1];
  P[IGRF_IDX(0,0)] = 1.0f; dP[IGRF_IDX(0,0)] = 0.0f;
  float sm[14], cm[14];
  for (int m = 0; m <= NM; ++m) { sm[m] = sinf(m*ph); cm[m] = cosf(m*ph); }
  float ar = IGRF_A / rKm, arn = ar*ar;      // becomes (a/r)^(n+2) inside the loop
  Br = Bt = Bp = 0.0f;
  for (int n = 1; n <= NM; ++n) {
    arn *= ar;
    for (int m = 0; m <= n; ++m) {
      if (n == m) {
        float k = (n == 1) ? 1.0f : sqrtf(1.0f - 0.5f/(float)n);
        P [IGRF_IDX(n,m)] = k * st * P[IGRF_IDX(n-1,m-1)];
        dP[IGRF_IDX(n,m)] = k * (st * dP[IGRF_IDX(n-1,m-1)] + ct * P[IGRF_IDX(n-1,m-1)]);
      } else {
        float den = sqrtf((float)(n*n - m*m));
        float k1  = (float)(2*n - 1) / den;
        float k2  = (n-1 >= m) ? sqrtf((float)((n-1)*(n-1) - m*m)) / den : 0.0f;
        float Pnm2  = (n-2 >= m) ? P [IGRF_IDX(n-2,m)] : 0.0f;
        float dPnm2 = (n-2 >= m) ? dP[IGRF_IDX(n-2,m)] : 0.0f;
        P [IGRF_IDX(n,m)] = k1 * ct * P[IGRF_IDX(n-1,m)] - k2 * Pnm2;
        dP[IGRF_IDX(n,m)] = k1 * (ct * dP[IGRF_IDX(n-1,m)] - st * P[IGRF_IDX(n-1,m)]) - k2 * dPnm2;
      }
      float g = IGRF_G[IGRF_IDX(n,m)], h = IGRF_H[IGRF_IDX(n,m)];
      if (n <= IGRF_SVMAX) {                       // linear secular variation
        g += yrs * IGRF_GSV[IGRF_IDX(n,m)];
        h += yrs * IGRF_HSV[IGRF_IDX(n,m)];
      }
      float gc = g*cm[m] + h*sm[m], gs = g*sm[m] - h*cm[m];
      Br += arn * (float)(n+1) * gc * P[IGRF_IDX(n,m)];
      Bt -= arn * gc * dP[IGRF_IDX(n,m)];
      Bp += arn * (float)m * gs * P[IGRF_IDX(n,m)] / st;
    }
  }
}


static void igrfVec(const float p[3], float yrs, float b[3]) {
  float r = sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
  if (r < 1.0f) { b[0]=b[1]=b[2]=0; return; }
  float colat = acosf(p[2]/r)*57.2957795f, lon = atan2f(p[1],p[0])*57.2957795f;
  float Br,Bt,Bp; igrfField(r,colat,lon,yrs,Br,Bt,Bp);
  float th=colat/57.2957795f, ph=lon/57.2957795f;
  float st=sinf(th),ct=cosf(th),sp=sinf(ph),cp=cosf(ph);
  b[0]=Br*st*cp+Bt*ct*cp-Bp*sp; b[1]=Br*st*sp+Bt*ct*sp+Bp*cp; b[2]=Br*ct-Bt*st;
}

bool maybeInBelt(double latDeg, double lonEDeg, double altKm) {
  const double RE = 6371.0, D2R = 0.017453292519943295;
  const double poleLat = 80.79, poleLon = -72.76;      // IGRF-14 2025.0 dipole axis
  double s = sin(latDeg*D2R)*sin(poleLat*D2R)
           + cos(latDeg*D2R)*cos(poleLat*D2R)*cos((lonEDeg - poleLon)*D2R);
  if (s >  1) s =  1; if (s < -1) s = -1;
  double sin2 = s*s, cos2 = 1.0 - sin2;
  if (cos2 < 1e-6) return false;                        // over the magnetic pole
  double L = (RE + altKm) / RE / cos2;
  if (L < 1.05 || L > 9.0) return false;
  double cos6 = cos2*cos2*cos2;
  double ratio = sqrt(1.0 + 3.0*sin2) / cos6;           // dipole B/B0
  return ratio <= 3.0 * ZONE_BRATIO_MAX;
}

ShellInfo shellAt(double latDeg, double lonEDeg, double altKm) {
  ShellInfo s;
  const float yrs = (float)g_testIgrfYears;
  const float RE = 6371.0f;
  float r = RE + (float)altKm;
  float colat = (90.0f - (float)latDeg) / 57.2957795f, ph = (float)lonEDeg / 57.2957795f;
  float p[3] = { r*sinf(colat)*cosf(ph), r*sinf(colat)*sinf(ph), r*cosf(colat) };
  float b[3]; igrfVec(p, yrs, b);
  float bm = sqrtf(b[0]*b[0] + b[1]*b[1] + b[2]*b[2]);
  s.bSat = bm; s.b0 = bm; s.shellL = r / RE;
  if (bm <= 0) { s.bRatio = 1.0f; return s; }

  // Downhill direction: one probe each way along the line.
  int sign = +1;
  { float q[3], t[3];
    for (int i=0;i<3;i++) q[i] = p[i] + 50.0f * b[i] / bm;
    igrfVec(q, yrs, t);
    if (sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]) > bm) sign = -1; }

  float prev = bm; int rising = 0;
  for (int step = 0; step < SHELL_MAX_STEPS; ++step) {
    float rr = sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if (rr > 12.0f*RE || rr < RE + 80.0f) break;    // magnetopause / atmosphere
    float h = 0.02f * rr; if (h < 15.0f) h = 15.0f; if (h > 500.0f) h = 500.0f;
    float k1[3],k2[3],k3[3],k4[3],q[3],t[3],m;      // RK4 on dp/ds = sign * B/|B|
    igrfVec(p,yrs,t); m = sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); if (m<=0) break;
    for (int i=0;i<3;i++) k1[i] = sign*t[i]/m;
    for (int i=0;i<3;i++) q[i] = p[i] + 0.5f*h*k1[i];
    igrfVec(q,yrs,t); m = sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); if (m<=0) break;
    for (int i=0;i<3;i++) k2[i] = sign*t[i]/m;
    for (int i=0;i<3;i++) q[i] = p[i] + 0.5f*h*k2[i];
    igrfVec(q,yrs,t); m = sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); if (m<=0) break;
    for (int i=0;i<3;i++) k3[i] = sign*t[i]/m;
    for (int i=0;i<3;i++) q[i] = p[i] + h*k3[i];
    igrfVec(q,yrs,t); m = sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]); if (m<=0) break;
    for (int i=0;i<3;i++) k4[i] = sign*t[i]/m;
    for (int i=0;i<3;i++) p[i] += (h/6.0f)*(k1[i] + 2*k2[i] + 2*k3[i] + k4[i]);
    igrfVec(p,yrs,t);
    float bnew = sqrtf(t[0]*t[0]+t[1]*t[1]+t[2]*t[2]);
    float rn   = sqrtf(p[0]*p[0]+p[1]*p[1]+p[2]*p[2]);
    if (bnew < s.b0) { s.b0 = bnew; s.shellL = rn / RE; }
    if (bnew > prev) { if (++rising >= 2) break; } else rising = 0;
    prev = bnew;
  }
  s.bRatio = (s.b0 > 0) ? s.bSat / s.b0 : 1.0f;
  return s;
}

double lShellAt(double latDeg, double lonEDeg, double altKm) {
  const double RE = 6371.0, D2R = 0.017453292519943295;
  const double poleLat = 80.79, poleLon = -72.76;      // IGRF-14 2025.0 dipole axis
  double s = sin(latDeg*D2R)*sin(poleLat*D2R)
           + cos(latDeg*D2R)*cos(poleLat*D2R)*cos((lonEDeg - poleLon)*D2R);
  if (s >  1) s =  1; if (s < -1) s = -1;             // sin(magnetic latitude)
  double cos2 = 1.0 - s*s;                             // cos^2(magnetic latitude)
  if (cos2 < 1e-6) cos2 = 1e-6;                         // guard near the magnetic poles
  return (RE + altKm) / RE / cos2;
}

bool zoneContains(int zone, double lat, double lonE, double altKm, bool sunlit) {
  switch (zone) {
    case 0: {
      // Ellipse centered ~27S, 53W with ~0.3 deg/yr westward drift from 2025.0.
      double yrs = g_testYears;
      double cLat = -27.0, cLon = -53.0 - 0.30*yrs, aLat = 25.0, aLon = 55.0;
      double dLon = lonE - cLon;
      while (dLon < -180) dLon += 360; while (dLon > 180) dLon -= 360;
      double u = (lat - cLat)/aLat, v = dLon/aLon;
      return u*u + v*v <= 1.0;
    }
    case 1: return !sunlit;
    case 2:   return fabs(lat) >= 60.0;
    case 3:
    case 4: {
      // Trapped-particle test in McIlwain (L, B/B0) coordinates, from the real
      // IGRF-14 field. BOTH conditions must hold:
      //   * the field line through the satellite is a belt shell (L range), and
      //   * the satellite is near that shell's magnetic equator (B/B0 <= cutoff),
      //     i.e. inside the belt itself rather than out on its high-latitude horn
      //     where the flux tube dips to low altitude and the trapped population
      //     is depleted into the atmosphere.
      // The old altitude floor is gone: it could not express this (the horns pass
      // straight through any altitude), and the real field makes it unnecessary --
      // a low-altitude point only lands on a belt shell near its equator inside
      // the South Atlantic Anomaly, which IS the inner belt reaching down to LEO.
      if (altKm < 300.0) return false;              // dense atmosphere: nothing trapped
      if (!maybeInBelt(lat, lonE, altKm)) return false;     // cheap analytic reject
      ShellInfo s = shellAt(lat, lonE, altKm);
      if (s.bRatio > ZONE_BRATIO_MAX) return false;
      return (zone == 3) ? (s.shellL >= 1.2 && s.shellL <= 2.5)
                                  : (s.shellL >= 3.0 && s.shellL <= 7.0);
    }
  }
  return false;
}
