#include "kitforge/KitOps.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace kitforge
{

namespace
{
bool sameStr(const char* a, const char* b) { return a && b && std::strcmp(a, b) == 0; }

bool isDiscrete(const ParamInfo& p)
{
    switch (p.dtype)
    {
        case DType::Menu:
        case DType::OnOff:
        case DType::LfoTarget:
        case DType::VeloTarget:
        case DType::VoiceLfo:
        case DType::AutomTarget:
            return true;
        default:
            return false;
    }
}

int remapModTarget(int target, int toVoice)
{
    if (target <= 1 || target >= ParamTable::numModTargets())
        return target;
    const auto& t = ParamTable::modTargets()[target];
    const int eq = equivalentParam(t.param, toVoice);
    if (eq < 0)
        return 0;
    const auto r = ParamTable::modTargetRange(toVoice);
    for (int i = r.first; i <= r.last; ++i)
        if (ParamTable::modTargets()[i].param == eq)
            return i;
    return 0;
}
} // namespace

std::vector<ParamDiff> diffKits(const Kit& a, const Kit& b)
{
    std::vector<ParamDiff> out;
    for (int i : ParamTable::soundParamsInOrder())
        if (a.get(i) != b.get(i))
            out.push_back({ i, a.get(i), b.get(i) });
    return out;
}

std::vector<std::string> describeDiff(const std::vector<ParamDiff>& diff)
{
    std::vector<std::string> out;
    out.reserve(diff.size());
    for (const auto& d : diff)
        out.push_back(ParamTable::fullName(d.param) + ": " + ParamTable::formatValue(d.param, d.a) + " -> "
                      + ParamTable::formatValue(d.param, d.b));
    return out;
}

int equivalentParam(int param, int toVoice)
{
    const auto& p = ParamTable::get(param);
    if (!p.label || p.voice <= 0)
        return -1;
    if (p.voice == toVoice)
        return param;
    // voice-indexed parameters with no label match (e.g. MIDI channels) are not copied
    for (int i : ParamTable::paramsForVoice(toVoice))
    {
        const auto& q = ParamTable::get(i);
        if (sameStr(q.section, p.section) && sameStr(q.label, p.label))
            return i;
    }
    return -1;
}

int copyVoice(Kit& kit, int fromVoice, int toVoice, const Kit* sourceKit)
{
    const Kit& src = sourceKit ? *sourceKit : kit;
    const Kit snapshot = src;
    int n = 0;
    for (int i : ParamTable::paramsForVoice(fromVoice))
    {
        const int dst = equivalentParam(i, toVoice);
        if (dst < 0)
            continue;
        const auto& info = ParamTable::get(i);
        int v = snapshot.get(i);
        if (info.dtype == DType::VeloTarget)
            v = remapModTarget(v, toVoice);
        else if (info.dtype == DType::VoiceLfo && v == fromVoice)
            v = toVoice;
        else if (info.dtype == DType::LfoTarget)
        {
            // LFO targets point at the voice chosen by the voice-LFO param; remap only self-targeting LFOs
            const int t = v;
            if (t > 1 && t < ParamTable::numModTargets() && ParamTable::modTargets()[t].voice == fromVoice)
                v = remapModTarget(t, toVoice);
        }
        kit.set(dst, ParamTable::clamp(dst, v));
        ++n;
    }
    return n;
}

int randomizeKit(Kit& kit, const RandomizeOptions& opts, std::mt19937& rng)
{
    int n = 0;
    const double amt = std::clamp(opts.amount, 0.0, 1.0);
    for (int i : ParamTable::soundParamsInOrder())
    {
        const auto& p = ParamTable::get(i);
        if (opts.lockedParams.count(i) || !opts.voices.count(std::max(p.voice, 0)))
            continue;
        if (!opts.sections.empty() && (!p.section || !opts.sections.count(p.section)))
            continue;
        if (i == pid::PAR_MORPH_KIT_DEST || p.dtype == DType::VoiceLfo)
            continue;
        const bool isTarget = p.dtype == DType::LfoTarget || p.dtype == DType::VeloTarget;
        if (isTarget && !opts.includeModTargets)
            continue;
        if (!opts.includeWaveforms && p.dtype == DType::Menu && p.valueNames && p.numValueNames > 6
            && std::strcmp(p.valueNames[0], "Sin") == 0)
            continue;

        int lo = p.minValue, hi = p.maxValue;
        if (p.dtype == DType::Menu && p.valueNames && p.numValueNames > 6 && std::strcmp(p.valueNames[0], "Sin") == 0)
            hi = 5; // only built-in waveforms; user samples may not exist on the card
        if (p.dtype == DType::VeloTarget && p.voice > 0)
        {
            const auto r = ParamTable::modTargetRange(p.voice);
            lo = r.first;
            hi = r.last;
        }
        if (p.dtype == DType::LfoTarget)
        {
            const int lfoVoice = kit.get(pid::PAR_VOICE_LFO1 + (p.voice - 1));
            const auto r = ParamTable::modTargetRange(lfoVoice >= 1 && lfoVoice <= 6 ? lfoVoice : p.voice);
            lo = r.first;
            hi = r.last;
        }

        int v;
        if (amt >= 1.0 || isDiscrete(p))
        {
            if (!isDiscrete(p) || std::uniform_real_distribution<double>(0, 1)(rng) < amt)
                v = std::uniform_int_distribution<int>(lo, hi)(rng);
            else
                continue;
        }
        else
        {
            const double span = (hi - lo) * amt * 0.5;
            const double delta = std::uniform_real_distribution<double>(-span, span)(rng);
            v = (int) std::lround(std::clamp(kit.get(i) + delta, (double) lo, (double) hi));
        }
        if (v != kit.get(i))
        {
            kit.set(i, v);
            ++n;
        }
    }
    return n;
}

Kit morphKits(const Kit& a, const Kit& b, double t)
{
    t = std::clamp(t, 0.0, 1.0);
    Kit out = t < 0.5 ? a : b;
    for (int i : ParamTable::soundParamsInOrder())
    {
        const auto& p = ParamTable::get(i);
        if (isDiscrete(p))
            continue;
        out.set(i, (int) std::lround(a.get(i) + (b.get(i) - a.get(i)) * t));
    }
    return out;
}

} // namespace kitforge
