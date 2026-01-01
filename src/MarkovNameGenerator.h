// Generates random names based on the statistical weight of letter sequences in a collection of sample names
// Original code by LucidDion

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "DeterministicRng.h"

class MarkovNameGenerator
{
public:
    void BuildChains();
    MarkovNameGenerator(const std::vector<std::string>& sampleNames,
                        int order,
                        int minLength,
                        int maxLength);

    std::string NextName();
    void Reset(std::uint32_t id);

private:
    std::map<std::string, std::vector<char>> _chainDictionary;
    std::vector<std::string> _sampleList;

    // Bounded "used names" ring buffer (keeps deterministic behavior stable, avoids erase-shifts).
    static constexpr std::size_t kUsedCap = 256;
    std::vector<std::string> _usedList;   // size <= kUsedCap (when full, we overwrite)
    std::size_t _usedWrite = 0;           // next overwrite slot when full
    bool _usedFull = false;

    // Two RNG streams to reduce sensitivity to rejection/loop behavior.
    DeterministicRng _rngPick;   // sample/start/target length
    DeterministicRng _rngChain;  // letter selection

    int _order = 1;
    int _minLength = 1;
    int _maxLength = 8;

    char GetLetter(const std::string& token);
    bool IsUsed(const std::string& s) const;
    void AddUsed(const std::string& s);
};
