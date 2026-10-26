#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include "kitforge/Bytes.h"

namespace kitforge
{

namespace sysex
{
/** "packed8" (7-in-8) encoding from docs/firmware/sysex.md: groups of up to 7 raw bytes are sent as their
    low 7 bits followed by one MSB byte whose bit i holds bit 7 of raw byte i. 81 02 FF -> 01 02 7F 05. */
Bytes pack8(const uint8_t* data, size_t size);
inline Bytes pack8(const Bytes& b) { return pack8(b.data(), b.size()); }
std::optional<Bytes> unpack8(const uint8_t* data, size_t size);
inline std::optional<Bytes> unpack8(const Bytes& b) { return unpack8(b.data(), b.size()); }
constexpr size_t packedSize(size_t n) { return n + (n + 6) / 7; }
/** Raw byte count for a packed8 length, or -1 when no raw length packs to that size. */
int unpackedSize(size_t packed);

/** XOR of the bytes, masked to 7 bits. */
uint8_t checksum(const uint8_t* data, size_t size);

/** Splits a raw MIDI byte stream into complete F0..F7 messages; incomplete tails are kept for the next call.
    Real-time bytes (F8..FF) may interleave; any other status byte aborts the message. */
class Assembler
{
public:
    explicit Assembler(size_t maxLength = 4096) : maxLength_(maxLength) {}
    std::vector<Bytes> feed(const uint8_t* data, size_t size);
    void reset() { buf_.clear(); inSysex_ = false; }

private:
    size_t maxLength_;
    Bytes buf_;
    bool inSysex_ = false;
};
} // namespace sysex

/**
 * Extended-mode protocol v1 of the TWildberger/lxr-02-firmware fork (docs/firmware/sysex.md).
 *
 *   F0 7D 4C dd vv cc <payload> ss F7
 *
 * dd = device ID - 1 (00..0F) or 7F (all), vv = 01, ss = XOR of dd..payload & 7F. u14 values are LSB first.
 * Stock firmware ignores all of it; a missing IDENTITY_REPLY within 500 ms means "use CC/NRPN".
 */
namespace ext
{
constexpr uint8_t kManufacturer = 0x7D;
constexpr uint8_t kModel = 0x4C;
constexpr uint8_t kProtocolVersion = 1;
constexpr uint8_t kAllDevices = 0x7F;
constexpr uint8_t kEditBuffer = 0x7F;    // kit slot
constexpr uint8_t kActivePattern = 0x7F; // pattern slot
constexpr int kNumSlots = 64;
constexpr int kMaxFrameLength = 320;
constexpr int kMaxSetPairs = 64;
constexpr int kMaxGetIds = 64;
constexpr int kMaxNotifyPairs = 32;
constexpr int kMaxChunkRaw = 196;
constexpr int kNameLength = 8;
constexpr int kPatternRawLength = 3657; // name[8] + body; a .pat V3 file minus the "PatV03" tag
constexpr uint8_t kFlagSave = 0x01;
constexpr int kReplyTimeoutMs = 500;
constexpr int kPatternTimeoutMs = 3000;

enum class Command : uint8_t
{
    IdentityRequest = 0x01,
    IdentityReply = 0x02,
    StatusRequest = 0x03,
    StatusReply = 0x04,
    KitRequest = 0x10,
    KitDump = 0x11,
    KitStore = 0x12,
    KitLoad = 0x13,
    KitName = 0x14,
    PatternRequest = 0x20,
    PatternChunk = 0x21,
    PatternName = 0x22,
    GlobalsRequest = 0x30,
    GlobalsDump = 0x31,
    ParamGet = 0x40,
    ParamSet = 0x41,
    ParamValue = 0x42,
    ParamNotify = 0x43,
    EchoSet = 0x44,
    Event = 0x45,
    FxFreeze = 0x46,
    Ack = 0x7E,
    Nak = 0x7F,
};
const char* commandName(Command c);

enum class Error : uint8_t
{
    None = 0,
    Checksum = 1,
    UnknownCommand = 2,
    Length = 3,
    Range = 4,
    Busy = 5,
    Sequence = 6,
    Version = 7,
    ReadOnly = 8,
    SdCard = 9,
};
const char* errorName(Error e);

enum Capability : uint16_t
{
    CapKits = 1 << 0,
    CapPatterns = 1 << 1,
    CapGlobals = 1 << 2,
    CapParams = 1 << 3,
    CapEcho = 1 << 4,
    CapSave = 1 << 5,
    CapEngineTrailer = 1 << 7,
    CapFreeze = 1 << 8,
};

enum class EventType : uint8_t
{
    KitChanged = 1,
    PatternChanged = 2,
};

struct Identity
{
    int protocolVersion = kProtocolVersion;
    int fwMajor = 0, fwMinor = 0, fwPatch = 0;
    std::string fwString;
    int numParams = 0, endOfSound = 0, firstGlobal = 0;
    int kitSlots = 0, patternSlots = 0, tracks = 0, steps = 0;
    int patternLength = 0, maxFrame = 0;
    uint16_t capabilities = 0;
    int deviceId = 1; // 1..16
    bool echo = false;

    bool has(Capability c) const { return (capabilities & c) != 0; }
    /** True when the device's kit/global layout matches the table this editor was built with. */
    bool layoutMatchesEditor() const;
    std::string describe() const;
};

struct Status
{
    int kitSlot = 0;
    int pattern = 0;
    bool running = false;
    bool echo = false;
};

struct ParamPair
{
    int id = 0;
    int value = 0;
    bool operator==(const ParamPair& o) const { return id == o.id && value == o.value; }
};

struct Message
{
    Command command {};
    int device = kAllDevices; // dd byte as received (0..15 or 7F)
    int version = kProtocolVersion;

    int slot = -1;
    int flags = 0;
    int count = 0;      // KIT_DUMP parameter count, GLOBALS_DUMP n
    int first = 0;      // GLOBALS_DUMP first id
    int chunkIndex = 0; // PATTERN_CHUNK
    int chunkCount = 0;
    int totalLength = 0;
    Bytes data;         // unpacked: KIT_DUMP name+params, PATTERN_CHUNK raw chunk, GLOBALS_DUMP values
    std::string name;   // KIT_NAME / PATTERN_NAME (raw 8 bytes, trailing pad trimmed)
    Bytes rawName;

    Identity identity;
    Status status;
    std::vector<int> ids;          // PARAM_GET
    std::vector<ParamPair> pairs;  // PARAM_SET / PARAM_VALUE / PARAM_NOTIFY
    bool on = false;               // ECHO_SET
    EventType event {};            // EVENT
    Command answered {};           // ACK / NAK
    Error error = Error::None;     // NAK
    int arg = 0;                   // ACK / NAK / EVENT argument
};

/** Wraps a payload in a frame. device = 0..15 or kAllDevices. */
Bytes frame(int device, Command cmd, const Bytes& payload, int version = kProtocolVersion);

// host -> device
Bytes identityRequest(int device = kAllDevices);
Bytes statusRequest(int device);
Bytes kitRequest(int device, int slot = kEditBuffer);
/** snd = name[8] followed by the parameter bytes (an .snd file). */
Bytes kitDump(int device, int slot, int flags, const Bytes& snd);
Bytes kitStore(int device, int slot, int flags);
Bytes kitLoad(int device, int slot);
Bytes kitName(int device, int slot, const std::string& name);
Bytes patternRequest(int device, int slot = kActivePattern);
Bytes patternName(int device, int slot, const std::string& name);
/** Splits raw pattern data (kPatternRawLength bytes) into PATTERN_CHUNK frames; flags go in chunk 0. */
std::vector<Bytes> patternChunks(int device, int slot, int flags, const Bytes& raw, int chunkRaw = kMaxChunkRaw);
Bytes patternChunk(int device, int slot, int flags, int index, int count, int totalLength, const Bytes& chunk);
Bytes globalsRequest(int device);
Bytes globalsDump(int device, int flags, int first, const Bytes& values);
Bytes paramGet(int device, const std::vector<int>& ids);
/** One PARAM_SET frame; at most kMaxSetPairs pairs. */
Bytes paramSet(int device, const std::vector<ParamPair>& pairs);
/** Splits any number of pairs into PARAM_SET frames of at most kMaxSetPairs. */
std::vector<Bytes> paramSetFrames(int device, const std::vector<ParamPair>& pairs);
Bytes echoSet(int device, bool on);
/** No success reply. Values: 0 release, 1..8 rate, 0x7F use P1. */
Bytes fxFreeze(int device, int value);

// device -> host (used by tests and the device simulator)
Bytes identityReply(const Identity& id);
Bytes statusReply(int device, const Status& s);
Bytes paramValue(int device, const std::vector<ParamPair>& pairs);
Bytes paramNotify(int device, const std::vector<ParamPair>& pairs);
Bytes event(int device, EventType type, int arg);
Bytes ack(int device, Command answered, int arg);
Bytes nak(int device, Command answered, Error err, int arg);

/** Parses a complete F0..F7 frame. Returns nullopt (with reason) for foreign, corrupt or malformed frames. */
std::optional<Message> parse(const Bytes& frame, std::string* error = nullptr);
/** Cheap header test: F0 7D 4C ... F7. */
bool isOurs(const Bytes& frame);

/** MIDI universal identity request, F0 7E 7F 06 01 F7. */
Bytes universalIdentityRequest();

/** .pat V3 file <-> raw pattern transfer data. */
std::optional<Bytes> patternRawFromPat(const Bytes& pat);
Bytes patFromPatternRaw(const Bytes& raw);

/** Reassembles a PATTERN_REQUEST answer. */
class PatternReceiver
{
public:
    enum class State { Idle, Receiving, Complete, Failed };

    void reset() { *this = PatternReceiver(); }
    /** Feed every PATTERN_CHUNK; returns the new state. */
    State feed(const Message& chunk);
    State state() const { return state_; }
    const Bytes& data() const { return data_; }
    int slot() const { return slot_; }
    const std::string& error() const { return error_; }

private:
    State state_ = State::Idle;
    Bytes data_;
    int slot_ = -1, next_ = 0, count_ = 0, total_ = 0;
    std::string error_;
};

/**
 * Half-duplex request scheduler: one request in flight, its reply (or timeout) releases the next.
 * Pure logic with an injected clock so it can be unit-tested; thread-safe.
 * PARAM_SET frames do not get a reply and bypass this queue.
 */
class RequestQueue
{
public:
    enum class Match { No, Progress, Done, Failed };
    enum class Outcome { Ok, Nak, Timeout, Cancelled };
    using Matcher = std::function<Match(const Message&)>;
    using Callback = std::function<void(Outcome, const std::vector<Message>& replies)>;

    struct Request
    {
        std::string label;
        Bytes frame;
        Matcher matcher;
        Callback done;
        int timeoutMs = kReplyTimeoutMs;
        int group = 0; // a failure cancels every queued request of the same non-zero group
    };

    void push(Request r);
    /** Next frame to transmit, if nothing is in flight. Also fires timeouts. */
    std::optional<Bytes> poll(int64_t nowMs);
    /** Feeds a parsed reply; returns true if it belonged to the request in flight. */
    bool onMessage(const Message& m, int64_t nowMs);
    void cancelAll();
    bool idle() const;
    size_t pending() const;
    int newGroup() { return ++groupCounter_; }

    /** Matchers for the common replies. */
    static Matcher replyTo(Command request, Command reply);
    static Matcher ackFor(Command request, int arg = -1);
    static Matcher patternDump(std::shared_ptr<PatternReceiver> rx);

private:
    struct Active
    {
        Request req;
        int64_t deadline = 0;
        std::vector<Message> replies;
    };
    struct Call
    {
        Callback done;
        Outcome outcome;
        std::vector<Message> replies;
    };
    void finish(Outcome o, std::vector<Call>& calls, std::vector<Callback>& cancelled);

    mutable std::mutex lock_;
    std::deque<Request> queue_;
    std::optional<Active> active_;
    int groupCounter_ = 0;
};
} // namespace ext

} // namespace kitforge
