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
    if (data.size() > (size_t) kFileSize)
        k.extension_.assign(data.begin() + kFileSize, data.end());
    if (warning)
    {
        warning->clear();
        if (data.size() < (size_t) kFileSize)
            *warning = "short kit file (" + std::to_string(data.size()) + " bytes), padded with zeros";
        else if (!k.extension_.empty() && !k.hasEngineExtension())
            *warning = "kit has an unknown extension; trailing bytes preserved without interpretation";
    }
    return k;
}

Bytes Kit::toSnd() const
{
    Bytes out;
    out.reserve(kFileSize + extension_.size());
    out.insert(out.end(), rawName_.begin(), rawName_.end());
    out.insert(out.end(), params_.begin(), params_.end());
    out.insert(out.end(), extension_.begin(), extension_.end());
    return out;
}

bool Kit::hasEngineExtension() const
{
    static const Bytes tag { 'E', 'n', 'g', 'V', '0', '1' };
    return extension_ == tag;
}

bool Kit::setEngineSettings(int voice, int engine, int timbreDecay, std::string* error)
{
    if (error)
        error->clear();
    if (voice < 1 || voice > 3 || engine < 0 || engine > 1 || timbreDecay < 0 || timbreDecay > 127)
    {
        if (error)
            *error = "invalid feedback-FM voice, engine or timbre decay";
        return false;
    }
    if (!extension_.empty() && !hasEngineExtension())
    {
        if (error)
            *error = "cannot edit engines in an unknown kit extension; raw bytes preserved";
        return false;
    }
    if (!hasEngineExtension())
    {
        std::fill(params_.begin() + kEngineFirst, params_.begin() + kTimbreDecayFirst + 3, 0);
        extension_ = { 'E', 'n', 'g', 'V', '0', '1' };
    }
    set(kEngineFirst + voice - 1, engine);
    set(kTimbreDecayFirst + voice - 1, timbreDecay);
    return true;
}

std::optional<Kit> Kit::fromTmpSnd(const Bytes& data, uint8_t& slot, std::string* error)
{
    if (error)
        error->clear();
    if (data.size() < 1 + (size_t) kNumSoundParams)
    {
        if (error)
            *error = "short tmp.snd: expected a slot and 247 parameter bytes";
        return std::nullopt;
    }
    slot = data.front();
    Bytes snd(kNameBytes, 0);
    snd.insert(snd.end(), data.begin() + 1, data.end());
    return fromSnd(snd, error);
}

Bytes Kit::toTmpSnd(uint8_t slot) const
{
    Bytes out { slot };
    out.insert(out.end(), params_.begin(), params_.end());
    out.insert(out.end(), extension_.begin(), extension_.end());
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
        else if (info.dtype == DType::VoiceLfo && (params_[(size_t) i] < 1 || params_[(size_t) i] > kNumVoices))
        {
            params_[(size_t) i] = 1;
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
    // keep the stock layout when reading a stock file so it round-trips byte for byte
    g.fileSize_ = data.size() > (size_t) kStockFileSize ? kFileSize : kStockFileSize;
    if (warning)
    {
        warning->clear();
        if (data.size() < (size_t) kStockFileSize)
            *warning = "short glo.cfg (" + std::to_string(data.size()) + " bytes); missing values keep defaults";
        else if (data.size() > (size_t) kStockFileSize && data.size() < (size_t) kFileSize)
            *warning = "glo.cfg has " + std::to_string(data.size()) + " bytes; missing extended settings keep defaults";
        else if (data.size() > (size_t) kFileSize)
            *warning = "glo.cfg has trailing bytes, ignored";
    }
    return g;
}

Bytes Globals::toCfg() const { return Bytes(values_.begin(), values_.begin() + fileSize_); }

int Globals::get(int param) const
{
    const int i = param - kFirstGlobal;
    return (i >= 0 && i < kNumGlobals) ? values_[(size_t) i] : 0;
}

void Globals::set(int param, int value)
{
    const int i = param - kFirstGlobal;
    if (i >= 0 && i < kNumGlobals)
    {
        values_[(size_t) i] = (uint8_t) std::clamp(value, 0, 255);
        if (i >= kStockFileSize)
            fileSize_ = kFileSize;
    }
}

} // namespace kitforge
