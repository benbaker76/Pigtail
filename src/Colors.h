#pragma once
#include <stdint.h>
#include <array>

// Pico-8 palette (ARGB)
static constexpr uint32_t PICO_BLACK       = 0xFF000000u;
static constexpr uint32_t PICO_DARK_BLUE   = 0xFF1D2B53u;
static constexpr uint32_t PICO_DARK_PURPLE = 0xFF7E2553u;
static constexpr uint32_t PICO_DARK_GREEN  = 0xFF008751u;
static constexpr uint32_t PICO_BROWN       = 0xFFAB5236u;
static constexpr uint32_t PICO_DARK_GREY   = 0xFF5F574Fu;
static constexpr uint32_t PICO_LIGHT_GREY  = 0xFFC2C3C7u;
static constexpr uint32_t PICO_WHITE       = 0xFFFFF1E8u;
static constexpr uint32_t PICO_RED         = 0xFFFF004Du;
static constexpr uint32_t PICO_ORANGE      = 0xFFFFA300u;
static constexpr uint32_t PICO_YELLOW      = 0xFFFFEC27u;
static constexpr uint32_t PICO_GREEN       = 0xFF00E436u;
static constexpr uint32_t PICO_BLUE        = 0xFF29ADFFu;
static constexpr uint32_t PICO_LAVENDER    = 0xFF83769Cu;
static constexpr uint32_t PICO_PINK        = 0xFFFF77A8u;
static constexpr uint32_t PICO_PEACH       = 0xFFFFCCAAu;

static constexpr uint32_t C64_BLACK      = 0x00000000u;
static constexpr uint32_t C64_WHITE      = 0xFFFFFFFFu;
static constexpr uint32_t C64_RED        = 0xFF880000u;
static constexpr uint32_t C64_CYAN       = 0xFFAAFFEEu;
static constexpr uint32_t C64_VIOLET     = 0xFFCC44CCu;
static constexpr uint32_t C64_GREEN      = 0xFF00CC55u;
static constexpr uint32_t C64_BLUE       = 0xFF0000AAu;
static constexpr uint32_t C64_YELLOW     = 0xFFEEEE77u;
static constexpr uint32_t C64_ORANGE     = 0xFFDD8855u;
static constexpr uint32_t C64_BROWN      = 0xFF664400u;
static constexpr uint32_t C64_LIGHT_RED  = 0xFFFF7777u;
static constexpr uint32_t C64_DARK_GREY  = 0xFF333333u;
static constexpr uint32_t C64_GREY       = 0xFF777777u;
static constexpr uint32_t C64_LIGHT_GREEN= 0xFFAAFF66u;
static constexpr uint32_t C64_LIGHT_BLUE = 0xFF0088FFu;
static constexpr uint32_t C64_LIGHT_GREY = 0xFFBBBBBBu;

// ---- Conversions ----

// ARGB -> RGB888 components
static constexpr uint8_t ARGB_R(uint32_t argb) { return (uint8_t)((argb >> 16) & 0xFF); }
static constexpr uint8_t ARGB_G(uint32_t argb) { return (uint8_t)((argb >>  8) & 0xFF); }
static constexpr uint8_t ARGB_B(uint32_t argb) { return (uint8_t)((argb >>  0) & 0xFF); }

// RGB888 -> RGB565
static constexpr uint16_t RGB565(uint8_t r, uint8_t g, uint8_t b) {
  return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

// ARGB -> RGB565
static constexpr uint16_t ARGB_TO_RGB565(uint32_t argb) {
  return RGB565(ARGB_R(argb), ARGB_G(argb), ARGB_B(argb));
}

// ---- RGB565 palette (for M5.Display + LVGL if using RGB565) ----
static constexpr uint16_t C_BLACK       = ARGB_TO_RGB565(PICO_BLACK);
static constexpr uint16_t C_DARK_BLUE   = ARGB_TO_RGB565(PICO_DARK_BLUE);
static constexpr uint16_t C_DARK_PURPLE = ARGB_TO_RGB565(PICO_DARK_PURPLE);
static constexpr uint16_t C_DARK_GREEN  = ARGB_TO_RGB565(PICO_DARK_GREEN);
static constexpr uint16_t C_BROWN       = ARGB_TO_RGB565(PICO_BROWN);
static constexpr uint16_t C_DARK_GREY   = ARGB_TO_RGB565(PICO_DARK_GREY);
static constexpr uint16_t C_LIGHT_GREY  = ARGB_TO_RGB565(PICO_LIGHT_GREY);
static constexpr uint16_t C_WHITE       = ARGB_TO_RGB565(PICO_WHITE);
static constexpr uint16_t C_RED         = ARGB_TO_RGB565(PICO_RED);
static constexpr uint16_t C_ORANGE      = ARGB_TO_RGB565(PICO_ORANGE);
static constexpr uint16_t C_YELLOW      = ARGB_TO_RGB565(PICO_YELLOW);
static constexpr uint16_t C_GREEN       = ARGB_TO_RGB565(PICO_GREEN);
static constexpr uint16_t C_BLUE        = ARGB_TO_RGB565(PICO_BLUE);
static constexpr uint16_t C_LAVENDER    = ARGB_TO_RGB565(PICO_LAVENDER);
static constexpr uint16_t C_PINK        = ARGB_TO_RGB565(PICO_PINK);
static constexpr uint16_t C_PEACH       = ARGB_TO_RGB565(PICO_PEACH);

// ---- Semantic UI colors ----
static constexpr uint16_t UI_BG          = C_BLACK;
static constexpr uint16_t UI_TILE_BG     = C_BLACK;
static constexpr uint16_t UI_TILE_FG     = C_WHITE;
static constexpr uint16_t UI_SEL_BORDER  = C_YELLOW;   // selection box
static constexpr uint16_t UI_WARN        = C_ORANGE;   // tracker-like / warning accents
static constexpr uint16_t UI_OK          = C_GREEN;
static constexpr uint16_t UI_MUTED       = C_DARK_GREY;

struct Colors
{
    static inline constexpr std::array<std::uint16_t, 16> Pico8Colors = {
        C_BLACK,
        C_DARK_BLUE,
        C_DARK_PURPLE,
        C_DARK_GREEN,
        C_BROWN,
        C_DARK_GREY,
        C_LIGHT_GREY,
        C_WHITE,
        C_RED,
        C_ORANGE,
        C_YELLOW,
        C_GREEN,
        C_BLUE,
        C_LAVENDER,
        C_PINK,
        C_PEACH
    };

    static inline constexpr std::array<std::uint32_t, 16> C64Colors = {
        ARGB_TO_RGB565(C64_BLACK),
        ARGB_TO_RGB565(C64_WHITE),
        ARGB_TO_RGB565(C64_RED),
        ARGB_TO_RGB565(C64_CYAN),
        ARGB_TO_RGB565(C64_VIOLET),
        ARGB_TO_RGB565(C64_GREEN),
        ARGB_TO_RGB565(C64_BLUE),
        ARGB_TO_RGB565(C64_YELLOW),
        ARGB_TO_RGB565(C64_ORANGE),
        ARGB_TO_RGB565(C64_BROWN),
        ARGB_TO_RGB565(C64_LIGHT_RED),
        ARGB_TO_RGB565(C64_DARK_GREY),
        ARGB_TO_RGB565(C64_GREY),
        ARGB_TO_RGB565(C64_LIGHT_GREEN),
        ARGB_TO_RGB565(C64_LIGHT_BLUE),
        ARGB_TO_RGB565(C64_LIGHT_GREY)
    };
};
