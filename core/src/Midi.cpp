#include "kitforge/Midi.h"

#include <algorithm>

namespace kitforge
{

MidiMessage MidiMessage::cc(int channel1to16, int cc, int value)
{
    MidiMessage m;
    m.bytes[0] = (uint8_t) (0xB0 | ((channel1to16 - 1) & 0x0F));
    m.bytes[1] = (uint8_t) (cc & 0x7F);
    m.bytes[2] = (uint8_t) (value & 0x7F);
    m.size = 3;
    return m;
}

MidiMessage MidiMessage::programChange(int channel1to16, int program)
{
    MidiMessage m;
    m.bytes[0] = (uint8_t) (0xC0 | ((channel1to16 - 1) & 0x0F));
    m.bytes[1] = (uint8_t) (program & 0x7F);
    m.size = 2;
    return m;
}

void StockMidiEncoder::setChannel(int channel1to16)
{
    channel_ = std::clamp(channel1to16, 1, 16);
    lastNrpn_ = -1;
}

namespace
{
/** Stock firmware converts a raw param number into a mod-target index via a reverse table where the last entry wins. */
int stockModTargetParam(int modTargetIndex)
{
    if (modTargetIndex <= 0 || modTargetIndex >= ParamTable::numModTargets())
        return modTargetIndex == 0 ? 0 : -1;
    const int param = ParamTable::modTargets()[modTargetIndex].param;
    if (param >= 128)
        return -1;
    for (int i = ParamTable::numModTargets() - 1; i >= 0; --i)
        if (ParamTable::modTargets()[i].param == param)
            return i == modTargetIndex ? param : -1;
    return -1;
}
int modTargetFromStockParam(int param)
{
    for (int i = ParamTable::numModTargets() - 1; i >= 0; --i)
        if (ParamTable::modTargets()[i].param == param)
            return i;
    return 0;
}
} // namespace

LiveSendability StockMidiEncoder::canSend(int param, int value)
{
    const auto& p = ParamTable::get(param);
    if (p.scope != Scope::Sound || p.midiKind == MidiKind::None)
        return { false, "not a MIDI-controllable sound parameter" };
    if (p.dtype == DType::VoiceLfo)
        return { false, "LFO voice is ignored by stock firmware over MIDI" };
    if (param == pid::PAR_MORPH_KIT_DEST)
        return { false, "morph destination is not applied live by stock firmware" };
    if (p.dtype == DType::LfoTarget || p.dtype == DType::VeloTarget)
    {
        if (stockModTargetParam(value) < 0)
            return { false, "this modulation target cannot be selected over MIDI on stock firmware" };
        return {};
    }
    if (value > 127)
        return { false, "value exceeds 7-bit MIDI range" };
    return {};
}

void StockMidiEncoder::appendNrpn(std::vector<MidiMessage>& out, int nrpn, int value)
{
    if (!runningNrpn_ || nrpn != lastNrpn_)
    {
        out.push_back(MidiMessage::cc(channel_, 99, (nrpn >> 7) & 0x7F));
        out.push_back(MidiMessage::cc(channel_, 98, nrpn & 0x7F));
        lastNrpn_ = nrpn;
    }
    out.push_back(MidiMessage::cc(channel_, 6, value));
}

std::vector<MidiMessage> StockMidiEncoder::encodeParam(int param, int value)
{
    std::vector<MidiMessage> out;
    if (!canSend(param, value).ok)
        return out;
    const auto& p = ParamTable::get(param);
    int v = value;
    if (p.dtype == DType::LfoTarget || p.dtype == DType::VeloTarget)
        v = stockModTargetParam(value);
    if (p.midiKind == MidiKind::CC)
        out.push_back(MidiMessage::cc(channel_, p.midiNumber, v));
    else
        appendNrpn(out, p.midiNumber, v);
    return out;
}

std::vector<MidiMessage> StockMidiEncoder::encodeKit(const Kit& kit, std::vector<int>* skipped)
{
    std::vector<MidiMessage> out;
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (p.scope != Scope::Sound)
            continue;
        auto msgs = encodeParam(i, kit.get(i));
        if (msgs.empty() && skipped)
            skipped->push_back(i);
        out.insert(out.end(), msgs.begin(), msgs.end());
    }
    return out;
}

std::vector<MidiMessage> StockMidiEncoder::encodeKitDelta(const Kit& from, const Kit& to, std::vector<int>* skipped)
{
    std::vector<MidiMessage> out;
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        if (ParamTable::get(i).scope != Scope::Sound || from.get(i) == to.get(i))
            continue;
        auto msgs = encodeParam(i, to.get(i));
        if (msgs.empty() && skipped)
            skipped->push_back(i);
        out.insert(out.end(), msgs.begin(), msgs.end());
    }
    return out;
}

std::vector<MidiMessage> StockMidiEncoder::encodeMute(int track0to6, bool muted)
{
    std::vector<MidiMessage> out;
    if (track0to6 < 0 || track0to6 > 6)
        return out;
    appendNrpn(out, 1000 + track0to6, muted ? 1 : 0);
    return out;
}

MidiMessage StockMidiEncoder::encodeKitSelect(int ramSlot) const { return MidiMessage::cc(channel_, 0, ramSlot); }

MidiMessage StockMidiEncoder::encodePatternSelect(int globalChannel1to16, int pattern) const
{
    return MidiMessage::programChange(globalChannel1to16, pattern);
}

std::vector<uint8_t> StockMidiEncoder::toBytes(const std::vector<MidiMessage>& msgs)
{
    std::vector<uint8_t> b;
    for (const auto& m : msgs)
        b.insert(b.end(), m.bytes, m.bytes + m.size);
    return b;
}

bool StockMidiDecoder::feed(const uint8_t* msg, int size, Update& out)
{
    if (size < 3 || (msg[0] & 0xF0) != 0xB0)
        return false;
    const int cc = msg[1] & 0x7F;
    const int val = msg[2] & 0x7F;
    if (cc == 99)
    {
        nrpnMsb_ = val;
        return false;
    }
    if (cc == 98)
    {
        nrpnLsb_ = val;
        return false;
    }
    if (cc == 6)
    {
        const int nrpn = (nrpnMsb_ << 7) | nrpnLsb_;
        const int param = nrpn + 128;
        if (param >= kNumSoundParams)
            return false;
        const auto dt = ParamTable::get(param).dtype;
        out = { param, (dt == DType::LfoTarget || dt == DType::VeloTarget) ? modTargetFromStockParam(val) : val };
        return true;
    }
    if (cc == 0)
        return false;
    const int param = cc - 1;
    if (param <= 0 || param >= 127 || ParamTable::get(param).scope != Scope::Sound)
        return false;
    out = { param, val };
    return true;
}

} // namespace kitforge
