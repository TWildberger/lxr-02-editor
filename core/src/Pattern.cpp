#include "kitforge/Pattern.h"

#include <cstdlib>
#include <cstring>

namespace kitforge
{

namespace
{
struct Reader
{
    const Bytes& d;
    size_t pos = 0;
    bool get(uint8_t& v)
    {
        if (pos >= d.size())
            return false;
        v = d[pos++];
        return true;
    }
    size_t remaining() const { return d.size() - pos; }
};
} // namespace

std::optional<Pattern> Pattern::fromPat(const Bytes& data, std::string* error)
{
    auto fail = [&](const char* msg) -> std::optional<Pattern> {
        if (error)
            *error = msg;
        return std::nullopt;
    };

    if (data.size() < 8)
        return fail("pattern file too short for name");

    Pattern p;
    for (auto& t : p.tracks)
        t.length = 16;
    std::memcpy(p.rawName.data(), data.data(), 8);
    Reader r { data, 8 };

    p.sourceVersion = 1;
    if (data.size() >= 14 && std::memcmp(&data[8], "PatV", 4) == 0)
    {
        char num[3] = { (char) data[12], (char) data[13], 0 };
        p.sourceVersion = std::atoi(num);
        r.pos = 14;
    }

    for (auto& t : p.tracks)
    {
        if (r.remaining() < (size_t) PatternTrack::kSteps * kStepBytes)
            return fail("pattern step data truncated");
        for (auto& s : t.steps)
        {
            r.get(s.volume);
            r.get(s.prob);
            r.get(s.note);
            r.get(s.param1Nr);
            r.get(s.param1Val);
            r.get(s.param2Nr);
            r.get(s.param2Val);
        }
        if (p.sourceVersion > 2)
        {
            if (r.remaining() < (size_t) PatternTrack::kSteps)
                return fail("pattern step timing truncated");
            for (auto& b : t.timing)
                r.get(b);
        }
    }

    for (auto& t : p.tracks)
        for (auto& m : t.mainSteps)
        {
            uint8_t lo, hi;
            if (!r.get(lo) || !r.get(hi))
                return fail("pattern main steps truncated");
            m = (uint16_t) (lo | (hi << 8));
        }

    if (!r.get(p.shuffle))
        return fail("pattern shuffle missing");

    // track lengths are optional in older files; the firmware accepts their absence
    p.hadTrackLengths = r.remaining() >= (size_t) kTracks;
    if (p.hadTrackLengths)
        for (auto& t : p.tracks)
            r.get(t.length);

    if (p.sourceVersion > 1 && !r.get(p.kitNr))
        return fail("pattern kit number missing");

    if (error)
        error->clear();
    return p;
}

Bytes Pattern::toPat() const
{
    Bytes out;
    out.reserve(kV3FileSize);
    out.insert(out.end(), rawName.begin(), rawName.end());
    const char* magic = "PatV03";
    out.insert(out.end(), magic, magic + 6);
    for (const auto& t : tracks)
    {
        for (const auto& s : t.steps)
        {
            out.push_back(s.volume);
            out.push_back(s.prob);
            out.push_back(s.note);
            out.push_back(s.param1Nr);
            out.push_back(s.param1Val);
            out.push_back(s.param2Nr);
            out.push_back(s.param2Val);
        }
        out.insert(out.end(), t.timing.begin(), t.timing.end());
    }
    for (const auto& t : tracks)
        for (auto m : t.mainSteps)
        {
            out.push_back((uint8_t) (m & 0xFF));
            out.push_back((uint8_t) (m >> 8));
        }
    out.push_back(shuffle);
    for (const auto& t : tracks)
        out.push_back(t.length);
    out.push_back(kitNr);
    return out;
}

std::string Pattern::name() const { return decodeName(rawName.data(), 8); }
void Pattern::setName(const std::string& n) { encodeName(n, rawName.data(), 8); }

int Pattern::activeStepCount() const
{
    int n = 0;
    for (const auto& t : tracks)
        for (const auto& s : t.steps)
            n += s.active() ? 1 : 0;
    return n;
}

std::optional<Song> Song::fromSng(const Bytes& data, std::string* error)
{
    if (data.size() < 8)
    {
        if (error)
            *error = "song file too short";
        return std::nullopt;
    }
    Song s;
    std::memcpy(s.rawName.data(), data.data(), 8);
    for (size_t i = 0; i < (size_t) kLength && 8 + i < data.size(); ++i)
        s.entries[i] = data[8 + i];
    if (error)
        error->clear();
    return s;
}

Bytes Song::toSng() const
{
    Bytes out(rawName.begin(), rawName.end());
    out.insert(out.end(), entries.begin(), entries.end());
    return out;
}

std::string Song::name() const { return decodeName(rawName.data(), 8); }

} // namespace kitforge
