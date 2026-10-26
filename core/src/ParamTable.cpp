#include "kitforge/ParamTable.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace kitforge
{

namespace
{
const char* const kVoiceNames[] = { "Kit", "Drum 1", "Drum 2", "Drum 3", "Snare/Clap", "Cymbal", "Hi-Hat" };
const char* const kVoiceShort[] = { "Kit", "D1", "D2", "D3", "SN", "CY", "HH" };

std::string noteName(int n)
{
    static const char* const names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return std::string(names[n % 12]) + std::to_string(n / 12 - 1);
}

std::string signedStr(int v)
{
    return v > 0 ? "+" + std::to_string(v) : std::to_string(v);
}
} // namespace

const ParamInfo& ParamTable::get(int index)
{
    static_assert(kNumParams == pid::NUM_PARAMS, "param table mismatch");
    if (index < 0 || index >= size())
        return all()[0];
    return all()[index];
}

const ParamInfo* ParamTable::findByKey(const std::string& key)
{
    static const auto map = [] {
        std::unordered_map<std::string, int> m;
        for (int i = 0; i < size(); ++i)
            m[all()[i].key] = i;
        return m;
    }();
    const auto it = map.find(key);
    return it == map.end() ? nullptr : &all()[it->second];
}

const ParamInfo* ParamTable::findById(const std::string& id)
{
    for (int i = 0; i < size(); ++i)
        if (id == all()[i].id)
            return &all()[i];
    return nullptr;
}

const char* ParamTable::voiceName(int voice) { return (voice >= 0 && voice <= kNumVoices) ? kVoiceNames[voice] : ""; }
const char* ParamTable::voiceShortName(int voice) { return (voice >= 0 && voice <= kNumVoices) ? kVoiceShort[voice] : ""; }

std::vector<int> ParamTable::soundParamsInOrder()
{
    std::vector<int> out;
    for (int i = 0; i < size(); ++i)
        if (all()[i].scope == Scope::Sound && all()[i].label != nullptr)
            out.push_back(i);
    std::stable_sort(out.begin(), out.end(), [](int a, int b) { return all()[a].order < all()[b].order; });
    return out;
}

std::vector<int> ParamTable::paramsForVoice(int voice)
{
    std::vector<int> out;
    for (int i : soundParamsInOrder())
        if (all()[i].voice == voice)
            out.push_back(i);
    return out;
}

int ParamTable::clamp(int index, int value)
{
    const auto& p = get(index);
    if (p.dtype == DType::LfoTarget || p.dtype == DType::VeloTarget)
        return std::clamp(value, 0, numModTargets() - 1);
    return std::clamp(value, p.minValue, p.maxValue);
}

std::string ParamTable::formatValue(int index, int value)
{
    const auto& p = get(index);
    switch (p.dtype)
    {
        case DType::PM63:
            return signedStr(value - p.displayOffset);
        case DType::PM100:
            return signedStr(value - 100);
        case DType::OnOff:
            return value ? "on" : "off";
        case DType::Menu:
            if (p.valueNames && value >= 0 && value < p.numValueNames)
                return p.valueNames[value];
            return std::to_string(value);
        case DType::LfoTarget:
        case DType::VeloTarget:
        {
            if (value <= 0 || value >= numModTargets())
                return "none";
            const auto& t = modTargets()[value];
            return t.voice > 0 ? std::string(voiceShortName(t.voice)) + " " + t.label : std::string(t.label);
        }
        case DType::VoiceLfo:
            return (value >= 1 && value <= kNumVoices) ? voiceShortName(value) : std::to_string(value);
        case DType::Note:
            if (index >= pid::PAR_MIDI_NOTE1 && index <= pid::PAR_MIDI_NOTE7 && value == 0)
                return "any";
            return (value >= 0 && value <= 127) ? noteName(value) : std::to_string(value);
        default:
            break;
    }
    if (index == pid::PAR_BPM)
    {
        if (value == 0)
            return "EXT";
        if (value == 1)
            return "MID";
    }
    if (index == pid::PAR_MIDI_CHAN_GLOBAL && value == 0)
        return "all";
    return std::to_string(value);
}

std::string ParamTable::fullName(int index)
{
    const auto& p = get(index);
    if (!p.label)
        return p.id;
    std::string s;
    if (p.voice > 0)
        s = std::string(voiceName(p.voice)) + " ";
    if (p.section && std::string(p.section) != "FX" && std::string(p.section) != "Global")
        s += std::string(p.section) + " ";
    return s + p.label;
}

} // namespace kitforge
