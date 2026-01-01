#pragma once
#include <cstdint>

// Deterministic RNG based on PCG32.
// - Fully deterministic across platforms (fixed-width arithmetic).
// - Provides Next() APIs similar to C# Random, but deterministic from a seed (id).
class DeterministicRng
{
public:
    DeterministicRng();
    void Reset(std::uint64_t seed);

    std::uint32_t NextU32();
    int Next();
    int Next(int maxExclusive);
    int Next(int minInclusive, int maxExclusive);

private:
    std::uint64_t _state = 0;
    std::uint64_t _inc   = 0; // must be odd

    std::uint32_t NextU32Internal();
    std::uint32_t NextU32Bounded(std::uint32_t bound);
};
