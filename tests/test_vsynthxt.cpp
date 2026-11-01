#include "VSynthXT.h"
#include "Parameters.h"
#include "Algorithms.h"
#include "companion/SysExAssembler.h"
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <set>

namespace xt = vsynthxt;
namespace
{
xt::Patch fixture()
{
    xt::Patch p;
    for (const auto& b : xt::blocks())
    {
        xt::Bytes bytes(b.size);
        for (size_t i = 0; i < bytes.size(); ++i) bytes[i] = uint8_t((i + b.offset) % 128);
        p.data.emplace(b.offset, bytes);
    }
    p.setName("XT TEST");
    p.setValue(0x0d, 64); p.setValue(0x0e, 64); p.setValue(4 * 128, 1);
    return p;
}
}
TEST_CASE("XT codecs match independent Roland page 19 examples", "[vsynthxt]")
{
    REQUIRE(xt::dataSet(0x10, xt::temporary + 4 * 128, {1}) ==
        xt::Bytes {0xf0,0x41,0x10,0x00,0x53,0x12,0x10,0,4,0,1,0x6b,0xf7});
    const xt::Bytes expected {0xf0,0x41,0x10,0,0x53,0x11,0x20,2,2,0,0,0,1,4,0x57,0xf7};
    REQUIRE(xt::request(0x10, (0x20u << 21) + 2 * 16384 + 2 * 128, 132) == expected);
    REQUIRE(xt::digits(127 + 1) == xt::Bytes {0,0,1,0});
    REQUIRE(xt::address(xt::digits(xt::temporary).data()) == xt::temporary);
    REQUIRE(xt::checksum({0}) == 0);
    REQUIRE_THROWS(xt::digits(1u << 28));
    REQUIRE_THROWS(xt::request(0x7f, xt::temporary, 1));
    REQUIRE_THROWS(xt::dataSet(0x10, xt::temporary, {128}));
    REQUIRE_THROWS(xt::dataSet(0x10, xt::temporary, {}));
    REQUIRE_THROWS(xt::dataSet(0x10, xt::temporary, xt::Bytes(257)));
    auto f = xt::dataSet(0x10, xt::temporary, {1,2});
    REQUIRE(xt::decode(f).data == xt::Bytes {1,2});
    f[10] ^= 1;
    REQUIRE_THROWS(xt::decode(f));
    f = xt::dataSet(0x10, xt::temporary, {1}); f[4] = 0x54;
    REQUIRE_THROWS(xt::decode(f));
    f = xt::dataSet(0x10, xt::temporary, {1}); f.pop_back();
    REQUIRE_THROWS(xt::decode(f));
}
TEST_CASE("XT block table matches reviewed machine readable source", "[vsynthxt]")
{
    std::ifstream input(VSYNTHXT_SCHEMA);
    const auto schema = nlohmann::json::parse(input);
    REQUIRE(xt::blocks().size() == 103);
    for (const auto& family : schema.at("blocks"))
    {
        for (int i = 0; i < family.at("count").get<int>(); ++i)
        {
            const auto offset = family.at("offset").get<xt::Address>() + i * family.at("stride").get<xt::Address>();
            auto it = std::find_if(xt::blocks().begin(), xt::blocks().end(), [&](auto& b) { return b.offset == offset; });
            REQUIRE(it != xt::blocks().end());
            REQUIRE(it->size == family.at("size").get<size_t>());
            REQUIRE(it->page == family.at("page").get<int>());
        }
    }
}
TEST_CASE("XT snapshots preserve all zones and reserved bytes", "[vsynthxt]")
{
    const auto p = fixture();
    REQUIRE(p.complete());
    auto q = xt::Patch::fromSysex(p.sysex(0x1f));
    REQUIRE(q == p);
    q.setName("NEW");
    q.setValue(0x0d, 112);
    REQUIRE(q.value(0x0d) == 112);
    REQUIRE(q.value(0x0e) == p.value(0x0e));
    REQUIRE(q.data.at(0x6f * 128) == p.data.at(0x6f * 128));
    REQUIRE_THROWS(q.setName("Too long for XT"));
    REQUIRE_THROWS(q.setValue(0x0d, 113));
    REQUIRE_THROWS(q.setValue(0, 1));
    auto partial = p; partial.data.erase(0);
    REQUIRE_FALSE(partial.complete());
    REQUIRE_THROWS(partial.sysex());
    auto bytes = p.sysex(); bytes.pop_back();
    REQUIRE_THROWS(xt::Patch::fromSysex(bytes));
    bytes = p.sysex(); bytes.push_back(0);
    REQUIRE_THROWS(xt::Patch::fromSysex(bytes));
    const auto f = xt::dataSet(0x10, 0x20u << 21, {1});
    REQUIRE_THROWS(xt::Patch::fromSysex(f));
}
TEST_CASE("XT receive assembly rejects corruption and handles reordered fragments", "[vsynthxt]")
{
    xt::Receiver receiver(xt::blocks().front());
    REQUIRE_FALSE(receiver.accept({0x11, xt::temporary, {1}}, 0x10));
    REQUIRE_FALSE(receiver.accept({0x10, xt::temporary + 4096, {1}}, 0x10));
    REQUIRE(receiver.accept({0x10, xt::temporary + 40, xt::Bytes(74, 3)}, 0x10));
    REQUIRE_FALSE(receiver.complete());
    REQUIRE(receiver.accept({0x10, xt::temporary, xt::Bytes(40, 2)}, 0x10));
    REQUIRE(receiver.complete());
    REQUIRE(receiver.accept({0x10, xt::temporary, {2}}, 0x10));
    REQUIRE_THROWS(receiver.accept({0x10, xt::temporary, {3}}, 0x10));
    REQUIRE_THROWS(receiver.accept({0x10, xt::temporary + 113, {1,2}}, 0x10));
    auto f = xt::dataSet(0x10, xt::temporary, {1,2});
    f.insert(f.begin() + 8, 0xf8);
    companion::SysExAssembler stream(268);
    REQUIRE(stream.feed(f.data(), 7).empty());
    auto messages = stream.feed(f.data() + 7, f.size() - 7);
    REQUIRE(messages.size() == 1);
    REQUIRE(xt::decode(messages[0]).data == xt::Bytes {1,2});
}
TEST_CASE("XT library uses isolated lossless blobs and metadata", "[vsynthxt]")
{
    xt::Library lib(":memory:");
    auto p = fixture();
    const auto id = lib.add(p, "pad, factory", "Project A waves required", true);
    REQUIRE(lib.get(id) == p);
    REQUIRE(lib.add(p, "pad", "Updated notes", false) == id);
    REQUIRE(lib.search("").size() == 1);
    REQUIRE(lib.search("UPDATED").size() == 1);
    REQUIRE_FALSE(lib.search("updated").front().favorite);
    p.setName("OTHER");
    REQUIRE(lib.add(p, "lead", "", true) != id);
    REQUIRE(lib.search("").size() == 2);
    REQUIRE(lib.search("lead").front().name == "OTHER");
    REQUIRE(lib.search("unmatched").empty());
    REQUIRE_THROWS(lib.get(-1));
}

TEST_CASE("XT native parameters have unique IDs and non-overlapping documented bytes", "[vsynthxt]")
{
    std::set<std::string> ids;
    std::set<xt::Address> occupied;
    auto p = fixture();
    for (const auto& info : xt::parameters())
    {
        REQUIRE(ids.insert(info.key).second);
        REQUIRE(info.width >= 1);
        bool within = false;
        for (const auto& b : xt::blocks())
            if (info.offset >= b.offset && info.offset + info.width <= b.offset + b.size) within = true;
        REQUIRE(within);
        for (int byte = 0; byte < info.width; ++byte) REQUIRE(occupied.insert(info.offset + byte).second);
        auto original = p;
        for (int value : {info.minimum - info.bias, info.maximum - info.bias})
        {
            xt::setParameter(p, info, value);
            REQUIRE(xt::parameterValue(p, info) == value);
        }
        REQUIRE_THROWS(xt::setParameter(p, info, info.minimum - info.bias - 1));
        REQUIRE_THROWS(xt::setParameter(p, info, info.maximum - info.bias + 1));
        auto expected = original;
        auto block = expected.data.upper_bound(info.offset);
        --block;
        for (int byte = 0; byte < info.width; ++byte)
            block->second.at(info.offset - block->first + static_cast<size_t>(byte)) = static_cast<uint8_t>(p.value(info.offset + byte));
        REQUIRE(p == expected);
    }
    REQUIRE(xt::parameters().size() > 4000);
    const auto& env = xt::parameter("xt.native.envelope.zone16.env13.attack");
    REQUIRE(env.offset == 0x3e * 128 + 12 * 13);
    xt::setParameter(p, env, 144);
    REQUIRE(p.value(env.offset) == 9);
    REQUIRE(p.value(env.offset + 1) == 0);
    const auto& cosm = xt::parameter("xt.native.cosm.zone1.cosm1_param1.value");
    xt::setParameter(p, cosm, -20000);
    REQUIRE(p.value(cosm.offset) == 3);
    REQUIRE(p.value(cosm.offset + 1) == 1);
    REQUIRE(p.value(cosm.offset + 2) == 14);
    REQUIRE(p.value(cosm.offset + 3) == 0);
    xt::setParameter(p, cosm, 20000);
    REQUIRE(xt::parameterValue(p, cosm) == 20000);
    auto invalid = p;
    invalid.data.at(0x50 * 128)[2] = 16;
    REQUIRE_THROWS(xt::parameterValue(invalid, cosm));
    REQUIRE(xt::Patch::fromSysex(invalid.sysex()) == invalid);
    REQUIRE_THROWS(xt::parameter("not.a.parameter"));
}

TEST_CASE("XT native schema templates match every generated zone and section", "[vsynthxt]")
{
    std::ifstream input(VSYNTHXT_NATIVE_SCHEMA);
    const auto schema = nlohmann::json::parse(input);
    size_t count = 3;
    for (const auto& group : schema.at("groups"))
        for (int zone = 0; zone < group.at("zones").get<int>(); ++zone)
            for (const auto& section : group.at("sections"))
                for (int instance = 0; instance < section.value("count", 1); ++instance)
                    for (const auto& row : section.at("params"))
                    {
                        std::string groupName = group.at("group").get<std::string>();
                        std::transform(groupName.begin(), groupName.end(), groupName.begin(),
                            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                        auto key = "xt.native." + groupName;
                        if (group.at("zones").get<int>() > 1) key += ".zone" + std::to_string(zone + 1);
                        key += "." + section.at("key").get<std::string>();
                        if (section.value("count", 1) > 1) key += std::to_string(instance + 1);
                        key += "." + row[0].get<std::string>();
                        const auto& p = xt::parameter(key);
                        REQUIRE(p.offset == group.at("base").get<int>() + zone * group.at("zone_stride").get<int>()
                            + section.at("base").get<int>() + instance * section.value("stride", 0) + row[2].get<int>());
                        REQUIRE(p.width == row[3].get<int>());
                        REQUIRE(p.minimum == row[4].get<int>());
                        REQUIRE(p.maximum == row[5].get<int>());
                        REQUIRE(p.bias == row[6].get<int>());
                        ++count;
                    }
    REQUIRE(count == xt::parameters().size());
}

TEST_CASE("XT algorithm presentation follows type codes without changing wire bindings", "[vsynthxt]")
{
    REQUIRE(xt::algorithms("COSM").size() == 17);
    REQUIRE(xt::algorithms("MFX").size() == 42);
    REQUIRE(xt::algorithms("Chorus").size() == 9);
    REQUIRE(xt::algorithms("Reverb").size() == 11);
    REQUIRE(xt::algorithms("unknown").empty());
    auto p = fixture();
    const auto original = p;
    const auto& slot = xt::parameter("xt.native.cosm.zone16.cosm2_param16.value");
    REQUIRE(xt::algorithmTypeOffset(slot) == 0x6f * 128 + 1);
    const auto& type = xt::parameter("xt.native.cosm.zone16.cosm2.type");
    xt::setParameter(p, type, 10);
    auto expected = original;
    expected.data.at(0x6f * 128)[1] = 10;
    REQUIRE(p == expected);
    const auto* algorithm = xt::selectedAlgorithm(p, slot);
    REQUIRE(algorithm != nullptr);
    REQUIRE(std::string(algorithm->name) == "TVF");
    REQUIRE(algorithm->page == 162);
    REQUIRE(std::string(algorithm->reference).find("dB/oct") != std::string::npos);
    const auto beforePresentation = p.sysex();
    for (const auto& info : xt::parameters())
    {
        const auto family = xt::algorithmFamily(info);
        if (family.empty()) continue;
        const auto* selected = xt::selectedAlgorithm(p, info);
        REQUIRE(selected != nullptr);
        REQUIRE(selected->page >= 153);
        if (info.offset == xt::algorithmTypeOffset(info))
            REQUIRE(xt::algorithms(family).size() == static_cast<size_t>(info.maximum + 1));
    }
    REQUIRE(p.sysex() == beforePresentation);
    REQUIRE(xt::parameter(slot.key).minimum == 12768);
    REQUIRE(xt::parameter(slot.key).maximum == 52768);
    REQUIRE(std::string(xt::algorithms("MFX")[41].name) == "Pseudo Stereo");
    REQUIRE(std::string(xt::algorithms("Reverb")[10].name) == "Delay");
    auto unknown = p;
    unknown.data.at(0x6f * 128)[1] = 127;
    REQUIRE(xt::selectedAlgorithm(unknown, slot) == nullptr);
    REQUIRE(xt::selectedAlgorithm(xt::Patch(), slot) == nullptr);
}
