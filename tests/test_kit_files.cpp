#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>

#include "TestUtil.h"
#include "kitforge/Kit.h"

using namespace kitforge;
namespace fs = std::filesystem;

TEST_CASE("init kit matches the firmware defaults")
{
    Kit k;
    CHECK(k.name() == "Initkit");
    for (int i = 0; i < kNumSoundParams; ++i)
        CHECK(k.get(i) == ParamTable::get(i).defaultValue);
    CHECK(k.toSnd() == loadFixture("initkit.snd"));
}

TEST_CASE(".snd files round-trip byte-exactly")
{
    for (const char* f : { "initkit.snd", "synthetic.snd" })
    {
        INFO(f);
        const auto bytes = loadFixture(f);
        REQUIRE(bytes.size() == 255);
        std::string warn;
        const auto kit = Kit::fromSnd(bytes, &warn);
        CHECK(warn.empty());
        CHECK(kit.toSnd() == bytes);
    }
    CHECK(Kit::fromSnd(loadFixture("synthetic.snd")).name() == "Synth 1");
}

TEST_CASE("short kit files are zero padded like the firmware")
{
    const auto bytes = loadFixture("short.snd");
    REQUIRE(bytes.size() == 200);
    std::string warn;
    const auto kit = Kit::fromSnd(bytes, &warn);
    CHECK(!warn.empty());
    const auto out = kit.toSnd();
    REQUIRE(out.size() == 255);
    for (size_t i = 0; i < out.size(); ++i)
        CHECK(out[i] == (i < bytes.size() ? bytes[i] : 0));
}

TEST_CASE("kit names")
{
    Kit k;
    k.setName("Too long a name");
    CHECK(k.name() == "Too long");
    k.setName("ab");
    CHECK(k.rawName()[2] == 0);
    CHECK(k.name() == "ab");
}

TEST_CASE("sanitize mirrors the firmware; clampToRanges is explicit")
{
    Kit k;
    k.set(pid::PAR_TARGET_LFO1, 240);
    k.set(pid::PAR_FX_DRY_WET, 120);
    CHECK(k.sanitize() == 1);
    CHECK(k.get(pid::PAR_TARGET_LFO1) == 0);
    CHECK(k.get(pid::PAR_FX_DRY_WET) == 120);
    CHECK(k.clampToRanges() == 1);
    CHECK(k.get(pid::PAR_FX_DRY_WET) == 100);
}

TEST_CASE("glo.cfg round-trips")
{
    const auto bytes = loadFixture("glo.cfg");
    REQUIRE(bytes.size() == 30);
    const auto g = Globals::fromCfg(bytes);
    CHECK(g.toCfg() == bytes);
    CHECK(g.get(pid::PAR_BPM) == 120);
    CHECK(Globals().toCfg() == bytes);
}

// Optional: byte-exact round trip over real factory kits. Point KITFORGE_FACTORY_KITS_DIR at a folder of .snd files.
TEST_CASE("factory kits round-trip (optional)", "[factory]")
{
    const char* dir = std::getenv("KITFORGE_FACTORY_KITS_DIR");
    if (!dir)
    {
        SUCCEED("KITFORGE_FACTORY_KITS_DIR not set");
        return;
    }
    int n = 0, outOfRange = 0;
    for (const auto& e : fs::recursive_directory_iterator(dir))
    {
        auto ext = e.path().extension().string();
        if (ext != ".snd" && ext != ".SND")
            continue;
        const auto bytes = readFileBytes(e.path().string());
        REQUIRE(bytes);
        INFO(e.path().string());
        if (bytes->size() != (size_t) Kit::kFileSize)
            continue;
        auto kit = Kit::fromSnd(*bytes);
        CHECK(kit.toSnd() == *bytes);
        // the firmware tolerates some out-of-range bytes (e.g. hi-hat transient wave 24, hidden reverb FX type 5);
        // they must survive untouched, so only report them
        for (int i = 0; i < kNumSoundParams; ++i)
        {
            const auto& p = ParamTable::get(i);
            const int hi = (p.dtype == DType::LfoTarget || p.dtype == DType::VeloTarget) ? ParamTable::numModTargets() - 1 : p.maxValue;
            if (p.scope == Scope::Sound && (kit.get(i) < p.minValue || kit.get(i) > hi))
                ++outOfRange;
        }
        ++n;
    }
    CHECK(n > 0);
    WARN(n << " kits round-tripped; " << outOfRange << " out-of-range parameter bytes preserved");
}
