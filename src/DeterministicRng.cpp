#include "DeterministicRng.h"
#include <stdexcept>

DeterministicRng::DeterministicRng()
    : _state(0),
    _inc(0)
{
    Reset(0);
}

void DeterministicRng::Reset(std::uint64_t seed)
{
    _state = 0u;
    _inc = (seed << 1u) | 1u;

    (void)NextU32Internal();
    _state += seed;
    (void)NextU32Internal();
}

std::uint32_t DeterministicRng::NextU32()
{
    return NextU32Internal();
}

int DeterministicRng::Next()
{
    return (int)(NextU32Internal() & 0x7FFFFFFFu);
}

int DeterministicRng::Next(int maxExclusive)
{
    if (maxExclusive <= 0) return 0; // or 0..?
    return (int)NextU32Bounded((uint32_t)maxExclusive);
}

int DeterministicRng::Next(int minInclusive, int maxExclusive)
{
    if (maxExclusive <= minInclusive) return minInclusive;
    uint32_t range = (uint32_t)(maxExclusive - minInclusive);
    return minInclusive + (int)NextU32Bounded(range);
}

std::uint32_t DeterministicRng::NextU32Internal()
{
    std::uint64_t oldstate = _state;
    _state = oldstate * 6364136223846793005ULL + _inc;
    std::uint32_t xorshifted = (std::uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    std::uint32_t rot = (std::uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
}

std::uint32_t DeterministicRng::NextU32Bounded(std::uint32_t bound)
{
    if (bound == 0) return 0;
    std::uint32_t threshold = (std::uint32_t)(-bound) % bound;
    for (;;)
    {
        std::uint32_t r = NextU32Internal();
        if (r >= threshold)
            return r % bound;
    }
}