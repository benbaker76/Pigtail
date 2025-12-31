// Generates random names based on the statistical weight of letter sequences in a collection of sample names
// Original code by LucidDion

#include "MarkovNameGenerator.h"
#include "DeterministicRng.h"
#include <algorithm>
#include <cctype>

static std::string ToUpper(std::string s)
{
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

static std::string ToLower(std::string s)
{
    for (char& c : s) c = (char)std::tolower((unsigned char)c);
    return s;
}

static std::vector<std::string> Split(const std::string& s, char delim)
{
    std::vector<std::string> out;
    std::string cur;
    for (char c : s)
    {
        if (c == delim) { out.push_back(cur); cur.clear(); }
        else cur.push_back(c);
    }
    out.push_back(cur);
    return out;
}

MarkovNameGenerator::MarkovNameGenerator(const std::vector<std::string>& sampleNames,
                                         std::uint32_t id,
                                         int order,
                                         int minLength,
                                         int maxLength)
    : _random(DeterministicRng(id))
{
    // fix parameter values
    if (order < 1)
        order = 1;
    if (minLength < 1)
        minLength = 1;

    _order = order;
    _minLength = minLength;
    _maxLength = maxLength;
    _sampleList = sampleNames;

    // Build chains
    for (const std::string& word : _sampleList)
    {
        for (int letter = 0; letter < (int)word.size() - order; letter++)
        {
            std::string token = word.substr((size_t)letter, (size_t)order);
            auto& entry = _chainDictionary[token];
            entry.push_back(word[(size_t)letter + (size_t)order]);
        }
    }
}

bool MarkovNameGenerator::IsUsed(const std::string& s) const
{
    return std::find(_usedList.begin(), _usedList.end(), s) != _usedList.end();
}

char MarkovNameGenerator::GetLetter(const std::string& token)
{
    auto it = _chainDictionary.find(token);
    if (it == _chainDictionary.end())
        return '?';

    const auto& letters = it->second;
    int n = _random.Next((int)letters.size());
    return letters[(size_t)n];
}

std::string MarkovNameGenerator::NextName()
{
    std::string s;

    do
    {
        s.clear();

        int n = _random.Next((int)_sampleList.size());
        const std::string& sample = _sampleList[(size_t)n];
        int nameLength = (int)sample.size();

        int start = _random.Next(0, (int)sample.size() - _order);
        s = sample.substr((size_t)start, (size_t)_order);

        while ((int)s.size() < nameLength)
        {
            std::string token = s.substr(s.size() - (size_t)_order, (size_t)_order);
            char c = GetLetter(token);
            if (c == '?')
                break;

            s.push_back(c);
        }

        // Formatting behavior matches the provided C# version.
        if (s.find(' ') != std::string::npos)
        {
            auto tokens = Split(s, ' ');
            std::string rebuilt;
            for (auto& t : tokens)
            {
                if (t.empty()) continue;
                if (t.size() == 1)
                    t = ToUpper(t);
                else
                    t = t.substr(0, 1) + ToLower(t.substr(1));

                if (!rebuilt.empty())
                    rebuilt.push_back(' ');
                rebuilt += t;
            }
            s = rebuilt;
        }
        else if (!s.empty())
        {
            s = s.substr(0, 1) + ToLower(s.substr(1));
        }

        if ((int)s.size() > _maxLength)
        {
            s.clear();
            Reset();
        }
    }
    while (IsUsed(s) || (int)s.size() < _minLength);

    _usedList.push_back(s);
    return s;
}

void MarkovNameGenerator::Reset()
{
    _usedList.clear();
}
