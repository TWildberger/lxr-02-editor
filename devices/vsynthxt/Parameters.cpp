#include "Parameters.h"
#include <algorithm>
#include <unordered_map>

namespace vsynthxt
{
const std::vector<Parameter>& parameters()
{
#include "NativeParams.gen.inc"
    static const std::vector<Parameter> all = [] {
        std::vector<Parameter> out {
            {"xt.common.coarse", "Coarse tune", "Patch", "Common", 0, 13, 1, 16, 112, 64, 8},
            {"xt.common.fine", "Fine tune", "Patch", "Common", 0, 14, 1, 14, 114, 64, 8},
            {"xt.chorus.type", "Chorus type", "Effects", "Chorus", 0, 512, 1, 0, 8, 0, 11}
        };
        out.insert(out.end(), nativeParameters.begin(), nativeParameters.end());
        return out;
    }();
    return all;
}
const Parameter& parameter(const std::string& key)
{
    static const std::unordered_map<std::string, const Parameter*> index = [] {
        std::unordered_map<std::string, const Parameter*> out;
        for (const auto& p : parameters()) out.emplace(p.key, &p);
        return out;
    }();
    const auto it = index.find(key);
    if (it == index.end()) throw std::runtime_error("Unknown XT parameter: " + key);
    return *it->second;
}
int parameterValue(const Patch& patch, const Parameter& p)
{
    int value = 0;
    for (int i = 0; i < p.width; ++i)
    {
        const auto digit = patch.value(p.offset + static_cast<Address>(i));
        if (p.width > 1)
        {
            if (digit > 15) throw std::runtime_error("Invalid nibble encoding at " + std::string(p.key));
            value = (value << 4) | digit;
        }
        else value = digit;
    }
    return value - p.bias;
}
void setParameter(Patch& patch, const Parameter& p, int displayValue)
{
    if (!patch.complete()) throw std::runtime_error("Fetch or import a complete patch first");
    if (displayValue < p.minimum - p.bias || displayValue > p.maximum - p.bias)
        throw std::runtime_error("XT parameter value outside documented range");
    const int value = displayValue + p.bias;
    auto it = patch.data.upper_bound(p.offset);
    if (it == patch.data.begin()) throw std::runtime_error("XT parameter outside patch");
    --it;
    const auto relative = p.offset - it->first;
    if (relative + static_cast<size_t>(p.width) > it->second.size()) throw std::runtime_error("XT parameter crosses block");
    auto encoded = value;
    for (int i = p.width - 1; i >= 0; --i)
    {
        it->second[relative + static_cast<size_t>(i)] = static_cast<uint8_t>(p.width > 1 ? encoded & 15 : encoded);
        encoded >>= 4;
    }
}
}
