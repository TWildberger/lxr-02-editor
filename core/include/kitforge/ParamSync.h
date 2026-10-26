#pragma once

#include "kitforge/Bytes.h"
#include "kitforge/Kit.h"
#include "kitforge/Midi.h"

#include <array>
#include <atomic>
#include <vector>

namespace kitforge
{

/** Tracks the value the editor wants for every sound parameter and the value last sent to
    (or received from) the device, and turns the difference into MIDI.

    setTarget()/noteDeviceValue() are lock-free and may be called from any thread (e.g. a
    parameter listener on the audio thread). collect() must only be called from one thread
    at a time because it drives a stateful StockMidiEncoder.

    Fast automation is coalesced: only the newest value of each parameter is sent. */
class ParamSync
{
public:
    ParamSync();

    void setTarget(int param, int value);
    int target(int param) const;

    /** The device already has this value (received echo / dump): update the target without resending. */
    void noteDeviceValue(int param, int value);

    /** Forget what the device holds so every known target is sent again. */
    void invalidateDevice();

    /** A complete kit was transferred by other means (e.g. a SysEx kit dump). */
    void markInSync(const Kit& kit);

    /** Sets every target from a kit (does not mark it as sent). */
    void setTargets(const Kit& kit);

    bool hasPending() const;

    struct Result
    {
        int bytes = 0;
        std::vector<int> unsendable; // params whose value could not be sent on stock firmware
    };

    /** Appends complete MIDI messages (each a separate Bytes) for pending changes until
        byteBudget is used up. With extendedSysEx, values stock firmware cannot receive are
        sent as extended ParamSet messages instead of being reported as unsendable. */
    Result collect(StockMidiEncoder& encoder, bool extendedSysEx, int byteBudget, std::vector<Bytes>& out);

private:
    static constexpr int kUnknown = -1;

    std::array<std::atomic<int>, kNumSoundParams> target_;
    std::array<std::atomic<int>, kNumSoundParams> device_;
    std::atomic<bool> dirty_ { false };
    int cursor_ = 0;
};

} // namespace kitforge
