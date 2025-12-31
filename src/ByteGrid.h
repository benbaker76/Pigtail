#pragma once
#include <cstdint>
#include <vector>

// Simple 2D byte grid (row-major contiguous storage).
class ByteGrid
{
public:
    ByteGrid() = default;
    ByteGrid(int width, int height) { Reset(width, height); }

    void Reset(int width, int height)
    {
        _w = width;
        _h = height;
        _data.assign((size_t)_w * (size_t)_h, 0);
    }

    int Width()  const { return _w; }
    int Height() const { return _h; }

    bool InBounds(int x, int y) const
    {
        return (unsigned)x < (unsigned)_w && (unsigned)y < (unsigned)_h;
    }

    std::uint8_t& At(int x, int y)
    {
        return _data[(size_t)y * (size_t)_w + (size_t)x];
    }

    std::uint8_t At(int x, int y) const
    {
        return _data[(size_t)y * (size_t)_w + (size_t)x];
    }

    std::vector<std::uint8_t>& Raw() { return _data; }
    const std::vector<std::uint8_t>& Raw() const { return _data; }

private:
    int _w = 0, _h = 0;
    std::vector<std::uint8_t> _data;
};
