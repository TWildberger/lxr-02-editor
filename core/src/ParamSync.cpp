#include "kitforge/ParamSync.h"

#include "kitforge/SysEx.h"

namespace kitforge
{

namespace
{
bool isSyncable(int param)
{
    const auto& p = ParamTable::get(param);
    return p.scope == Scope::Sound && p.label != nullptr;
}
} // namespace

ParamSync::ParamSync()
{
    for (auto& t : target_)
        t.store(kUnknown);
    for (auto& d : device_)
        d.store(kUnknown);
}

void ParamSync::setTarget(int param, int value)
{
    if (param < 0 || param >= kNumSoundParams)
        return;
    if (target_[(size_t) param].exchange(value) != value)
        dirty_.store(true);
}

int ParamSync::target(int param) const
{
    return (param >= 0 && param < kNumSoundParams) ? target_[(size_t) param].load() : kUnknown;
}

void ParamSync::noteDeviceValue(int param, int value)
{
    if (param < 0 || param >= kNumSoundParams)
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

bool ParamSync::hasPending() const
{
    return dirty_.load();
}

ParamSync::Result ParamSync::collect(StockMidiEncoder& encoder, bool extendedSysEx, int byteBudget, std::vector<Bytes>& out)
{
    Result r;
    if (!dirty_.exchange(false))
        return r;

    bool stillPending = false;
    for (int n = 0; n < kNumSoundParams; ++n)
    {
        const int i = (cursor_ + n) % kNumSoundParams;
        const int want = target_[(size_t) i].load();
        if (want == kUnknown || device_[(size_t) i].load() == want || !isSyncable(i))
            continue;

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
        else if (extendedSysEx)
        {
            out.push_back(ext::paramSet(i, want));
            r.bytes += (int) out.back().size();
        }
        else
        {
            r.unsendable.push_back(i);
        }
        device_[(size_t) i].store(want);
    }
    if (!stillPending)
        cursor_ = 0;
    if (stillPending)
        dirty_.store(true);
    return r;
}

} // namespace kitforge
