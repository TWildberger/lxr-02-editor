#include <catch2/catch_test_macros.hpp>

#include "kitforge/ParamSync.h"
#include "kitforge/SysEx.h"

using namespace kitforge;

namespace
{
int countBytes(const std::vector<Bytes>& v)
{
    int n = 0;
    for (const auto& b : v)
        n += (int) b.size();
    return n;
}
} // namespace

TEST_CASE("only changed values are sent and automation is coalesced")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    std::vector<Bytes> out;
    CHECK_FALSE(sync.hasPending());

    for (int v = 0; v < 100; ++v)
        sync.setTarget(pid::PAR_COARSE1, v);
    CHECK(sync.hasPending());
    auto r = sync.collect(enc, false, 10000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == Bytes { 0xB0, (uint8_t) (pid::PAR_COARSE1 + 1), 99 });
    CHECK(r.bytes == 3);

    out.clear();
    sync.setTarget(pid::PAR_COARSE1, 99); // unchanged
    sync.collect(enc, false, 10000, out);
    CHECK(out.empty());
}

TEST_CASE("values received from the device are not echoed back")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    std::vector<Bytes> out;
    sync.noteDeviceValue(pid::PAR_COARSE2, 40);
    sync.setTarget(pid::PAR_COARSE2, 40);
    sync.collect(enc, false, 10000, out);
    CHECK(out.empty());
    CHECK(sync.target(pid::PAR_COARSE2) == 40);
}

TEST_CASE("full resend respects the byte budget and resumes")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    enc.setRunningNrpn(false);
    sync.setTargets(Kit::init());

    std::vector<Bytes> all;
    int rounds = 0;
    while (sync.hasPending() && rounds < 1000)
    {
        std::vector<Bytes> out;
        const auto r = sync.collect(enc, false, 64, out);
        CHECK(r.bytes <= 64 + 9); // a single NRPN may overshoot the budget
        all.insert(all.end(), out.begin(), out.end());
        ++rounds;
    }
    CHECK(rounds > 5);
    CHECK_FALSE(sync.hasPending());

    // every sendable parameter appears exactly once (CC or NRPN value message)
    int valueMsgs = 0;
    for (const auto& m : all)
        if (m[1] != 99 && m[1] != 98)
            ++valueMsgs;
    int expected = 0;
    const Kit k = Kit::init();
    for (int i = 0; i < kNumSoundParams; ++i)
        if (ParamTable::get(i).scope == Scope::Sound && ParamTable::get(i).label && StockMidiEncoder::canSend(i, k.get(i)).ok)
            ++expected;
    CHECK(valueMsgs == expected);

    // invalidating the device resends everything once more
    sync.invalidateDevice();
    std::vector<Bytes> again;
    while (sync.hasPending())
        sync.collect(enc, false, 1 << 20, again);
    CHECK(countBytes(again) == countBytes(all));
}

TEST_CASE("unsendable values are reported in stock mode and sent as SysEx in extended mode")
{
    StockMidiEncoder enc(1);
    {
        ParamSync sync;
        std::vector<Bytes> out;
        sync.setTarget(pid::PAR_VOICE_LFO1, 3);
        const auto r = sync.collect(enc, false, 1000, out);
        CHECK(out.empty());
        REQUIRE(r.unsendable.size() == 1);
        CHECK(r.unsendable[0] == pid::PAR_VOICE_LFO1);
    }
    {
        ParamSync sync;
        std::vector<Bytes> out;
        sync.setTarget(pid::PAR_VOICE_LFO1, 3);
        const auto r = sync.collect(enc, true, 1000, out);
        CHECK(r.unsendable.empty());
        REQUIRE(out.size() == 1);
        const auto msg = ext::parse(out[0]);
        REQUIRE(msg.has_value());
        CHECK(msg->command == ext::Command::ParamSet);
        CHECK(msg->param == pid::PAR_VOICE_LFO1);
        CHECK(msg->value == 3);
    }
}

TEST_CASE("markInSync suppresses a resend after a kit dump")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    Kit k = Kit::init();
    k.set(pid::PAR_FX_TYPE, 4);
    sync.setTargets(k);
    sync.markInSync(k);
    std::vector<Bytes> out;
    sync.collect(enc, false, 100000, out);
    CHECK(out.empty());
}
