#include "kitforge/ParamSync.h"

#include "kitforge/SysEx.h"

namespace kitforge
{

bool ParamSync::isSyncable(int param, bool extended)
{
    if (param < 0 || param >= kNumParams)
        return false;
    const auto& p = ParamTable::get(param);
    if (p.label == nullptr)
        return false;
    if (p.scope == Scope::Sound)
        return true;
    // SysEx enable and device ID are read-only over SysEx so a host cannot lock itself out
    return extended && p.scope == Scope::Global && param != pid::PAR_SYSEX_ENABLE && param != pid::PAR_SYSEX_DEVICE_ID;
}

ParamSync::ParamSync()
{
    for (auto& t : target_)
        t.store(kUnknown);
    for (auto& d : device_)
        d.store(kUnknown);
}

void ParamSync::setTarget(int param, int value)
{
    if (param < 0 || param >= kNumParams)
        return;
    if (target_[(size_t) param].exchange(value) != value)
        dirty_.store(true);
}

int ParamSync::target(int param) const
{
    return (param >= 0 && param < kNumParams) ? target_[(size_t) param].load() : kUnknown;
}

void ParamSync::noteDeviceValue(int param, int value)
{
    if (param < 0 || param >= kNumParams)
        return;
    device_[(size_t) param].store(value);
    target_[(size_t) param].store(value);
}

void ParamSync::invalidateDevice()
{
    for (auto& d : device_)
        d.store(kUnknown);
    dirty_.store(true);
}

void ParamSync::markInSync(const Kit& kit)
{
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        target_[(size_t) i].store(kit.get(i));
        device_[(size_t) i].store(kit.get(i));
    }
}

void ParamSync::setTargets(const Kit& kit)
{
    for (int i = 0; i < kNumSoundParams; ++i)
        setTarget(i, kit.get(i));
}

void ParamSync::markInSync(const Globals& g)
{
    for (int i = kFirstGlobal; i < kNumParams; ++i)
    {
        target_[(size_t) i].store(g.get(i));
        device_[(size_t) i].store(g.get(i));
    }
}

void ParamSync::setTargets(const Globals& g)
{
    for (int i = kFirstGlobal; i < kNumParams; ++i)
        setTarget(i, g.get(i));
}

bool ParamSync::hasPending() const
{
    return dirty_.load();
}

ParamSync::Result ParamSync::collect(StockMidiEncoder& encoder, Transport transport, int byteBudget, std::vector<Bytes>& out)
{
    Result r;
    if (!dirty_.exchange(false))
        return r;

    constexpr int kFrameOverhead = 8, kPairBytes = 4;
    std::vector<ext::ParamPair> pairs;
    auto flushPairs = [&] {
        if (pairs.empty())
            return;
        out.push_back(ext::paramSet(transport.device, pairs));
        pairs.clear();
    };

    bool stillPending = false;
    for (int n = 0; n < kNumParams; ++n)
    {
        const int i = (cursor_ + n) % kNumParams;
        const int want = target_[(size_t) i].load();
        if (want == kUnknown || device_[(size_t) i].load() == want || !isSyncable(i, transport.extended))
            continue;

        if (transport.extended)
        {
            const int cost = kPairBytes + (pairs.empty() ? kFrameOverhead : 0);
            if (r.bytes + cost > byteBudget && r.bytes > 0)
            {
                stillPending = true;
                cursor_ = i;
                break;
            }
            pairs.push_back({ i, want });
            r.bytes += cost;
            if ((int) pairs.size() == ext::kMaxSetPairs)
                flushPairs();
        }
        else
        {
            if (r.bytes >= byteBudget)
            {
                stillPending = true;
                cursor_ = i;
                break;
            }
            if (StockMidiEncoder::canSend(i, want).ok)
            {
                for (const auto& m : encoder.encodeParam(i, want))
                {
                    out.emplace_back(m.bytes, m.bytes + m.size);
                    r.bytes += m.size;
                }
            }
            else
                r.unsendable.push_back(i);
        }
        device_[(size_t) i].store(want);
    }
    flushPairs();
    if (!stillPending)
        cursor_ = 0;
    else
        dirty_.store(true);
    return r;
}

} // namespace kitforge
