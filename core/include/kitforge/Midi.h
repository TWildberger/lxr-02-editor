#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "kitforge/Kit.h"

namespace kitforge
{

struct MidiMessage
{
    uint8_t bytes[3] {};
    uint8_t size = 0;

    static MidiMessage cc(int channel1to16, int cc, int value);
    static MidiMessage programChange(int channel1to16, int program);
    bool operator==(const MidiMessage& o) const
    {
        return size == o.size && bytes[0] == o.bytes[0] && (size < 2 || bytes[1] == o.bytes[1]) && (size < 3 || bytes[2] == o.bytes[2]);
    }
};

/** Result of checking whether a value can be sent to stock firmware. */
struct LiveSendability
{
    bool ok = true;
    std::string reason;
};

/**
 * Encodes parameter changes for the stock firmware's MIDI receiver:
 *  - params 1..126  -> CC (param + 1)
 *  - params 128..246 -> NRPN (param - 128) via CC99/CC98/CC6
 *  - LFO / velocity targets are sent as raw parameter numbers (the firmware converts them)
 */
class StockMidiEncoder
{
public:
    explicit StockMidiEncoder(int channel1to16 = 1) { setChannel(channel1to16); }

    void setChannel(int channel1to16);
    int channel() const { return channel_; }

    /** When enabled, CC99/CC98 are only sent when the NRPN number changes. */
    void setRunningNrpn(bool enabled) { runningNrpn_ = enabled; }
    void resetNrpnState() { lastNrpn_ = -1; }

    static LiveSendability canSend(int param, int value);

    std::vector<MidiMessage> encodeParam(int param, int value);
    /** All sendable sound params of a kit; non-sendable params are reported in `skipped`. */
    std::vector<MidiMessage> encodeKit(const Kit& kit, std::vector<int>* skipped = nullptr);
    /** Only the params that differ between `from` and `to`. */
    std::vector<MidiMessage> encodeKitDelta(const Kit& from, const Kit& to, std::vector<int>* skipped = nullptr);

    std::vector<MidiMessage> encodeMute(int track0to6, bool muted);
    MidiMessage encodeKitSelect(int ramSlot) const; // CC0
    MidiMessage encodePatternSelect(int globalChannel1to16, int pattern) const;

    static std::vector<uint8_t> toBytes(const std::vector<MidiMessage>& msgs);

private:
    void appendNrpn(std::vector<MidiMessage>& out, int nrpn, int value);

    int channel_ = 1;
    bool runningNrpn_ = true;
    int lastNrpn_ = -1;
};

/**
 * Decodes incoming CC/NRPN into (param, value) updates. Stock firmware does not transmit
 * parameter changes, but this is used for MIDI-learn style host input and extended firmware echo.
 */
class StockMidiDecoder
{
public:
    struct Update
    {
        int param;
        int value;
    };

    /** Returns true and fills `out` when the message completes a parameter change. */
    bool feed(const uint8_t* msg, int size, Update& out);

private:
    int nrpnMsb_ = 0;
    int nrpnLsb_ = 0;
};

} // namespace kitforge
