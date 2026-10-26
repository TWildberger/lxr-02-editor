#include "kitforge/SysEx.h"

namespace kitforge
{
namespace sysex
{

Bytes pack7(const uint8_t* data, size_t size)
{
    Bytes out;
    out.reserve(packedSize(size));
    for (size_t i = 0; i < size; i += 7)
    {
        const size_t n = std::min<size_t>(7, size - i);
        uint8_t msbs = 0;
        for (size_t j = 0; j < n; ++j)
            msbs |= (uint8_t) (((data[i + j] >> 7) & 1) << j);
        out.push_back(msbs);
        for (size_t j = 0; j < n; ++j)
            out.push_back(data[i + j] & 0x7F);
    }
    return out;
}

std::optional<Bytes> unpack7(const uint8_t* data, size_t size)
{
    Bytes out;
    size_t i = 0;
    while (i < size)
    {
        const uint8_t msbs = data[i++];
        if (msbs & 0x80)
            return std::nullopt;
        const size_t n = std::min<size_t>(7, size - i);
        if (n == 0)
            return std::nullopt;
        for (size_t j = 0; j < n; ++j)
        {
            const uint8_t b = data[i + j];
            if (b & 0x80)
                return std::nullopt;
            out.push_back((uint8_t) (b | (((msbs >> j) & 1) << 7)));
        }
        i += n;
    }
    return out;
}

uint8_t checksum(const uint8_t* data, size_t size)
{
    unsigned sum = 0;
    for (size_t i = 0; i < size; ++i)
        sum += data[i];
    return (uint8_t) ((128 - (sum & 0x7F)) & 0x7F);
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
Bytes frame(Command cmd, const Bytes& body)
{
    Bytes m { 0xF0, kManufacturer, kMagic0, kMagic1, kProtocolVersion, (uint8_t) cmd };
    m.insert(m.end(), body.begin(), body.end());
    m.push_back(sysex::checksum(m.data() + 4, m.size() - 4));
    m.push_back(0xF7);
    return m;
}

Bytes withPacked(std::initializer_list<uint8_t> head, const Bytes& raw)
{
    Bytes b(head);
    const auto p = sysex::pack7(raw);
    b.insert(b.end(), p.begin(), p.end());
    return b;
}

Bytes paramBody(int param, int value)
{
    return { (uint8_t) ((param >> 7) & 0x7F), (uint8_t) (param & 0x7F), (uint8_t) ((value >> 7) & 0x7F), (uint8_t) (value & 0x7F) };
}
} // namespace

std::string Identity::describe() const
{
    return "protocol v" + std::to_string(protocolVersion) + ", firmware " + std::to_string(fwMajor) + "." + std::to_string(fwMinor)
           + "." + std::to_string(fwPatch);
}

Bytes identityRequest() { return frame(Command::IdentityRequest, {}); }

Bytes identityReply(const Identity& id)
{
    return frame(Command::IdentityReply,
                 { (uint8_t) (id.protocolVersion & 0x7F), (uint8_t) (id.fwMajor & 0x7F), (uint8_t) (id.fwMinor & 0x7F),
                   (uint8_t) (id.fwPatch & 0x7F), (uint8_t) ((id.capabilities >> 7) & 0x7F), (uint8_t) (id.capabilities & 0x7F) });
}

Bytes kitRequest(int slot) { return frame(Command::KitRequest, { (uint8_t) (slot & 0x7F) }); }
Bytes kitDump(int slot, const Bytes& snd) { return frame(Command::KitDump, withPacked({ (uint8_t) (slot & 0x7F) }, snd)); }
Bytes paramSet(int param, int value) { return frame(Command::ParamSet, paramBody(param, value)); }
Bytes paramChanged(int param, int value) { return frame(Command::ParamChanged, paramBody(param, value)); }
Bytes kitStore(int slot) { return frame(Command::KitStore, { (uint8_t) (slot & 0x7F) }); }
Bytes patternRequest(int slot) { return frame(Command::PatternRequest, { (uint8_t) (slot & 0x7F) }); }
Bytes patternDump(int slot, const Bytes& pat) { return frame(Command::PatternDump, withPacked({ (uint8_t) (slot & 0x7F) }, pat)); }
Bytes globalsRequest() { return frame(Command::GlobalsRequest, {}); }
Bytes globalsDump(const Bytes& cfg) { return frame(Command::GlobalsDump, withPacked({}, cfg)); }
Bytes ack(Command cmd, int status) { return frame(Command::Ack, { (uint8_t) cmd, (uint8_t) (status & 0x7F) }); }

Bytes universalIdentityRequest() { return { 0xF0, 0x7E, 0x7F, 0x06, 0x01, 0xF7 }; }

std::optional<Message> parse(const Bytes& m, std::string* error)
{
    auto fail = [&](const char* why) -> std::optional<Message> {
        if (error)
            *error = why;
        return std::nullopt;
    };
    if (m.size() < 8 || m.front() != 0xF0 || m.back() != 0xF7)
        return fail("not a complete sysex message");
    if (m[1] != kManufacturer || m[2] != kMagic0 || m[3] != kMagic1)
        return fail("foreign sysex");
    for (size_t i = 1; i + 1 < m.size(); ++i)
        if (m[i] & 0x80)
            return fail("data byte with high bit set");
    if (sysex::checksum(m.data() + 4, m.size() - 6) != m[m.size() - 2])
        return fail("checksum mismatch");

    Message msg;
    msg.version = m[4];
    msg.command = (Command) m[5];
    const uint8_t* body = m.data() + 6;
    const size_t n = m.size() - 8;

    auto needs = [&](size_t k) { return n >= k; };
    auto unpackFrom = [&](size_t offset) -> bool {
        auto d = sysex::unpack7(body + offset, n - offset);
        if (!d)
            return false;
        msg.data = std::move(*d);
        return true;
    };

    switch (msg.command)
    {
        case Command::IdentityRequest:
        case Command::GlobalsRequest:
            break;
        case Command::IdentityReply:
            if (!needs(6))
                return fail("short identity reply");
            msg.identity.protocolVersion = body[0];
            msg.identity.fwMajor = body[1];
            msg.identity.fwMinor = body[2];
            msg.identity.fwPatch = body[3];
            msg.identity.capabilities = (uint16_t) ((body[4] << 7) | body[5]);
            break;
        case Command::KitRequest:
        case Command::KitStore:
        case Command::PatternRequest:
            if (!needs(1))
                return fail("missing slot");
            msg.slot = body[0];
            break;
        case Command::KitDump:
        case Command::PatternDump:
            if (!needs(1))
                return fail("missing slot");
            msg.slot = body[0];
            if (!unpackFrom(1))
                return fail("bad packed payload");
            break;
        case Command::GlobalsDump:
            if (!unpackFrom(0))
                return fail("bad packed payload");
            break;
        case Command::ParamSet:
        case Command::ParamChanged:
            if (!needs(4))
                return fail("short param message");
            msg.param = (body[0] << 7) | body[1];
            msg.value = (body[2] << 7) | body[3];
            break;
        case Command::Ack:
            if (!needs(2))
                return fail("short ack");
            msg.param = body[0];
            msg.status = body[1];
            break;
        default:
            return fail("unknown command");
    }
    if (error)
        error->clear();
    return msg;
}

} // namespace ext
} // namespace kitforge
