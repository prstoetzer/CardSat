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

// CAT transport for the (Icom) radio: the wired CI-V UART, or the RS-BA1 LAN
// (UDP) protocol straight to the radio's network port (no PC/rigctld bridge).
enum CatType : uint8_t {
  CAT_WIRED = 0,   // CI-V over the TTL UART (default)
  CAT_NET   = 1,   // Icom LAN (RS-BA1 UDP): control 50001 + serial 50002
};

// Rotator transport: a directly-attached GS-232 controller, or a Hamlib
// rotctld server reached over TCP (CardSat is the client).
enum RotType : uint8_t {
  ROT_GS232 = 0,   // GS-232A/B via the SC16IS750 I2C->UART bridge (default)
  ROT_NET   = 1,   // rotctld (Hamlib "NET rotctl") over TCP
  ROT_PST   = 2,   // PstRotator UDP control
};

// Azimuth-axis convention of the rotator (matches Gpredict's rotator setting).
enum RotAzRange : uint8_t {
  ROT_AZ_360 = 0,  // 0..360 deg, 0=North, 180=South (default)
  ROT_AZ_180 = 1,  // -180..+180 deg, centred on 0=North
  ROT_AZ_450 = 2,  // 0..450 deg, 90 deg overlap to avoid cable-wrap at North
};

struct Settings {
  // WiFi
  char     ssid[33] = "";
  char     pass[65] = "";
  // Orbital data source (GP/OMM JSON). Editable in Settings.
  char     gpUrl[160] = AMSAT_GP_URL;
  char     myCall[14] = "";   // operator's own callsign (stored uppercase)
  // Location
  double   lat = 0.0, lon = 0.0, altM = 0.0;
  bool     useGps = false;
  uint8_t  gpsSource = GPS_SRC_CAP1262;  // GpsSource: Grove / Cap868 / Cap1262
  // Radio
  uint8_t  radioModel = RIG_IC9700;
  uint8_t  civAddr    = 0xA2;   // 0 => use model default
  uint32_t civBaud    = 19200;
  // CAT transport. CAT_NET drives the radio over the RS-BA1 LAN protocol using
  // the host/port/credentials below instead of the wired CI-V UART.
  uint8_t  catType    = CAT_WIRED;
  char     catHost[40] = "";    // radio IP / hostname (catType = CAT_NET)
  uint16_t catPort     = 50001; // RS-BA1 control port (serial = +1, audio = +2)
  char     catUser[24] = "";    // radio Network User1 id
  char     catPass[24] = "";    // radio Network User1 password
  // (CI-V is TTL serial only.) VFO roles + whether to command the rig's own
  // satellite mode when engaging radio control.
  uint8_t  vfoType    = VFO_MAIN_UP_SUB_DOWN;
  bool     satMode    = false;
  uint32_t catRateMs  = 500;   // CAT/Doppler update period (ms), adjustable in 10 ms steps
  uint16_t catDelayMs = 70;    // pause after each CAT command before the next (ms)
  // Tracking
  float    minPassEl  = 5.0f;
  bool     aosAlarm   = true;   // beep + flash before a favorite's AOS
  // Display / power
  uint16_t dimSecs    = 120;    // blank the backlight after this idle time (s); 0 = never
  // Calibration (persisted oscillator offsets, Hz)
  int32_t  calDlHz = 0;
  int32_t  calUlHz = 0;
  // Rotator (GS-232 over an I2C->UART bridge, or rotctld over TCP)
  bool     rotEnable   = false;
  uint8_t  rotType     = ROT_GS232;  // GS-232 (bridge) or rotctld (network)
  char     rotHost[40] = "";         // rotctld server host/IP (rotType=ROT_NET)
  uint16_t rotPort     = 4533;       // rotctld TCP port (Hamlib default 4533)
  uint32_t rotBaud     = 9600;   // GS-232 serial (commonly 9600)
  uint16_t rotLeadSec  = 120;    // pre-position lead before AOS (s; 0 = off)
  uint8_t  rotAzRange  = ROT_AZ_360; // 0..360 or -180..+180 azimuth axis
  int16_t  rotAzOff    = 0;      // deg added to commanded azimuth (alignment)
  int16_t  rotElOff    = 0;      // deg added to commanded elevation
  uint8_t  rotDeadband = 3;      // deg; suppress smaller moves (anti-chatter)
  uint16_t rotParkAz   = 0;      // park azimuth on LOS / when disabled
  uint8_t  rotParkEl   = 0;      // park elevation
  bool     rotFlip     = false;  // flip mode (450 az + 0-180 el) for overhead passes

  bool load();
  bool save();
};
