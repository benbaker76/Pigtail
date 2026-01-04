#pragma once
#include <cstdint>
#include <vector>

// Simple 2D 4bpp grid (row-major, packed: 2 pixels per byte).
// Pixel (x,y) is stored as a nibble:
//   even x -> high nibble, odd x -> low nibble.
class Indexed4bppImage
{
public:
    Indexed4bppImage() = default;
    Indexed4bppImage(int width, int height) { Reset(width, height); }

    void Reset(int width, int height)
    {
        _w = width;
        _h = height;
        _strideBytes = (_w + 1) >> 1; // 2 pixels per byte
        _data.assign((size_t)_strideBytes * (size_t)_h, 0);
    }

    int Width()  const { return _w; }
    int Height() const { return _h; }

    // Bytes per row in the packed buffer
    int StrideBytes() const { return _strideBytes; }

    bool InBounds(int x, int y) const
    {
        return (unsigned)x < (unsigned)_w && (unsigned)y < (unsigned)_h;
    }

    // Read pixel value (0..15)
    std::uint8_t At(int x, int y) const
    {
        const size_t i = ByteIndex(x, y);
        const std::uint8_t b = _data[i];

        if ((x & 1) == 0)   // even x -> high nibble
            return (std::uint8_t)((b >> 4) & 0x0F);
        else                // odd x -> low nibble
            return (std::uint8_t)(b & 0x0F);
    }

    // Write pixel value (only low 4 bits are stored)
    void Set(int x, int y, std::uint8_t value)
    {
        value &= 0x0F;

        const size_t i = ByteIndex(x, y);
        std::uint8_t b = _data[i];

        if ((x & 1) == 0)   // even x -> high nibble
            b = (std::uint8_t)((b & 0x0F) | (value << 4));
        else                // odd x -> low nibble
            b = (std::uint8_t)((b & 0xF0) | value);

        _data[i] = b;
    }

    // Optional: allow "grid.At(x,y) = v;" via a proxy object.
    class PixelRef
    {
    public:
        PixelRef(Indexed4bppImage& g, int x, int y) : _g(g), _x(x), _y(y) {}

        operator std::uint8_t() const
        {
            // IMPORTANT: force const overload, otherwise infinite recursion
            return static_cast<const Indexed4bppImage&>(_g).At(_x, _y);
        }

        PixelRef& operator=(std::uint8_t v)
        {
            _g.Set(_x, _y, v);
            return *this;
        }

        PixelRef& operator=(const PixelRef& other)
        {
            return (*this = static_cast<std::uint8_t>(other));
        }

    private:
        Indexed4bppImage& _g;
        int _x, _y;
    };

    // Non-const overload returns a proxy, not a uint8_t&
    PixelRef At(int x, int y)
    {
        return PixelRef(*this, x, y);
    }

    std::vector<std::uint8_t>& Raw() { return _data; }
    const std::vector<std::uint8_t>& Raw() const { return _data; }

    // Packed byte size (NOT pixel count)
    size_t Size() const { return _data.size(); }

private:
    size_t ByteIndex(int x, int y) const
    {
        // row-major packed: each row has _strideBytes bytes
        // within row: byte is x/2
        return (size_t)y * (size_t)_strideBytes + (size_t)(x >> 1);
    }

private:
    int _w = 0, _h = 0;
    int _strideBytes = 0;
    std::vector<std::uint8_t> _data;
};
