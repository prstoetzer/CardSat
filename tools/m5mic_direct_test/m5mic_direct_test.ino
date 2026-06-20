// Test M5Unified's OWN Mic_Class (M5.Mic) directly — NOT M5Cardputer.Mic, NOT a
// custom driver. The factory demo records fine and is built on M5Unified, so
// M5's Mic_Class CAN drive the ADV ES8311 mic. M5's mic_task does the manual
// i2s_ll clock-divider programming internally, so if M5.Mic runs we get that
// clock machinery for free.
//
// The mic-enable callback (_microphone_enabled_cb_cardputer_adv) is only wired
// up when M5Unified::begin() runs with cfg.internal_mic = true. So we use plain
// M5.begin(cfg) with internal_mic explicitly set, and M5.Mic — not the
// Cardputer wrapper, which may configure begin() differently.
//
// Build on IDF 5.4.x. Open Serial @115200, talk during capture, read peak.

#include <M5Unified.h>

void setup() {
  auto cfg = M5.config();
  cfg.internal_mic = true;     // ensure the ADV mic-enable callback is wired
  cfg.internal_spk = true;     // speaker shares the codec; let M5 manage it
  cfg.serial_baudrate = 115200;
  M5.begin(cfg);
  delay(500);
  Serial.printf("SDK: %s  (need 5.4.x)\n", ESP.getSdkVersion());
  Serial.printf("board: %d\n", (int)M5.getBoard());

  // Free the speaker so the mic owns the shared I2S/codec, the same ordering
  // the working memo path uses.
  M5.Speaker.end();
  delay(50);

  // Configure + start M5's own mic.
  auto mc = M5.Mic.config();
  mc.sample_rate = 16000;
  mc.magnification = 16;       // M5's internal gain (default)
  M5.Mic.config(mc);

  bool ok = M5.Mic.begin();
  Serial.printf("M5.Mic.begin: %d   isEnabled: %d\n", ok, M5.Mic.isEnabled());
  if (!ok) { Serial.println("M5.Mic.begin FAILED"); return; }

  Serial.println("\n>>> TALK or TAP THE MIC for ~3 seconds <<<\n");

  static int16_t buf[1024];
  int32_t peak = 0;
  size_t total = 0;
  uint32_t t0 = millis();
  while (millis() - t0 < 3000) {
    if (M5.Mic.record(buf, 1024, 16000)) {
      while (M5.Mic.isRecording()) M5.delay(1);
      for (int i = 0; i < 1024; i++) { int v = abs(buf[i]); if (v > peak) peak = v; }
      total += 1024;
    }
  }

  Serial.printf("RAW MIC PEAK: %ld  (samples: %u)\n", peak, (unsigned)total);
  Serial.print("first 16 samples: ");
  for (int i = 0; i < 16; i++) Serial.printf("%d ", buf[i]);
  Serial.println();
  if (peak == 0)       Serial.println("*** silent ***");
  else if (peak < 50)  Serial.println("*** faint/constant -- still not real audio ***");
  else                 Serial.println("*** SUCCESS: M5.Mic records real audio! ***");
}

void loop() {}
