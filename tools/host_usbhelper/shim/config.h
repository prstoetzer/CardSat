#pragma once
// ===========================================================================
//  config.h  --  HOST-TEST SHIM. Not shipped, not compiled into firmware.
// ===========================================================================
//  Stands in for src/config.h when building src/usbhelper.cpp on a host. Only
//  the handful of constants that file actually reads are here.
//
//  These MUST track the real header. tools/check_csuh_parity.py compares them,
//  so a Grove pin that moves in config.h and not here is caught by a gate rather
//  than by a test that passes against pins the firmware does not use.
// ===========================================================================
#include <Arduino.h>

#define CARDSAT_HAS_USBHELPER 1

static constexpr int CIV_UART_NUM = 1;     // CI-V / helper own UART1 on G1/G2
static constexpr int CIV_RX_PIN   = 1;     // G1
static constexpr int CIV_TX_PIN   = 2;     // G2
