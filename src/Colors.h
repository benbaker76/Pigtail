#pragma once
#include <M5Cardputer.h>
#include <stdint.h>
#include <array>
#include "Icons.h"

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

static constexpr uint32_t C64_BLACK        = 0x00000000u;
static constexpr uint32_t C64_WHITE        = 0xFFFFFFFFu;
static constexpr uint32_t C64_RED          = 0xFF880000u;
static constexpr uint32_t C64_CYAN         = 0xFFAAFFEEu;
static constexpr uint32_t C64_VIOLET       = 0xFFCC44CCu;
static constexpr uint32_t C64_GREEN        = 0xFF00CC55u;
static constexpr uint32_t C64_BLUE         = 0xFF0000AAu;
static constexpr uint32_t C64_YELLOW       = 0xFFEEEE77u;
static constexpr uint32_t C64_ORANGE       = 0xFFDD8855u;
static constexpr uint32_t C64_BROWN        = 0xFF664400u;
static constexpr uint32_t C64_LIGHT_RED    = 0xFFFF7777u;
static constexpr uint32_t C64_DARK_GREY    = 0xFF333333u;
static constexpr uint32_t C64_GREY         = 0xFF777777u;
static constexpr uint32_t C64_LIGHT_GREEN  = 0xFFAAFF66u;
static constexpr uint32_t C64_LIGHT_BLUE   = 0xFF0088FFu;
static constexpr uint32_t C64_LIGHT_GREY   = 0xFFBBBBBBu;

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
static constexpr uint32_t C_BLACK       = 0;
static constexpr uint32_t C_DARK_BLUE   = 1;
static constexpr uint32_t C_DARK_PURPLE = 2;
static constexpr uint32_t C_DARK_GREEN  = 3;
static constexpr uint32_t C_BROWN       = 4;
static constexpr uint32_t C_DARK_GREY   = 5;
static constexpr uint32_t C_LIGHT_GREY  = 6;
static constexpr uint32_t C_WHITE       = 7;
static constexpr uint32_t C_RED         = 8;
static constexpr uint32_t C_ORANGE      = 9;
static constexpr uint32_t C_YELLOW      = 10;
static constexpr uint32_t C_GREEN       = 11;
static constexpr uint32_t C_BLUE        = 12;
static constexpr uint32_t C_LAVENDER    = 13;
static constexpr uint32_t C_PINK        = 14;
static constexpr uint32_t C_PEACH       = 15;


struct Colors
{
    static inline constexpr lgfx::rgb565_t Pico8Colors[] = {
        ARGB_TO_RGB565(PICO_BLACK),
        ARGB_TO_RGB565(PICO_DARK_BLUE),
        ARGB_TO_RGB565(PICO_DARK_PURPLE),
        ARGB_TO_RGB565(PICO_DARK_GREEN),
        ARGB_TO_RGB565(PICO_BROWN),
        ARGB_TO_RGB565(PICO_DARK_GREY),
        ARGB_TO_RGB565(PICO_LIGHT_GREY),
        ARGB_TO_RGB565(PICO_WHITE),
        ARGB_TO_RGB565(PICO_RED),
        ARGB_TO_RGB565(PICO_ORANGE),
        ARGB_TO_RGB565(PICO_YELLOW),
        ARGB_TO_RGB565(PICO_GREEN),
        ARGB_TO_RGB565(PICO_BLUE),
        ARGB_TO_RGB565(PICO_LAVENDER),
        ARGB_TO_RGB565(PICO_PINK),
        ARGB_TO_RGB565(PICO_PEACH)
    };

    static inline constexpr lgfx::rgb565_t C64Colors[] = {
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

    static inline constexpr std::uint8_t VendorColorIndices[Icons::kVendorsCount] = {
        C_BLACK,       // Unknown
        C_LIGHT_GREY,  // Apple
        C_BLUE,        // Asus
        C_RED,         // Broadcom
        C_PINK,        // Chipolo
        C_BLUE,        // Cisco
        C_LAVENDER,    // Csr
        C_BLUE,        // DLink
        C_RED,         // Espressif
        C_LIGHT_GREY,  // Eufy
        C_LIGHT_GREY,  // Google
        C_RED,         // Huawei
        C_GREEN,       // Innway
        C_BLUE,        // Intel
        C_GREEN,       // Intelbras
        C_BLUE,        // Jio
        C_ORANGE,      // Mercury
        C_GREEN,       // Mercusys
        C_DARK_GREY,   // Microsoft
        C_ORANGE,      // Mikrotik
        C_BLUE,        // Motorola
        C_LAVENDER,    // Netgear
        C_YELLOW,      // Pebblebee
        C_BLUE,        // Qualcomm
        C_RED,         // RaspberryPi
        C_LIGHT_GREY,  // RollingSquare
        C_BLUE,        // Samsung
        C_DARK_GREY,   // Sony
        C_RED,         // Ti
        C_YELLOW,      // Tile
        C_GREEN,       // TpLink
        C_PEACH,       // Tracki
        C_BLUE,        // Ubiquiti
    };
};
