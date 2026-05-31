// ===========================================================================
//  main.cpp  -  entry point for the Cardputer ADV satellite tracker
//
//  All hardware bring-up (M5Cardputer.begin, display, keyboard, LittleFS,
//  config load, radio + GPS init) happens inside App::setup(). This file is
//  intentionally a thin shell so the whole program lives in the App class.
// ===========================================================================
#include <Arduino.h>
#include "app.h"

static App app;

void setup() {
  app.setup();
}

void loop() {
  app.loop();
}
