#include <catch2/catch_test_macros.hpp>

#include <random>

#include "kitforge/Kit.h"
#include "kitforge/SysEx.h"

using namespace kitforge;

TEST_CASE("pack7/unpack7 round trip for all lengths")
{
    std::mt19937 rng(1);
    for (size_t n = 0; n < 40; ++n)
    {
        Bytes data(n);
        for (auto& b : data)
            b = (uint8_t) rng();
        const auto packed = sysex::pack7(data);
        CHECK(packed.size() == sysex::packedSize(n));
        for (auto b : packed)
            CHECK((b & 0x80) == 0);
        const auto back = sysex::unpack7(packed);
        REQUIRE(back);
        CHECK(*back == data);
    }
    CHECK(sysex::packedSize(255) == 292);
}

TEST_CASE("pack7 known vector")
{
    const Bytes in { 0x80, 0x01, 0xFF };
    const Bytes expect { 0x05, 0x00, 0x01, 0x7F };
    CHECK(sysex::pack7(in) == expect);
    CHECK(!sysex::unpack7(Bytes { 0x80, 0x00 }));
    CHECK(!sysex::unpack7(Bytes { 0x01 }));
}

TEST_CASE("checksum")
{
    const Bytes d { 0x10, 0x20, 0x7F };
    const auto c = sysex::checksum(d.data(), d.size());
    CHECK(((0x10 + 0x20 + 0x7F + c) & 0x7F) == 0);
}

TEST_CASE("extended protocol messages round trip")
{
    ext::Identity id;
    id.protocolVersion = 1;
    id.fwMajor = 1;
    id.fwMinor = 8;
    id.fwPatch = 2;
    id.capabilities = ext::CapKitDump | ext::CapParamEcho | ext::CapFullModTargets;
    auto m = ext::parse(ext::identityReply(id));
    REQUIRE(m);
    CHECK(m->command == ext::Command::IdentityReply);
    CHECK(m->identity.fwMinor == 8);
    CHECK(m->identity.has(ext::CapParamEcho));
    CHECK(!m->identity.has(ext::CapPatternDump));

    Kit kit;
    kit.set(pid::PAR_TARGET_LFO1, 200);
    kit.setName("Dump");
    m = ext::parse(ext::kitDump(12, kit.toSnd()));
    REQUIRE(m);
    CHECK(m->command == ext::Command::KitDump);
    CHECK(m->slot == 12);
    CHECK(Kit::fromSnd(m->data) == kit);

    m = ext::parse(ext::paramSet(pid::PAR_MORPH_KIT_DEST, 200));
    REQUIRE(m);
    CHECK(m->param == pid::PAR_MORPH_KIT_DEST);
    CHECK(m->value == 200);

    m = ext::parse(ext::globalsDump(Globals().toCfg()));
    REQUIRE(m);
    CHECK(Globals::fromCfg(m->data) == Globals());

    m = ext::parse(ext::kitRequest());
    REQUIRE(m);
    CHECK(m->slot == ext::kEditBuffer);
}

TEST_CASE("corrupt and foreign messages are rejected")
{
    auto msg = ext::paramSet(10, 20);
    msg[7] ^= 1;
    std::string err;
    CHECK(!ext::parse(msg, &err));
    CHECK(err == "checksum mismatch");
    CHECK(!ext::parse(ext::universalIdentityRequest()));
    CHECK(!ext::parse(Bytes { 0xF0, 0x00, 0x20, 0x29, 0x01, 0x02, 0x03, 0xF7 }));
}

TEST_CASE("assembler splits a byte stream")
{
    sysex::Assembler a;
    const auto m1 = ext::identityRequest();
    const auto m2 = ext::kitRequest(3);
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
}
