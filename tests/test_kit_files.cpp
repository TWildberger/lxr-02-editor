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

TEST_CASE("engine suffix and unknown extensions round-trip without changing legacy bytes")
{
    auto bytes = loadFixture("synthetic.snd");
    const auto legacy = Kit::fromSnd(bytes);
    CHECK(legacy.extension().empty());
    CHECK_FALSE(legacy.hasEngineExtension());
    bytes.insert(bytes.end(), { 'E', 'n', 'g', 'V', '0', '1' });
    bytes[Kit::kNameBytes + Kit::kEngineFirst] = 1;
    bytes[Kit::kNameBytes + Kit::kTimbreDecayFirst] = 127;
    std::string warning;
    auto k = Kit::fromSnd(bytes, &warning);
    CHECK(warning.empty());
    CHECK(k.hasEngineExtension());
    CHECK(k.toSnd().size() == 261);
    CHECK(k.toSnd() == bytes);
    CHECK(k != legacy);
    k.set(pid::PAR_COARSE1, 33);
    k.setName("Renamed");
    CHECK(k.extension() == Bytes { 'E', 'n', 'g', 'V', '0', '1' });

    for (const Bytes suffix : { Bytes { 'E', 'n', 'g' }, Bytes { 'E', 'n', 'g', 'V', '0', '2', 0xFF } })
    {
        auto raw = legacy.toSnd();
        raw.insert(raw.end(), suffix.begin(), suffix.end());
        auto unknown = Kit::fromSnd(raw, &warning);
        CHECK_FALSE(warning.empty());
        CHECK_FALSE(unknown.hasEngineExtension());
        CHECK(unknown.toSnd() == raw);
        CHECK_FALSE(unknown.setEngineSettings(1, 1, 60, &warning));
        CHECK_FALSE(warning.empty());
        CHECK(unknown.toSnd() == raw);
    }
}

TEST_CASE("engine opt-in clears only formerly reserved bytes and validates settings")
{
    auto k = Kit::fromSnd(loadFixture("synthetic.snd"));
    const auto before = k;
    REQUIRE(k.setEngineSettings(2, 1, 100));
    CHECK(k.hasEngineExtension());
    CHECK(k.get(221) == 0);
    CHECK(k.get(222) == 1);
    CHECK(k.get(223) == 0);
    CHECK(k.get(224) == 0);
    CHECK(k.get(225) == 100);
    CHECK(k.get(226) == 0);
    for (int i = 0; i < kNumSoundParams; ++i)
        if (i < 221 || i > 226)
            CHECK(k.get(i) == before.get(i));
    const auto tagged = k;
    CHECK_FALSE(k.setEngineSettings(4, 1, 30));
    CHECK_FALSE(k.setEngineSettings(1, 2, 30));
    CHECK_FALSE(k.setEngineSettings(1, 1, 128));
    CHECK(k == tagged);
}

TEST_CASE("autosave codec preserves slot, parameters and suffix at legacy and extended lengths")
{
    for (const bool extended : { false, true })
    {
        auto k = Kit::fromSnd(loadFixture("synthetic.snd"));
        if (extended)
            REQUIRE(k.setEngineSettings(3, 1, 127));
        const auto bytes = k.toTmpSnd(63);
        CHECK(bytes.size() == (extended ? 254 : 248));
        uint8_t slot = 0;
        std::string error;
        auto back = Kit::fromTmpSnd(bytes, slot, &error);
        REQUIRE(back);
        CHECK(error.empty());
        CHECK(slot == 63);
        CHECK(back->params() == k.params());
        CHECK(back->extension() == k.extension());
        CHECK(back->toTmpSnd(slot) == bytes);
    }
    uint8_t slot = 7;
    std::string error;
    CHECK_FALSE(Kit::fromTmpSnd(Bytes(247), slot, &error));
    CHECK_FALSE(error.empty());
    CHECK(slot == 7);
}

TEST_CASE("sanitize mirrors the firmware; clampToRanges is explicit")
{
    Kit k;
    k.set(pid::PAR_TARGET_LFO1, 240);
    k.set(pid::PAR_FX_DRY_WET, 120);
    k.set(pid::PAR_VOICE_LFO3, 0);
    CHECK(k.sanitize() == 2);
    CHECK(k.get(pid::PAR_TARGET_LFO1) == 0);
    CHECK(k.get(pid::PAR_VOICE_LFO3) == 1);
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
    CHECK(g.fileSize() == Globals::kStockFileSize);
    CHECK(g.get(pid::PAR_SYSEX_ENABLE) == 1); // fork defaults for settings a stock file lacks
    CHECK(g.get(pid::PAR_SYSEX_DEVICE_ID) == 1);
    CHECK(g.get(pid::PAR_SYSEX_ECHO) == 0);

    // a default Globals uses the extended 33-byte layout whose first 30 bytes are the stock file
    const auto ext = Globals().toCfg();
    REQUIRE(ext.size() == 33);
    CHECK(Bytes(ext.begin(), ext.begin() + 30) == bytes);
    CHECK(Globals::fromCfg(ext).toCfg() == ext);

    // editing an extended setting upgrades a stock file
    auto g2 = g;
    g2.set(pid::PAR_SYSEX_ECHO, 1);
    CHECK(g2.toCfg().size() == 33);
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
