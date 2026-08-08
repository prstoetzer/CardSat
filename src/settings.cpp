// ===========================================================================
//  settings.cpp
// ===========================================================================
#include "settings.h"
#include "config.h"
#include <LittleFS.h>
#include "storage.h"
#include <ArduinoJson.h>

// M17: clamp the values that are used as array subscripts or fed to math/hardware, so a
// hand-edited, partially-corrupted, or future-version config can't drive an out-of-range
// index or a non-finite coordinate into the predictor, displays, or radio libraries. This
// is deliberately focused on the crash/garbage-risk fields; it is safe to call after load,
// restore, and migration. Enums that makeRig / pickers already range-check are left alone.
void Settings::validate() {
  // Feed radii. aprsRangeKm becomes an APRS-IS r/ filter (servers cap the radius
  // anyway) and adsbRangeKm becomes a nautical-mile conversion in a URL path, so
  // neither may be zero or negative.
  if (aprsRangeKm < 1)    aprsRangeKm = 1;
  if (aprsRangeKm > 2000) aprsRangeKm = 2000;
  if (adsbRangeKm < 1)    adsbRangeKm = 1;
  if (adsbRangeKm > 500)  adsbRangeKm = 500;
  // Array-index enum: GPS_PROFILES[gpsSource] is indexed unguarded on at least one path.
  if (gpsSource >= GPS_SRC_COUNT) gpsSource = GPS_SRC_CAP1262;
  // C2: Grove rigctl baud must be one of the supported UART rates; snap anything else to the
  // 115200 companion default so a hand-edited/corrupt value can't misconfigure Serial1.
  switch (catGroveBaud) {
    case 9600: case 19200: case 38400: case 57600: case 115200: break;
    default: catGroveBaud = 115200; break;
  }
  // The helper LINK baud must be a rate the companion actually scans for. It
  // auto-bauds across CSUH_BAUDS[] and nothing else, so a value outside that list
  // could never link -- and a link that silently never comes up is the hardest
  // fault on this feature to read. Snap to the default instead.
  {
    bool okBaud = false;
    for (int i = 0; i < CSUH_BAUD_N; ++i) if (CSUH_BAUDS[i] == helperBaud) { okBaud = true; break; }
    if (!okBaud) helperBaud = CSUH_BAUDS[0];
  }
  // Physical coordinates feed SGP4 and the Maidenhead math. Reject non-finite and clamp.
  if (!isfinite(lat) || lat < -90.0  || lat > 90.0)  lat = 0.0;
  if (!isfinite(lon) || lon < -180.0 || lon > 180.0) lon = 0.0;
  if (!isfinite(altM) || altM < -500.0 || altM > 9000.0) altM = 0.0;
  // Per-QTH presets are indexed by the preset picker; clamp the same way.
  for (int i = 0; i < 5; ++i) {
    if (!isfinite(qthLat[i]) || qthLat[i] < -90.0  || qthLat[i] > 90.0)  qthLat[i] = 0.0;
    if (!isfinite(qthLon[i]) || qthLon[i] < -180.0 || qthLon[i] > 180.0) qthLon[i] = 0.0;
  }

  // M20: LoRa radio parameters. A hand-edited or corrupt config.json must not push the
  // SX1262 outside its usable range or select an unsupported bandwidth. Bounds mirror the
  // LRX_FREQ_MIN/MAX_KHZ and BW_TABLE authorities in lorarx.cpp; kept in sync by comment
  // since those are file-static there.
  if (loraRegion > 2) loraRegion = 0;                                   // 0=US 1=EU 2=JP
  if (loraFreqKHz < 150000UL) loraFreqKHz = 150000UL;                   // SX1262 low edge
  if (loraFreqKHz > 960000UL) loraFreqKHz = 960000UL;                   // SX1262 high edge
  if (loraSf < 7)  loraSf = 7;                                          // valid SF 7..12
  if (loraSf > 12) loraSf = 12;
  {
    // Snap bandwidth to the nearest supported ladder value; default 125 kHz if unrecognized.
    static const uint32_t BW_OK[] = { 7800, 10400, 15600, 20800, 31250,
                                      41700, 62500, 125000, 250000, 500000 };
    bool found = false;
    for (uint32_t v : BW_OK) if (loraBwHz == v) { found = true; break; }
    if (!found) loraBwHz = 125000UL;
  }
  if (loraTxDbm < -9) loraTxDbm = -9;                                   // SX1262 PA floor
  if (loraTxDbm > 22) loraTxDbm = 22;                                   // SX1262 PA ceiling
  if (msgNotify > 2) msgNotify = 1;                                     // 0=off 1=banner 2=+beep
}

bool Settings::load() {
  File f = Store::fs().open(FILE_CFG, "r");
  if (!f) {
#ifdef CARDSAT_CFG_DEBUG
    Serial.printf("[cfg] load: %s absent (first boot?)\n", FILE_CFG);
#endif
    cfgFileMissing = true;            // genuinely no file -> defaults are correct
    return false;
  }
  cfgFileMissing = false;            // a file exists; a failure here is a READ error
  JsonDocument d;
  DeserializationError err = deserializeJson(d, f);
  size_t sz = f.size();
  f.close();
  if (err) {
#ifdef CARDSAT_CFG_DEBUG
    Serial.printf("[cfg] load: PARSE FAILED (%s) on %u-byte file -- "
                  "keeping file intact, using defaults this boot\n",
                  err.c_str(), (unsigned)sz);
#endif
    return false;                    // do NOT let the caller overwrite a real file
  }

  strncpy(ssid, d["ssid"] | "", sizeof(ssid)-1); ssid[sizeof(ssid)-1]=0;   // M16: terminate
  strncpy(pass, d["pass"] | "", sizeof(pass)-1); pass[sizeof(pass)-1]=0;   // M16: terminate
  strncpy(ssid2, d["ssid2"] | "", sizeof(ssid2)-1); ssid2[sizeof(ssid2)-1]=0;
  strncpy(pass2, d["pass2"] | "", sizeof(pass2)-1); pass2[sizeof(pass2)-1]=0;
  strncpy(gpUrl, d["gpurl"] | AMSAT_GP_URL, sizeof(gpUrl)-1); gpUrl[sizeof(gpUrl)-1]=0;
  strncpy(printerHost, d["prhost"] | "", sizeof(printerHost)-1); printerHost[sizeof(printerHost)-1]=0;
  printerPort = d["prport"] | 9100;
  printerCols = d["prcols"] | 32;
  printFormat = d["prfmt"] | 0;
  printTransport = d["prtx"] | 0;
  printPaper = d["prpr"] | 0;
  printToSerial = d["prser"] | false;
  basicFileWrite = d["basfw"] | false;
  printToFile   = d["prfile"] | false;
  strncpy(myCall, d["mycall"] | "", sizeof(myCall)-1); myCall[sizeof(myCall)-1]=0;
  strncpy(opName,  d["opname"]  | "", sizeof(opName)-1);  opName[sizeof(opName)-1]=0;
  strncpy(opEmail, d["opemail"] | "", sizeof(opEmail)-1); opEmail[sizeof(opEmail)-1]=0;
  strncpy(qrzUser, d["qrzuser"] | "", sizeof(qrzUser)-1); qrzUser[sizeof(qrzUser)-1]=0;
  strncpy(qrzPass, d["qrzpass"] | "", sizeof(qrzPass)-1); qrzPass[sizeof(qrzPass)-1]=0;
  strncpy(stUser, d["stuser"] | "", sizeof(stUser)-1); stUser[sizeof(stUser)-1]=0;
  strncpy(stPass, d["stpass"] | "", sizeof(stPass)-1); stPass[sizeof(stPass)-1]=0;
  strncpy(clUrl,  d["clurl"]  | "", sizeof(clUrl)-1);  clUrl[sizeof(clUrl)-1]=0;
  strncpy(clKey,  d["clkey"]  | "", sizeof(clKey)-1);  clKey[sizeof(clKey)-1]=0;
  strncpy(clStation, d["clstation"] | "", sizeof(clStation)-1); clStation[sizeof(clStation)-1]=0;
  strncpy(lotwDxcc, d["lotwdxcc"] | "", sizeof(lotwDxcc)-1); lotwDxcc[sizeof(lotwDxcc)-1]=0;
  strncpy(lotwCqz,  d["lotwcqz"]  | "", sizeof(lotwCqz)-1);  lotwCqz[sizeof(lotwCqz)-1]=0;
  strncpy(lotwItuz, d["lotwituz"] | "", sizeof(lotwItuz)-1); lotwItuz[sizeof(lotwItuz)-1]=0;
  strncpy(lotwState, d["lotwstate"] | "", sizeof(lotwState)-1); lotwState[sizeof(lotwState)-1]=0;
  strncpy(lotwCnty,  d["lotwcnty"]  | "", sizeof(lotwCnty)-1);  lotwCnty[sizeof(lotwCnty)-1]=0;
  strncpy(lotwSubdiv, d["lotwsubdiv"] | "", sizeof(lotwSubdiv)-1); lotwSubdiv[sizeof(lotwSubdiv)-1]=0;
  strncpy(lotwSubdiv2, d["lotwsubdiv2"] | "", sizeof(lotwSubdiv2)-1); lotwSubdiv2[sizeof(lotwSubdiv2)-1]=0;
  strncpy(lotwIota,  d["lotwiota"]  | "", sizeof(lotwIota)-1);  lotwIota[sizeof(lotwIota)-1]=0;
  lat        = d["lat"] | 0.0;
  lon        = d["lon"] | 0.0;
  altM       = d["alt"] | 0.0;
  useGps     = d["gps"] | false;
  gpsSource  = d["gpssrc"] | (uint8_t)GPS_SRC_CAP1262;
  radioModel = d["rig"] | (uint8_t)RIG_IC9700;
  civAddr    = d["addr"]| (uint8_t)0xA2;
  civBaud    = d["baud"]| 19200u;
  civPinMode = d["civpin"] | (uint8_t)0;     // CI-V wiring: 0 TX/RX, 1 G2, 2 G1
  if (civPinMode > 2) civPinMode = 0;
  catType    = d["cattype"] | (uint8_t)CAT_WIRED;
  strncpy(catUsbKey, d["catusbkey"] | "", sizeof(catUsbKey)-1);
  catUsbKey[sizeof(catUsbKey)-1] = 0;
  consoleLog = d["conslog"] | false;
  // Validate against an EXPLICIT WHITELIST, never a range.
  //
  // This clamp has now silently discarded a saved transport TWICE, each time by
  // the same mechanism: it was written as `> <the last enumerator at the time>`,
  // and the next transport added to the enum inherited a bound that predates it.
  //   * It read `> CAT_RIGCTL` (2) when CAT_USB (4) was added: a saved USB config
  //     came back up driving the G1/G2 UART, and the USB path was never entered.
  //   * It read `> CAT_USB` (4) when CAT_DUAL (5) was added in 0.9.68: a saved
  //     native dual-radio config reverted to wired CI-V on every reboot, which
  //     could seize the Grove UART out from under a Grove GPS or rotator.
  // A range check silently couples this function to the enum's growth. A switch
  // does not: a new enumerator either appears here or the compiler's
  // -Wswitch warning flags it, and either way a wrong value lands in default.
  switch (catType) {
    case CAT_WIRED:
    case CAT_NET:
    case CAT_RIGCTL:
    case CAT_RIGCTL_GROVE:
    case CAT_DUAL:            // Grove/LAN legs need no USB support to be valid
      break;
#if CARDSAT_HAS_USBCAT
    case CAT_USB: break;
#else
    case CAT_USB:             // built without USB CAT: fall back to something usable
#endif
    default:
      catType = CAT_WIRED;
      break;
  }
  strncpy(catHost, d["cathost"] | "", sizeof(catHost)-1); catHost[sizeof(catHost)-1]=0;
  catPort    = d["catport"] | (uint16_t)50001;
  if (catPort == 0) catPort = 50001;
  // C2: Grove baud is its own field. Migration: if a pre-catgbaud config is loaded, the
  // key is absent -> default 115200 (the companion default) rather than inheriting the
  // uint16_t catPort as a baud. validate() further clamps to the supported UART set.
  catGroveBaud = d["catgbaud"] | (uint32_t)115200;
  strncpy(catUser, d["catuser"] | "", sizeof(catUser)-1); catUser[sizeof(catUser)-1]=0;
  // CardSatUsbHelper. Absent keys (any config written before 0.9.73) give the
  // defaults, which is "no device nominated at the default link rate" -- inert
  // until the operator selects the helper as a transport.
  helperBaud = d["helpbaud"] | (uint32_t)230400;
  strncpy(helperKey, d["helpkey"] | "", sizeof(helperKey)-1); helperKey[sizeof(helperKey)-1]=0;
  // Dual-rig legs (CAT_DUAL). Missing keys leave the "no legs assigned" defaults.
  for (int L = 0; L < 2; ++L) {
    const char* K = L ? "u" : "d";           // key suffix: d = downlink, u = uplink
    char k[16];
    snprintf(k, sizeof(k), "dl%smodel", K); dualModel[L] = d[k] | (uint8_t)LEG_NONE;
    if (dualModel[L] >= LEG_COUNT) dualModel[L] = LEG_NONE;
    snprintf(k, sizeof(k), "dl%sbus",  K);  dualBus[L]  = d[k] | (uint8_t)LEGBUS_GROVE;
    if (dualBus[L] >= LEGBUS_N) dualBus[L] = LEGBUS_GROVE;
    snprintf(k, sizeof(k), "dl%sciv",  K);  dualCiv[L]  = d[k] | (uint8_t)0;
    snprintf(k, sizeof(k), "dl%sbaud", K);  dualBaud[L] = d[k] | (uint32_t)0;
    snprintf(k, sizeof(k), "dl%shost", K);
    strncpy(dualHost[L], d[k] | "", sizeof(dualHost[L])-1); dualHost[L][sizeof(dualHost[L])-1]=0;
    snprintf(k, sizeof(k), "dl%sport", K);  dualPort[L] = d[k] | (uint16_t)50001;
    snprintf(k, sizeof(k), "dl%suser", K);
    strncpy(dualUser[L], d[k] | "", sizeof(dualUser[L])-1); dualUser[L][sizeof(dualUser[L])-1]=0;
    snprintf(k, sizeof(k), "dl%spass", K);
    strncpy(dualPass[L], d[k] | "", sizeof(dualPass[L])-1); dualPass[L][sizeof(dualPass[L])-1]=0;
  }
  // Catalog-revision check BEFORE the models are trusted: a saved index means a
  // different radio once LEG_RADIOS grows, so a stale file must not be believed.
  dualCatVer = d["dlcatver"] | (uint8_t)0;
  if (dualCatVer != LEG_CATALOG_VER) {
    dualModel[0] = dualModel[1] = LEG_NONE;   // re-pick rather than drive the wrong rig
    dualCatVer = LEG_CATALOG_VER;
  }
  strncpy(dualUsbKey[0], d["dlusbkeyd"] | "", sizeof(dualUsbKey[0])-1); dualUsbKey[0][sizeof(dualUsbKey[0])-1]=0;
  strncpy(dualUsbKey[1], d["dlusbkeyu"] | "", sizeof(dualUsbKey[1])-1); dualUsbKey[1][sizeof(dualUsbKey[1])-1]=0;
  // Legacy (0.9.68 development): a single "dlusbkey" served the then-single USB leg.
  // Migrate it onto whichever leg is on the USB bus (downlink wins a tie).
  if (!dualUsbKey[0][0] && !dualUsbKey[1][0]) {
    const char* legacy = d["dlusbkey"] | "";
    if (legacy[0]) {
      int L = (dualBus[0] == LEGBUS_USB) ? 0 : (dualBus[1] == LEGBUS_USB) ? 1 : 0;
      strncpy(dualUsbKey[L], legacy, sizeof(dualUsbKey[L])-1);
      dualUsbKey[L][sizeof(dualUsbKey[L])-1]=0;
    }
  }
  strncpy(catPass, d["catpass"] | "", sizeof(catPass)-1); catPass[sizeof(catPass)-1]=0;
  vfoType    = d["vfotype"] | (uint8_t)VFO_MAIN_UP_SUB_DOWN;
  rxOnlyVfo  = d["rxovfo"]  | (uint8_t)RXO_FOLLOW;
  if (rxOnlyVfo > RXO_SUB) rxOnlyVfo = RXO_FOLLOW;
  satMode    = d["satmode"] | false;
  if (vfoType > VFO_MAIN_DOWN_SUB_UP) vfoType = VFO_MAIN_UP_SUB_DOWN;
  catRateMs  = d["catms"] | 500u;
  if (catRateMs < 10) catRateMs = 10;
  catDelayMs = d["catdly"] | (uint16_t)70;
  if (catDelayMs > 200) catDelayMs = 200;
  doppThreshFmHz  = d["dpfm"]  | (uint16_t)300;
  doppThreshLinHz = d["dplin"] | (uint16_t)50;
  doppLeadMs      = d["dplead"]| (uint16_t)50;
  if (doppLeadMs > 100) doppLeadMs = 100;
  minPassEl  = d["minel"] | 5.0f;
  visPasses   = d["vispass"] | true;
  visSunElMax = (int8_t)(int)(d["vissun"] | -6);
  visMinEl    = d["visel"] | 10.0f;
  aosAlarm   = d["aosalarm"] | true;
  irBeacon   = d["irbeacon"] | false;
  beaconMHz  = d["beacon"] | 145.8;  if (beaconMHz < 0.1) beaconMHz = 145.8;
  solarAct   = d["solar"] | (uint8_t)SOLAR_MEAN;  if (solarAct > SOLAR_AUTO) solarAct = SOLAR_MEAN;
  wxUnits    = d["wxunits"] | (uint8_t)WX_IMPERIAL; if (wxUnits > WX_METRIC_MS) wxUnits = WX_IMPERIAL;
  // Independent unit fields (replaced the bundled wxUnits). When an old config lacks
  // them, migrate from the legacy bundle: IMPERIAL -> F/mph, METRIC -> C/kmh,
  // METRIC_MS -> C/ms; pressure had no legacy setting, so default by temperature
  // (F -> inHg, C -> hPa) to match what a US vs metric user would expect.
  { uint8_t mT = (wxUnits == WX_IMPERIAL) ? WXT_F : WXT_C;
    uint8_t mW = (wxUnits == WX_IMPERIAL) ? WXW_MPH : (wxUnits == WX_METRIC_MS ? WXW_MS : WXW_KMH);
    uint8_t mP = (wxUnits == WX_IMPERIAL) ? WXP_INHG : WXP_HPA;
    wxTemp = d["wxtemp"] | mT; if (wxTemp > WXT_F)  wxTemp = mT;
    wxWind = d["wxwind"] | mW; if (wxWind > WXW_MS)  wxWind = mW;
    wxPres = d["wxpres"] | mP; if (wxPres > WXP_INHG) wxPres = mP; }
  antUnits   = d["antunits"] | (uint8_t)0; if (antUnits > 1) antUnits = 0;
  dimSecs    = d["dimsecs"] | (uint16_t)120;
  bright     = d["bright"] | (uint8_t)180; if (bright < 10) bright = 10;
  spkVolume  = d["spkvol"] | (uint8_t)180;
  mapCenterLon = d["mapclon"] | (int16_t)0;
  mapNightShade = d["mapnight"] | true;
  if (mapCenterLon < -180) mapCenterLon = -180; if (mapCenterLon > 180) mapCenterLon = 180;
  tiltTune   = d["tilttune"] | false;
  gameTilt   = d["gametilt"] | false;   // games: IMU tilt steering (ADV-only)
  gameSound  = d["gamesnd"]  | false;   // games: sound effects
  morseSwap  = d["morseswap"]| false;   // Morse Meteors: swap dot/dash keys
  calDlHz    = d["caldl"] | 0;
  calUlHz    = d["calul"] | 0;
  xvtrDlHz   = d["xvtrdl"] | (freq_t)0;   // transverter LO offsets (0 = none)
  xvtrUlHz   = d["xvtrul"] | (freq_t)0;
  rotEnable  = d["roten"]  | false;
  rotType    = d["rottype"]| (uint8_t)ROT_GS232;
  // Bearing reference for the rotator. Was NEVER persisted: the operator could set
  // "magnetic" on the Settings row, it took effect, and it silently reverted to
  // true on the next boot -- leaving the rotor mispointed by the local magnetic
  // declination. Found by tools/audit_settings_persist.py, which exists for exactly
  // this class of "works until you power-cycle" defect.
  rotMagCorrect = d["rotmagc"] | false;
  // Clamp to the LAST defined type. The old bound was ROT_PST (2), written before
  // ROT_YAESU(3), ROT_EASYCOMM1..3(4-6), ROT_SPID(7) and ROT_NONE(8) existed -- so
  // every config using one of those was silently reset to GS-232 on load.
  if (rotType > ROT_NONE) rotType = ROT_GS232;
  // Migrate the legacy separate on/off flag into the type: the "Rotator: on/off" row
  // was replaced by a "None" entry in the type selector. A config saved with the
  // rotator disabled becomes ROT_NONE; otherwise rotEnable is derived from the type.
  if (!rotEnable) rotType = ROT_NONE;
  rotEnable = (rotType != ROT_NONE);
  rotTransport = d["rotxport"] | (uint8_t)ROT_XPORT_BRIDGE;
  for (int i = 0; i < 5; ++i) {                  // QTH presets q0n/q0a/q0o/q0h ..
    char k[6];
    snprintf(k, sizeof(k), "q%dn", i);
    const char* nm = d[k] | "";
    strncpy(qthName[i], nm, sizeof(qthName[i]) - 1); qthName[i][13] = 0;
    snprintf(k, sizeof(k), "q%da", i); qthLat[i] = d[k] | 0.0;
    snprintf(k, sizeof(k), "q%do", i); qthLon[i] = d[k] | 0.0;
    snprintf(k, sizeof(k), "q%dh", i); qthAlt[i] = d[k] | 0.0f;
  }
  if (rotTransport >= ROT_XPORT_N) rotTransport = ROT_XPORT_BRIDGE;
  strncpy(rotUsbKey, d["rotusbkey"] | "", sizeof(rotUsbKey)-1);
  rotUsbKey[sizeof(rotUsbKey)-1] = 0;
  strncpy(rotHost, d["rothost"] | "", sizeof(rotHost)-1); rotHost[sizeof(rotHost)-1]=0;
  rotPort    = d["rotport"]| (uint16_t)4533;
  if (rotPort == 0) rotPort = 4533;
  rotBaud    = d["rotbaud"]| 9600u;
  rotLeadSec = d["rotlead"]| (uint16_t)120;
  rotAzLookSec = d["rotazlk"] | (uint8_t)3;
  rotAzRange = d["rotazr"] | (uint8_t)ROT_AZ_360;
  if (rotAzRange > ROT_AZ_450) rotAzRange = ROT_AZ_360;
  rotAzOff   = d["rotaz"]  | (int16_t)0;
  rotElOff   = d["rotel"]  | (int16_t)0;
  rotDeadband= d["rotdb"]  | (uint8_t)3;
  rotParkAz  = d["rotpaz"] | (uint16_t)0;
  rotParkEl  = d["rotpel"] | (uint8_t)0;
  rotFlip    = d["rotflip"]| false;
  rotAzCnt0  = d["rotazc0"]| (int16_t)0;
  rotAzCntF  = d["rotazcf"]| (int16_t)0;
  rotElCnt0  = d["rotelc0"]| (int16_t)0;
  rotElCntF  = d["rotelcf"]| (int16_t)0;
  rigdEnable = d["rigden"] | false;
  rigdPort   = d["rigdport"] | (uint16_t)4532;
  if (rigdPort == 0) rigdPort = 4532;
  rotdEnable = d["rotden"] | false;
  rotdPort   = d["rotdport"] | (uint16_t)4533;
  webEnable  = d["weben"] | false;
  webPort    = d["webport"] | (uint16_t)80;
  loraEnable = d["loraen"] | false;
  loraRegion = d["lorargn"] | (uint8_t)0;
  loraFreqKHz= d["lorafk"] | (uint32_t)906875;
  loraSf     = d["lorasf"] | (uint8_t)12;
  loraBwHz   = d["lorabw"] | (uint32_t)125000;
  loraTxDbm  = d["loratx"] | (int8_t)20;
  lrxFreqKHz = d["lrxfk"]  | (uint32_t)433775;
  lrxSf      = d["lrxsf"]  | (uint8_t)12;
  lrxBwHz    = d["lrxbw"]  | (uint32_t)125000;
  lrxCr      = d["lrxcr"]  | (uint8_t)5;
  lrxSync    = d["lrxsw"]  | (uint8_t)0x12;
  lrxPreamble= d["lrxpre"] | (uint16_t)8;
  lrxCrc     = d["lrxcrc"] | (uint8_t)1;
  msgNotify  = d["msgntfy"] | (uint8_t)1;
  if (msgNotify > 2) msgNotify = 1;
  autoPosReply = d["autopos"] | false;
  aosLeadMin = d["aoslead"] | (uint8_t)0;
  { const uint8_t ok[] = {0,2,5,10,15}; bool v = false;
    for (uint8_t x : ok) if (aosLeadMin == x) v = true;
    if (!v) aosLeadMin = 0; }                 // snap a stray value back to a valid step
  amsatWindowH = d["amsatwin"] | (uint8_t)24;
  // Nearby & DX feeds. These were added to the struct and to the editors but never
  // to save()/load(), so every edit was discarded at reboot while appearing to work.
  aprsRangeKm = d["aprsrng"] | 150;
  strncpy(dxcUrl,  d["dxcurl"]  | "", sizeof(dxcUrl)-1);  dxcUrl[sizeof(dxcUrl)-1]=0;
  strncpy(adsbUrl, d["adsburl"] | "https://api.adsb.lol", sizeof(adsbUrl)-1); adsbUrl[sizeof(adsbUrl)-1]=0;
  adsbRangeKm = d["adsbrng"] | 50;
  { const uint8_t ok[] = {3,6,12,24,48,72}; bool v = false;
    for (uint8_t x : ok) if (amsatWindowH == x) v = true;
    if (!v) amsatWindowH = 24; }
  if (rotdPort == 0) rotdPort = 4533;
  if (radioModel >= RIG_COUNT) radioModel = RIG_IC9700;
#ifdef CARDSAT_CFG_DEBUG
  // Diagnostic: dump the LoRa group as read back, plus the raw JSON key presence,
  // so a serial log shows whether SF persisted, was dropped, or mis-read.
  Serial.printf("[cfg] load lora: en=%d rgn=%u fk=%lu sf=%u bw=%lu tx=%d  "
                "(json has lorasf=%d)\n",
                (int)loraEnable, loraRegion, (unsigned long)loraFreqKHz, loraSf,
                (unsigned long)loraBwHz, (int)loraTxDbm,
                (int)d["lorasf"].is<uint8_t>());
#endif
  validate();   // M17: clamp array-index enums + coordinates before anything uses them
  return true;
}

// Seed a legal amateur LoRa frequency + bandwidth for a region preset. SF and TX
// power are left as the operator set them; only the carrier and bandwidth move.
//   US (0): 33cm band 902-928 MHz, 906.875 MHz @ 125 kHz.
//   EU (1): 70cm band 430-440 MHz, 433.775 MHz @ 125 kHz (LoRa-APRS standard).
//   JP (2): 430 MHz band 430-440 MHz, 431.000 MHz @ 125 kHz.
void Settings::loraApplyRegion(uint8_t region) {
  loraRegion = region;
  switch (region) {
    case 1: loraFreqKHz = 433775; loraBwHz = 125000; break;   // EU
    case 2: loraFreqKHz = 431000; loraBwHz = 125000; break;   // JP
    case 0:
    default: loraRegion = 0; loraFreqKHz = 906875; loraBwHz = 125000; break; // US
  }
}

bool Settings::save() {
  validate();   // M17/M20: clamp any out-of-range edit before it is persisted, so the
                // in-memory state and the written config.json are both always valid.
  JsonDocument d;
  d["ssid"] = ssid;  d["pass"] = pass;
  d["ssid2"] = ssid2; d["pass2"] = pass2;
  d["gpurl"] = gpUrl;
  d["prhost"] = printerHost;
  d["prport"] = printerPort;
  d["prcols"] = printerCols;
  d["prfmt"] = printFormat;
  d["prtx"] = printTransport;
  d["prpr"] = printPaper;
  d["prser"] = printToSerial;
  d["basfw"] = basicFileWrite;
  d["prfile"] = printToFile;
  d["mycall"] = myCall;
  d["opname"] = opName;
  d["opemail"] = opEmail;
  d["qrzuser"] = qrzUser; d["qrzpass"] = qrzPass;
  d["stuser"] = stUser; d["stpass"] = stPass;
  d["clurl"] = clUrl; d["clkey"] = clKey; d["clstation"] = clStation;
  d["lotwdxcc"] = lotwDxcc; d["lotwcqz"] = lotwCqz; d["lotwituz"] = lotwItuz;
  d["lotwstate"] = lotwState; d["lotwcnty"] = lotwCnty;
  d["lotwsubdiv"] = lotwSubdiv; d["lotwsubdiv2"] = lotwSubdiv2; d["lotwiota"] = lotwIota;
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["gpssrc"] = gpsSource;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["civpin"] = civPinMode;
  d["cattype"] = catType; d["cathost"] = catHost; d["catport"] = catPort;
  d["catgbaud"] = catGroveBaud;
  d["catusbkey"] = catUsbKey;
  d["conslog"] = consoleLog;
  d["catuser"] = catUser; d["catpass"] = catPass;
  d["helpbaud"] = helperBaud; d["helpkey"] = helperKey;
  for (int L = 0; L < 2; ++L) {                      // dual-rig legs (CAT_DUAL)
    const char* K = L ? "u" : "d";
    char k[16];
    snprintf(k, sizeof(k), "dl%smodel", K); d[k] = dualModel[L];
    snprintf(k, sizeof(k), "dl%sbus",  K);  d[k] = dualBus[L];
    snprintf(k, sizeof(k), "dl%sciv",  K);  d[k] = dualCiv[L];
    snprintf(k, sizeof(k), "dl%sbaud", K);  d[k] = dualBaud[L];
    snprintf(k, sizeof(k), "dl%shost", K);  d[k] = dualHost[L];
    snprintf(k, sizeof(k), "dl%sport", K);  d[k] = dualPort[L];
    snprintf(k, sizeof(k), "dl%suser", K);  d[k] = dualUser[L];
    snprintf(k, sizeof(k), "dl%spass", K);  d[k] = dualPass[L];
  }
  d["dlcatver"] = dualCatVer;
  d["dlusbkeyd"] = dualUsbKey[0]; d["dlusbkeyu"] = dualUsbKey[1];
  d["vfotype"] = vfoType; d["satmode"] = satMode; d["catms"] = catRateMs;
  d["rxovfo"] = rxOnlyVfo;
  d["catdly"] = catDelayMs;
  d["dpfm"] = doppThreshFmHz; d["dplin"] = doppThreshLinHz; d["dplead"] = doppLeadMs;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  d["xvtrdl"]= xvtrDlHz; d["xvtrul"] = xvtrUlHz;
  d["vispass"] = visPasses; d["vissun"] = visSunElMax; d["visel"] = visMinEl;
  d["aosalarm"] = aosAlarm;
  d["irbeacon"] = irBeacon;
  d["beacon"] = beaconMHz;
  d["solar"] = solarAct;
  d["wxunits"] = wxUnits;   // still written so an older firmware can still read a bundle
  d["wxtemp"] = wxTemp;
  d["wxwind"] = wxWind;
  d["wxpres"] = wxPres;
  d["antunits"] = antUnits;
  d["dimsecs"] = dimSecs;
  d["bright"]  = bright;
  d["spkvol"]  = spkVolume;
  d["mapclon"] = mapCenterLon;
  d["mapnight"] = mapNightShade;
  d["tilttune"] = tiltTune;
  d["gametilt"] = gameTilt;
  d["gamesnd"]  = gameSound;
  d["morseswap"]= morseSwap;
  d["roten"]=rotEnable; d["rottype"]=rotType; d["rothost"]=rotHost;
  d["rotmagc"]=rotMagCorrect;
  d["rotxport"]=rotTransport; d["rotusbkey"]=rotUsbKey;
  for (int i = 0; i < 5; ++i) {
    char k[6];
    snprintf(k, sizeof(k), "q%dn", i); d[k] = qthName[i];
    snprintf(k, sizeof(k), "q%da", i); d[k] = qthLat[i];
    snprintf(k, sizeof(k), "q%do", i); d[k] = qthLon[i];
    snprintf(k, sizeof(k), "q%dh", i); d[k] = qthAlt[i];
  }
  d["rotport"]=rotPort; d["rotbaud"]=rotBaud; d["rotlead"]=rotLeadSec; d["rotazlk"]=rotAzLookSec; d["rotazr"]=rotAzRange; d["rotaz"]=rotAzOff;
  d["rotel"]=rotElOff; d["rotdb"]=rotDeadband; d["rotpaz"]=rotParkAz;
  d["rotpel"]=rotParkEl; d["rotflip"]=rotFlip;
  d["rotazc0"]=rotAzCnt0; d["rotazcf"]=rotAzCntF; d["rotelc0"]=rotElCnt0; d["rotelcf"]=rotElCntF;
  d["rigden"]=rigdEnable; d["rigdport"]=rigdPort;
  d["rotden"]=rotdEnable; d["rotdport"]=rotdPort;
  d["weben"]=webEnable; d["webport"]=webPort;
  d["loraen"]=loraEnable; d["lorargn"]=loraRegion; d["lorafk"]=loraFreqKHz; d["lorasf"]=loraSf;
  d["lorabw"]=loraBwHz; d["loratx"]=loraTxDbm;
  d["lrxfk"]=lrxFreqKHz; d["lrxsf"]=lrxSf; d["lrxbw"]=lrxBwHz; d["lrxcr"]=lrxCr;
  d["lrxsw"]=lrxSync; d["lrxpre"]=lrxPreamble; d["lrxcrc"]=lrxCrc;
  d["msgntfy"]=msgNotify;
  d["autopos"]=autoPosReply;
  d["aoslead"]=aosLeadMin;
  d["amsatwin"]=amsatWindowH;
  d["aprsrng"]=aprsRangeKm;
  d["dxcurl"]=dxcUrl; d["adsburl"]=adsbUrl; d["adsbrng"]=adsbRangeKm;
  // H19: transactional save. Serialize to a String first (config is a few KB), then commit
  // via the atomic replace helper: temp-write -> verify -> rotate live to backup -> promote.
  // A power loss or short write can no longer truncate config.json into malformed JSON --
  // the previous good config always survives. serializeJson returning 0 (nothing serialized)
  // is treated as a failure so an empty file can't overwrite a valid one.
  String out;
  size_t wrote = serializeJson(d, out);
#ifdef CARDSAT_CFG_DEBUG
  // Diagnostic: report the serialized size and the SF value committed, so a
  // partial/failed write (size 0 or short) or a wrong SF is visible on serial.
  Serial.printf("[cfg] save: serialized %u bytes for %s, sf=%u\n",
                (unsigned)wrote, FILE_CFG, loraSf);
#endif
  if (wrote == 0) return false;
  return Store::writeFileAtomic(FILE_CFG, (const uint8_t*)out.c_str(), out.length());
}
