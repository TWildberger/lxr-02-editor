#include <catch2/catch_test_macros.hpp>

#include "kitforge/KitOps.h"

using namespace kitforge;

TEST_CASE("diff lists changed sound params")
{
    Kit a, b;
    CHECK(diffKits(a, b).empty());
    b.set(pid::PAR_COARSE1, 72);
    b.set(pid::PAR_UNUSED_1, 9); // reserved bytes are ignored
    const auto d = diffKits(a, b);
    REQUIRE(d.size() == 1);
    CHECK(d[0].param == pid::PAR_COARSE1);
    const auto text = describeDiff(d);
    CHECK(text[0] == "Drum 1 Osc Coarse: " + ParamTable::formatValue(pid::PAR_COARSE1, a.get(pid::PAR_COARSE1)) + " -> +12");
}

TEST_CASE("copy voice between identical drum voices copies everything")
{
    Kit k;
    for (int i : ParamTable::paramsForVoice(1))
        k.set(i, ParamTable::clamp(i, ParamTable::get(i).minValue + 1));
    const int n = copyVoice(k, 1, 2);
    CHECK(n == (int) ParamTable::paramsForVoice(1).size());
    for (int i : ParamTable::paramsForVoice(1))
    {
        const int j = equivalentParam(i, 2);
        REQUIRE(j >= 0);
        CHECK(ParamTable::get(j).voice == 2);
        if (ParamTable::get(i).dtype != DType::VoiceLfo && ParamTable::get(i).dtype != DType::LfoTarget
            && ParamTable::get(i).dtype != DType::VeloTarget)
            CHECK(k.get(j) == k.get(i));
    }
}

TEST_CASE("copy voice remaps velocity targets")
{
    Kit k;
    const auto r1 = ParamTable::modTargetRange(1);
    const auto r2 = ParamTable::modTargetRange(2);
    k.set(pid::PAR_VEL_DEST_1, r1.first); // Drum 1 coarse
    copyVoice(k, 1, 2);
    CHECK(k.get(pid::PAR_VEL_DEST_2) == r2.first);
}

TEST_CASE("copy to a different voice type copies the shared subset")
{
    Kit k;
    const int n = copyVoice(k, 1, 6);
    CHECK(n > 10);
    CHECK(n < (int) ParamTable::paramsForVoice(1).size());
}

TEST_CASE("randomize honours locks, voices and ranges")
{
    std::mt19937 rng(42);
    Kit k;
    const Kit orig = k;
    RandomizeOptions o;
    o.voices = { 1 };
    o.lockedParams = { pid::PAR_COARSE1 };
    o.includeModTargets = true;
    const int n = randomizeKit(k, o, rng);
    CHECK(n > 0);
    CHECK(k.get(pid::PAR_COARSE1) == orig.get(pid::PAR_COARSE1));
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (p.scope != Scope::Sound)
            continue;
        if (p.voice != 1)
            CHECK(k.get(i) == orig.get(i));
        CHECK(k.get(i) >= p.minValue);
        CHECK(k.get(i) <= std::max(p.maxValue, 0));
    }
    const auto r = ParamTable::modTargetRange(1);
    CHECK(k.get(pid::PAR_VEL_DEST_1) >= r.first);
    CHECK(k.get(pid::PAR_VEL_DEST_1) <= r.last);
}

TEST_CASE("partial randomize stays near the original")
{
    std::mt19937 rng(7);
    Kit k;
    const Kit orig = k;
    RandomizeOptions o;
    o.amount = 0.1;
    o.sections = { "Filter" };
    randomizeKit(k, o, rng);
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (p.scope != Scope::Sound)
            continue;
        if (!p.section || std::string(p.section) != "Filter")
            CHECK(k.get(i) == orig.get(i));
        else if (p.dtype == DType::U0_127)
            CHECK(std::abs(k.get(i) - orig.get(i)) <= 7);
    }
}

TEST_CASE("morph interpolates continuous params")
{
    Kit a, b;
    a.set(pid::PAR_COARSE1, 0);
    b.set(pid::PAR_COARSE1, 100);
    b.set(pid::PAR_FX_TYPE, 3);
    const auto m = morphKits(a, b, 0.25);
    CHECK(m.get(pid::PAR_COARSE1) == 25);
    CHECK(m.get(pid::PAR_FX_TYPE) == a.get(pid::PAR_FX_TYPE));
    CHECK(morphKits(a, b, 0.75).get(pid::PAR_FX_TYPE) == 3);
}
