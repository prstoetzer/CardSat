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
  lat        = d["lat"] | 0.0;
  lon        = d["lon"] | 0.0;
  altM       = d["alt"] | 0.0;
  useGps     = d["gps"] | false;
  gpsSource  = d["gpssrc"] | (uint8_t)GPS_SRC_CAP1262;
  radioModel = d["rig"] | (uint8_t)RIG_IC9700;
  civAddr    = d["addr"]| (uint8_t)0xA2;
  civBaud    = d["baud"]| 19200u;
  minPassEl  = d["minel"] | 5.0f;
  aosAlarm   = d["aosalarm"] | true;
  calDlHz    = d["caldl"] | 0;
  calUlHz    = d["calul"] | 0;
  rotEnable  = d["roten"]  | false;
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
  d["lat"]  = lat;   d["lon"]  = lon;  d["alt"] = altM;  d["gps"] = useGps;
  d["gpssrc"] = gpsSource;
  d["rig"]  = radioModel; d["addr"] = civAddr; d["baud"] = civBaud;
  d["minel"]= minPassEl;  d["caldl"]= calDlHz; d["calul"] = calUlHz;
  d["aosalarm"] = aosAlarm;
  d["roten"]=rotEnable; d["rotbaud"]=rotBaud; d["rotaz"]=rotAzOff;
  d["rotel"]=rotElOff; d["rotdb"]=rotDeadband; d["rotpaz"]=rotParkAz;
  d["rotpel"]=rotParkEl; d["rotflip"]=rotFlip;
  File f = Store::fs().open(FILE_CFG, "w");
  if (!f) return false;
  serializeJson(d, f);
  f.close();
  return true;
}
