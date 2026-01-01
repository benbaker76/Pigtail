// RetroAvatar.h
// Original code by Richard Phipps
// Updates by Ben Baker

#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "Geometry.h"
#include "ByteGrid.h"
#include "DeterministicRng.h"
#include "FontRenderer.h"
#include "MarkovNameGenerator.h"

class RetroAvatar
{
public:
    RetroAvatar();

    void GenerateAvatar(std::uint32_t id);
    void DrawName(int offsetY);
    void DrawAvatar(ByteGrid &imageData, int offsetX, int offsetY, int scale);

    static constexpr size_t ColorPaletteSize() { return 16; }

private:
    DeterministicRng _random;

    static constexpr int COLOR_NONE  = 0;
    static constexpr int COLOR_TEXT  = 1;
    static constexpr int COLOR_EYE   = 2;
    static constexpr int COLOR_NOSE  = 3;
    static constexpr int COLOR_MOUTH = 4;
    static constexpr int COLOR_TEMP  = 5;
    static constexpr int COLOR_BODY  = 6;

    bool _symX = true;
    bool _symY = false;
    int  _noise = 4;

    Size _avatarSize{ 12, 12 };

    ByteGrid _avatarData;

    // Mutable mapping of semantic colors -> palette indices.
    std::vector<std::uint8_t> _colorIndices;

    static const std::uint8_t  kFontData[];
    static const std::vector<std::string>& Names();
    static const std::vector<std::string>& Names2();

    void GeneratePalette();
    void GrowBitmap();

    void FloodFill(int x, int y, std::uint8_t color);
    void SetPixel(int x, int y, std::uint8_t c);
    std::uint8_t GetPixel(int x, int y) const;

    void RemoveNoise(int type);
    void Mirror();

    void EnhanceFace();
    int  CheckForFilledEdge();
    void OutlineArea(int color);
    void TrimArea(int color, int x2, int y2);
};
