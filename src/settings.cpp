// ===========================================================================
//  settings.cpp
// ===========================================================================
#include "settings.h"
#include "config.h"
#include <LittleFS.h>
#include "storage.h"
#include <ArduinoJson.h>

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

  strncpy(ssid, d["ssid"] | "", sizeof(ssid)-1);
  strncpy(pass, d["pass"] | "", sizeof(pass)-1);
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
  // Bounds-check against the LAST enumerator, not a hardcoded one. This read
  // `> CAT_RIGCTL` (2), which silently reset a saved CAT_USB (3) to CAT_WIRED on
  // every load: the setting survived in RAM until reboot, then reverted -- so a
  // build with USB CAT selected came back up driving the G1/G2 UART instead, and
  // the USB path was never entered at all. The clamp was written before CAT_USB
  // existed and was never revisited when it was added.
  //
  // When USB CAT is compiled out, a config that selects it must still fall back to
  // something usable rather than leave an unhandled value.
#if CARDSAT_HAS_USBCAT
  if (catType > CAT_USB) catType = CAT_WIRED;
#else
  if (catType > CAT_RIGCTL) catType = CAT_WIRED;   // CAT_USB not built: fall back
#endif
  strncpy(catHost, d["cathost"] | "", sizeof(catHost)-1); catHost[sizeof(catHost)-1]=0;
  catPort    = d["catport"] | (uint16_t)50001;
  if (catPort == 0) catPort = 50001;
  strncpy(catUser, d["catuser"] | "", sizeof(catUser)-1); catUser[sizeof(catUser)-1]=0;
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
  rotEnable  = d["roten"]  | false;
  rotType    = d["rottype"]| (uint8_t)ROT_GS232;
  // Clamp to the LAST defined type, not ROT_PST. The old bound was ROT_PST (2),
  // written before ROT_YAESU(3), ROT_EASYCOMM1..3(4-6) and ROT_SPID(7) existed --
  // so every config using one of those was silently reset to GS-232 on load, and
  // the Settings screen would show GS-232 no matter what the user picked.
  if (rotType > ROT_SPID) rotType = ROT_GS232;
  rotTransport = d["rotxport"] | (uint8_t)ROT_XPORT_BRIDGE;
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
  d["clurl"] = clUrl; d["clkey"] = clKey; d["clstation"] = clStation;
  d["lotwdxcc"] = lotwDxcc; d["lotwcqz"] = lotwCqz; d["lotwituz"] = lotwItuz;
  d["lotwstate"] = lotwState; d["lotwcnty"] = lotwCnty;
  d["lotwsubdiv"] = lotwSubdiv; d["lotwsubdiv2"] = lotwSubdiv2; d["lotwiota"] = lotwIota;
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["gpssrc"] = gpsSource;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["civpin"] = civPinMode;
  d["cattype"] = catType; d["cathost"] = catHost; d["catport"] = catPort;
  d["catusbkey"] = catUsbKey;
  d["conslog"] = consoleLog;
  d["catuser"] = catUser; d["catpass"] = catPass;
  d["vfotype"] = vfoType; d["satmode"] = satMode; d["catms"] = catRateMs;
  d["rxovfo"] = rxOnlyVfo;
  d["catdly"] = catDelayMs;
  d["dpfm"] = doppThreshFmHz; d["dplin"] = doppThreshLinHz; d["dplead"] = doppLeadMs;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  d["vispass"] = visPasses; d["vissun"] = visSunElMax; d["visel"] = visMinEl;
  d["aosalarm"] = aosAlarm;
  d["irbeacon"] = irBeacon;
  d["beacon"] = beaconMHz;
  d["solar"] = solarAct;
  d["wxunits"] = wxUnits;
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
  d["rotxport"]=rotTransport; d["rotusbkey"]=rotUsbKey;
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
  File f = Store::fs().open(FILE_CFG, "w");
  if (!f) return false;
  size_t wrote = serializeJson(d, f);
  f.close();
#ifdef CARDSAT_CFG_DEBUG
  // Diagnostic: report the serialized size and the SF value committed, so a
  // partial/failed write (size 0 or short) or a wrong SF is visible on serial.
  Serial.printf("[cfg] save: wrote %u bytes to %s, sf=%u\n",
                (unsigned)wrote, FILE_CFG, loraSf);
#endif
  return wrote > 0;
}
