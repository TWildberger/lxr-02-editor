#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "TestUtil.h"
#include "kitforge/SdCard.h"

using namespace kitforge;
namespace fs = std::filesystem;

TEST_CASE("firmware file naming")
{
    using namespace filenames;
    CHECK(clearIllegalCharacters("My Kit*1") == "My_Kit_1");
    CHECK(clearIllegalCharacters("a.b/c") == "a_b_c");
    CHECK(makeSaveName(3, "Big Kick", FileKind::Kit) == "03-Big_K.snd");
    CHECK(makeSaveName(12, "x", FileKind::Pattern) == "12-x.pat");
    CHECK(makeSaveName(0, "", FileKind::Song) == "00-.sng");
    CHECK(makeOldName(7, FileKind::Kit) == "p007.snd");
    CHECK(slotFromFileName("/a/b/05-Hello.snd") == 5);
    CHECK(slotFromFileName("p012.pat") == 12);
    CHECK(slotFromFileName("Kick.snd") == -1);
    CHECK(kindFromPath("X/01-ABC.SND") == FileKind::Kit);
    CHECK(kindFromPath("glo.cfg") == FileKind::Globals);
    CHECK(kindFromPath("notes.txt") == FileKind::Unknown);
}

TEST_CASE("scanning an SD card skips junk")
{
    const auto items = scanFolder(fixture("card"));
    int kits = 0, pats = 0, songs = 0, glo = 0;
    for (const auto& i : items)
    {
        CHECK(i.path.find("._") == std::string::npos);
        kits += i.kind == FileKind::Kit;
        pats += i.kind == FileKind::Pattern;
        songs += i.kind == FileKind::Song;
        glo += i.kind == FileKind::Globals;
    }
    CHECK(kits == 3);
    CHECK(pats == 1);
    CHECK(songs == 1);
    CHECK(glo == 1);
}

TEST_CASE("project load/save round trip")
{
    std::vector<std::string> warnings;
    const auto prj = Project::load(fixture("card/proj00"), &warnings);
    CHECK(warnings.empty());
    CHECK(prj.name == "Demo");
    REQUIRE(prj.globals);
    REQUIRE(prj.kits[0]);
    REQUIRE(prj.kits[1]);
    CHECK(prj.kits[1]->name() == "Synth 1");
    REQUIRE(prj.patterns[0]);
    REQUIRE(prj.songs[0]);

    const auto tmp = fs::temp_directory_path() / "kitforge_test_project";
    fs::remove_all(tmp);
    REQUIRE(prj.save(tmp.string()));
    CHECK(fs::exists(tmp / "01-Synth_.snd") == false); // only 5 name chars
    CHECK(fs::exists(tmp / "01-Synth.snd"));
    CHECK(fs::exists(tmp / "glo.cfg"));
    CHECK(readFileBytes((tmp / "01-Synth.snd").string()) == readFileBytes(fixture("card/proj00/01-Synth.snd")));
    CHECK(readFileBytes((tmp / "00-Pat_V.pat").string()) == readFileBytes(fixture("card/proj00/00-Pat_V.pat")));

    const auto again = Project::load(tmp.string());
    CHECK(again.name == prj.name);
    CHECK(*again.kits[1] == *prj.kits[1]);
    CHECK(*again.patterns[0] == *prj.patterns[0]);
    fs::remove_all(tmp);
}

TEST_CASE("project save and load keep versioned engine kits byte-exactly")
{
    Project project;
    Kit k;
    k.setName("Engine");
    REQUIRE(k.setEngineSettings(3, 1, 127));
    project.kits[4] = k;
    const auto dir = fs::temp_directory_path() / "kitforge_engine_project_test";
    REQUIRE(project.save(dir.string()));
    std::vector<std::string> warnings;
    const auto back = Project::load(dir.string(), &warnings);
    CHECK(warnings.empty());
    REQUIRE(back.kits[4]);
    CHECK(back.kits[4]->toSnd() == k.toSnd());
    fs::remove_all(dir);
}
