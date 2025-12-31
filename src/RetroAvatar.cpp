// RetroAvatar.cpp
// Original code by Richard Phipps
// Updates by Ben Baker

#include "RetroAvatar.h"
#include <deque>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <iostream>

RetroAvatar::RetroAvatar(std::uint32_t id)
    : _random(DeterministicRng(id))
    , _colorIndices({ 0, 7, 2, 3, 4, 5, 6 })
{
}

void RetroAvatar::GenerateAvatar()
{
    _avatarData.Reset(_avatarSize.w, _avatarSize.h);

    GeneratePalette();
    GrowBitmap();
}

void RetroAvatar::DrawAvatar(ByteGrid& imageData, int offsetX, int offsetY, int scale)
{
    // Source is always the avatar grid (fixed size).
    // offsetX/offsetY are the source top-left in _avatarData (same semantics as your current code).
    Rect srcRect{ offsetX, offsetY, _avatarSize.w, _avatarSize.h };

    for (int y = 0; y < srcRect.h; y++)
    {
        for (int x = 0; x < srcRect.w; x++)
        {
            std::uint8_t src = _avatarData.At(x, y);
            std::uint8_t colorIndex = _colorIndices[(size_t)src];

            // Destination top-left for this avatar pixel (scaled).
            const int dstX0 = srcRect.x + (x * scale);
            const int dstY0 = srcRect.y + (y * scale);

            // Fill a scale x scale block, clipped to image bounds.
            for (int sy = 0; sy < scale; ++sy)
            {
                const int dy = dstY0 + sy;
                if ((unsigned)dy >= (unsigned)imageData.Height())
                    continue;

                for (int sx = 0; sx < scale; ++sx)
                {
                    const int dx = dstX0 + sx;
                    if ((unsigned)dx >= (unsigned)imageData.Width())
                        continue;

                    imageData.At(dx, dy) = colorIndex;
                }
            }
        }
    }
}

void RetroAvatar::GeneratePalette()
{
    std::vector<bool> used(16, false);

    used[_colorIndices[(size_t)COLOR_NONE]] = true;
    used[_colorIndices[(size_t)COLOR_TEXT]] = true;

    for (int i = COLOR_EYE; i < (int)_colorIndices.size(); i++)
    {
        std::uint8_t idx = 0;
        do { idx = (std::uint8_t)_random.Next(16); } while (used[idx]);

        _colorIndices[(size_t)i] = idx;
        used[idx] = true;
    }
}

void RetroAvatar::FloodFill(int x, int y, std::uint8_t color)
{
    std::uint8_t floodTo = color;
    std::uint8_t floodFrom = _avatarData.At(x, y);
    _avatarData.At(x, y) = floodTo;

    if (floodFrom == floodTo)
        return;

    std::deque<Point> q;
    q.push_back({ x, y });

    while (!q.empty())
    {
        Point cur = q.front();
        q.pop_front();

        const Point offsets[4] = { {0,-1},{0,1},{-1,0},{1,0} };
        for (const Point& off : offsets)
        {
            Point nxt{ cur.x + off.x, cur.y + off.y };
            if (_avatarData.InBounds(nxt.x, nxt.y) && _avatarData.At(nxt.x, nxt.y) == floodFrom)
            {
                q.push_back(nxt);
                _avatarData.At(nxt.x, nxt.y) = floodTo;
            }
        }
    }
}

void RetroAvatar::SetPixel(int x, int y, std::uint8_t c)
{
    if (!_avatarData.InBounds(x, y))
        return;
    _avatarData.At(x, y) = c;
}

std::uint8_t RetroAvatar::GetPixel(int x, int y) const
{
    if (!_avatarData.InBounds(x, y))
        return 0;
    return _avatarData.At(x, y);
}

void RetroAvatar::GrowBitmap()
{
    for (int y = 0; y < _avatarData.Height(); y++)
    {
        for (int x = 0; x < _avatarData.Width(); x++)
        {
            // Very simple! The higher the value of C the more solid pixels are placed.
            int c = 158;
            if (_random.Next(32767) % 356 > c)
                SetPixel(x, y, (std::uint8_t)COLOR_NONE); // Empty
            else
                SetPixel(x, y, (std::uint8_t)COLOR_BODY); // Solid
        }
    }

    // All other colour values are used for reserved area of the image (eyes, nose & mouth).

    // Remove single pixel isolated noise and join up gaps 
    // (The higher the global noise value, the more times this is done).
    RemoveNoise(0);

    Mirror(); // Make processed image symmetrical.

    EnhanceFace(); // Identify (or create), and enhance facial characteristics.
}

void RetroAvatar::EnhanceFace()
{
    int x = 0, y = 0, ny = 0;
    bool eyesFound = false; // Found eyes, nose, mouth
    bool noseFound = false;
    bool mouthFound = false;
    int halfX = (_avatarData.Width() / 2) - 1; // Half width of sprite variable.

    // Detect eyes one pixel away from horizontal centre (look from just below the top edge to the middle of the vertical height).
    for (y = 1; y < _avatarData.Height() / 2; y++)
    {
        if (GetPixel(halfX - 1, y) == COLOR_NONE) // 0 - Empty, Pixel?
        {
            FloodFill(halfX - 1, y, (std::uint8_t)COLOR_EYE); // Mark area with reserved colour 1 (normal colours are 0 - empty & 255 - solid).
            
            if (CheckForFilledEdge() == COLOR_NONE) // If this eye area doesn't touch the edges of the image, then stop searching.
                break;
            
            FloodFill(halfX - 1, y, (std::uint8_t)COLOR_NONE); // It reaches the edge, so refill as 0 - empty and keep looking.
        }

        if (GetPixel(halfX - 2, y) == COLOR_NONE) // Any potential eye areas one pixel further away?
        {
            FloodFill(halfX - 2, y, (std::uint8_t)COLOR_EYE);
            
            if (CheckForFilledEdge() == COLOR_NONE)
                break;
            
            FloodFill(halfX - 2, y, (std::uint8_t)COLOR_NONE);
        }
    }

    if (y == _avatarData.Height() / 2) // Ok, we didn't find anything!
    {
        // Try to make eyes from any centre pixels (converting them to one pixel further away. i.e. xx -> x  x
        for (y = 1; y < _avatarData.Height(); y++)
        {
            if (GetPixel(halfX - 1, y) == COLOR_BODY && GetPixel(halfX, y) == COLOR_NONE)
            {
                SetPixel(halfX - 1, y, (std::uint8_t)COLOR_NONE);
                SetPixel(halfX, y, (std::uint8_t)COLOR_BODY);
                SetPixel(halfX - 1, y + 1, (std::uint8_t)COLOR_NONE); // Make the eye 2 pixels (at least) high.

                FloodFill(halfX - 1, y, (std::uint8_t)COLOR_EYE);
                
                if (CheckForFilledEdge() == COLOR_NONE)
                    break;
                
                FloodFill(halfX - 1, y, (std::uint8_t)COLOR_NONE);
            }
        }
    }

    ny = y + 1;

    if (y < _avatarData.Height())
        eyesFound = true; // Ok, we did find eyes

    if (!eyesFound) // Still NO eyes.
    {
        // Ok, create fake eyes!
        y = 1 + _random.Next(32767) % (_avatarData.Height() / 2);

        SetPixel(halfX - 1, y, (std::uint8_t)COLOR_EYE);

        OutlineArea(COLOR_EYE); // Outline to protect area.

        eyesFound = true;

        ny = y + 1;
    }

    // Remove any joined up eyes (i.e xx instead of x  x)
    for (y = 1; y < _avatarData.Height(); y++)
    {
        if (GetPixel(halfX, y) == COLOR_EYE)
        {
            SetPixel(halfX, y, (std::uint8_t)COLOR_BODY);
            SetPixel(halfX - 1, y, (std::uint8_t)COLOR_EYE);
        }
        else
        {
            if (GetPixel(halfX - 2, y) == COLOR_EYE)
                SetPixel(halfX, y, (std::uint8_t)COLOR_BODY);

            if (GetPixel(halfX - 1, y) == COLOR_EYE)
                SetPixel(halfX, y, (std::uint8_t)COLOR_BODY);
        }
    }

    Mirror(); // Mirror all eye work.

    if (eyesFound)
        OutlineArea(COLOR_EYE); // Protect eyes with solid outline.

    // -------

    // Detect nose
    for (y = ny; y < _avatarData.Height(); y++)
    {
        if (GetPixel(halfX, y) == COLOR_NONE)
        {
            FloodFill(halfX, y, (std::uint8_t)COLOR_NOSE); // Fill with area colour 2.
            
            if (CheckForFilledEdge() == COLOR_NONE)
                break;
            
            FloodFill(halfX, y, (std::uint8_t)COLOR_NONE);
        }
    }

    if (y < 10)
        noseFound = true;

    // No nose?
    if (!noseFound)
    {
        // Ok, we won't find a mouth either, but we need to make a nose/mouth one out of any open sections (regardless of touching the edge)
        for (y = ny; y < _avatarData.Height() - 1; y++)
        {
            if (GetPixel(halfX, y) == COLOR_NONE)
            {
                SetPixel(halfX, y, (std::uint8_t)COLOR_NOSE);
                
                noseFound = true;
                
                goto skip; // Found a nose in the centre.
            }
        }
         // Try to find a nose/mouth one pixel away which we can join up. i.e. x  x -> xxxx
        for (y = ny; y < _avatarData.Height() - 1; y++)
        {
            if (GetPixel(halfX - 1, y) == COLOR_NONE)
            {
                SetPixel(halfX - 1, y, (std::uint8_t)COLOR_NOSE);
                SetPixel(halfX, y, (std::uint8_t)COLOR_NOSE);

                noseFound = true;

                goto skip;
            }
        }

        // Ok, NOTHING, just create fake mouth/nose!
        y = ny + 1 + _random.Next(32767) % (_avatarData.Height() / 3);
        
        if (y > _avatarData.Height() - 2)
            y = _avatarData.Height() - 2;

        SetPixel(halfX, y, (std::uint8_t)COLOR_NOSE);

        noseFound = true;

        ny = y + 1;

        goto skip; // No need to check for mouth as in same X position as a nose.
    }

    ny = y + 1;

    // --------
    // Detect mouth
    for (y = ny; y < _avatarData.Height(); y++)
    {
        if (GetPixel(halfX, y) == COLOR_NONE)
        {
            FloodFill(halfX, y, (std::uint8_t)COLOR_MOUTH);

            if (CheckForFilledEdge() == COLOR_NONE)
                break;

            FloodFill(halfX, y, (std::uint8_t)COLOR_NONE);
        }
    }

    if (y < _avatarData.Height()) mouthFound = true;

    if (!mouthFound) // Still no mouse, so look one pixel further away and then if found, join up.
    {
        for (y = ny; y < _avatarData.Height() - 1; y++)
        {
            if (GetPixel(halfX - 1, y) == COLOR_NONE)
            {
                SetPixel(halfX, y, (std::uint8_t)COLOR_NONE);
                FloodFill(halfX, y, (std::uint8_t)COLOR_MOUTH);

                if (CheckForFilledEdge() == COLOR_NONE)
                    break;

                FloodFill(halfX, y, (std::uint8_t)COLOR_NONE);
            }
        }
    }

    if (y < _avatarData.Height())
        mouthFound = true;

skip:
    // Outline mouth / nose to protect and stop surrounding gfx 'bleeding' in.
    if (mouthFound)
        OutlineArea(COLOR_MOUTH); // Mouth

    if (noseFound)
        OutlineArea(COLOR_NOSE); // Nose
    
    if (eyesFound)
        TrimArea(COLOR_EYE, 3, 3); // Trim eyes to no more than 3 x 3

    if (noseFound && mouthFound)
        TrimArea(COLOR_NOSE, 3, 3); // Trim nose to no more than 3 x 3. Mouth can be bigger..;

    Mirror(); // Mirror to fix changes symmetrically.

    // Now search for any fill in any holes that doesn't leak to the edge of the sprite (passing over eyes, mouth and nose areas).
    for (y = 1; y < _avatarData.Height() - 1; y++)
    {
        for (x = 1; x < halfX - 1; x++)
        {
            if (GetPixel(x, y) == COLOR_NONE)
            {
                FloodFill(x, y, (std::uint8_t)COLOR_TEMP);

                int color = CheckForFilledEdge();

                if (color != COLOR_NONE)
                    FloodFill(x, y, (std::uint8_t)COLOR_NONE);
                else
                    FloodFill(x, y, (std::uint8_t)COLOR_BODY);
            }
        }
    }

    Mirror(); // Mirror finally (neccessary?)
}

int RetroAvatar::CheckForFilledEdge()
{
    for (int y = 0; y < _avatarData.Height(); y++)
    {
        int c = GetPixel(0, y);

        if (c > COLOR_NONE && c < COLOR_BODY)
            return COLOR_EYE;

        c = GetPixel(_avatarData.Width() - 1, y);

        if (c > COLOR_NONE && c < COLOR_BODY)
            return COLOR_EYE;
    }

    for (int x = 0; x < _avatarData.Width(); x++)
    {
        int c = GetPixel(x, 0);
        
        if (c > COLOR_NONE && c < COLOR_BODY)
            return COLOR_NOSE;

        c = GetPixel(x, _avatarData.Height() - 1);

        if (c > COLOR_NONE && c < COLOR_BODY)
            return COLOR_NOSE;
    }

    return COLOR_NONE;
}

void RetroAvatar::OutlineArea(int color)
{
    for (int y = 0; y < _avatarData.Height(); y++)
    {
        for (int x = 0; x < _avatarData.Width(); x++)
        {
            int c = GetPixel(x, y);

            if (c != color)
                continue;

            // diagonals are only outlined if blank (and not another reserved area).
            if (GetPixel(x - 1, y - 1) == COLOR_NONE)
                SetPixel(x - 1, y - 1, (std::uint8_t)COLOR_BODY);

            if (GetPixel(x,     y - 1) != color)
                SetPixel(x,     y - 1, (std::uint8_t)COLOR_BODY);
           
            if (GetPixel(x + 1, y - 1) == COLOR_NONE)
                SetPixel(x + 1, y - 1, (std::uint8_t)COLOR_BODY);

            if (GetPixel(x - 1, y) != color)
                SetPixel(x - 1, y, (std::uint8_t)COLOR_BODY);
            
            if (GetPixel(x + 1, y) != color)
                SetPixel(x + 1, y, (std::uint8_t)COLOR_BODY);

            if (GetPixel(x - 1, y + 1) == COLOR_NONE)
                SetPixel(x - 1, y + 1, (std::uint8_t)COLOR_BODY);

            if (GetPixel(x,     y + 1) != color)
                SetPixel(x,     y + 1, (std::uint8_t)COLOR_BODY);
            
            if (GetPixel(x + 1, y + 1) == COLOR_NONE)
                SetPixel(x + 1, y + 1, (std::uint8_t)COLOR_BODY);
        }
    }
}

void RetroAvatar::TrimArea(int color, int x2, int y2)
{
    int nx = -1, ny = -1, rx = 0;

    for (int y = 0; y < _avatarData.Height(); y++)
    {
        for (int x = 0; x < _avatarData.Width() / 2; x++)
        {
            if (color == COLOR_EYE) rx = x;
            if (color > COLOR_EYE)  rx = ((_avatarData.Width() / 2) - 1) - x;

            int c = GetPixel(rx, y);
            if (c != color) continue;

            if (nx == -1) nx = x;
            if (ny == -1) ny = y;

            if (x >= nx + x2) SetPixel(rx, y, (std::uint8_t)COLOR_BODY);
            if (y >= ny + y2) SetPixel(rx, y, (std::uint8_t)COLOR_BODY);
        }
    }
}

void RetroAvatar::RemoveNoise(int type)
{
    for (int c2 = 0; c2 < _noise; c2++)
    {
        // Remove isolated pixels
        for (int y = 0; y < _avatarData.Height(); y++)
        {
            for (int x = 0; x < _avatarData.Width(); x++)
            {
                int c = GetPixel(x, y);

                if (c == COLOR_NONE) // Black pixel.
                {
                    c = GetPixel(x, y - 1) + GetPixel(x - 1, y) + GetPixel(x - 1, y - 1) +
                        GetPixel(x + 1, y - 1) + GetPixel(x + 1, y) + GetPixel(x, y + 1) +
                        GetPixel(x - 1, y + 1) + GetPixel(x + 1, y + 1);

                    if (type == 0 && c >= 8 * COLOR_BODY)
                        SetPixel(x, y, (std::uint8_t)COLOR_BODY);
                    
                    if (type == 1 && c >= 7 * COLOR_BODY)
                        SetPixel(x, y, (std::uint8_t)COLOR_BODY);

                    // Join up 'one pixel' horizontal and vertical gaps, (adds a little order to the image).
                    if (GetPixel(x, y + 1) == COLOR_BODY && GetPixel(x, y - 1) == COLOR_BODY &&
                        GetPixel(x - 1, y) != COLOR_BODY && GetPixel(x + 1, y) != COLOR_BODY &&
                        _random.Next(32767) % 5 > 2)
                        SetPixel(x, y, (std::uint8_t)COLOR_BODY);

                    if (GetPixel(x, y + 1) != COLOR_BODY && GetPixel(x, y - 1) != COLOR_BODY &&
                        GetPixel(x - 1, y) == COLOR_BODY && GetPixel(x + 1, y) == COLOR_BODY &&
                        _random.Next(32767) % 5 > 2)
                        SetPixel(x, y, (std::uint8_t)COLOR_BODY);
                }
            }
        }

        // Remove isolated pixels
        for (int y = 0; y < _avatarData.Height(); y++)
        {
            for (int x = 0; x < _avatarData.Width(); x++)
            {
                int c = GetPixel(x, y);

                if (c == COLOR_BODY) // Solid coloured pixel.
                {
                    // Add up surrounding pixels.
                    c = GetPixel(x, y - 1) + GetPixel(x - 1, y) + GetPixel(x - 1, y - 1) +
                        GetPixel(x + 1, y - 1) + GetPixel(x + 1, y) + GetPixel(x, y + 1) +
                        GetPixel(x - 1, y + 1) + GetPixel(x + 1, y + 1);

                    // No lit pixels around this one.
                    if (c <= COLOR_NONE)
                        SetPixel(x, y, (std::uint8_t)COLOR_NONE);
                }
            }
        }
    }
}

void RetroAvatar::Mirror()
{
    for (int y = 0; y < _avatarData.Height(); y++)
    {
        // X Mirror
        for (int x = 0; x < _avatarData.Width() / 2; x++)
        {
            std::uint8_t c = GetPixel(x, y);

            if (_symX)
                SetPixel(_avatarData.Width() - 1 - x, y, c);
        }
    }

    for (int y = 0; y < _avatarData.Height() / 2; y++)
    {
        // Y Mirror
        for (int x = 0; x < _avatarData.Width(); x++)
        {
            std::uint8_t c = GetPixel(x, y);

            if (_symY)
                SetPixel(x, _avatarData.Height() - 1 - y, c);
        }
    }
}
