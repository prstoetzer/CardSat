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
  CAT_RIGCTL = 2,  // rigctld (Hamlib NET rigctl) client: drive a remote rig over TCP
};

// Rotator transport: a directly-attached GS-232 controller, or a Hamlib
// rotctld server reached over TCP (CardSat is the client).
enum RotType : uint8_t {
  ROT_GS232 = 0,   // GS-232A/B via the SC16IS750 I2C->UART bridge (default)
  ROT_NET   = 1,   // rotctld (Hamlib "NET rotctl") over TCP
  ROT_PST   = 2,   // PstRotator UDP control
  ROT_YAESU = 3,   // Yaesu rotator wired directly via I2C ADC + output expander
  ROT_EASYCOMM1 = 4,  // Easycomm I (integer ASCII) via the I2C->UART bridge
  ROT_EASYCOMM2 = 5,  // Easycomm II (decimal ASCII) via the bridge
  ROT_EASYCOMM3 = 6,  // Easycomm III (II grammar + velocity) via the bridge
  ROT_SPID      = 7,  // SPID Rot2Prog (MD-01/02) binary via the bridge
};

// Azimuth-axis convention of the rotator (matches Gpredict's rotator setting).
enum RotAzRange : uint8_t {
  ROT_AZ_360 = 0,  // 0..360 deg, 0=North, 180=South (default)
  ROT_AZ_180 = 1,  // -180..+180 deg, centred on 0=North
  ROT_AZ_450 = 2,  // 0..450 deg, 90 deg overlap to avoid cable-wrap at North
};

// Assumed thermospheric activity level for the orbital-decay estimate. Density
// at 300-500 km swings ~an order of magnitude over the solar cycle, which is the
// single biggest driver of lifetime uncertainty; this lets the user bracket it.
enum SolarActivity : uint8_t {
  SOLAR_LOW  = 0,  // solar minimum (thin atmosphere -> longer life)
  SOLAR_MEAN = 1,  // cycle average (default)
  SOLAR_HIGH = 2,  // solar maximum (puffed-up atmosphere -> shorter life)
  SOLAR_AUTO = 3,  // derive density scale from the live F10.7 flux (fetched with GP)
};

// Units for the terrestrial Weather screen.
enum WxUnits {
  WX_IMPERIAL = 0,   // deg F, mph
  WX_METRIC   = 1,   // deg C, km/h
  WX_METRIC_MS = 2,  // deg C, m/s
};

struct Settings {
  // WiFi
  char     ssid[33] = "";
  char     pass[65] = "";
  char     ssid2[33] = "";    // optional 2nd network (field use: phone hotspot, etc.)
  char     pass2[65] = "";
  // Orbital data source (GP/OMM JSON). Editable in Settings.
  char     gpUrl[160] = AMSAT_GP_URL;
  char     myCall[14] = "";   // operator's own callsign (stored uppercase)
  // QRZ.com XML subscription credentials (for the callsign-lookup screen).
  char     qrzUser[24] = "";  // QRZ username
  char     qrzPass[32] = "";  // QRZ password
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
  // Doppler CAT write deadband + predictive lead (tunable; see app.h DOPP_* defaults)
  uint16_t doppThreshFmHz  = 300; // FM leg write deadband (Hz)
  uint16_t doppThreshLinHz = 50;  // linear SSB/CW leg write deadband (Hz)
  uint16_t doppLeadMs      = 50;  // predictive-lead cap (ms); 0 = lead off
  // Tracking
  float    minPassEl  = 5.0f;
  bool     aosAlarm   = true;   // beep + flash before a favorite's AOS
  bool     irBeacon   = false;  // also flash the IR LED on each pass alert
                                // (distinct flash count per event; user-built RX)
  double   beaconMHz  = 145.800; // Doppler-page reference freq (orbital analysis)
  uint8_t  solarAct   = SOLAR_MEAN; // assumed solar activity for the decay estimate
  uint8_t  wxUnits    = WX_IMPERIAL; // units for the terrestrial Weather screen
  int16_t  mapCenterLon = 0;     // world-map center longitude (deg); 0 = classic
                                 // 0-degree-centered view, else recenter on QTH
  // Display / power
  uint8_t  bright     = 180;    // active screen brightness (10..255)
  bool     tiltTune   = false;  // accelerometer (tilt) passband tuning, ADV-only
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
  uint8_t  rotAzLookSec = 3;     // 450-overlap az lookahead horizon (s; 0 = off)
  uint8_t  rotAzRange  = ROT_AZ_360; // 0..360 or -180..+180 azimuth axis
  int16_t  rotAzOff    = 0;      // deg added to commanded azimuth (alignment)
  int16_t  rotElOff    = 0;      // deg added to commanded elevation
  uint8_t  rotDeadband = 3;      // deg; suppress smaller moves (anti-chatter)
  uint16_t rotParkAz   = 0;      // park azimuth on LOS / when disabled
  uint8_t  rotParkEl   = 0;      // park elevation
  bool     rotFlip     = false;  // flip mode (450 az + 0-180 el) for overhead passes
  // Yaesu direct (ROT_YAESU) calibration: raw ADC counts at each axis endpoint.
  int16_t  rotAzCnt0   = 0;      // counts at azimuth 0 deg
  int16_t  rotAzCntF   = 0;      // counts at azimuth full-scale (per rotAzRange)
  int16_t  rotElCnt0   = 0;      // counts at elevation 0 deg
  int16_t  rotElCntF   = 0;      // counts at elevation 180 deg

  // rigctld server: CardSat runs a Hamlib NET rigctl (rigctld) TCP server so a
  // PC (Gpredict, WSJT-X via Hamlib NET rigctl, a logger...) can drive the
  // wired/LAN rig through CardSat. VFOA = downlink (Sub/RX), VFOB = uplink (Main/TX).
  bool     rigdEnable  = false;
  uint16_t rigdPort    = 4532;   // Hamlib rigctld default port

  // rotctld server: CardSat runs a Hamlib NET rotctl (rotctld) TCP server so a
  // networked PC (Gpredict, ...) can drive the GS-232 rotator wired to CardSat.
  bool     rotdEnable  = false;
  uint16_t rotdPort    = 4533;   // Hamlib rotctld default port

  // Built-in mobile web control page, served over the WiFi LAN. Opt-in: when on,
  // a phone on the same network can select a satellite, see pass times, and drive
  // the radio/rotator. Plain HTTP on the LAN only (no auth); leave off if you
  // don't want it exposed.
  bool     webEnable   = false;
  uint16_t webPort     = 80;

  // LoRa text messaging (CardSat-to-CardSat broadcast). Uses the Cap LoRa SX1262.
  bool     loraEnable  = false;     // bring the radio up at boot
  // Region presets pick a legal amateur LoRa frequency/bandwidth for the operator:
  //   0 = US  : 33cm amateur band (902-928 MHz). 70cm in the US is held to 100 kHz
  //             occupied bandwidth, so 33cm is the home for 125 kHz LoRa. Default
  //             906.875 MHz, clear of the busy 915 MHz ISM centre.
  //   1 = EU  : 70cm amateur band (430-440 MHz). LoRa-APRS standard 433.775 MHz.
  //   2 = JP  : 430 MHz amateur band (430-440 MHz). Japan's 920 MHz band is ISM,
  //             not amateur, so amateur LoRa belongs on 430 MHz. Default 431.000.
  // The operator can still set any frequency 150-960 MHz by hand after choosing a
  // region; the region just seeds sensible, legal defaults.
  uint8_t  loraRegion  = 0;         // 0 = US (default), 1 = EU, 2 = JP
  uint32_t loraFreqKHz = 906875;    // carrier in kHz (US 33cm default, 906.875 MHz)
  uint8_t  loraSf      = 12;        // spreading factor 7..12 (12 = max range)
  uint32_t loraBwHz    = 125000;    // bandwidth in Hz (125 kHz standard)
  int8_t   loraTxDbm   = 20;        // TX power dBm (<=22 on SX1262)
  uint8_t  msgNotify   = 1;         // LoRa msg alert: 0=off, 1=banner, 2=banner+beep
  void loraApplyRegion(uint8_t region);   // seed freq/BW from a region preset

  bool load();
  bool save();
};
