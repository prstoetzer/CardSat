// AUTO-EXTRACTED from src/predict.cpp by extract_doppler.py -- do not edit.
void dopplerFreqs(freq_t dlNominal, freq_t ulNominal,
                             double rangeRateKmS,
                             int32_t calDlHz, int32_t calUlHz,
                             freq_t& rxHz, freq_t& txHz) {
  double rr = rangeRateKmS * 1000.0;       // m/s, +ve receding
  double beta = rr / C_LIGHT;

  // Downlink: observer receives dl*(1 - beta) -> tune RX there.
  double rx = (double)dlNominal * (1.0 - beta) + (double)calDlHz;
  // Uplink: transmit so the satellite hears ul nominal -> ul/(1 - beta).
  double tx = (ulNominal ? ((double)ulNominal / (1.0 - beta)) : 0.0);
  if (ulNominal) tx += (double)calUlHz;

  rxHz = (freq_t)llround(rx);
  txHz = (freq_t)llround(tx);
}

freq_t uplinkForFixedDownlink(freq_t dlOp, freq_t ulOp,
                                           bool invert, double rangeRateKmS,
                                           int32_t calDlHz, int32_t calUlHz) {
  if (ulOp == 0) return 0;
  double beta = rangeRateKmS * 1000.0 / C_LIGHT;   // +ve receding
  double oneMinusBeta = 1.0 - beta;
  if (oneMinusBeta == 0.0) oneMinusBeta = 1e-12;    // guard (never physical)

  // The operator parks RX at the ground frequency dlOp+calDl. For the ground to
  // hear that, the bird must EMIT a downlink of Fdl_sat = (dlOp+calDl)/(1-beta).
  // delta is how far that emit sits above the nominal operating downlink.
  double parkedGround = (double)dlOp + (double)calDlHz;
  double fdlSat = parkedGround / oneMinusBeta;
  double delta  = fdlSat - (double)dlOp;

  // Map the shifted emit back to the uplink the bird must HEAR, using the same
  // inversion sense as passbandFreqs: inverting -> uplink moves opposite the
  // downlink; non-inverting -> it tracks. Then Doppler-compensate that uplink so
  // the bird actually hears it, and add the uplink calibration.
  double fulSat = invert ? ((double)ulOp - delta) : ((double)ulOp + delta);
  double tx = fulSat / oneMinusBeta + (double)calUlHz;
  if (tx < 0) tx = 0;
  return (freq_t)llround(tx);
}

freq_t downlinkForFixedUplink(freq_t dlOp, freq_t ulOp,
                                           bool invert, double rangeRateKmS,
                                           int32_t calDlHz, int32_t calUlHz) {
  double beta = rangeRateKmS * 1000.0 / C_LIGHT;   // +ve receding
  double oneMinusBeta = 1.0 - beta;
  if (oneMinusBeta == 0.0) oneMinusBeta = 1e-12;    // guard (never physical)
  // No uplink (downlink-only bird): just the plain Doppler-shifted downlink.
  if (ulOp == 0)
    return (freq_t)llround((double)dlOp * oneMinusBeta + (double)calDlHz);

  // The operator parks TX at the ground frequency ulOp+calUl; the bird hears that
  // Doppler-shifted to Ful_sat.
  double parkedTxGround = (double)ulOp + (double)calUlHz;
  double fulSat = parkedTxGround * oneMinusBeta;
  // Translate the heard uplink to the emitted downlink with the same inversion
  // sense as passbandFreqs (inverting -> downlink moves opposite the uplink).
  double fdlSat = invert ? ((double)dlOp - (fulSat - (double)ulOp))
                         : ((double)dlOp + (fulSat - (double)ulOp));
  // That emit is Doppler-shifted again on the way down; add downlink calibration.
  double rx = fdlSat * oneMinusBeta + (double)calDlHz;
  if (rx < 0) rx = 0;
  return (freq_t)llround(rx);
}

void passbandFreqs(const Transponder& t, int32_t pbOffsetHz,
                              freq_t& dlOp, freq_t& ulOp) {
  // No tunable downlink passband -> single channel; ignore the offset.
  uint32_t dlBw = t.bandwidth();
  if (!t.isLinear || dlBw == 0) {
    dlOp = t.downlink;
    ulOp = t.uplink;
    return;
  }

  // Clamp the tuning offset into [0, downlink bandwidth].
  int32_t off = pbOffsetHz;
  if (off < 0) off = 0;
  if ((uint32_t)off > dlBw) off = (int32_t)dlBw;

  dlOp = t.downlink + (uint32_t)off;

  if (t.uplink == 0) { ulOp = 0; return; }

  // Assume equal up/down passband width when the uplink top edge is missing.
  uint32_t ulBw = (t.uplinkHigh > t.uplink) ? (t.uplinkHigh - t.uplink) : dlBw;
  if (t.invert) {
    // Inverting: bottom of uplink maps to top of downlink. As the downlink
    // tunes up by `off`, the uplink tunes down by the same amount.
    ulOp = t.uplink + ulBw - (uint32_t)off;
  } else {
    ulOp = t.uplink + (uint32_t)off;
  }
}
