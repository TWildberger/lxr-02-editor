#include "kitforge/Kit.h"

#include <algorithm>
#include <cstring>

namespace kitforge
{

Kit::Kit()
{
    const auto* init = ParamTable::initKitBytes();
    const int n = std::min(ParamTable::initKitSize(), kFileSize);
    std::memcpy(rawName_.data(), init, kNameBytes);
    for (int i = kNameBytes; i < n; ++i)
        params_[(size_t) (i - kNameBytes)] = init[i];
}

Kit Kit::init() { return Kit(); }

Kit Kit::fromSnd(const Bytes& data, std::string* warning)
{
    Kit k;
    k.rawName_.fill(0);
    k.params_.fill(0);
    const size_t n = std::min(data.size(), (size_t) kFileSize);
    for (size_t i = 0; i < n; ++i)
    {
        if (i < (size_t) kNameBytes)
            k.rawName_[i] = data[i];
        else
            k.params_[i - kNameBytes] = data[i];
    }
    if (warning)
    {
        warning->clear();
        if (data.size() < (size_t) kFileSize)
            *warning = "short kit file (" + std::to_string(data.size()) + " bytes), padded with zeros";
        else if (data.size() > (size_t) kFileSize)
            *warning = "kit file has " + std::to_string(data.size() - kFileSize) + " trailing bytes, ignored";
    }
    return k;
}

Bytes Kit::toSnd() const
{
    Bytes out;
    out.reserve(kFileSize);
    out.insert(out.end(), rawName_.begin(), rawName_.end());
    out.insert(out.end(), params_.begin(), params_.end());
    return out;
}

std::string Kit::name() const { return decodeName(rawName_.data(), kNameBytes); }
void Kit::setName(const std::string& n) { encodeName(n, rawName_.data(), kNameBytes); }

int Kit::get(int param) const
{
    return (param >= 0 && param < kNumSoundParams) ? params_[(size_t) param] : 0;
}

void Kit::set(int param, int value)
{
    if (param >= 0 && param < kNumSoundParams)
        params_[(size_t) param] = (uint8_t) std::clamp(value, 0, 255);
}

int Kit::sanitize()
{
    int changed = 0;
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        const auto& info = ParamTable::get(i);
        if ((info.dtype == DType::LfoTarget || info.dtype == DType::VeloTarget) && params_[(size_t) i] >= ParamTable::numModTargets())
        {
            params_[(size_t) i] = 0;
            ++changed;
        }
    }
    return changed;
}

int Kit::clampToRanges()
{
    int changed = sanitize();
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        const auto& info = ParamTable::get(i);
        if (info.scope != Scope::Sound)
            continue;
        const int v = params_[(size_t) i];
        const int fixed = ParamTable::clamp(i, v);
        if (fixed != v)
        {
            params_[(size_t) i] = (uint8_t) fixed;
            ++changed;
        }
    }
    return changed;
}

Globals::Globals()
{
    for (int i = 0; i < kNumGlobals; ++i)
        values_[(size_t) i] = (uint8_t) ParamTable::get(kFirstGlobal + i).defaultValue;
}

Globals Globals::fromCfg(const Bytes& data, std::string* warning)
{
    Globals g;
    const size_t n = std::min(data.size(), (size_t) kFileSize);
    for (size_t i = 0; i < n; ++i)
        g.values_[i] = data[i];
    if (warning)
    {
        warning->clear();
        if (data.size() < (size_t) kFileSize)
            *warning = "short glo.cfg (" + std::to_string(data.size()) + " bytes); missing values keep defaults";
        else if (data.size() > (size_t) kFileSize)
            *warning = "glo.cfg has trailing bytes, ignored";
    }
    return g;
}

Bytes Globals::toCfg() const { return Bytes(values_.begin(), values_.end()); }

int Globals::get(int param) const
{
    const int i = param - kFirstGlobal;
    return (i >= 0 && i < kNumGlobals) ? values_[(size_t) i] : 0;
}

void Globals::set(int param, int value)
{
    const int i = param - kFirstGlobal;
    if (i >= 0 && i < kNumGlobals)
        values_[(size_t) i] = (uint8_t) std::clamp(value, 0, 255);
}

} // namespace kitforge
