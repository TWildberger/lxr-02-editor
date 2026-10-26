#include <catch2/catch_test_macros.hpp>

#include <algorithm>

#include "kitforge/Midi.h"

using namespace kitforge;

TEST_CASE("CC params encode as CC param+1")
{
    StockMidiEncoder enc(10);
    const auto m = enc.encodeParam(pid::PAR_COARSE1, 64);
    REQUIRE(m.size() == 1);
    CHECK(m[0].bytes[0] == 0xB9);
    CHECK(m[0].bytes[1] == pid::PAR_COARSE1 + 1);
    CHECK(m[0].bytes[2] == 64);
}

TEST_CASE("NRPN params encode with CC99/98/6 and running status")
{
    StockMidiEncoder enc(1);
    const int param = pid::PAR_FX_DRY_WET;
    auto m = enc.encodeParam(param, 50);
    REQUIRE(m.size() == 3);
    const int n = param - 128;
    CHECK(m[0] == MidiMessage::cc(1, 99, n >> 7));
    CHECK(m[1] == MidiMessage::cc(1, 98, n & 0x7F));
    CHECK(m[2] == MidiMessage::cc(1, 6, 50));
    // same NRPN again: only data entry
    m = enc.encodeParam(param, 51);
    REQUIRE(m.size() == 1);
    CHECK(m[0] == MidiMessage::cc(1, 6, 51));
    enc.setRunningNrpn(false);
    CHECK(enc.encodeParam(param, 52).size() == 3);
}

TEST_CASE("mutes use NRPN 1000+")
{
    StockMidiEncoder enc(1);
    const auto m = enc.encodeMute(2, true);
    REQUIRE(m.size() == 3);
    CHECK(m[0].bytes[2] == (1002 >> 7));
    CHECK(m[1].bytes[2] == (1002 & 0x7F));
    CHECK(m[2].bytes[2] == 1);
}

TEST_CASE("stock limitations are enforced")
{
    CHECK(!StockMidiEncoder::canSend(pid::PAR_VOICE_LFO1, 2).ok);
    CHECK(!StockMidiEncoder::canSend(pid::PAR_MORPH_KIT_DEST, 2).ok);
    CHECK(!StockMidiEncoder::canSend(pid::PAR_BPM, 120).ok);
    CHECK(!StockMidiEncoder::canSend(0, 0).ok);
    CHECK(StockMidiEncoder::canSend(pid::PAR_TARGET_LFO1, 0).ok);

    // targets whose parameter is < 128 can be sent as a raw parameter number
    const auto r = ParamTable::modTargetRange(1);
    int sendable = 0, blocked = 0;
    StockMidiEncoder enc(1);
    for (int t = r.first; t <= r.last; ++t)
    {
        auto msgs = enc.encodeParam(pid::PAR_TARGET_LFO1, t);
        if (msgs.empty())
        {
            ++blocked;
            continue;
        }
        ++sendable;
        CHECK(msgs.back().bytes[2] == ParamTable::modTargets()[t].param);
    }
    CHECK(sendable > 0);
    CHECK(blocked > 0);
}

TEST_CASE("kit encoding and decoding round trip through the decoder")
{
    Kit kit;
    StockMidiEncoder enc(3);
    std::vector<int> skipped;
    const auto msgs = enc.encodeKit(kit, &skipped);
    CHECK(!msgs.empty());
    CHECK(!skipped.empty());

    Kit received;
    for (int i = 0; i < kNumSoundParams; ++i)
        received.set(i, 0);
    StockMidiDecoder dec;
    for (const auto& m : msgs)
    {
        StockMidiDecoder::Update u {};
        if (dec.feed(m.bytes, m.size, u))
            received.set(u.param, u.value);
    }
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        if (ParamTable::get(i).scope != Scope::Sound || std::find(skipped.begin(), skipped.end(), i) != skipped.end())
            continue;
        INFO(ParamTable::get(i).id);
        CHECK(received.get(i) == kit.get(i));
    }
}

TEST_CASE("kit delta only sends changes")
{
    Kit a, b;
    b.set(pid::PAR_COARSE1, 70);
    b.set(pid::PAR_FX_DRY_WET, 10);
    StockMidiEncoder enc(1);
    const auto msgs = enc.encodeKitDelta(a, b);
    CHECK(msgs.size() == 4);
}

TEST_CASE("kit and pattern select")
{
    StockMidiEncoder enc(2);
    CHECK(enc.encodeKitSelect(5) == MidiMessage::cc(2, 0, 5));
    const auto pc = enc.encodePatternSelect(16, 7);
    CHECK(pc.size == 2);
    CHECK(pc.bytes[0] == 0xCF);
    CHECK(pc.bytes[1] == 7);
}
