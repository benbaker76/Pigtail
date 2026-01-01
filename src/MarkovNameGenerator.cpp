// Generates random names based on the statistical weight of letter sequences in a collection of sample names
// Original code by LucidDion

#include "MarkovNameGenerator.h"
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

void MarkovNameGenerator::BuildChains()
{
    _chainDictionary.clear();

    for (const std::string& word : _sampleList)
    {
        const int L = (int)word.size();
        if (L <= _order) continue;

        for (int i = 0; i + _order < L; ++i)
        {
            std::string token = word.substr((size_t)i, (size_t)_order);
            _chainDictionary[token].push_back(word[(size_t)i + (size_t)_order]);
        }
    }
}

MarkovNameGenerator::MarkovNameGenerator(const std::vector<std::string>& sampleNames,
                                         int order,
                                         int minLength,
                                         int maxLength)
{
    if (order < 1) order = 1;
    if (minLength < 1) minLength = 1;
    if (maxLength < minLength) maxLength = minLength;

    _order = order;
    _minLength = minLength;
    _maxLength = maxLength;
    _sampleList = sampleNames;

    // Reserve used-list capacity once to minimize heap churn.
    _usedList.reserve(kUsedCap);

    BuildChains();
}

void MarkovNameGenerator::Reset(std::uint32_t id)
{
    _usedList.clear();
    _usedWrite = 0;
    _usedFull = false;

    // Two deterministic streams derived from id.
    // Use uint64_t so it works cleanly with your RNG Reset signature.
    _rngPick.Reset((std::uint64_t)id ^ 0xA5A5A5A5u);
    _rngChain.Reset((std::uint64_t)id ^ 0x5A5A5A5Au);

    // Do NOT rebuild chains here (chains depend only on samples + order).
}

bool MarkovNameGenerator::IsUsed(const std::string& s) const
{
    // Ring buffer is stored in _usedList in no guaranteed chronological order once full.
    return std::find(_usedList.begin(), _usedList.end(), s) != _usedList.end();
}

void MarkovNameGenerator::AddUsed(const std::string& s)
{
    if (!_usedFull)
    {
        _usedList.push_back(s);
        if (_usedList.size() >= kUsedCap)
        {
            _usedFull = true;
            _usedWrite = 0;
        }
        return;
    }

    // Overwrite oldest slot in a fixed cycle.
    _usedList[_usedWrite] = s;
    _usedWrite = (_usedWrite + 1) % kUsedCap;
}

char MarkovNameGenerator::GetLetter(const std::string& token)
{
    auto it = _chainDictionary.find(token);
    if (it == _chainDictionary.end() || it->second.empty())
        return '?';

    const auto& letters = it->second;
    const int n = _rngChain.Next((int)letters.size());
    return letters[(size_t)n];
}

std::string MarkovNameGenerator::NextName()
{
    if (_sampleList.empty())
        return std::string();

    // Deterministic bounded loop.
    constexpr int kMaxTries = 128;

    for (int attempt = 0; attempt < kMaxTries; ++attempt)
    {
        const std::string& sample = _sampleList[(size_t)_rngPick.Next((int)_sampleList.size())];
        const int L = (int)sample.size();
        if (L < _order) continue;

        const int targetLen = _rngPick.Next(_minLength, _maxLength + 1);

        // start in [0, L - _order] inclusive (implemented as maxExclusive = L - _order + 1).
        const int startMaxExclusive = std::max(1, L - _order + 1);
        const int start = _rngPick.Next(startMaxExclusive);

        std::string s = sample.substr((size_t)start, (size_t)_order);

        // Expand up to targetLen; cap steps so attempts are well-behaved.
        // Burn one chain draw on dead-ends to reduce sensitivity to early breaks.
        const int maxSteps = std::max(0, _maxLength - _order);

        for (int step = 0; step < maxSteps && (int)s.size() < targetLen; ++step)
        {
            std::string token = s.substr(s.size() - (size_t)_order, (size_t)_order);

            auto it = _chainDictionary.find(token);
            if (it == _chainDictionary.end() || it->second.empty())
            {
                (void)_rngChain.NextU32(); // burn draw for stability
                break;
            }

            const auto& letters = it->second;
            const int n = _rngChain.Next((int)letters.size());
            s.push_back(letters[(size_t)n]);
        }

        // Formatting (matches your updated behavior)
        if (s.find(' ') != std::string::npos)
        {
            auto tokens = Split(s, ' ');
            std::string rebuilt;
            for (auto& t : tokens)
            {
                if (t.empty()) continue;
                if (t.size() == 1) t = ToUpper(t);
                else               t = ToUpper(t.substr(0, 1)) + ToLower(t.substr(1));

                if (!rebuilt.empty()) rebuilt.push_back(' ');
                rebuilt += t;
            }
            s = rebuilt;
        }
        else if (!s.empty())
        {
            s = ToUpper(s.substr(0, 1)) + ToLower(s.substr(1));
        }

        if ((int)s.size() < _minLength || (int)s.size() > _maxLength)
            continue;

        if (IsUsed(s))
            continue;

        AddUsed(s);
        return s;
    }

    // Deterministic fallback (still depends only on sample list and constraints).
    std::string s = _sampleList[0];
    if ((int)s.size() > _maxLength) s.resize((size_t)_maxLength);
    if (!s.empty()) s = ToUpper(s.substr(0, 1)) + ToLower(s.substr(1));
    return s;
}
