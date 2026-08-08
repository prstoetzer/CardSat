#pragma once
// ===========================================================================
//  rig.h  --  HOST-TEST SHIM. Not shipped, not compiled into firmware.
// ===========================================================================
//  src/usbhelper.cpp pulls in rig.h for exactly one symbol: civUartOpen(), the
//  single place the Grove UART is configured. Everything else in the real rig.h
//  is irrelevant to the link layer, and dragging it in would make this test
//  depend on the CAT backends it is specifically meant to be independent of.
// ===========================================================================
#include <Arduino.h>

HardwareSerial& civUartOpen(uint8_t pinMode, uint32_t baud, int uartNum,
                            int rxPin, int txPin);
