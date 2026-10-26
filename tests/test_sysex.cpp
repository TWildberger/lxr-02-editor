#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <random>

#include "kitforge/Kit.h"
#include "kitforge/Pattern.h"
#include "kitforge/SysEx.h"

#include "TestUtil.h"

using namespace kitforge;

namespace
{
ext::Message mustParse(const Bytes& f)
{
    std::string err;
    auto m = ext::parse(f, &err);
    INFO(err);
    REQUIRE(m);
    return *m;
}
} // namespace

TEST_CASE("packed8 matches the spec example and round-trips")
{
    CHECK(sysex::pack8(Bytes { 0x81, 0x02, 0xFF }) == Bytes { 0x01, 0x02, 0x7F, 0x05 });

    std::mt19937 rng(1);
    for (size_t n = 0; n < 40; ++n)
    {
        Bytes data(n);
        for (auto& b : data)
            b = (uint8_t) rng();
        const auto packed = sysex::pack8(data);
        CHECK(packed.size() == sysex::packedSize(n));
        CHECK(sysex::unpackedSize(packed.size()) == (int) n);
        for (auto b : packed)
            CHECK((b & 0x80) == 0);
        const auto back = sysex::unpack8(packed);
        REQUIRE(back);
        CHECK(*back == data);
    }
    CHECK(sysex::packedSize(8) == 10); // names
    CHECK(sysex::packedSize(196) == 224);
    CHECK(!sysex::unpack8(Bytes { 0x01 }));             // a lone byte cannot be a group
    CHECK(!sysex::unpack8(Bytes { 0x01, 0x02, 0x80 })); // MSB byte with bit 7 set
}

TEST_CASE("frames match the spec examples")
{
    CHECK(ext::identityRequest() == Bytes { 0xF0, 0x7D, 0x4C, 0x7F, 0x01, 0x01, 0x7F, 0xF7 });
    CHECK(ext::kitRequest(ext::kAllDevices, ext::kEditBuffer) == Bytes { 0xF0, 0x7D, 0x4C, 0x7F, 0x01, 0x10, 0x7F, 0x11, 0xF7 });
    CHECK(ext::paramSet(ext::kAllDevices, { { 3, 100 } }) == Bytes { 0xF0, 0x7D, 0x4C, 0x7F, 0x01, 0x41, 0x03, 0x00, 0x64, 0x00, 0x58, 0xF7 });
    const Bytes d { 0x10, 0x20, 0x7F };
    CHECK(sysex::checksum(d.data(), d.size()) == (0x10 ^ 0x20 ^ 0x7F));
}

TEST_CASE("identity reply round trip")
{
    ext::Identity id;
    id.fwMajor = 1;
    id.fwMinor = 7;
    id.fwPatch = 0;
    id.fwString = "1.7-sx";
    id.numParams = kNumParams;
    id.endOfSound = kNumSoundParams;
    id.firstGlobal = kFirstGlobal;
    id.kitSlots = id.patternSlots = 64;
    id.tracks = 7;
    id.steps = 64;
    id.patternLength = ext::kPatternRawLength;
    id.maxFrame = ext::kMaxFrameLength;
    id.capabilities = ext::CapKits | ext::CapPatterns | ext::CapGlobals | ext::CapParams | ext::CapEcho | ext::CapSave;
    id.deviceId = 5;
    id.echo = true;
    const auto f = ext::identityReply(id);
    CHECK(f[3] == 4); // replies carry the unit's own ID - 1
    const auto m = mustParse(f);
    CHECK(m.command == ext::Command::IdentityReply);
    CHECK(m.device == 4);
    const auto& r = m.identity;
    CHECK(r.fwString == "1.7-sx");
    CHECK(r.numParams == 305);
    CHECK(r.endOfSound == 247);
    CHECK(r.firstGlobal == 272);
    CHECK(r.patternLength == 3657);
    CHECK(r.maxFrame == 320);
    CHECK(r.deviceId == 5);
    CHECK(r.echo);
    CHECK(r.has(ext::CapSave));
    CHECK(r.layoutMatchesEditor());
    CHECK(!r.describe().empty());

    auto other = id;
    other.numParams = 302;
    CHECK(!mustParse(ext::identityReply(other)).identity.layoutMatchesEditor());
}

TEST_CASE("kit dump carries an .snd image")
{
    Kit kit = Kit::init();
    kit.set(pid::PAR_TARGET_LFO1, 200);
    kit.set(pid::PAR_FX_TYPE, 5); // out-of-range bytes pass through unchanged
    kit.setName("Dump");
    const auto f = ext::kitDump(0, 12, ext::kFlagSave, kit.toSnd());
    CHECK(f.size() <= (size_t) ext::kMaxFrameLength);
    const auto m = mustParse(f);
    CHECK(m.command == ext::Command::KitDump);
    CHECK(m.slot == 12);
    CHECK(m.flags == ext::kFlagSave);
    CHECK(m.count == kNumSoundParams);
    CHECK(m.name == "Dump");
    CHECK(Kit::fromSnd(m.data) == kit);

    // a dump whose data is shorter than its parameter count is rejected
    const Bytes payload(f.begin() + 6, f.end() - 2 - 8);
    std::string err;
    CHECK(!ext::parse(ext::frame(0, ext::Command::KitDump, payload), &err));
    CHECK(err == "malformed KIT_DUMP");
}

TEST_CASE("small commands round trip")
{
    auto m = mustParse(ext::kitStore(2, 9, ext::kFlagSave));
    CHECK((m.command == ext::Command::KitStore && m.slot == 9 && m.flags == 1 && m.device == 2));
    m = mustParse(ext::kitLoad(0, 63));
    CHECK((m.command == ext::Command::KitLoad && m.slot == 63));
    m = mustParse(ext::kitName(0, 4, "Boom"));
    CHECK((m.command == ext::Command::KitName && m.slot == 4 && m.name == "Boom"));
    CHECK(m.rawName == Bytes { 'B', 'o', 'o', 'm', ' ', ' ', ' ', ' ' });
    m = mustParse(ext::patternName(0, 7, "LongName12"));
    CHECK((m.command == ext::Command::PatternName && m.name == "LongName"));
    m = mustParse(ext::statusReply(0, { 3, 4, true, false }));
    CHECK((m.status.kitSlot == 3 && m.status.pattern == 4 && m.status.running && !m.status.echo));
    m = mustParse(ext::paramGet(0, { 1, 300 }));
    CHECK(m.ids == std::vector<int> { 1, 300 });
    m = mustParse(ext::paramValue(0, { { 272, 255 }, { 1, 0 } }));
    CHECK(m.pairs == std::vector<ext::ParamPair> { { 272, 255 }, { 1, 0 } });
    m = mustParse(ext::paramNotify(0, { { 5, 64 } }));
    CHECK(m.command == ext::Command::ParamNotify);
    m = mustParse(ext::echoSet(0, true));
    CHECK((m.command == ext::Command::EchoSet && m.on));
    m = mustParse(ext::event(0, ext::EventType::KitChanged, 17));
    CHECK((m.event == ext::EventType::KitChanged && m.arg == 17));
    m = mustParse(ext::ack(0, ext::Command::PatternChunk, 18));
    CHECK((m.command == ext::Command::Ack && m.answered == ext::Command::PatternChunk && m.arg == 18));
    m = mustParse(ext::nak(0, ext::Command::ParamSet, ext::Error::ReadOnly, 250));
    CHECK((m.error == ext::Error::ReadOnly && m.arg == 250 && m.answered == ext::Command::ParamSet));
    CHECK(std::string(ext::errorName(m.error)) == "read-only parameter");
}

TEST_CASE("param set frames split at 64 pairs and stay under the frame limit")
{
    std::vector<ext::ParamPair> pairs;
    for (int i = 0; i < 150; ++i)
        pairs.push_back({ i, 255 });
    const auto frames = ext::paramSetFrames(0, pairs);
    REQUIRE(frames.size() == 3);
    std::vector<ext::ParamPair> back;
    for (const auto& f : frames)
    {
        CHECK(f.size() <= (size_t) ext::kMaxFrameLength);
        const auto m = mustParse(f);
        back.insert(back.end(), m.pairs.begin(), m.pairs.end());
    }
    CHECK(back == pairs);
}

TEST_CASE("globals dump round trip")
{
    Globals g;
    g.set(pid::PAR_BPM, 140);
    const auto m = mustParse(ext::globalsDump(0, 0, kFirstGlobal, g.toCfg()));
    CHECK(m.first == 272);
    CHECK(m.count == 33);
    CHECK(Globals::fromCfg(m.data) == g);
}

TEST_CASE("pattern transfer: .pat <-> raw, chunking and reassembly")
{
    const auto pat = loadFixture("pattern_v3.pat");
    const auto raw = ext::patternRawFromPat(pat);
    REQUIRE(raw);
    CHECK(raw->size() == (size_t) ext::kPatternRawLength);
    CHECK(ext::patFromPatternRaw(*raw) == Pattern::fromPat(pat)->toPat());

    // older versions are normalised to V3 first
    const auto rawV1 = ext::patternRawFromPat(loadFixture("pattern_v1.pat"));
    REQUIRE(rawV1);
    CHECK(rawV1->size() == (size_t) ext::kPatternRawLength);

    const auto chunks = ext::patternChunks(0, 5, ext::kFlagSave, *raw);
    REQUIRE(chunks.size() == 19); // 18 x 196 + 129
    auto rx = std::make_shared<ext::PatternReceiver>();
    for (size_t i = 0; i < chunks.size(); ++i)
    {
        CHECK(chunks[i].size() <= (size_t) ext::kMaxFrameLength);
        const auto m = mustParse(chunks[i]);
        CHECK(m.flags == (i == 0 ? ext::kFlagSave : 0));
        const auto st = rx->feed(m);
        CHECK(st == (i + 1 == chunks.size() ? ext::PatternReceiver::State::Complete : ext::PatternReceiver::State::Receiving));
    }
    CHECK(rx->data() == *raw);
    CHECK(rx->slot() == 5); // chunks carry the real slot, also when 7F was requested

    // out-of-order chunks fail
    ext::PatternReceiver bad;
    bad.feed(mustParse(chunks[0]));
    CHECK(bad.feed(mustParse(chunks[2])) == ext::PatternReceiver::State::Failed);

    // any chunk size works
    const auto small = ext::patternChunks(0, 5, 0, *raw, 50);
    ext::PatternReceiver rx2;
    for (const auto& c : small)
        rx2.feed(mustParse(c));
    CHECK(rx2.state() == ext::PatternReceiver::State::Complete);
    CHECK(rx2.data() == *raw);
}

TEST_CASE("corrupt, foreign and wrong-version frames are rejected")
{
    auto msg = ext::paramSet(0, { { 10, 20 } });
    msg[7] ^= 1;
    std::string err;
    CHECK(!ext::parse(msg, &err));
    CHECK(err == "checksum mismatch");
    CHECK(!ext::parse(ext::universalIdentityRequest()));
    CHECK(!ext::parse(Bytes { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x02, 0x03, 0xF7 }));
    CHECK(!ext::parse(ext::frame(0, ext::Command::KitRequest, { 1 }, 2), &err));
    CHECK(err.find("version") != std::string::npos);
    CHECK(ext::parse(ext::frame(0x7F, ext::Command::IdentityRequest, {}, 2))); // always answerable
    CHECK(!ext::parse(ext::frame(0, ext::Command::KitRequest, { 1, 2 }))); // wrong length
    CHECK(!ext::parse(ext::frame(0, ext::Command::ParamSet, { 1, 2, 3 })));
    CHECK(!ext::parse(ext::frame(0, (ext::Command) 0x55, {})));
}

TEST_CASE("assembler splits a byte stream")
{
    sysex::Assembler a;
    const auto m1 = ext::identityRequest();
    const auto m2 = ext::kitRequest(0, 3);
    Bytes stream { 0x90, 0x40, 0x7F };
    stream.insert(stream.end(), m1.begin(), m1.end());
    stream.insert(stream.end(), m2.begin(), m2.begin() + 4);
    auto out = a.feed(stream.data(), stream.size());
    REQUIRE(out.size() == 1);
    CHECK(out[0] == m1);
    Bytes rest(m2.begin() + 4, m2.end());
    rest.insert(rest.begin() + 1, 0xF8); // realtime clock in the middle
    out = a.feed(rest.data(), rest.size());
    REQUIRE(out.size() == 1);
    CHECK(out[0] == m2);

    // a status byte aborts; oversized frames are dropped
    Bytes aborted { 0xF0, 0x7D, 0x4C, 0x90, 0x40, 0x7F };
    CHECK(a.feed(aborted.data(), aborted.size()).empty());
    sysex::Assembler tiny(16);
    Bytes big(40, 0x01);
    big.front() = 0xF0;
    big.back() = 0xF7;
    CHECK(tiny.feed(big.data(), big.size()).empty());
    CHECK(tiny.feed(m1.data(), m1.size()).size() == 1);
}

TEST_CASE("request queue keeps one request in flight")
{
    ext::RequestQueue q;
    std::vector<std::string> log;
    auto req = [&](const std::string& label, Bytes frame, ext::RequestQueue::Matcher m, int group = 0) {
        ext::RequestQueue::Request r;
        r.label = label;
        r.frame = std::move(frame);
        r.matcher = std::move(m);
        r.group = group;
        r.done = [&log, label](ext::RequestQueue::Outcome o, const std::vector<ext::Message>& replies) {
            const char* names[] = { "ok", "nak", "timeout", "cancelled" };
            log.push_back(label + ":" + names[(int) o] + ":" + std::to_string(replies.size()));
        };
        return r;
    };
    using C = ext::Command;
    q.push(req("id", ext::identityRequest(), ext::RequestQueue::replyTo(C::IdentityRequest, C::IdentityReply)));
    q.push(req("kit", ext::kitRequest(0x7F), ext::RequestQueue::replyTo(C::KitRequest, C::KitDump)));

    auto f = q.poll(0);
    REQUIRE(f);
    CHECK(*f == ext::identityRequest());
    CHECK(q.pending() == 2);
    CHECK(!q.poll(100)); // waiting for the reply
    CHECK(!q.onMessage(mustParse(ext::paramNotify(0, { { 1, 1 } })), 120)); // unrelated traffic is not consumed

    // the identity request times out; the same poll hands out the next request
    f = q.poll(ext::kReplyTimeoutMs);
    REQUIRE(f);
    CHECK(*f == ext::kitRequest(0x7F));
    REQUIRE(log.size() == 1);
    CHECK(log[0] == "id:timeout:0");

    Kit kit = Kit::init();
    CHECK(q.onMessage(mustParse(ext::kitDump(0, 0x7F, 0, kit.toSnd())), 700));
    CHECK(log.back() == "kit:ok:1");
    CHECK(q.idle());

    // a NAK fails the request and cancels the rest of its group, but not other requests
    const int g = q.newGroup();
    Bytes raw(ext::kPatternRawLength, 0);
    const auto chunks = ext::patternChunks(0, 3, 0, raw);
    for (int i = 0; i < 3; ++i)
        q.push(req("chunk" + std::to_string(i), chunks[(size_t) i], ext::RequestQueue::ackFor(C::PatternChunk, i), g));
    q.push(req("status", ext::statusRequest(0), ext::RequestQueue::replyTo(C::StatusRequest, C::StatusReply)));
    REQUIRE(q.poll(1000) == chunks[0]);
    CHECK(!q.onMessage(mustParse(ext::ack(0, C::PatternChunk, 5)), 1001)); // wrong chunk index
    CHECK(q.onMessage(mustParse(ext::ack(0, C::PatternChunk, 0)), 1002));
    REQUIRE(q.poll(1003) == chunks[1]);
    CHECK(q.onMessage(mustParse(ext::nak(0, C::PatternChunk, ext::Error::Sequence, 1)), 1004));
    CHECK(log[log.size() - 2] == "chunk1:nak:1");
    CHECK(log.back() == "chunk2:cancelled:0");
    REQUIRE(q.poll(1005) == ext::statusRequest(0));
    CHECK(q.onMessage(mustParse(ext::statusReply(0, {})), 1006));
    CHECK(log.back() == "status:ok:1");

    // a pattern download collects every chunk as one reply set
    auto rx = std::make_shared<ext::PatternReceiver>();
    auto pr = req("pattern", ext::patternRequest(0, 3), ext::RequestQueue::patternDump(rx));
    pr.timeoutMs = ext::kPatternTimeoutMs;
    q.push(pr);
    REQUIRE(q.poll(2000));
    for (const auto& c : ext::patternChunks(0, 3, 0, raw))
        CHECK(q.onMessage(mustParse(c), 2100));
    CHECK(log.back() == "pattern:ok:19");
    CHECK(rx->data() == raw);

    // requests without a matcher complete as soon as they are sent
    q.push(req("fire", ext::echoSet(0, false), nullptr));
    CHECK(q.poll(3000));
    CHECK(log.back() == "fire:ok:0");

    q.push(req("a", ext::statusRequest(0), ext::RequestQueue::replyTo(C::StatusRequest, C::StatusReply)));
    q.push(req("b", ext::statusRequest(0), ext::RequestQueue::replyTo(C::StatusRequest, C::StatusReply)));
    q.poll(4000);
    q.cancelAll();
    CHECK(log.back() == "b:cancelled:0");
    CHECK(q.idle());
}
