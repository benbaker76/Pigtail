#include <M5Cardputer.h>

#include "nvs_flash.h"
#include "DeviceTracker.h"
#include "GNSSModule.h"
#include "UIGrid.h"
#include "Logo.h"
#include "Colors.h"

#define VERSION "1.0.00"

static DeviceTracker g_tracker;
static UIGrid g_ui(VERSION);

static const uint32_t UI_FRAME_MS = 33;

// Total time the splash should be visible (includes init work you do after drawing it)
static constexpr uint32_t SPLASH_MS = 5000;

// If your nibble order is wrong (colors look "scrambled"), flip this.
// Common conventions vary; this makes it easy to correct.
static constexpr bool LOGO_HIGH_NIBBLE_FIRST = true; // even-x pixel uses high nibble

static inline uint8_t logoPixelIndex32x32(int x, int y)
{
    // packed: 2 pixels per byte
    const int bytesPerRow = 32 / 2; // 16
    const int idx = y * bytesPerRow + (x >> 1);
    const uint8_t b = Logo::PigtailLogo[idx];

    if (LOGO_HIGH_NIBBLE_FIRST) {
        // x even => high nibble, x odd => low nibble
        return (x & 1) ? (uint8_t)(b & 0x0F) : (uint8_t)((b >> 4) & 0x0F);
    } else {
        // x even => low nibble, x odd => high nibble
        return (x & 1) ? (uint8_t)((b >> 4) & 0x0F) : (uint8_t)(b & 0x0F);
    }
}

static void drawSplashScreen()
{
    auto& d = M5Cardputer.Display;
    const int W = d.width();   // 240
    const int H = d.height();  // 135

    // Logo: 32x32 -> 4x => 128x128
    constexpr int logoSrcW = 32;
    constexpr int logoSrcH = 32;
    constexpr int scale    = 4;
    constexpr int logoW    = logoSrcW * scale; // 128
    constexpr int logoH    = logoSrcH * scale; // 128

    const int x0 = (W - logoW) / 2;  // (240-128)/2 = 56
    const int y0 = 0;               // minimize footer overlap

    d.startWrite();
    d.fillScreen(C_BLACK);

    // Draw logo as scaled blocks (fast enough at boot, and simplest).
    for (int y = 0; y < logoSrcH; ++y) {
        for (int x = 0; x < logoSrcW; ++x) {
            const uint8_t pi = logoPixelIndex32x32(x, y);
            const uint16_t c = (uint16_t)Colors::C64Colors[pi]; // your palette is RGB565 values
            d.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, c);
        }
    }

    // Footer text (draw over bottom area; unavoidable with 128px logo on 135px display)
    d.setTextSize(1);
    d.setTextColor(C_WHITE, C_BLACK);

    const char* right = "benbaker76";

    // M5GFX text height at size 1 is typically 8px; place at bottom line
    const int footerY = H - 10; // gives a bit of breathing room

    // Left: version
    d.setCursor(0, footerY);
    d.print(VERSION);

    // Right: author (right-justified)
    const int rightW = d.textWidth(right);
    d.setCursor(W - rightW, footerY);
    d.print(right);

    d.endWrite();
}

void setup() {
  Serial.begin(115200);

  // Cardputer init (your established pattern)
  auto cfg = M5.config();
  cfg.serial_baudrate = 115200;
  cfg.fallback_board  = m5::board_t::board_M5Cardputer;
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Keyboard.begin();

  M5.Display.setRotation(1);
  M5.Display.setBrightness(128);

  const uint32_t t0 = millis();

  // Draw splash immediately
  drawSplashScreen();

  // Start GNSS with M5Cardputer CAP LoRa868 GPS configuration
  // Based on the demo, GPS uses:
  // - GPIO15 (RX) - ESP32 receives from GPS TX
  // - GPIO13 (TX) - ESP32 transmits to GPS RX
  // Demo uses 115200 baud rate for GPS communication
  
  // Configure GPS for M5Cardputer CAP LoRa868 module
  gnss_begin(115200, 15, 13);  // Baud=115200, RX=GPIO15, TX=GPIO13
  Serial.println("GNSS started on GPIO15(RX)/GPIO13(TX) at 115200 baud");
  Serial.println("Using CAP LoRa868 GPS configuration");
  Serial.println("GPS: Waiting for satellites (30-60s with clear sky view)");

  // Tracker
  if (!g_tracker.begin()) {
    Serial.println("DeviceTracker.begin failed");
  }

  const uint32_t elapsed = millis() - t0;
  if (elapsed < SPLASH_MS) {
      delay(SPLASH_MS - elapsed);
  }

  // UI
  g_ui.begin(&g_tracker);

  Serial.printf("[heap] free=%u min=%u\n",
              (unsigned)esp_get_free_heap_size(),
              (unsigned)esp_get_minimum_free_heap_size());
}

void loop() {
  M5Cardputer.update();

  auto s = gnssModule.snapshot();
  g_tracker.setGpsFix(s.valid, s.lat, s.lon);

  /* if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) {
    Keyboard_Class::KeysState ks = M5Cardputer.Keyboard.keysState();
    g_ui.handleKeys(ks);
  } */

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())
    g_ui.handleKeyboard(M5Cardputer.Keyboard);

  // Stationary ratio heuristic:
  // If the environment segmentation hasn't advanced recently, user is likely stationary.
  const uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  const uint32_t last_env = g_tracker.lastEnvTickS();
  const uint32_t dt = (last_env > 0) ? (ts - last_env) : 0;
  float stationary_ratio = dt >= 120 ? 1.0f : (float)dt / 120.0f;

  // UI refresh ~2 Hz
  static uint32_t last_ms = 0;
  const uint32_t ms = millis();
  if (ms - last_ms >= UI_FRAME_MS) {
    last_ms = ms;
    g_ui.update(stationary_ratio);
  }

  delay(1);
}
