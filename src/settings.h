#pragma once
// ===========================================================================
//  settings.h  -  persistent user configuration (LittleFS JSON)
// ===========================================================================
#include <Arduino.h>
#include "radio_profiles.h"
#include "config.h"

// Which physical VFO (Main/Sub) carries the uplink vs the downlink.
enum VfoType : uint8_t {
  VFO_MAIN_UP_SUB_DOWN = 0,   // uplink on MAIN, downlink on SUB (default)
  VFO_MAIN_DOWN_SUB_UP = 1,   // uplink on SUB,  downlink on MAIN
};

struct Settings {
  // WiFi
  char     ssid[33] = "";
  char     pass[65] = "";
  // Orbital data source (GP/OMM JSON). Editable in Settings.
  char     gpUrl[160] = AMSAT_GP_URL;
  // Location
  double   lat = 0.0, lon = 0.0, altM = 0.0;
  bool     useGps = false;
  uint8_t  gpsSource = GPS_SRC_CAP1262;  // GpsSource: Grove / Cap868 / Cap1262
  // Radio
  uint8_t  radioModel = RIG_IC9700;
  uint8_t  civAddr    = 0xA2;   // 0 => use model default
  uint32_t civBaud    = 19200;
  // (CI-V is TTL serial only.) VFO roles + whether to command the rig's own
  // satellite mode when engaging radio control.
  uint8_t  vfoType    = VFO_MAIN_UP_SUB_DOWN;
  bool     satMode    = false;
  uint32_t catRateMs  = 500;   // CAT/Doppler update period (ms), adjustable in 10 ms steps
  // Tracking
  float    minPassEl  = 5.0f;
  bool     aosAlarm   = true;   // beep + flash before a favorite's AOS
  // Display / power
  uint16_t dimSecs    = 120;    // blank the backlight after this idle time (s); 0 = never
  // Calibration (persisted oscillator offsets, Hz)
  int32_t  calDlHz = 0;
  int32_t  calUlHz = 0;
  // Rotator (GS-232 az/el over an I2C->UART bridge)
  bool     rotEnable   = false;
  uint32_t rotBaud     = 9600;   // GS-232 serial (commonly 9600)
  int16_t  rotAzOff    = 0;      // deg added to commanded azimuth (alignment)
  int16_t  rotElOff    = 0;      // deg added to commanded elevation
  uint8_t  rotDeadband = 3;      // deg; suppress smaller moves (anti-chatter)
  uint16_t rotParkAz   = 0;      // park azimuth on LOS / when disabled
  uint8_t  rotParkEl   = 0;      // park elevation
  bool     rotFlip     = false;  // flip mode (450 az + 0-180 el) for overhead passes

  bool load();
  bool save();
};
