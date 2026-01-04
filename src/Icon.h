#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <string_view>

#include "Indexed4bppImage.h"
#include "FontRenderer.h"
#include "DeterministicRng.h"
#include "RetroAvatar.h"
#include "MarkovNameGenerator.h"
#include "Geometry.h"
#include "Icons.h"

static constexpr int COLOR_TEXT  = 6;
static constexpr int COLOR_TEXT_MAC = 0;
static constexpr int COLOR_COLON_BG = 1;
static constexpr int COLOR_BAR_BG = 1;

static constexpr int SCALE_1X = 1;
static constexpr int SCALE_2X = 2;
static constexpr int SCALE_4X = 4;
static constexpr int SCALE_8X = 8;

// Indexed4bppImage is a row-major 2D byte buffer with Reset(w,h) and Raw() returning std::vector<uint8_t>&
// FontRenderer exposes DrawText(std::vector<uint8_t>&, int w, int h, uint8_t color, int x, int y, std::string_view)
// and glyph dimensions (GlyphWidth/GlyphHeight constants or equivalent).
class Icon
{
public:
    enum class IconType
    {
        RetroAvatar,
        RetroAvatarWithMac,
        LargeIconWithMac
    };

    Icon();
    void Reset(std::uint32_t id);
    void Reset(std::uint32_t id, std::string macAddress);

    void DrawName(int offsetY);
    void DrawMacAddress();
    void DrawIcon(const std::uint8_t* iconData, Rect iconRect, int colorIndex);
    void DrawIcon(IconType iconType,
                  float bar1Value,
                  std::uint8_t bar1ColorIndex,
                  float bar2Value,
                  std::uint8_t bar2ColorIndex,
                  const std::uint8_t* largeIcon,
                  std::uint8_t largeIconColorIndex,
                  const std::uint8_t* smallIcon1,
                  std::uint8_t smallIcon1ColorIndex,
                  const std::uint8_t* smallIcon2,
                  std::uint8_t smallIcon2ColorIndex);
    void DrawAvatar(Indexed4bppImage &imageData, int offsetX, int offsetY, int scale) {
        _retroAvatar.DrawAvatar(imageData, offsetX, offsetY, scale);
    }

    // Access the rendered indexed image (32x32).
    const Indexed4bppImage& ImageData() const { return _imageData; }
    Indexed4bppImage& ImageData() { return _imageData; }

    const std::vector<std::uint8_t>& Pixels() const { return _imageData.Raw(); }
    int ImageW() const { return _imageW; }
    int ImageH() const { return _imageH; }
    std::string &Name() { return _name; }
    std::string &MacAddress() { return _macAddress; }

private:
    std::string GenerateRandomMac();
    static std::uint8_t MapByteToColorIndex(std::uint8_t value);
    static bool TryParseMacBytes(std::string_view mac, std::array<std::uint8_t, 6>& outBytes);

    void DrawVerticalBar(Rect rect, float value, std::uint8_t colorIndex);
    void DrawHorizontalBar(Rect rect, float value, std::uint8_t colorIndex);
    void DrawRect(Rect rect, std::uint8_t colorIndex);

    std::uint32_t _id = 0;
    DeterministicRng _random;
    std::string _macAddress;
    FontRenderer _fontRenderer;
    MarkovNameGenerator _markovNameGenerator;
    RetroAvatar _retroAvatar;

    Size _iconSize{ 32, 32 };
    Size _glyphSize{ 4, 5 };

    // Output image size
    int _imageW = 32;
    int _imageH = 32;

    std::string _name;

    Indexed4bppImage _imageData;
};
