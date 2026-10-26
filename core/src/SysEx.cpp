#include "kitforge/SysEx.h"

#include <algorithm>
#include <cstdio>

#include "kitforge/ParamTable.h"
#include "kitforge/Pattern.h"

namespace kitforge
{
namespace sysex
{

Bytes pack8(const uint8_t* data, size_t size)
{
    Bytes out;
    out.reserve(packedSize(size));
    for (size_t i = 0; i < size; i += 7)
    {
        const size_t n = std::min<size_t>(7, size - i);
        uint8_t msbs = 0;
        for (size_t j = 0; j < n; ++j)
        {
            out.push_back(data[i + j] & 0x7F);
            msbs |= (uint8_t) (((data[i + j] >> 7) & 1) << j);
        }
        out.push_back(msbs);
    }
    return out;
}

int unpackedSize(size_t packed)
{
    const size_t full = packed / 8, rest = packed % 8;
    if (rest == 1)
        return -1;
    return (int) (full * 7 + (rest ? rest - 1 : 0));
}

std::optional<Bytes> unpack8(const uint8_t* data, size_t size)
{
    if (unpackedSize(size) < 0)
        return std::nullopt;
    Bytes out;
    out.reserve((size_t) unpackedSize(size));
    for (size_t i = 0; i < size; i += 8)
    {
        const size_t n = std::min<size_t>(8, size - i) - 1;
        const uint8_t msbs = data[i + n];
        if (msbs & 0x80)
            return std::nullopt;
        for (size_t j = 0; j < n; ++j)
        {
            const uint8_t b = data[i + j];
            if (b & 0x80)
                return std::nullopt;
            out.push_back((uint8_t) (b | (((msbs >> j) & 1) << 7)));
        }
    }
    return out;
}

uint8_t checksum(const uint8_t* data, size_t size)
{
    uint8_t x = 0;
    for (size_t i = 0; i < size; ++i)
        x ^= data[i];
    return x & 0x7F;
}

std::vector<Bytes> Assembler::feed(const uint8_t* data, size_t size)
{
    std::vector<Bytes> out;
    for (size_t i = 0; i < size; ++i)
    {
        const uint8_t b = data[i];
        if (b == 0xF0)
        {
            buf_.assign(1, b);
            inSysex_ = true;
        }
        else if (!inSysex_)
            continue;
        else if (b == 0xF7)
        {
            buf_.push_back(b);
            out.push_back(buf_);
            buf_.clear();
            inSysex_ = false;
        }
        else if (b >= 0xF8)
            continue; // realtime bytes may interleave
        else if (b & 0x80)
        {
            buf_.clear(); // any other status byte aborts the message
            inSysex_ = false;
        }
        else if (buf_.size() >= maxLength_)
        {
            buf_.clear(); // oversized: drop and resync at the next F0
            inSysex_ = false;
        }
        else
            buf_.push_back(b);
    }
    return out;
}

} // namespace sysex

namespace ext
{

namespace
{
void u7(Bytes& b, int v) { b.push_back((uint8_t) (v & 0x7F)); }
void u14(Bytes& b, int v)
{
    b.push_back((uint8_t) (v & 0x7F));
    b.push_back((uint8_t) ((v >> 7) & 0x7F));
}
void packed(Bytes& b, const Bytes& raw)
{
    const auto p = sysex::pack8(raw);
    b.insert(b.end(), p.begin(), p.end());
}
Bytes nameBytes(const std::string& name)
{
    Bytes n(kNameLength, ' ');
    for (size_t i = 0; i < name.size() && i < (size_t) kNameLength; ++i)
        n[i] = (uint8_t) name[i];
    return n;
}
std::string trimName(const Bytes& raw)
{
    std::string s(raw.begin(), raw.end());
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0'))
        s.pop_back();
    return s;
}
Bytes pairsPayload(const std::vector<ParamPair>& pairs)
{
    Bytes b;
    for (const auto& p : pairs)
    {
        u14(b, p.id);
        u14(b, p.value);
    }
    return b;
}

struct Reader
{
    const uint8_t* p;
    size_t n, i = 0;
    bool ok = true;
    size_t left() const { return n - i; }
    int u7()
    {
        if (i >= n) { ok = false; return 0; }
        return p[i++];
    }
    int u14()
    {
        const int lo = u7(), hi = u7();
        return lo | (hi << 7);
    }
    Bytes rest()
    {
        Bytes b(p + i, p + n);
        i = n;
        return b;
    }
};
} // namespace

const char* commandName(Command c)
{
    switch (c)
    {
        case Command::IdentityRequest: return "IDENTITY_REQUEST";
        case Command::IdentityReply: return "IDENTITY_REPLY";
        case Command::StatusRequest: return "STATUS_REQUEST";
        case Command::StatusReply: return "STATUS_REPLY";
        case Command::KitRequest: return "KIT_REQUEST";
        case Command::KitDump: return "KIT_DUMP";
        case Command::KitStore: return "KIT_STORE";
        case Command::KitLoad: return "KIT_LOAD";
        case Command::KitName: return "KIT_NAME";
        case Command::PatternRequest: return "PATTERN_REQUEST";
        case Command::PatternChunk: return "PATTERN_CHUNK";
        case Command::PatternName: return "PATTERN_NAME";
        case Command::GlobalsRequest: return "GLOBALS_REQUEST";
        case Command::GlobalsDump: return "GLOBALS_DUMP";
        case Command::ParamGet: return "PARAM_GET";
        case Command::ParamSet: return "PARAM_SET";
        case Command::ParamValue: return "PARAM_VALUE";
        case Command::ParamNotify: return "PARAM_NOTIFY";
        case Command::EchoSet: return "ECHO_SET";
        case Command::Event: return "EVENT";
        case Command::Ack: return "ACK";
        case Command::Nak: return "NAK";
    }
    return "?";
}

const char* errorName(Error e)
{
    switch (e)
    {
        case Error::None: return "none";
        case Error::Checksum: return "checksum mismatch";
        case Error::UnknownCommand: return "unknown command";
        case Error::Length: return "bad length";
        case Error::Range: return "out of range";
        case Error::Busy: return "busy";
        case Error::Sequence: return "pattern chunk out of sequence";
        case Error::Version: return "unsupported protocol version";
        case Error::ReadOnly: return "read-only parameter";
        case Error::SdCard: return "SD card write failed";
    }
    return "unknown error";
}

bool Identity::layoutMatchesEditor() const
{
    return numParams == kNumParams && endOfSound == kNumSoundParams && firstGlobal == kFirstGlobal;
}

std::string Identity::describe() const
{
    char buf[160];
    std::snprintf(buf, sizeof buf, "firmware %d.%d.%d (%s), protocol v%d, device ID %d, %d params%s",
                  fwMajor, fwMinor, fwPatch, fwString.c_str(), protocolVersion, deviceId, numParams,
                  layoutMatchesEditor() ? "" : " (layout differs from editor)");
    return buf;
}

Bytes frame(int device, Command cmd, const Bytes& payload, int version)
{
    Bytes m { 0xF0, kManufacturer, kModel, (uint8_t) (device & 0x7F), (uint8_t) (version & 0x7F), (uint8_t) cmd };
    m.insert(m.end(), payload.begin(), payload.end());
    m.push_back(sysex::checksum(m.data() + 3, m.size() - 3));
    m.push_back(0xF7);
    return m;
}

Bytes identityRequest(int device) { return frame(device, Command::IdentityRequest, {}); }
Bytes statusRequest(int device) { return frame(device, Command::StatusRequest, {}); }
Bytes kitRequest(int device, int slot) { return frame(device, Command::KitRequest, { (uint8_t) (slot & 0x7F) }); }

Bytes kitDump(int device, int slot, int flags, const Bytes& snd)
{
    Bytes b;
    u7(b, slot);
    u7(b, flags);
    u14(b, (int) std::max<size_t>(snd.size(), (size_t) kNameLength) - kNameLength);
    Bytes raw = snd;
    raw.resize(std::max<size_t>(raw.size(), (size_t) kNameLength), ' ');
    packed(b, raw);
    return frame(device, Command::KitDump, b);
}

Bytes kitStore(int device, int slot, int flags) { return frame(device, Command::KitStore, { (uint8_t) (slot & 0x7F), (uint8_t) (flags & 0x7F) }); }
Bytes kitLoad(int device, int slot) { return frame(device, Command::KitLoad, { (uint8_t) (slot & 0x7F) }); }

Bytes kitName(int device, int slot, const std::string& name)
{
    Bytes b;
    u7(b, slot);
    packed(b, nameBytes(name));
    return frame(device, Command::KitName, b);
}

Bytes patternRequest(int device, int slot) { return frame(device, Command::PatternRequest, { (uint8_t) (slot & 0x7F) }); }

Bytes patternName(int device, int slot, const std::string& name)
{
    Bytes b;
    u7(b, slot);
    packed(b, nameBytes(name));
    return frame(device, Command::PatternName, b);
}

Bytes patternChunk(int device, int slot, int flags, int index, int count, int totalLength, const Bytes& chunk)
{
    Bytes b;
    u7(b, slot);
    u7(b, flags);
    u7(b, index);
    u7(b, count);
    u14(b, totalLength);
    packed(b, chunk);
    return frame(device, Command::PatternChunk, b);
}

std::vector<Bytes> patternChunks(int device, int slot, int flags, const Bytes& raw, int chunkRaw)
{
    chunkRaw = std::clamp(chunkRaw, 1, kMaxChunkRaw);
    const int total = (int) raw.size();
    const int count = std::max(1, (total + chunkRaw - 1) / chunkRaw);
    std::vector<Bytes> out;
    for (int i = 0; i < count; ++i)
    {
        const size_t a = (size_t) (i * chunkRaw), e = std::min(raw.size(), a + (size_t) chunkRaw);
        out.push_back(patternChunk(device, slot, i == 0 ? flags : 0, i, count, total, Bytes(raw.begin() + (long) a, raw.begin() + (long) e)));
    }
    return out;
}

Bytes globalsRequest(int device) { return frame(device, Command::GlobalsRequest, {}); }

Bytes globalsDump(int device, int flags, int first, const Bytes& values)
{
    Bytes b;
    u7(b, flags);
    u14(b, first);
    u14(b, (int) values.size());
    packed(b, values);
    return frame(device, Command::GlobalsDump, b);
}

Bytes paramGet(int device, const std::vector<int>& ids)
{
    Bytes b;
    for (int id : ids)
        u14(b, id);
    return frame(device, Command::ParamGet, b);
}

Bytes paramSet(int device, const std::vector<ParamPair>& pairs) { return frame(device, Command::ParamSet, pairsPayload(pairs)); }

std::vector<Bytes> paramSetFrames(int device, const std::vector<ParamPair>& pairs)
{
    std::vector<Bytes> out;
    for (size_t i = 0; i < pairs.size(); i += kMaxSetPairs)
    {
        const auto e = std::min(pairs.size(), i + (size_t) kMaxSetPairs);
        out.push_back(paramSet(device, std::vector<ParamPair>(pairs.begin() + (long) i, pairs.begin() + (long) e)));
    }
    return out;
}

Bytes echoSet(int device, bool on) { return frame(device, Command::EchoSet, { (uint8_t) (on ? 1 : 0) }); }

Bytes identityReply(const Identity& id)
{
    Bytes b;
    u7(b, id.protocolVersion);
    u7(b, id.fwMajor);
    u7(b, id.fwMinor);
    u7(b, id.fwPatch);
    for (int i = 0; i < 8; ++i)
        u7(b, i < (int) id.fwString.size() ? id.fwString[(size_t) i] : 0);
    u14(b, id.numParams);
    u14(b, id.endOfSound);
    u14(b, id.firstGlobal);
    u7(b, id.kitSlots);
    u7(b, id.patternSlots);
    u7(b, id.tracks);
    u7(b, id.steps);
    u14(b, id.patternLength);
    u14(b, id.maxFrame);
    u14(b, id.capabilities);
    u7(b, id.deviceId - 1);
    u7(b, id.echo ? 1 : 0);
    return frame(id.deviceId - 1, Command::IdentityReply, b);
}

Bytes statusReply(int device, const Status& s)
{
    return frame(device, Command::StatusReply, { (uint8_t) (s.kitSlot & 0x7F), (uint8_t) (s.pattern & 0x7F),
                                                 (uint8_t) (s.running ? 1 : 0), (uint8_t) (s.echo ? 1 : 0) });
}

Bytes paramValue(int device, const std::vector<ParamPair>& pairs) { return frame(device, Command::ParamValue, pairsPayload(pairs)); }
Bytes paramNotify(int device, const std::vector<ParamPair>& pairs) { return frame(device, Command::ParamNotify, pairsPayload(pairs)); }

Bytes event(int device, EventType type, int arg)
{
    Bytes b { (uint8_t) type };
    u14(b, arg);
    return frame(device, Command::Event, b);
}

Bytes ack(int device, Command answered, int arg)
{
    Bytes b { (uint8_t) answered };
    u14(b, arg);
    return frame(device, Command::Ack, b);
}

Bytes nak(int device, Command answered, Error err, int arg)
{
    Bytes b { (uint8_t) answered, (uint8_t) err };
    u14(b, arg);
    return frame(device, Command::Nak, b);
}

Bytes universalIdentityRequest() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }

bool isOurs(const Bytes& f)
{
    return f.size() >= 8 && f[0] == 0xF0 && f[1] == kManufacturer && f[2] == kModel && f.back() == 0xF7;
}

std::optional<Message> parse(const Bytes& f, std::string* error)
{
    auto fail = [&](const std::string& why) -> std::optional<Message> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (!isOurs(f))
        return fail("not an extended-protocol frame");
    for (size_t i = 1; i + 1 < f.size(); ++i)
        if (f[i] & 0x80)
            return fail("status byte inside frame");
    const size_t sumAt = f.size() - 2;
    if (sysex::checksum(f.data() + 3, sumAt - 3) != f[sumAt])
        return fail("checksum mismatch");

    Message m;
    m.device = f[3];
    m.version = f[4];
    m.command = (Command) f[5];
    if (m.version != kProtocolVersion && m.command != Command::IdentityRequest)
        return fail("unsupported protocol version " + std::to_string(m.version));

    Reader r { f.data() + 6, sumAt - 6 };
    auto unpackRest = [&](Bytes& into) {
        const auto rest = r.rest();
        auto raw = sysex::unpack8(rest);
        if (!raw)
            return false;
        into = std::move(*raw);
        return true;
    };
    auto readName = [&]() {
        Bytes raw;
        if (!unpackRest(raw) || raw.size() != (size_t) kNameLength)
            return false;
        m.rawName = raw;
        m.name = trimName(raw);
        return true;
    };
    auto readPairs = [&]() {
        if (r.left() == 0 || r.left() % 4 != 0)
            return false;
        while (r.left())
        {
            ParamPair p;
            p.id = r.u14();
            p.value = r.u14();
            m.pairs.push_back(p);
        }
        return true;
    };
    bool ok = true;
    size_t expect = SIZE_MAX; // exact payload length where fixed
    switch (m.command)
    {
        case Command::IdentityRequest:
        case Command::StatusRequest:
        case Command::GlobalsRequest:
            expect = 0;
            break;
        case Command::IdentityReply:
        {
            expect = 30;
            if (r.left() != expect)
                break;
            auto& id = m.identity;
            id.protocolVersion = r.u7();
            id.fwMajor = r.u7();
            id.fwMinor = r.u7();
            id.fwPatch = r.u7();
            for (int i = 0; i < 8; ++i)
                if (const int c = r.u7(); c != 0)
                    id.fwString.push_back((char) c);
            id.numParams = r.u14();
            id.endOfSound = r.u14();
            id.firstGlobal = r.u14();
            id.kitSlots = r.u7();
            id.patternSlots = r.u7();
            id.tracks = r.u7();
            id.steps = r.u7();
            id.patternLength = r.u14();
            id.maxFrame = r.u14();
            id.capabilities = (uint16_t) r.u14();
            id.deviceId = r.u7() + 1;
            id.echo = r.u7() != 0;
            break;
        }
        case Command::StatusReply:
            expect = 4;
            if (r.left() != expect)
                break;
            m.status.kitSlot = r.u7();
            m.status.pattern = r.u7();
            m.status.running = r.u7() != 0;
            m.status.echo = r.u7() != 0;
            break;
        case Command::KitRequest:
        case Command::KitLoad:
        case Command::PatternRequest:
            expect = 1;
            m.slot = r.u7();
            break;
        case Command::KitStore:
            expect = 2;
            m.slot = r.u7();
            m.flags = r.u7();
            break;
        case Command::KitDump:
            m.slot = r.u7();
            m.flags = r.u7();
            m.count = r.u14();
            ok = r.ok && unpackRest(m.data) && m.data.size() == (size_t) (kNameLength + m.count);
            if (ok)
            {
                m.rawName.assign(m.data.begin(), m.data.begin() + kNameLength);
                m.name = trimName(m.rawName);
            }
            break;
        case Command::KitName:
        case Command::PatternName:
            m.slot = r.u7();
            ok = r.ok && readName();
            break;
        case Command::PatternChunk:
            m.slot = r.u7();
            m.flags = r.u7();
            m.chunkIndex = r.u7();
            m.chunkCount = r.u7();
            m.totalLength = r.u14();
            ok = r.ok && unpackRest(m.data) && m.chunkIndex < m.chunkCount;
            break;
        case Command::GlobalsDump:
            m.flags = r.u7();
            m.first = r.u14();
            m.count = r.u14();
            ok = r.ok && unpackRest(m.data) && m.data.size() == (size_t) m.count;
            break;
        case Command::ParamGet:
            ok = r.left() >= 2 && r.left() % 2 == 0 && r.left() / 2 <= (size_t) kMaxGetIds;
            while (ok && r.left())
                m.ids.push_back(r.u14());
            break;
        case Command::ParamSet:
            ok = readPairs() && m.pairs.size() <= (size_t) kMaxSetPairs;
            break;
        case Command::ParamValue:
        case Command::ParamNotify:
            ok = readPairs();
            break;
        case Command::EchoSet:
            expect = 1;
            m.on = r.u7() != 0;
            break;
        case Command::Event:
            expect = 3;
            m.event = (EventType) r.u7();
            m.arg = r.u14();
            break;
        case Command::Ack:
            expect = 3;
            m.answered = (Command) r.u7();
            m.arg = r.u14();
            break;
        case Command::Nak:
            expect = 4;
            m.answered = (Command) r.u7();
            m.error = (Error) r.u7();
            m.arg = r.u14();
            break;
        default:
            return fail("unknown command " + std::to_string((int) m.command));
    }
    if (expect != SIZE_MAX && sumAt - 6 != expect)
        return fail(std::string("bad payload length for ") + commandName(m.command));
    if (!ok || !r.ok)
        return fail(std::string("malformed ") + commandName(m.command));
    return m;
}

std::optional<Bytes> patternRawFromPat(const Bytes& pat)
{
    std::string err;
    const auto p = Pattern::fromPat(pat, &err);
    if (!p)
        return std::nullopt;
    const auto v3 = p->toPat(); // normalises V1/V2 to V3
    Bytes raw(v3.begin(), v3.begin() + kNameLength);
    raw.insert(raw.end(), v3.begin() + kNameLength + 6, v3.end());
    return raw;
}

Bytes patFromPatternRaw(const Bytes& raw)
{
    Bytes pat(raw.begin(), raw.begin() + (long) std::min(raw.size(), (size_t) kNameLength));
    pat.resize(kNameLength, ' ');
    const char tag[] = "PatV03";
    pat.insert(pat.end(), tag, tag + 6);
    if (raw.size() > (size_t) kNameLength)
        pat.insert(pat.end(), raw.begin() + kNameLength, raw.end());
    return pat;
}

PatternReceiver::State PatternReceiver::feed(const Message& c)
{
    if (c.command != Command::PatternChunk || state_ == State::Complete || state_ == State::Failed)
        return state_;
    auto failWith = [&](const std::string& why) {
        error_ = why;
        state_ = State::Failed;
        return state_;
    };
    if (c.chunkIndex == 0)
    {
        if (c.totalLength <= 0 || c.totalLength > 16384 || c.chunkCount <= 0)
            return failWith("implausible pattern size");
        slot_ = c.slot;
        count_ = c.chunkCount;
        total_ = c.totalLength;
        next_ = 0;
        data_.clear();
        data_.reserve((size_t) total_);
        state_ = State::Receiving;
    }
    else if (state_ != State::Receiving)
        return state_; // a stray chunk before chunk 0
    if (c.chunkIndex != next_ || c.slot != slot_ || c.chunkCount != count_ || c.totalLength != total_)
        return failWith("pattern chunk out of sequence");
    data_.insert(data_.end(), c.data.begin(), c.data.end());
    if (data_.size() > (size_t) total_)
        return failWith("pattern data longer than announced");
    ++next_;
    if (next_ == count_)
    {
        if (data_.size() != (size_t) total_)
            return failWith("pattern data shorter than announced");
        state_ = State::Complete;
    }
    return state_;
}

void RequestQueue::push(Request r)
{
    std::lock_guard<std::mutex> g(lock_);
    queue_.push_back(std::move(r));
}

void RequestQueue::finish(Outcome o, std::vector<Call>& calls, std::vector<Callback>& cancelled)
{
    auto a = std::move(*active_);
    active_.reset();
    if (o != Outcome::Ok && a.req.group != 0)
    {
        for (auto it = queue_.begin(); it != queue_.end();)
        {
            if (it->group == a.req.group)
            {
                if (it->done)
                    cancelled.push_back(std::move(it->done));
                it = queue_.erase(it);
            }
            else
                ++it;
        }
    }
    if (a.req.done)
        calls.push_back({ std::move(a.req.done), o, std::move(a.replies) });
}

std::optional<Bytes> RequestQueue::poll(int64_t nowMs)
{
    std::vector<Call> calls;
    std::vector<Callback> cancelled;
    std::optional<Bytes> send;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (active_ && nowMs >= active_->deadline)
            finish(Outcome::Timeout, calls, cancelled);
        if (!active_ && !queue_.empty())
        {
            Active a;
            a.req = std::move(queue_.front());
            queue_.pop_front();
            a.deadline = nowMs + a.req.timeoutMs;
            send = a.req.frame;
            if (!a.req.matcher)
            {
                // fire-and-forget: completes as soon as it is handed out
                if (a.req.done)
                    calls.push_back({ std::move(a.req.done), Outcome::Ok, {} });
            }
            else
                active_ = std::move(a);
        }
    }
    for (auto& c : calls)
        c.done(c.outcome, c.replies);
    for (auto& c : cancelled)
        c(Outcome::Cancelled, {});
    return send;
}

bool RequestQueue::onMessage(const Message& m, int64_t nowMs)
{
    std::vector<Call> calls;
    std::vector<Callback> cancelled;
    Outcome outcome = Outcome::Ok;
    bool mine = false;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (!active_)
            return false;
        const auto match = active_->req.matcher(m);
        if (match == Match::No)
            return false;
        mine = true;
        active_->replies.push_back(m);
        if (match == Match::Progress)
            active_->deadline = std::max(active_->deadline, nowMs + 200);
        else
        {
            outcome = match == Match::Done ? Outcome::Ok : Outcome::Nak;
            finish(outcome, calls, cancelled);
        }
    }
    for (auto& c : calls)
        c.done(c.outcome, c.replies);
    for (auto& c : cancelled)
        c(Outcome::Cancelled, {});
    return mine;
}

void RequestQueue::cancelAll()
{
    std::vector<Callback> cancelled;
    {
        std::lock_guard<std::mutex> g(lock_);
        if (active_ && active_->req.done)
            cancelled.push_back(std::move(active_->req.done));
        active_.reset();
        for (auto& r : queue_)
            if (r.done)
                cancelled.push_back(std::move(r.done));
        queue_.clear();
    }
    for (auto& c : cancelled)
        c(Outcome::Cancelled, {});
}

bool RequestQueue::idle() const
{
    std::lock_guard<std::mutex> g(lock_);
    return !active_ && queue_.empty();
}

size_t RequestQueue::pending() const
{
    std::lock_guard<std::mutex> g(lock_);
    return queue_.size() + (active_ ? 1 : 0);
}

RequestQueue::Matcher RequestQueue::replyTo(Command request, Command reply)
{
    return [request, reply](const Message& m) {
        if (m.command == reply)
            return Match::Done;
        if (m.command == Command::Nak && m.answered == request)
            return Match::Failed;
        return Match::No;
    };
}

RequestQueue::Matcher RequestQueue::ackFor(Command request, int arg)
{
    return [request, arg](const Message& m) {
        if (m.command == Command::Ack && m.answered == request && (arg < 0 || m.arg == arg))
            return Match::Done;
        if (m.command == Command::Nak && m.answered == request)
            return Match::Failed;
        return Match::No;
    };
}

RequestQueue::Matcher RequestQueue::patternDump(std::shared_ptr<PatternReceiver> rx)
{
    return [rx](const Message& m) {
        if (m.command == Command::Nak && m.answered == Command::PatternRequest)
            return Match::Failed;
        if (m.command != Command::PatternChunk)
            return Match::No;
        switch (rx->feed(m))
        {
            case PatternReceiver::State::Complete: return Match::Done;
            case PatternReceiver::State::Failed: return Match::Failed;
            default: return Match::Progress;
        }
    };
}

} // namespace ext
} // namespace kitforge
