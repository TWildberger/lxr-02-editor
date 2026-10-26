#include <catch2/catch_test_macros.hpp>

#include "TestUtil.h"
#include "kitforge/Pattern.h"

using namespace kitforge;

TEST_CASE("V3 patterns round-trip byte-exactly")
{
    const auto bytes = loadFixture("pattern_v3.pat");
    REQUIRE(bytes.size() == (size_t) Pattern::kV3FileSize);
    REQUIRE(Pattern::kV3FileSize == 3663);
    std::string err;
    const auto p = Pattern::fromPat(bytes, &err);
    REQUIRE(p);
    CHECK(err.empty());
    CHECK(p->sourceVersion == 3);
    CHECK(p->name() == "Pat V3");
    CHECK(p->toPat() == bytes);
    CHECK(p->activeStepCount() > 0);
}

TEST_CASE("V2 and V1 patterns upgrade to V3 without losing data")
{
    for (const char* f : { "pattern_v2.pat", "pattern_v1.pat" })
    {
        INFO(f);
        const auto bytes = loadFixture(f);
        const auto p = Pattern::fromPat(bytes);
        REQUIRE(p);
        const auto v3 = p->toPat();
        CHECK(v3.size() == (size_t) Pattern::kV3FileSize);
        const auto again = Pattern::fromPat(v3);
        REQUIRE(again);
        CHECK(*again == *p);
        for (const auto& t : p->tracks)
            for (auto timing : t.timing)
                CHECK(timing == 0);
    }
    const auto v1 = Pattern::fromPat(loadFixture("pattern_v1.pat"));
    CHECK(v1->sourceVersion == 1);
    CHECK(!v1->hadTrackLengths);
    CHECK(v1->tracks[0].length == 16);
    const auto v2 = Pattern::fromPat(loadFixture("pattern_v2.pat"));
    CHECK(v2->sourceVersion == 2);
}

TEST_CASE("truncated patterns are rejected")
{
    auto bytes = loadFixture("pattern_v3.pat");
    bytes.resize(1000);
    std::string err;
    CHECK(!Pattern::fromPat(bytes, &err));
    CHECK(!err.empty());
    CHECK(!Pattern::fromPat(Bytes(4, 0)));
}

TEST_CASE("step timing helpers")
{
    const auto t = PatternTrack::makeTiming(3, -2);
    CHECK(PatternTrack::flam(t) == 3);
    CHECK(PatternTrack::shift(t) == -2);
    CHECK(PatternTrack::shift(PatternTrack::makeTiming(0, 7)) == 7);
}

TEST_CASE("songs round-trip")
{
    const auto bytes = loadFixture("song.sng");
    const auto s = Song::fromSng(bytes);
    REQUIRE(s);
    CHECK(s->name() == "Song 1");
    CHECK(s->entries[0] == 0);
    CHECK(s->entries[63] == Song::kEmpty);
    CHECK(s->toSng() == bytes);
}
