// RetroAvatar.h
// Original code by Richard Phipps
// Updates by Ben Baker

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <array>

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
    void DrawAvatar(ByteGrid& imageData, int offsetX, int offsetY, int scale);

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

    static constexpr int kSemanticColorCount = 7; // NONE..BODY inclusive

    bool _symX = true;
    bool _symY = false;
    int  _noise = 4;

    // Keep avatar size fixed for predictable memory footprint.
    Size _avatarSize{ 12, 12 };

    ByteGrid _avatarData;

    // Semantic color -> palette index (fixed, no heap)
    std::array<std::uint8_t, kSemanticColorCount> _colorIndices;

    // Flood fill queue: fixed capacity = W*H (no heap)
    static constexpr size_t kMaxFloodFillCells = 12u * 12u;
    std::array<Point, kMaxFloodFillCells> _ffQueue{};
    size_t _ffHead = 0;
    size_t _ffTail = 0;

    static const std::uint8_t  kFontData[];
    static const std::vector<std::string>& Names();
    static const std::vector<std::string>& Names2();

    void EnsureAvatarBuffer();
    void ClearAvatar(std::uint8_t value);

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

    // Flood fill queue helpers
    void FfClear();
    bool FfPush(Point p);
    bool FfPop(Point& out);
};
