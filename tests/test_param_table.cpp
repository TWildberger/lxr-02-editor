#include <catch2/catch_test_macros.hpp>

#include <cstring>
#include <fstream>
#include <set>
#include <sstream>

#include "kitforge/ParamTable.h"

#include <nlohmann/json.hpp>

using namespace kitforge;

TEST_CASE("param table layout matches firmware enum")
{
    REQUIRE(ParamTable::size() == kNumParams);
    REQUIRE(kNumParams == 305);
    REQUIRE(kNumSoundParams == 247);
    REQUIRE(kFirstGlobal == 272);
    REQUIRE(kNumGlobals == 33);
    REQUIRE(pid::STOCK_NUM_GLOBALS == 30);
    for (int i = 0; i < ParamTable::size(); ++i)
        REQUIRE(ParamTable::get(i).index == i);

    CHECK(std::strcmp(ParamTable::get(pid::PAR_COARSE1).id, "PAR_COARSE1") == 0);
    CHECK(pid::PAR_FX_TYPE == 234);
    CHECK(pid::PAR_MORPH_KIT_DEST == 246);
    CHECK(pid::PAR_BPM == 272);
    CHECK(pid::PAR_PATTERN_MODE == 301);
    CHECK(pid::PAR_SYSEX_ENABLE == 302);
    CHECK(pid::PAR_SYSEX_DEVICE_ID == 303);
    CHECK(pid::PAR_SYSEX_ECHO == 304);
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

TEST_CASE("generated table agrees with docs/editor-params.json")
{
    std::ifstream in(KITFORGE_PARAMS_JSON);
    REQUIRE(in);
    const auto doc = nlohmann::json::parse(in);
    CHECK(doc["numParams"] == kNumParams);
    REQUIRE(doc["params"].size() == (size_t) kNumParams);
    for (const auto& p : doc["params"])
    {
        const auto& info = ParamTable::get(p["index"].get<int>());
        CHECK(p["id"] == info.id);
        CHECK(p["min"] == info.minValue);
        CHECK(p["max"] == info.maxValue);
        CHECK(p["default"] == info.defaultValue);
    }
}

// The vendored table from the firmware fork is authoritative; deviations must be deliberate.
TEST_CASE("generated table agrees with the vendored firmware params.json")
{
    std::ifstream in(KITFORGE_VENDORED_PARAMS_JSON);
    REQUIRE(in);
    const auto v = nlohmann::json::parse(in);
    CHECK(v["counts"]["NUM_PARAMS"] == kNumParams);
    CHECK(v["counts"]["END_OF_SOUND_PARAMETERS"] == kNumSoundParams);
    CHECK(v["counts"]["PAR_BEGINNING_OF_GLOBALS"] == kFirstGlobal);
    CHECK(v["counts"]["globals"] == kNumGlobals);
    CHECK(v["counts"]["stock_v1_7_globals"] == pid::STOCK_NUM_GLOBALS);
    CHECK(v["counts"]["mod_targets"] == ParamTable::numModTargets());

    const std::set<std::string> userSampleWaves { "PAR_OSC_WAVE_DRUM1", "PAR_OSC_WAVE_DRUM2", "PAR_OSC_WAVE_DRUM3",
                                                  "PAR_OSC_WAVE_SNARE", "PAR_WAVE1_CYM", "PAR_WAVE1_HH",
                                                  "PAR_MOD_WAVE_DRUM1", "PAR_MOD_WAVE_DRUM2", "PAR_MOD_WAVE_DRUM3",
                                                  "PAR_WAVE2_CYM", "PAR_WAVE3_CYM", "PAR_WAVE2_HH", "PAR_WAVE3_HH",
                                                  "PAR_FX_WAVE" };
    for (const auto& p : v["params"])
    {
        const int id = p["id"];
        const auto& info = ParamTable::get(id);
        INFO(info.id);
        if (id != 1)
            CHECK(p["symbol"] == info.id);
        if (info.scope != Scope::Sound && info.scope != Scope::Global)
            continue;
        if (!(userSampleWaves.count(info.id) && p["max"] == 5))
            CHECK(p["max"] == info.maxValue);
        else
            CHECK(info.maxValue == 69);
        if (id != pid::PAR_MIDI_CHAN_GLOBAL)
            CHECK(p["min"] == info.minValue);
        if (p["midi"].is_null())
            CHECK(info.midiKind == MidiKind::None);
        else
        {
            CHECK(p["midi"]["type"] == (info.midiKind == MidiKind::CC ? "cc" : "nrpn"));
            CHECK(p["midi"]["number"] == info.midiNumber);
        }
        if (p.contains("snd_offset"))
            CHECK(p["snd_offset"] == 8 + id);
        if (p.contains("glo_cfg_offset"))
            CHECK(p["glo_cfg_offset"] == id - kFirstGlobal);
    }
    const auto& targets = v["mod_targets"];
    REQUIRE(targets.size() == (size_t) ParamTable::numModTargets());
    for (size_t i = 0; i < targets.size(); ++i)
    {
        CHECK(targets[i]["param"] == ParamTable::modTargets()[i].param);
        CHECK((targets[i]["voice"].is_null() ? 0 : targets[i]["voice"].get<int>()) == ParamTable::modTargets()[i].voice);
    }
    for (const auto& r : v["mod_target_voice_ranges"])
    {
        const auto range = ParamTable::modTargetRange(r["voice"]);
        CHECK(r["start"] == range.first);
        CHECK(r["end"] == range.last);
    }
}
