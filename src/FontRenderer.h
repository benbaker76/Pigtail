#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Renders a built-in 4x5 pixel font into an 8bpp indexed image.
//
// Destination format:
//   - imageData is a flat row-major buffer (size >= imageWidth * imageHeight)
//   - each byte is a palette index (0..255)
//
// Font format:
//   - 4 pixels wide, 5 pixels high
//   - packed as one nibble per row
//   - glyphIndex 0 corresponds to ASCII 32 (' ')
class FontRenderer
{
public:
    static constexpr int GlyphWidth  = 4;
    static constexpr int GlyphHeight = 5;

    // Draw a single glyph.
    void DrawGlyph(std::vector<std::uint8_t>& imageData,
                   int imageWidth,
                   int imageHeight,
                   std::uint8_t colorIndex,
                   int x,
                   int y,
                   int glyphIndex) const;

    // Draw an ASCII string. Characters are mapped via (c - 32).
    void DrawText(std::vector<std::uint8_t>& imageData,
                  int imageWidth,
                  int imageHeight,
                  std::uint8_t colorIndex,
                  int x,
                  int y,
                  std::string_view text) const;
};
