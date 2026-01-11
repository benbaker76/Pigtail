#include <M5Cardputer.h>
#include <SD.h>
#include <SPIFFS.h>

#include "nvs_flash.h"
#include "DeviceTracker.h"
#include "GNSSModule.h"
#include "UIGrid.h"
#include "Logo.h"
#include "Colors.h"

#define VERSION "1.0.07"

static DeviceTracker g_tracker;
static UIGrid g_ui(VERSION);

static const uint32_t UI_FRAME_MS = 33;

// Total time the splash should be visible (includes init work you do after drawing it)
static constexpr uint32_t SPLASH_MS = 5000;

// If your nibble order is wrong (colors look "scrambled"), flip this.
// Common conventions vary; this makes it easy to correct.
static constexpr bool LOGO_HIGH_NIBBLE_FIRST = true; // even-x pixel uses high nibble

// Prefer explicit caps queries. MALLOC_CAP_DEFAULT is "whatever malloc() uses"
// and can span multiple regions; it is useful but not the whole story.
static inline void PrintHeapTelemetry(const char* tag = nullptr)
{
    // 8-bit addressable heap (most general allocations land here)
    const size_t free_8   = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    const size_t large_8  = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
    const size_t min_8    = heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT);

    // Internal RAM (excludes PSRAM). Good proxy for "real" headroom on non-PSRAM boards.
    const size_t free_int  = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t large_int = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t min_int   = heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL);

    // DMA-capable internal RAM (critical for some peripherals / drivers / sprites depending on libs)
    const size_t free_dma  = heap_caps_get_free_size(MALLOC_CAP_DMA);
    const size_t large_dma = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    const size_t min_dma   = heap_caps_get_minimum_free_size(MALLOC_CAP_DMA);

    // PSRAM/SPIRAM heap (0 if not present / not enabled)
    const size_t free_psram  = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t large_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    const size_t min_psram   = heap_caps_get_minimum_free_size(MALLOC_CAP_SPIRAM);

    // Default heap (what malloc/new will normally use)
    const size_t free_def  = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    const size_t large_def = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    const size_t min_def   = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);

    // Stack high-water mark (words -> bytes)
    const UBaseType_t stack_words = uxTaskGetStackHighWaterMark(nullptr);
    const size_t stack_bytes = (size_t)stack_words * sizeof(StackType_t);

    if (tag && *tag) {
        Serial.printf("[%s] ", tag);
    } else {
        Serial.print("[heap] ");
    }

    // Keep it single-line to make log scanning easy.
    Serial.printf("def free=%u largest=%u min=%u | "
                  "int free=%u largest=%u min=%u | "
                  "dma free=%u largest=%u min=%u | "
                  "8bit free=%u largest=%u min=%u | "
                  "psram free=%u largest=%u min=%u | "
                  "stack_hiwater=%u bytes\n",
                  (unsigned)free_def,  (unsigned)large_def,  (unsigned)min_def,
                  (unsigned)free_int,  (unsigned)large_int,  (unsigned)min_int,
                  (unsigned)free_dma,  (unsigned)large_dma,  (unsigned)min_dma,
                  (unsigned)free_8,    (unsigned)large_8,    (unsigned)min_8,
                  (unsigned)free_psram,(unsigned)large_psram,(unsigned)min_psram,
                  (unsigned)stack_bytes);
}

// Call this from loop() to throttle printing.
static inline void PrintHeapTelemetryEvery(uint32_t intervalMs, const char* tag = nullptr)
{
    static uint32_t last = 0;
    const uint32_t now = millis();
    if ((uint32_t)(now - last) >= intervalMs) {
        last = now;
        PrintHeapTelemetry(tag);
    }
}

bool initStorage() {
  if (!SPIFFS.begin(true)) {
    Serial.println("[fs] SPIFFS mount failed");
  }

  if (!SD.begin(GPIO_NUM_12, SPI, 25000000, "/sd", 1)) {
    Serial.println("[kml] SD card init failed");
    return false;
  }

  return true;
}

static void toneMs(int f, int ms)
{
  M5Cardputer.Speaker.tone(f, ms);
  delay(ms + 8);
}

static inline void playStartupSound()
{
  // quick upward sweep
  for (int f = 220; f <= 880; f += 40) {
    M5Cardputer.Speaker.tone(f, 8);
    delay(9);
  }
  delay(25);

  // fast arpeggio to mimic a richer sound
  toneMs(988,  45);   // B5
  toneMs(1319, 45);   // E6
  toneMs(1568, 70);   // G6

  // tiny “confirmation” ping
  toneMs(1760, 35);   // A6
}

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
    d.fillScreen(Colors::Pico8Colors[C_BLACK].raw);

    // Draw logo as scaled blocks (fast enough at boot, and simplest).
    for (int y = 0; y < logoSrcH; ++y) {
        for (int x = 0; x < logoSrcW; ++x) {
            const uint8_t pi = logoPixelIndex32x32(x, y);
            const lgfx::rgb565_t c = Colors::C64Colors[pi]; // your palette is RGB565 values
            d.fillRect(x0 + x * scale, y0 + y * scale, scale, scale, c);
        }
    }

    // Footer text (draw over bottom area; unavoidable with 128px logo on 135px display)
    d.setTextSize(1);
    d.setTextColor(Colors::Pico8Colors[C_WHITE].raw, Colors::Pico8Colors[C_BLACK].raw);

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
  delay(100);

  // Init M5Cardputer hardware
  auto cfg = M5.config();
  M5.begin(cfg);
  M5Cardputer.begin(cfg, true);
  M5Cardputer.Keyboard.begin();

  // Configure G0 button (GPIO0) as input with pullup
  pinMode(0, INPUT_PULLUP);

  // Disable LoRa GPIO5 to avoid conflicts
  pinMode(5, INPUT_PULLUP);

  const uint32_t t0 = millis();

  // Draw splash immediately
  drawSplashScreen();

  playStartupSound();

  bool sdAvailable = initStorage();

  g_tracker.setSdAvailable(sdAvailable);

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

  //PrintHeapTelemetryEvery(2000, "main");
}

void loop() {
  M5Cardputer.update();

  auto s = gnssModule.snapshot();
  g_tracker.setGpsFix(s.valid, s.lat, s.lon);

  if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed())
    g_ui.handleKeyboard(M5Cardputer.Keyboard);

  // Stationary ratio heuristic:
  // If the environment segmentation hasn't advanced recently, user is likely stationary.
  const uint32_t ts = (uint32_t)(esp_timer_get_time() / 1000000ULL);
  const uint32_t last_env = g_tracker.lastEnvTickS();
  const uint32_t dt = (last_env > 0) ? (ts - last_env) : 0;
  float stationary_ratio = dt >= 120 ? 1.0f : (float)dt / 120.0f;

  // UI refresh ~30.3 Hz
  static uint32_t last_ms = 0;
  const uint32_t ms = millis();
  if (ms - last_ms >= UI_FRAME_MS) {
    last_ms = ms;
    g_ui.update(stationary_ratio);
  }

  delay(1);
}
