// AUTO-EXTRACTED from src/app.cpp by extract_zones.py -- do not edit.
static double g_testYears = 0.0;

double lShellAt(double latDeg, double lonEDeg, double altKm) {
  const double RE = 6371.0, D2R = 0.017453292519943295;
  const double poleLat = 80.65, poleLon = -72.68;
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
      // Ellipse centred ~27S, 53W with ~0.3 deg/yr westward drift from 2025.0.
      double yrs = g_testYears;
      double cLat = -27.0, cLon = -53.0 - 0.30*yrs, aLat = 25.0, aLon = 55.0;
      double dLon = lonE - cLon;
      while (dLon < -180) dLon += 360; while (dLon > 180) dLon -= 360;
      double u = (lat - cLat)/aLat, v = dLon/aLon;
      return u*u + v*v <= 1.0;
    }
    case 1: return !sunlit;
    case 2:   return fabs(lat) >= 60.0;
    case 3: { double L = lShellAt(lat, lonE, altKm);
                       return altKm >= 1000.0 && L >= 1.2 && L <= 2.5; }
    case 4: { double L = lShellAt(lat, lonE, altKm);
                       return altKm >= 1000.0 && L >= 3.0 && L <= 7.0; }
  }
  return false;
}
