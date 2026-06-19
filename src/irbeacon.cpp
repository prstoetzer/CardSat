// ===========================================================================
//  irbeacon.cpp  -  silent IR pass-alert beacon (built-in IR LED, GPIO 44)
// ===========================================================================
#include "irbeacon.h"
#include "config.h"

// LEDC (ESP32 hardware PWM) generates the 38 kHz carrier on the IR LED pin. We
// gate it on for IR_BURST_MS, off for IR_GAP_MS, `count` times -- a burst group.
// A standard IR receiver/demodulator (38 kHz, e.g. TSOP38238) sees each carrier-on
// window as one detected pulse, so the receiver can count the pulses per group and
// map the count back to the alert event.
static constexpr int      IR_LEDC_CH   = 7;       // a high LEDC channel, unlikely to clash
static constexpr uint8_t  IR_LEDC_BITS = 8;       // 8-bit duty (0..255)
static constexpr uint8_t  IR_DUTY_ON   = 96;      // ~38% duty -- typical IR drive

void IrBeacon::begin() {
#if defined(ARDUINO_ARCH_ESP32)
  // Configure the LEDC timer/channel for the carrier and park it off.
  ledcSetup(IR_LEDC_CH, IR_CARRIER_HZ, IR_LEDC_BITS);
  ledcAttachPin(IR_LED_PIN, IR_LEDC_CH);
  ledcWrite(IR_LEDC_CH, 0);                        // carrier off (LED dark)
  _ok = true;
#else
  _ok = false;
#endif
  _state = IDLE; _left = 0; _stepMs = 0;
}

void IrBeacon::carrier(bool on) {
#if defined(ARDUINO_ARCH_ESP32)
  ledcWrite(IR_LEDC_CH, on ? IR_DUTY_ON : 0);
#else
  (void)on;
#endif
}

// Queue a group of `count` bursts. If a group is already playing we just replace
// the remaining count -- alerts are spaced seconds apart, so groups never overlap
// in practice, but this keeps a late call from stacking oddly.
void IrBeacon::flash(uint8_t count) {
  if (!_ok || count == 0) return;
  _left   = count;
  _state  = ON;
  carrier(true);
  _stepMs = millis() + IR_BURST_MS;
}

// One cooperative step. Called every loop tick; never blocks. Advances the ON ->
// GAP -> (next burst) sequence by time, decrementing the remaining count, and
// returns to IDLE when the group is done.
void IrBeacon::service() {
  if (_state == IDLE) return;
  if ((int32_t)(millis() - _stepMs) < 0) return;   // current step still running

  if (_state == ON) {
    carrier(false);                                // end this burst
    if (--_left == 0) { _state = IDLE; return; }   // group complete
    _state  = GAP;                                 // inter-burst gap
    _stepMs = millis() + IR_GAP_MS;
  } else { // GAP
    carrier(true);                                 // start the next burst
    _state  = ON;
    _stepMs = millis() + IR_BURST_MS;
  }
}
