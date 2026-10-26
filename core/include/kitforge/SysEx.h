#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "kitforge/Bytes.h"

namespace kitforge
{

namespace sysex
{
/** 8-bit -> 7-bit packing: each group of up to 7 bytes is preceded by a byte holding their MSBs (bit i = byte i). */
Bytes pack7(const uint8_t* data, size_t size);
inline Bytes pack7(const Bytes& b) { return pack7(b.data(), b.size()); }
std::optional<Bytes> unpack7(const uint8_t* data, size_t size);
inline std::optional<Bytes> unpack7(const Bytes& b) { return unpack7(b.data(), b.size()); }
constexpr size_t packedSize(size_t n) { return n + (n + 6) / 7; }

/** Roland-style checksum: low 7 bits of (sum + checksum) == 0. */
uint8_t checksum(const uint8_t* data, size_t size);

/** Splits a raw MIDI byte stream into complete F0..F7 messages; incomplete tails are kept for the next call. */
class Assembler
{
public:
    std::vector<Bytes> feed(const uint8_t* data, size_t size);
    void reset() { buf_.clear(); inSysex_ = false; }

private:
    Bytes buf_;
    bool inSysex_ = false;
};
} // namespace sysex

/**
 * Extended-mode protocol for modified firmware.
 *
 * PROVISIONAL: the firmware fork had not published docs/sysex.md when this was written.
 * Framing:  F0 7D 'K' 'F' <version> <command> <payload...> <checksum> F7
 * 0x7D is the MIDI "non-commercial / development" manufacturer ID. Multi-byte payloads are pack7()-encoded.
 * See docs/extended-protocol.md. All constants live here so reconciling with the fork is a one-file change.
 */
namespace ext
{
constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kMagic0 = 'K';
constexpr uint8_t kMagic1 = 'F';
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kEditBuffer = 0x7F;

enum class Command : uint8_t
{
    IdentityRequest = 0x01,
    IdentityReply = 0x02,
    KitRequest = 0x10,
    KitDump = 0x11,
    ParamSet = 0x12,
    ParamChanged = 0x13, // device -> host when a knob moves
    KitStore = 0x14,     // copy edit buffer into a RAM kit slot
    PatternRequest = 0x20,
    PatternDump = 0x21,
    GlobalsRequest = 0x30,
    GlobalsDump = 0x31,
    Ack = 0x7E,
};

enum Capability : uint16_t
{
    CapKitDump = 1 << 0,
    CapParamEcho = 1 << 1,
    CapPatternDump = 1 << 2,
    CapGlobalsDump = 1 << 3,
    CapKitStore = 1 << 4,
    CapFullModTargets = 1 << 5,
};

struct Identity
{
    int protocolVersion = 0;
    int fwMajor = 0, fwMinor = 0, fwPatch = 0;
    uint16_t capabilities = 0;
    bool has(Capability c) const { return (capabilities & c) != 0; }
    std::string describe() const;
};

struct Message
{
    Command command {};
    int version = 0;
    int slot = -1;       // kit/pattern slot for requests and dumps
    int param = -1;      // ParamSet / ParamChanged
    int value = 0;
    int status = 0;      // Ack
    Identity identity;   // IdentityReply
    Bytes data;          // unpacked dump payload
};

Bytes identityRequest();
Bytes identityReply(const Identity& id);
Bytes kitRequest(int slot = kEditBuffer);
Bytes kitDump(int slot, const Bytes& snd);
Bytes paramSet(int param, int value);
Bytes paramChanged(int param, int value);
Bytes kitStore(int slot);
Bytes patternRequest(int slot);
Bytes patternDump(int slot, const Bytes& pat);
Bytes globalsRequest();
Bytes globalsDump(const Bytes& cfg);
Bytes ack(Command cmd, int status);

/** Parses a complete F0..F7 message. Returns nullopt (with reason) for foreign or corrupt messages. */
std::optional<Message> parse(const Bytes& sysexMsg, std::string* error = nullptr);

/** MIDI universal identity request (F0 7E 7F 06 01 F7), also sent during handshake. */
Bytes universalIdentityRequest();
} // namespace ext

} // namespace kitforge
