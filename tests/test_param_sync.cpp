#include <catch2/catch_test_macros.hpp>

#include <map>

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
    auto r = sync.collect(enc, {}, 10000, out);
    REQUIRE(out.size() == 1);
    CHECK(out[0] == Bytes { 0xB0, (uint8_t) (pid::PAR_COARSE1 + 1), 99 });
    CHECK(r.bytes == 3);

    out.clear();
    sync.setTarget(pid::PAR_COARSE1, 99); // unchanged
    sync.collect(enc, {}, 10000, out);
    CHECK(out.empty());
}

TEST_CASE("values received from the device are not echoed back")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    std::vector<Bytes> out;
    sync.noteDeviceValue(pid::PAR_COARSE2, 40);
    sync.setTarget(pid::PAR_COARSE2, 40);
    sync.collect(enc, {}, 10000, out);
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
        const auto r = sync.collect(enc, {}, 64, out);
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
        sync.collect(enc, {}, 1 << 20, again);
    CHECK(countBytes(again) == countBytes(all));
}

TEST_CASE("unsendable values are reported in stock mode and sent as SysEx in extended mode")
{
    StockMidiEncoder enc(1);
    {
        ParamSync sync;
        std::vector<Bytes> out;
        sync.setTarget(pid::PAR_VOICE_LFO1, 3);
        const auto r = sync.collect(enc, {}, 1000, out);
        CHECK(out.empty());
        REQUIRE(r.unsendable.size() == 1);
        CHECK(r.unsendable[0] == pid::PAR_VOICE_LFO1);
    }
    {
        ParamSync sync;
        std::vector<Bytes> out;
        sync.setTarget(pid::PAR_VOICE_LFO1, 3);
        const auto r = sync.collect(enc, { true, 3 }, 1000, out);
        CHECK(r.unsendable.empty());
        REQUIRE(out.size() == 1);
        const auto msg = ext::parse(out[0]);
        REQUIRE(msg.has_value());
        CHECK(msg->command == ext::Command::ParamSet);
        CHECK(msg->device == 3);
        REQUIRE(msg->pairs.size() == 1);
        CHECK(msg->pairs[0] == ext::ParamPair { pid::PAR_VOICE_LFO1, 3 });
    }
}

TEST_CASE("extended mode batches a full kit into PARAM_SET frames")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    Kit k = Kit::init();
    k.set(pid::PAR_TARGET_LFO1, 200); // mod-target index travels unchanged over SysEx
    sync.setTargets(k);
    Globals g;
    g.set(pid::PAR_BPM, 133);
    sync.setTargets(g);

    std::vector<Bytes> out;
    const auto r = sync.collect(enc, { true, 0x7F }, 1 << 20, out);
    CHECK(r.unsendable.empty());
    std::map<int, int> sent;
    for (const auto& f : out)
    {
        CHECK(f.size() <= (size_t) ext::kMaxFrameLength);
        const auto m = ext::parse(f);
        REQUIRE(m);
        REQUIRE(m->command == ext::Command::ParamSet);
        CHECK(m->pairs.size() <= (size_t) ext::kMaxSetPairs);
        for (const auto& p : m->pairs)
            sent[p.id] = p.value;
    }
    for (int i = 0; i < kNumParams; ++i)
        CHECK(sent.count(i) == (size_t) ParamSync::isSyncable(i, true));
    CHECK(sent[pid::PAR_TARGET_LFO1] == 200);
    CHECK(sent[pid::PAR_BPM] == 133);
    CHECK(sent.count(pid::PAR_SYSEX_DEVICE_ID) == 0);
    CHECK(sent.count(pid::PAR_SYSEX_ENABLE) == 0);
    CHECK(sent.count(pid::PAR_SYSEX_ECHO) == 1);

    // a small budget spreads the work over several calls without losing anything
    ParamSync slow;
    slow.setTargets(k);
    std::vector<Bytes> parts;
    int calls = 0;
    while (slow.hasPending() && calls < 1000)
    {
        slow.collect(enc, { true, 0 }, 100, parts);
        ++calls;
    }
    CHECK(calls > 3);
    int pairsSent = 0;
    for (const auto& f : parts)
        pairsSent += (int) ext::parse(f)->pairs.size();
    int soundSyncable = 0;
    for (int i = 0; i < kNumSoundParams; ++i)
        soundSyncable += ParamSync::isSyncable(i, true);
    CHECK(pairsSent == soundSyncable);
}

TEST_CASE("stock mode never sends globals")
{
    ParamSync sync;
    StockMidiEncoder enc(1);
    Globals g;
    sync.setTargets(g);
    std::vector<Bytes> out;
    const auto r = sync.collect(enc, {}, 100000, out);
    CHECK(out.empty());
    CHECK(r.unsendable.empty());
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
    sync.collect(enc, {}, 100000, out);
    CHECK(out.empty());
}
