#include <catch2/catch_test_macros.hpp>

#include <filesystem>

#include "TestUtil.h"
#include "kitforge/Librarian.h"

using namespace kitforge;
namespace fs = std::filesystem;

TEST_CASE("add, dedupe and fetch")
{
    auto lib = Librarian::open(":memory:");
    REQUIRE(lib);
    Kit k;
    k.setName("Kick");
    auto r1 = lib->addKit(k, "editor");
    REQUIRE(r1);
    CHECK(!r1->duplicate);
    auto r2 = lib->addKit(k, "elsewhere");
    REQUIRE(r2);
    CHECK(r2->duplicate);
    CHECK(r2->id == r1->id);
    CHECK(lib->count() == 1);

    const auto item = lib->get(r1->id);
    REQUIRE(item);
    CHECK(item->name == "Kick");
    CHECK(item->kind == FileKind::Kit);
    CHECK(Kit::fromSnd(item->data) == k);
}

TEST_CASE("import an SD card folder")
{
    auto lib = Librarian::open(":memory:");
    REQUIRE(lib);
    const auto rep = lib->importFolder(fixture("card"));
    CHECK(rep.failed == 0);
    CHECK(rep.added == 5); // 2 kits, 1 pattern, 1 song, 1 globals
    CHECK(rep.duplicates == 1); // p002.snd is a copy of 01-Synth.snd
    CHECK(lib->count(FileKind::Kit) == 2);
    CHECK(lib->count(FileKind::Pattern) == 1);

    SearchQuery q;
    q.tags = { "proj00" };
    CHECK(lib->search(q).size() == 5);
    CHECK(lib->allTags() == std::vector<std::string> { "proj00" });

    // importing again adds nothing
    const auto rep2 = lib->importFolder(fixture("card"));
    CHECK(rep2.added == 0);
    CHECK(rep2.duplicates == 6);
}

TEST_CASE("tags, favourites and search")
{
    auto lib = Librarian::open(":memory:");
    REQUIRE(lib);
    Kit a, b, c;
    a.setName("Techno 1");
    b.setName("Electro");
    b.set(pid::PAR_COARSE1, 1);
    c.setName("Techno 2");
    c.set(pid::PAR_COARSE1, 2);
    const auto ia = lib->addKit(a, "x")->id;
    const auto ib = lib->addKit(b, "x")->id;
    const auto ic = lib->addKit(c, "x")->id;

    CHECK(lib->addTag(ia, "  Dark  Kick "));
    CHECK(lib->addTag(ib, "dark kick"));
    CHECK(lib->addTag(ib, "909"));
    CHECK(lib->setFavorite(ic, true));
    CHECK(lib->setNotes(ib, "great snare"));

    SearchQuery q;
    q.text = "techno";
    auto res = lib->search(q);
    REQUIRE(res.size() == 2);
    CHECK(res[0].id == ic); // favourites first

    q = {};
    q.text = "SNARE";
    CHECK(lib->search(q).size() == 1);

    q = {};
    q.tags = { "Dark Kick" };
    CHECK(lib->search(q).size() == 2);
    q.tags = { "dark kick", "909" };
    res = lib->search(q);
    REQUIRE(res.size() == 1);
    CHECK(res[0].tags == std::vector<std::string> { "909", "dark kick" });

    q = {};
    q.favoritesOnly = true;
    CHECK(lib->search(q).size() == 1);

    q = {};
    q.text = "100%";
    CHECK(lib->search(q).empty());

    CHECK(lib->removeTag(ib, "909"));
    CHECK(lib->allTags() == std::vector<std::string> { "dark kick" });
    CHECK(lib->remove(ia));
    CHECK(lib->count() == 2);
}

TEST_CASE("export with firmware naming")
{
    auto lib = Librarian::open(":memory:");
    REQUIRE(lib);
    Kit k;
    k.setName("Old");
    const auto id = lib->addKit(k, "x")->id;
    CHECK(lib->rename(id, "New Name"));
    const auto dir = fs::temp_directory_path() / "kitforge_export_test";
    fs::remove_all(dir);
    const auto path = lib->exportItem(id, dir.string(), 4);
    REQUIRE(path);
    CHECK(fs::path(*path).filename() == "04-New_N.snd");
    const auto back = Kit::fromSnd(*readFileBytes(*path));
    CHECK(back.name() == "New Name");
    fs::remove_all(dir);
}

TEST_CASE("library persists on disk")
{
    const auto file = fs::temp_directory_path() / "kitforge_lib_test.sqlite";
    fs::remove(file);
    {
        auto lib = Librarian::open(file.string());
        REQUIRE(lib);
        lib->addKit(Kit(), "x");
    }
    auto lib = Librarian::open(file.string());
    REQUIRE(lib);
    CHECK(lib->count() == 1);
    lib.reset();
    fs::remove(file);
}
