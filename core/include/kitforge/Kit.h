#pragma once

#include <array>
#include <string>

#include "kitforge/Bytes.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

/** A drum kit as stored in a .snd file: 8-byte name + one byte per sound parameter 0..246. */
class Kit
{
public:
    static constexpr int kNameBytes = 8;
    static constexpr int kFileSize = kNameBytes + kNumSoundParams; // 255

    Kit(); // "Initkit" defaults from the firmware

    static Kit init();

    /** Parse .snd bytes. Short files are zero-padded (as the firmware does); extra bytes are ignored. */
    static Kit fromSnd(const Bytes& data, std::string* warning = nullptr);
    Bytes toSnd() const;

    std::string name() const;
    void setName(const std::string& name);
    const std::array<uint8_t, kNameBytes>& rawName() const { return rawName_; }

    int get(int param) const;
    void set(int param, int value); // stored raw; use ParamTable::clamp for UI input
    const std::array<uint8_t, kNumSoundParams>& params() const { return params_; }

    /** Applies the corrections the firmware applies when loading a kit (invalid mod targets -> 0, invalid LFO target voice -> 1). */
    int sanitize();
    /** Clamps every sound parameter into its editor range. Factory kits may legitimately hold
        out-of-range bytes (e.g. hidden reverb FX type 5), so this is never applied implicitly. */
    int clampToRanges();

    bool operator==(const Kit& o) const { return rawName_ == o.rawName_ && params_ == o.params_; }
    bool operator!=(const Kit& o) const { return !(*this == o); }

private:
    std::array<uint8_t, kNameBytes> rawName_ {};
    std::array<uint8_t, kNumSoundParams> params_ {};
};

/** Global settings as stored in glo.cfg: one byte per parameter 272..301. */
class Globals
{
public:
    static constexpr int kFileSize = kNumGlobals; // 30

    Globals(); // firmware defaults

    static Globals fromCfg(const Bytes& data, std::string* warning = nullptr);
    Bytes toCfg() const;

    int get(int param) const;
    void set(int param, int value);

    bool operator==(const Globals& o) const { return values_ == o.values_; }
    bool operator!=(const Globals& o) const { return !(*this == o); }

private:
    std::array<uint8_t, kNumGlobals> values_ {};
};

} // namespace kitforge
