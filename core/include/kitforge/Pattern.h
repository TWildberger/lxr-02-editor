#pragma once

#include <array>
#include <optional>
#include <string>

#include "kitforge/Bytes.h"

namespace kitforge
{

/** One sequencer step (7 bytes, layout of the firmware's Step struct). */
struct PatternStep
{
    uint8_t volume = 0; // bit 7 = active, bits 0..6 = volume
    uint8_t prob = 0;
    uint8_t note = 0;
    uint8_t param1Nr = 0;
    uint8_t param1Val = 0;
    uint8_t param2Nr = 0;
    uint8_t param2Val = 0;

    bool active() const { return (volume & 0x80) != 0; }
    bool operator==(const PatternStep& o) const
    {
        return volume == o.volume && prob == o.prob && note == o.note && param1Nr == o.param1Nr
               && param1Val == o.param1Val && param2Nr == o.param2Nr && param2Val == o.param2Val;
    }
};

struct PatternTrack
{
    static constexpr int kSteps = 64;
    static constexpr int kBars = 4;

    std::array<PatternStep, kSteps> steps {};
    std::array<uint8_t, kSteps> timing {};   // low nibble = flam index, high nibble = shift (signed 4-bit)
    std::array<uint16_t, kBars> mainSteps {}; // 16 main-step bits per bar
    uint8_t length = 16;

    static int flam(uint8_t t) { return t & 0x0F; }
    static int shift(uint8_t t) { int s = (t >> 4) & 0x0F; return s >= 8 ? s - 16 : s; }
    static uint8_t makeTiming(int flam, int shift) { return (uint8_t) ((flam & 0x0F) | ((shift & 0x0F) << 4)); }

    bool operator==(const PatternTrack& o) const
    {
        return steps == o.steps && timing == o.timing && mainSteps == o.mainSteps && length == o.length;
    }
};

/** Sequencer pattern as stored in a .pat file (versions 1..3 read, version 3 written). */
class Pattern
{
public:
    static constexpr int kTracks = 7;
    static constexpr int kStepBytes = 7;
    static constexpr int kV3FileSize = 8 + 6 + kTracks * (PatternTrack::kSteps * kStepBytes + PatternTrack::kSteps)
                                       + kTracks * PatternTrack::kBars * 2 + 1 + kTracks + 1; // 3663

    static std::optional<Pattern> fromPat(const Bytes& data, std::string* error = nullptr);
    Bytes toPat() const; // always writes "PatV03"

    std::string name() const;
    void setName(const std::string& n);

    int sourceVersion = 3;
    bool hadTrackLengths = true;
    std::array<uint8_t, 8> rawName {};
    std::array<PatternTrack, kTracks> tracks {};
    uint8_t shuffle = 0;
    uint8_t kitNr = 0;

    int activeStepCount() const;

    bool operator==(const Pattern& o) const
    {
        return rawName == o.rawName && tracks == o.tracks && shuffle == o.shuffle && kitNr == o.kitNr;
    }
};

/** Song: 8-byte name + 64 pattern indices (0x40 = empty). */
class Song
{
public:
    static constexpr int kLength = 64;
    static constexpr uint8_t kEmpty = 0x40;
    static constexpr int kFileSize = 8 + kLength;

    Song() { rawName.fill(0); entries.fill(kEmpty); }
    static std::optional<Song> fromSng(const Bytes& data, std::string* error = nullptr);
    Bytes toSng() const;
    std::string name() const;

    std::array<uint8_t, 8> rawName {};
    std::array<uint8_t, kLength> entries {};

    bool operator==(const Song& o) const { return rawName == o.rawName && entries == o.entries; }
};

} // namespace kitforge
