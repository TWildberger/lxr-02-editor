#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "kitforge/ParamTable.h"

using namespace kitforge;

TEST_CASE("param table layout matches firmware enum")
{
    REQUIRE(ParamTable::size() == kNumParams);
    REQUIRE(kNumParams == 302);
    REQUIRE(kNumSoundParams == 247);
    REQUIRE(kFirstGlobal == 272);
    REQUIRE(kNumGlobals == 30);
    for (int i = 0; i < ParamTable::size(); ++i)
        REQUIRE(ParamTable::get(i).index == i);

    CHECK(std::strcmp(ParamTable::get(pid::PAR_COARSE1).id, "PAR_COARSE1") == 0);
    CHECK(pid::PAR_FX_TYPE == 234);
    CHECK(pid::PAR_MORPH_KIT_DEST == 246);
    CHECK(pid::PAR_BPM == 272);
    CHECK(pid::PAR_PATTERN_MODE == 301);
}

TEST_CASE("scopes partition the parameter space")
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (i < kNumSoundParams)
            CHECK((p.scope == Scope::Sound || p.scope == Scope::Reserved));
        else if (i < kFirstGlobal)
            CHECK(p.scope == Scope::Sequencer);
        else
            CHECK(p.scope == Scope::Global);
    }
    for (int reserved : { 0, 5, 97, 98, 127, 221, 222, 223, 224, 225, 226, 227 })
        CHECK(ParamTable::get(reserved).scope == Scope::Reserved);
}

TEST_CASE("stock MIDI mapping follows the firmware rules")
{
    std::set<int> ccs, nrpns;
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (p.midiKind == MidiKind::CC)
        {
            CHECK(p.scope == Scope::Sound);
            CHECK(p.midiNumber == i + 1);
            CHECK(i < 127);
            CHECK(ccs.insert(p.midiNumber).second);
        }
        else if (p.midiKind == MidiKind::NRPN)
        {
            CHECK(p.scope == Scope::Sound);
            CHECK(p.midiNumber == i - 128);
            CHECK(nrpns.insert(p.midiNumber).second);
        }
        else if (p.scope == Scope::Sound)
            FAIL("sound param without MIDI mapping: " << p.id);
    }
    // reserved MIDI numbers are never used for parameters
    for (int cc : { 0, 6, 98, 99 })
        CHECK(ccs.count(cc) == 0);
}

TEST_CASE("defaults are in range and user-facing params are labelled")
{
    for (int i = 0; i < kNumParams; ++i)
    {
        const auto& p = ParamTable::get(i);
        if (p.scope != Scope::Sound && p.scope != Scope::Global)
            continue;
        INFO(p.id);
        CHECK(p.minValue <= p.maxValue);
        CHECK(p.defaultValue >= p.minValue);
        CHECK(p.defaultValue <= p.maxValue);
        CHECK(p.label != nullptr);
        CHECK(p.section != nullptr);
        if (p.dtype == DType::Menu)
        {
            REQUIRE(p.valueNames != nullptr);
            CHECK(p.numValueNames >= p.maxValue + 1);
        }
        if (p.scope == Scope::Sound)
            CHECK((p.voice >= 0 && p.voice <= 6));
    }
}

TEST_CASE("every voice has the core sections")
{
    for (int v = 1; v <= 6; ++v)
    {
        auto params = ParamTable::paramsForVoice(v);
        std::set<std::string> sections;
        for (int i : params)
            sections.insert(ParamTable::get(i).section);
        INFO(ParamTable::voiceName(v));
        CHECK(params.size() >= 30);
        for (const char* s : { "Osc", "Amp EG", "Filter", "LFO", "Mix" })
            CHECK(sections.count(s) == 1);
    }
    CHECK(!ParamTable::paramsForVoice(0).empty()); // FX
}

TEST_CASE("mod targets")
{
    REQUIRE(ParamTable::numModTargets() == 229);
    CHECK(std::string(ParamTable::modTargets()[0].label) == "none");
    for (int v = 1; v <= 6; ++v)
    {
        const auto r = ParamTable::modTargetRange(v);
        for (int i = r.first; i <= r.last; ++i)
        {
            const auto& t = ParamTable::modTargets()[i];
            CHECK(t.index == i);
            CHECK(t.voice == v);
            CHECK(t.param > 0);
            CHECK(t.param < kNumSoundParams);
        }
    }
    CHECK(ParamTable::modTargetRange(1).first == 2);
    CHECK(ParamTable::modTargetRange(6).last == 228);
}

TEST_CASE("value formatting mirrors the device display")
{
    CHECK(ParamTable::formatValue(pid::PAR_COARSE1, 60) == "0");
    CHECK(ParamTable::formatValue(pid::PAR_COARSE1, 72) == "+12");
    CHECK(ParamTable::formatValue(pid::PAR_FINE1, 63) == "0");
    CHECK(ParamTable::formatValue(pid::PAR_FINE1, 0) == "-63");
    CHECK(ParamTable::formatValue(pid::PAR_OSC_WAVE_DRUM1, 0) == "Sin");
    CHECK(ParamTable::formatValue(pid::PAR_OSC_WAVE_DRUM1, 6) == "s0");
    CHECK(ParamTable::formatValue(pid::PAR_FX_TYPE, 4) == "del");
    CHECK(ParamTable::formatValue(pid::PAR_BPM, 0) == "EXT");
    CHECK(ParamTable::formatValue(pid::PAR_BPM, 120) == "120");
    CHECK(ParamTable::formatValue(pid::PAR_MIDI_NOTE1, 60) == "C4");
    CHECK(ParamTable::formatValue(pid::PAR_MIDI_NOTE1, 0) == "any");
    CHECK(ParamTable::formatValue(pid::PAR_TARGET_LFO1, 0) == "none");
    CHECK(ParamTable::formatValue(pid::PAR_TARGET_LFO1, 2) == "D1 Osc Coarse");
    CHECK(ParamTable::fullName(pid::PAR_COARSE2) == "Drum 2 Osc Coarse");
    CHECK(std::string(ParamTable::fxParamLabel(4, pid::PAR_FX_P1)) == "Time");
}

TEST_CASE("lookup by key and id")
{
    REQUIRE(ParamTable::findByKey("coarse1") != nullptr);
    CHECK(ParamTable::findByKey("coarse1")->index == pid::PAR_COARSE1);
    REQUIRE(ParamTable::findById("PAR_BPM") != nullptr);
    CHECK(ParamTable::findById("PAR_BPM")->index == pid::PAR_BPM);
    CHECK(ParamTable::findByKey("nope") == nullptr);
}

TEST_CASE("clamp respects ranges")
{
    CHECK(ParamTable::clamp(pid::PAR_FX_DRY_WET, 200) == 100);
    CHECK(ParamTable::clamp(pid::PAR_COARSE1, -5) == 0);
    CHECK(ParamTable::clamp(pid::PAR_TARGET_LFO1, 500) == 228);
}

TEST_CASE("generated table agrees with docs/params.json")
{
    std::ifstream in(KITFORGE_PARAMS_JSON);
    REQUIRE(in);
    std::stringstream ss;
    ss << in.rdbuf();
    const auto json = ss.str();
    // light-weight check without a JSON dependency: every param id appears in the document
    for (int i = 0; i < kNumParams; ++i)
        CHECK(json.find(std::string("\"") + ParamTable::get(i).id + "\"") != std::string::npos);
    CHECK(json.find("\"numParams\": 302") != std::string::npos);
}
