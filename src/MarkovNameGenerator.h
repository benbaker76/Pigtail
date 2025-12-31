// Generates random names based on the statistical weight of letter sequences in a collection of sample names
// Original code by LucidDion

#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include "DeterministicRng.h"

// Generates random names based on statistical weight of letter sequences
// in a collection of sample names (Markov chain).
class MarkovNameGenerator
{
public:
    MarkovNameGenerator(const std::vector<std::string>& sampleNames,
                        std::uint32_t id,
                        int order,
                        int minLength,
                        int maxLength);

    std::string NextName();
    void Reset();

private:
    std::map<std::string, std::vector<char>> _chainDictionary;
    std::vector<std::string> _sampleList;
    std::vector<std::string> _usedList;

    DeterministicRng _random;
    int _order = 1;
    int _minLength = 1;
    int _maxLength = 8;

    char GetLetter(const std::string& token);
    bool IsUsed(const std::string& s) const;
};
