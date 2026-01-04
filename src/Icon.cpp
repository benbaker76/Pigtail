#include "Icon.h"
#include "RetroAvatar.h"
#include "Names.h"
#include "Icons.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <random>
#include <vector>
#include <iostream>

namespace
{
    std::string ToUpper(std::string s)
    {
        for (char& c : s) c = (char)std::toupper((unsigned char)c);
        return s;
    }

    // Parse exactly 2 hex chars into a byte. Returns true on success.
    bool ParseHexByte(std::string_view s, std::uint8_t& out)
    {
        if (s.size() != 2)
            return false;

        auto hexVal = [](char ch) -> int
        {
            if (ch >= '0' && ch <= '9') return ch - '0';
            ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
            if (ch >= 'A' && ch <= 'F') return 10 + (ch - 'A');
            return -1;
        };

        const int hi = hexVal(s[0]);
        const int lo = hexVal(s[1]);
        if (hi < 0 || lo < 0)
            return false;

        out = static_cast<std::uint8_t>((hi << 4) | lo);
        return true;
    }
}

Icon::Icon()
    : _id(0)
    , _random(DeterministicRng())
    , _macAddress("00:00:00:00:00:00")
    , _fontRenderer()
    , _markovNameGenerator(Names::Hindu(), 1, 4, 8)
    , _retroAvatar()
{
}

void Icon::Reset(std::uint32_t id)
{
    _id = id;
    _random.Reset(id);
    _markovNameGenerator.Reset(id);
    _retroAvatar.GenerateAvatar(id);

    _name = ToUpper(_markovNameGenerator.NextName());
    _imageData.Reset(_imageW, _imageH);
}

void Icon::Reset(std::uint32_t id, std::string macAddress)
{
    _macAddress = macAddress;
    Reset(id);
}

std::uint8_t Icon::MapByteToColorIndex(std::uint8_t value)
{
    constexpr std::array<std::uint8_t, 2> ignore{ 0, 7 };

    std::array<std::uint8_t, 16> indices{};
    int count = 0;

    for (std::uint8_t i = 0; i < 16; ++i)
    {
        bool skip = false;
        for (std::uint8_t ig : ignore)
        {
            if (i == ig) { skip = true; break; }
        }

        if (!skip)
            indices[count++] = i;
    }

    const int bucket = (static_cast<int>(value) * count) / 256; // 0..count-1
    return indices[bucket];
}

bool Icon::TryParseMacBytes(std::string_view mac, std::array<std::uint8_t, 6>& outBytes)
{
    // Expected: "AA:BB:CC:DD:EE:FF" (17 chars)
    // We'll be tolerant of case but strict about separators and 2-digit groups.

    if (mac.size() != 17)
        return false;

    for (int i = 0; i < 6; ++i)
    {
        const std::size_t off = static_cast<std::size_t>(i) * 3;
        if (i > 0 && mac[off - 1] != ':')
            return false;

        std::uint8_t b = 0;
        if (!ParseHexByte(mac.substr(off, 2), b))
            return false;

        outBytes[static_cast<std::size_t>(i)] = b;
    }

    return true;
}

void Icon::DrawName(int offsetY)
{
    Size textSize{ (int)_name.size() * _glyphSize.w, _glyphSize.h };
    Point textLoc{ (_iconSize.w / 2) - (textSize.w / 2) + 1, offsetY };

    _fontRenderer.DrawText(_imageData, COLOR_TEXT, textLoc.x, textLoc.y, _name);
}

void Icon::DrawMacAddress()
{
    // Split into two lines:
    // line1 = "AA:BB:CC" (chars 0..7)
    // line2 = "DD:EE:FF" (chars 9..16)
    const std::string line1 = _macAddress.substr(0, 8);
    const std::string line2 = _macAddress.substr(9);

    // Font metrics (your FontRenderer uses 4x5).
    // If your FontRenderer exposes other names, adjust here.
    constexpr int glyphW = FontRenderer::GlyphWidth;   // 4
    constexpr int glyphH = FontRenderer::GlyphHeight;  // 5

    const int byteRectW  = glyphW * 2; // 8
    const int byteRectH  = glyphH;     // 5
    const int colonRectW = glyphW;     // 4
    const int colonRectH = glyphH;     // 5

    constexpr int line1Y = 20;
    constexpr int line2Y = 26;

    // Background rectangles based on MAC bytes.
    std::array<std::uint8_t, 6> macBytes{};

    if (TryParseMacBytes(_macAddress, macBytes))
    {
        for (int i = 0; i < 6; ++i)
        {
            const std::uint8_t b = macBytes[static_cast<std::size_t>(i)];
            const std::uint8_t bgColor = MapByteToColorIndex(b);

            const int line = i / 3; // 0 or 1
            const int pos  = i % 3; // 0..2 within the line

            // In "AA:BB:CC", bytes start at character indices 0, 3, 6.
            const int byteCharIndex = pos * 3;

            const int xByte = byteCharIndex * glyphW;
            const int yLine = (line == 0) ? line1Y : line2Y;

            // Background for the two hex chars.
            DrawRect({ xByte, yLine, byteRectW, byteRectH }, bgColor);

            // Background for ':' after this byte (and yes, draw it too), except after the last byte on the line.
            if (pos < 2)
            {
                const int colonCharIndex = byteCharIndex + 2; // after two hex chars
                const int xColon = colonCharIndex * glyphW;
                DrawRect({ xColon, yLine, colonRectW, colonRectH }, COLOR_COLON_BG);
            }
        }
    }

    // Draw text on top.
    // FontRenderer expects std::vector<uint8_t>&, so we pass the Indexed4bppImage's Raw vector.
    _fontRenderer.DrawText(_imageData, COLOR_TEXT_MAC, 0, line1Y, line1);
    _fontRenderer.DrawText(_imageData, COLOR_TEXT_MAC, 0, line2Y, line2);
}

void Icon::DrawVerticalBar(Rect rect, float value, std::uint8_t colorIndex)
{
    DrawRect(rect, COLOR_BAR_BG);
    DrawRect({rect.x, rect.y + rect.h - static_cast<int>(rect.h * value), rect.w, static_cast<int>(rect.h * value)}, colorIndex);
}

void Icon::DrawHorizontalBar(Rect rect, float value, std::uint8_t colorIndex)
{
    DrawRect(rect, COLOR_BAR_BG);
    DrawRect({rect.x, rect.y, static_cast<int>(rect.w * value), rect.h}, colorIndex);
}

void Icon::DrawRect(Rect rect, std::uint8_t colorIndex)
{
    if (rect.w <= 0 || rect.h <= 0)
        return;

    const int x0 = std::max(0, rect.x);
    const int y0 = std::max(0, rect.y);
    const int x1 = std::min(_imageW, rect.x + rect.w);
    const int y1 = std::min(_imageH, rect.y + rect.h);

    if (x0 >= x1 || y0 >= y1)
        return;

    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            _imageData.At(xx, yy) = colorIndex;
}

void Icon::DrawIcon(const std::uint8_t* iconData, Rect iconRect, int colorIndex)
{
    const int width  = iconRect.w;
    const int height = iconRect.h;

    if (width <= 0 || height <= 0)
        return;

    // 1bpp, row-major, LSB-first per byte (XBM-style), like the C# code.
    const int bytesPerRow  = (width + 7) >> 3;

    if (!iconData)
        return;

    // Destination bounds (clip like the C# code).
    const int dstW = _imageData.Width();   // or GetWidth()
    const int dstH = _imageData.Height();  // or GetHeight()

    const int startX = std::max(0, iconRect.x);
    const int startY = std::max(0, iconRect.y);
    const int endX   = std::min(dstW, iconRect.x + width);
    const int endY   = std::min(dstH, iconRect.y + height);

    for (int yDst = startY; yDst < endY; ++yDst)
    {
        const int y = yDst - iconRect.y;
        const int rowBase = y * bytesPerRow;

        for (int xDst = startX; xDst < endX; ++xDst)
        {
            const int x = xDst - iconRect.x;

            const int byteIndex = rowBase + (x >> 3); // x / 8
            const int bitIndex = 7 - (x & 7);         // MSB-first
            //const int bitIndex  = (x & 7);            // LSB-first (XBM)

            const bool on = ((iconData[byteIndex] >> bitIndex) & 1u) != 0;

            if (on)
                _imageData.At(xDst, yDst) = colorIndex;
        }
    }
}

void Icon::DrawIcon(IconType iconType,
                  float bar1Value,
                  std::uint8_t bar1ColorIndex,
                  float bar2Value,
                  std::uint8_t bar2ColorIndex,
                  const std::uint8_t* largeIcon,
                  std::uint8_t largeIconColorIndex,
                  const std::uint8_t* smallIcon1,
                  std::uint8_t smallIcon1ColorIndex,
                  const std::uint8_t* smallIcon2,
                  std::uint8_t smallIcon2ColorIndex)
{
    _imageData.Reset(_imageW, _imageH);

    switch (iconType)
    {
        case IconType::RetroAvatar:
        {
            _retroAvatar.DrawAvatar(_imageData, 4, 1, SCALE_2X);
            DrawName(26);
            break;
        }
        case IconType::RetroAvatarWithMac:
        {
            _retroAvatar.DrawAvatar(_imageData, 9, 4, SCALE_1X);
            DrawVerticalBar({1, 1, 2, 17}, bar1Value, bar1ColorIndex);
            DrawVerticalBar({4, 1, 2, 17}, bar2Value, bar2ColorIndex);
            DrawIcon(smallIcon1, {24, 1, 8, 8}, smallIcon1ColorIndex);
            DrawIcon(smallIcon2, {24, 10, 8, 8}, smallIcon2ColorIndex);
            DrawMacAddress();
            break;
        }
        case IconType::LargeIconWithMac:
        {
            DrawVerticalBar({1, 1, 2, 17}, bar1Value, bar1ColorIndex);
            DrawVerticalBar({4, 1, 2, 17}, bar2Value, bar2ColorIndex);
            DrawIcon(largeIcon, {7, 2, 16, 16}, largeIconColorIndex);
            DrawIcon(smallIcon1, {24, 1, 8, 8}, smallIcon1ColorIndex);
            DrawIcon(smallIcon2, {24, 10, 8, 8}, smallIcon2ColorIndex);
            DrawMacAddress();
            break;
        }
        default:
            return;
    }
}
