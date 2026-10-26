#pragma once

#include "kitforge/Bytes.h"
#include "kitforge/Kit.h"
#include "kitforge/Midi.h"

#include <array>
#include <atomic>
#include <vector>

namespace kitforge
{

/** Tracks the value the editor wants for every sound and global parameter and the value last sent to
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

    /** A complete kit / globals block was transferred by other means (e.g. a SysEx dump). */
    void markInSync(const Kit& kit);
    void markInSync(const Globals& globals);

    /** Sets every target from a kit / globals block (does not mark them as sent). */
    void setTargets(const Kit& kit);
    void setTargets(const Globals& globals);

    bool hasPending() const;

    struct Result
    {
        int bytes = 0;
        std::vector<int> unsendable; // params whose value could not be sent on stock firmware
    };

    struct Transport
    {
        bool extended = false; // extended firmware detected: use PARAM_SET for everything
        int device = 0x7F;     // SysEx device byte (device ID - 1, or 0x7F)
    };

    /** Appends complete MIDI messages (each a separate Bytes) for pending changes until byteBudget is used up.
        Stock firmware: CC/NRPN for sound parameters; values it cannot receive are reported as unsendable and
        globals are skipped (they only travel in glo.cfg). Extended firmware: PARAM_SET frames of up to 64 pairs
        covering sound and writable global parameters. */
    Result collect(StockMidiEncoder& encoder, Transport transport, int byteBudget, std::vector<Bytes>& out);

    static bool isSyncable(int param, bool extended);

private:
    static constexpr int kUnknown = -1;

    std::array<std::atomic<int>, kNumParams> target_;
    std::array<std::atomic<int>, kNumParams> device_;
    std::atomic<bool> dirty_ { false };
    int cursor_ = 0;
};

} // namespace kitforge
