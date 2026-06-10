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
  if (!f) return false;
  JsonDocument d;
  if (deserializeJson(d, f)) { f.close(); return false; }
  f.close();

  strncpy(ssid, d["ssid"] | "", sizeof(ssid)-1);
  strncpy(pass, d["pass"] | "", sizeof(pass)-1);
  strncpy(gpUrl, d["gpurl"] | AMSAT_GP_URL, sizeof(gpUrl)-1); gpUrl[sizeof(gpUrl)-1]=0;
  strncpy(myCall, d["mycall"] | "", sizeof(myCall)-1); myCall[sizeof(myCall)-1]=0;
  lat        = d["lat"] | 0.0;
  lon        = d["lon"] | 0.0;
  altM       = d["alt"] | 0.0;
  useGps     = d["gps"] | false;
  gpsSource  = d["gpssrc"] | (uint8_t)GPS_SRC_CAP1262;
  radioModel = d["rig"] | (uint8_t)RIG_IC9700;
  civAddr    = d["addr"]| (uint8_t)0xA2;
  civBaud    = d["baud"]| 19200u;
  vfoType    = d["vfotype"] | (uint8_t)VFO_MAIN_UP_SUB_DOWN;
  satMode    = d["satmode"] | false;
  if (vfoType > VFO_MAIN_DOWN_SUB_UP) vfoType = VFO_MAIN_UP_SUB_DOWN;
  catRateMs  = d["catms"] | 500u;
  if (catRateMs < 10) catRateMs = 10;
  catDelayMs = d["catdly"] | (uint16_t)70;
  if (catDelayMs > 200) catDelayMs = 200;
  minPassEl  = d["minel"] | 5.0f;
  aosAlarm   = d["aosalarm"] | true;
  dimSecs    = d["dimsecs"] | (uint16_t)120;
  calDlHz    = d["caldl"] | 0;
  calUlHz    = d["calul"] | 0;
  rotEnable  = d["roten"]  | false;
  rotType    = d["rottype"]| (uint8_t)ROT_GS232;
  if (rotType > ROT_NET) rotType = ROT_GS232;
  strncpy(rotHost, d["rothost"] | "", sizeof(rotHost)-1); rotHost[sizeof(rotHost)-1]=0;
  rotPort    = d["rotport"]| (uint16_t)4533;
  if (rotPort == 0) rotPort = 4533;
  rotBaud    = d["rotbaud"]| 9600u;
  rotAzOff   = d["rotaz"]  | (int16_t)0;
  rotElOff   = d["rotel"]  | (int16_t)0;
  rotDeadband= d["rotdb"]  | (uint8_t)3;
  rotParkAz  = d["rotpaz"] | (uint16_t)0;
  rotParkEl  = d["rotpel"] | (uint8_t)0;
  rotFlip    = d["rotflip"]| false;
  if (radioModel >= RIG_COUNT) radioModel = RIG_IC9700;
  return true;
}

bool Settings::save() {
  JsonDocument d;
  d["ssid"] = ssid;  d["pass"] = pass;
  d["gpurl"] = gpUrl;
  d["mycall"] = myCall;
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["gpssrc"] = gpsSource;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["vfotype"] = vfoType; d["satmode"] = satMode; d["catms"] = catRateMs;
  d["catdly"] = catDelayMs;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  d["aosalarm"] = aosAlarm;
  d["dimsecs"] = dimSecs;
  d["roten"]=rotEnable; d["rottype"]=rotType; d["rothost"]=rotHost;
  d["rotport"]=rotPort; d["rotbaud"]=rotBaud; d["rotaz"]=rotAzOff;
  d["rotel"]=rotElOff; d["rotdb"]=rotDeadband; d["rotpaz"]=rotParkAz;
  d["rotpel"]=rotParkEl; d["rotflip"]=rotFlip;
  File f = Store::fs().open(FILE_CFG, "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  return true;
}
