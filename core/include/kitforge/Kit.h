#pragma once

#include <array>
#include <optional>
#include <string>

#include "kitforge/Bytes.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

/** A drum kit: name, 247 sound parameters, and an optional opaque extension suffix. */
class Kit
{
public:
    static constexpr int kNameBytes = 8;
    static constexpr int kFileSize = kNameBytes + kNumSoundParams; // 255
    static constexpr int kEngineFirst = 221;
    static constexpr int kTimbreDecayFirst = 224;
    static constexpr int kEngineFileSize = kFileSize + 6;

    Kit(); // "Initkit" defaults from the firmware

    static Kit init();

    /** Short files are zero-padded; all bytes after the sound array are preserved, not parameters. */
    static Kit fromSnd(const Bytes& data, std::string* warning = nullptr);
    Bytes toSnd() const;
    /** Explicit autosave codec: slot + 247 parameters + suffix, without a kit name. */
    static std::optional<Kit> fromTmpSnd(const Bytes& data, uint8_t& slot, std::string* error = nullptr);
    Bytes toTmpSnd(uint8_t slot) const;
    const Bytes& extension() const { return extension_; }
    bool hasEngineExtension() const;
    /** Explicit opt-in. Initializing a legacy kit clears only the six formerly reserved bytes. */
    bool setEngineSettings(int voice, int engine, int timbreDecay, std::string* error = nullptr);

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

    bool operator==(const Kit& o) const { return rawName_ == o.rawName_ && params_ == o.params_ && extension_ == o.extension_; }
    bool operator!=(const Kit& o) const { return !(*this == o); }

private:
    std::array<uint8_t, kNameBytes> rawName_ {};
    std::array<uint8_t, kNumSoundParams> params_ {};
    Bytes extension_;
};

/** Global settings as stored in glo.cfg: one byte per parameter 272..301. */
class Globals
{
public:
    static constexpr int kFileSize = kNumGlobals;                 // 33 (extended firmware)
    static constexpr int kStockFileSize = pid::STOCK_NUM_GLOBALS; // 30 (stock v1.7)

    Globals(); // firmware defaults, extended (33-byte) layout

    /** Size written by toCfg(): 30 for files read from stock firmware, otherwise 33.
        Stock firmware reads the first 30 bytes of a 33-byte file, so both are safe. */
    int fileSize() const { return fileSize_; }
    void setFileSize(int size) { fileSize_ = size <= kStockFileSize ? kStockFileSize : kFileSize; }

    static Globals fromCfg(const Bytes& data, std::string* warning = nullptr);
    Bytes toCfg() const;

    int get(int param) const;
    void set(int param, int value);

    bool operator==(const Globals& o) const { return values_ == o.values_ && fileSize_ == o.fileSize_; }
    bool operator!=(const Globals& o) const { return !(*this == o); }

private:
    std::array<uint8_t, kNumGlobals> values_ {};
    int fileSize_ = kFileSize;
};

} // namespace kitforge
