// ===========================================================================
//  main.cpp  -  entry point for the Cardputer ADV satellite tracker
//
//  All hardware bring-up (M5Cardputer.begin, display, keyboard, LittleFS,
//  config load, radio + GPS init) happens inside App::setup(). This file is
//  intentionally a thin shell so the whole program lives in the App class.
// ===========================================================================
#include <Arduino.h>
#include "app.h"

// The default Arduino loop task gets an 8 KB stack. That is too tight for the
// deepest call chain in the app: a BASIC program that calls SATSEL runs the whole
// interpreter inside the key handler (loop -> handleKey -> keyBasic -> basicRun ->
// run -> execLine -> basHookSatsel -> Predictor::lookFor -> temeStateAt) and only
// THEN enters SGP4 (Sgp4::init -> twoline2rv -> sgp4init -> sgp4), whose functions
// each carry very large floating-point frames. That combined depth overflowed the
// 8 KB stack and hard-faulted (exc_cause on ESP32-S3). Double it to 16 KB for solid
// headroom; the cost is ~8 KB of DRAM for the loop task. (Ordinary tracking reaches
// SGP4 from a much shallower stack and was always fine.)
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

static App app;

void setup() {
  app.setup();
}

void loop() {
  app.loop();
}
