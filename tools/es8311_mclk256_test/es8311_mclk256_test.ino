// ES8311 ADV mic test — MCLK routed via GPIO matrix at mclk_multiple=256.
//
// This is the ONE configuration the esp-idf#18621 reporter documented as the
// WORKING ADV mic clocking: "MCLK: routed via GPIO matrix ... mclk_multiple=256
// x fs". Every prior CardSat driver attempt left MCLK unrouted (I2S_GPIO_UNUSED)
// because the schematic appeared to have no MCLK net. This test routes a real
// MCLK pin (GPIO0) at multiple 256 and clocks the codec as MCLK-slave.
//
// Build on IDF 5.4.x (arduino-esp32 / M5Stack core 3.2.x). Open Serial @115200,
// talk/tap during the capture, read "RAW MIC PEAK". Non-zero => this is the fix.

#include <M5Cardputer.h>
#include <driver/i2s_std.h>
#include <driver/i2s_common.h>

// ADV ES8311 audio bus (official pinmap)
static const int PIN_MCLK = 0;    // route MCLK here via GPIO matrix (S3: 0/1/3 only)
static const int PIN_BCLK = 41;   // SCLK
static const int PIN_WS   = 43;   // LRCK
static const int PIN_DIN  = 46;   // ASDOUT (codec ADC -> ESP)
static const uint8_t ES_ADDR = 0x18;

static i2s_chan_handle_t s_rx = nullptr;

static bool rw(uint8_t reg, uint8_t val) {
  return M5.In_I2C.writeRegister8(ES_ADDR, reg, val, 400000);
}
static uint8_t rr(uint8_t reg) {
  return M5.In_I2C.readRegister8(ES_ADDR, reg, 400000);
}

static bool codec_init() {
  // Chip ID
  uint8_t id0 = rr(0xFD), id1 = rr(0xFE);
  Serial.printf("[es8311] chip id: 0x%02X 0x%02X (expect 0x83 0x11)\n", id0, id1);

  // M5Unified's EXACT _microphone_enabled_cb_cardputer_adv sequence, verbatim.
  // Just 8 register writes -- no extra registers fighting it. Critically:
  //   0x01=0xBA (not 0xB5/0x3F) is the MIC clock-manager value,
  //   0x14=0x10 (not 0x1A) selects Mic1p-Mic1n / PGA gain,
  //   0x0E=0x02 enables the analog PGA + ADC modulator.
  bool ok = true;
  ok &= rw(0x00, 0x80);   // RESET / CSM power on
  ok &= rw(0x01, 0xBA);   // CLOCK_MANAGER / MCLK=BCLK
  ok &= rw(0x02, 0x18);   // CLOCK_MANAGER / MULT_PRE=3
  ok &= rw(0x0D, 0x01);   // SYSTEM / power up analog
  ok &= rw(0x0E, 0x02);   // SYSTEM / enable analog PGA + ADC modulator
  ok &= rw(0x14, 0x10);   // ADC_REG14 / select Mic1p-Mic1n, PGA gain
  ok &= rw(0x17, 0xBF);   // ADC_REG17 / ADC volume (0 dB)
  ok &= rw(0x1C, 0x6A);   // ADC_REG1C / EQ bypass, DC-offset cancel
  delay(10);

  Serial.printf("[es8311] verify 0x01=%02X 0x02=%02X 0x0E=%02X 0x14=%02X 0x17=%02X 0x1C=%02X\n",
                rr(0x01), rr(0x02), rr(0x0E), rr(0x14), rr(0x17), rr(0x1C));
  return ok && (id0 == 0x83) && (id1 == 0x11);
}

static bool i2s_up(uint32_t fs) {
  i2s_chan_config_t chan = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
  chan.auto_clear = true;
  if (i2s_new_channel(&chan, nullptr, &s_rx) != ESP_OK) { Serial.println("[es8311] new_channel fail"); return false; }

  i2s_std_config_t c;
  memset(&c, 0, sizeof(c));
  c.clk_cfg.clk_src        = I2S_CLK_SRC_DEFAULT;
  c.clk_cfg.sample_rate_hz = fs;
  c.clk_cfg.mclk_multiple  = I2S_MCLK_MULTIPLE_256;   // <<< the reporter's working value
  c.slot_cfg.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT;
  c.slot_cfg.slot_bit_width = I2S_SLOT_BIT_WIDTH_16BIT;
  c.slot_cfg.slot_mode      = I2S_SLOT_MODE_MONO;
  c.slot_cfg.slot_mask      = I2S_STD_SLOT_LEFT;
  c.slot_cfg.ws_width       = 16;
  c.slot_cfg.ws_pol         = false;
  c.slot_cfg.bit_shift      = true;
  c.slot_cfg.left_align     = true;
  c.slot_cfg.big_endian     = false;
  c.slot_cfg.bit_order_lsb  = false;
  c.gpio_cfg.mclk = (gpio_num_t)PIN_MCLK;   // <<< ROUTE MCLK (was I2S_GPIO_UNUSED before)
  c.gpio_cfg.bclk = (gpio_num_t)PIN_BCLK;
  c.gpio_cfg.ws   = (gpio_num_t)PIN_WS;
  c.gpio_cfg.dout = (gpio_num_t)I2S_GPIO_UNUSED;
  c.gpio_cfg.din  = (gpio_num_t)PIN_DIN;

  if (i2s_channel_init_std_mode(s_rx, &c) != ESP_OK) { Serial.println("[es8311] init_std fail"); return false; }
  if (i2s_channel_enable(s_rx) != ESP_OK) { Serial.println("[es8311] enable fail"); return false; }
  Serial.printf("[es8311] i2s up: %lu Hz, MCLK=G%d@256x BCLK=G%d WS=G%d DIN=G%d\n",
                (unsigned long)fs, PIN_MCLK, PIN_BCLK, PIN_WS, PIN_DIN);
  return true;
}

void setup() {
  auto cfg = M5.config();
  M5Cardputer.begin(cfg);
  Serial.begin(115200);
  delay(500);
  Serial.printf("SDK: %s  (need 5.4.x)\n", ESP.getSdkVersion());

  M5Cardputer.Speaker.end();   // free shared I2S/codec
  delay(50);

  if (!codec_init()) { Serial.println("[es8311] codec init/id FAILED"); }
  if (!i2s_up(16000)) { Serial.println("[es8311] i2s FAILED"); return; }

  // Sweep the PGA gain register 0x14 to find a good level. The ES8311 0x14
  // low nibble is the analog PGA gain (0..10, ~3 dB/step); the high bits select
  // the mic input. M5 uses 0x10 (input select, gain=0 minimum). We try 0x10,
  // 0x13, 0x16, 0x19, 0x1A (max ~30 dB) and report the peak for each so you can
  // pick the value that gives a clean, strong level.
  static const uint8_t gains[] = { 0x10, 0x13, 0x16, 0x19, 0x1A };
  static int16_t buf[2048];

  Serial.println("\n>>> TALK CONTINUOUSLY for the next ~10 seconds <<<\n");
  for (uint8_t g : gains) {
    rw(0x14, g);
    delay(50);
    // drain stale frames
    size_t junk = 0;
    for (int i = 0; i < 4; i++) i2s_channel_read(s_rx, buf, sizeof(buf), &junk, 50);

    int32_t peak = 0; size_t total = 0;
    uint32_t t0 = millis();
    while (millis() - t0 < 1500) {          // ~1.5 s per gain
      size_t got = 0;
      if (i2s_channel_read(s_rx, buf, sizeof(buf), &got, 200) == ESP_OK) {
        size_t n = got / sizeof(int16_t);
        for (size_t i = 0; i < n; i++) { int v = abs(buf[i]); if (v > peak) peak = v; }
        total += n;
      }
    }
    const char* verdict = peak == 0 ? "silent" : peak < 50 ? "faint" :
                          peak < 8000 ? "GOOD" : "loud/clipping?";
    Serial.printf("  0x14=0x%02X  ->  peak %5ld   [%s]\n", g, peak, verdict);

    // On the loudest gain, dump a few raw samples so we can tell a gain problem
    // (tiny values like -2,1,0,-1) from a slot/format problem (values clustered
    // in high bits, e.g. always even, or large DC with no swing).
    if (g == 0x1A) {
      size_t got = 0;
      i2s_channel_read(s_rx, buf, sizeof(buf), &got, 200);
      Serial.print("  raw samples @0x1A: ");
      for (int i = 0; i < 16 && i < (int)(got/2); i++) Serial.printf("%d ", buf[i]);
      Serial.println();
    }
  }
  Serial.println("\nPick the 0x14 value with a strong peak (a few hundred to a few thousand).");
}

void loop() {}
