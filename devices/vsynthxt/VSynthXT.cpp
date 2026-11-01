#include "VSynthXT.h"
#include <algorithm>
#include <memory>
#include <sqlite3.h>

namespace vsynthxt
{
namespace
{
void require(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
bool sevenBit(const Bytes& b)
{
    return std::all_of(b.begin(), b.end(), [](uint8_t v) { return v < 128; });
}
void deviceCheck(uint8_t device)
{
    require(device >= 0x10 && device <= 0x1f, "XT device ID must be 17-32 (wire 10-1F)");
}
Bytes frame(uint8_t device, uint8_t command, Address start, const Bytes& payload)
{
    deviceCheck(device);
    Bytes body = digits(start);
    body.insert(body.end(), payload.begin(), payload.end());
    Bytes out { 0xf0, 0x41, device, 0x00, 0x53, command };
    out.insert(out.end(), body.begin(), body.end());
    out.push_back(checksum(body));
    out.push_back(0xf7);
    return out;
}
}

const std::vector<Block>& blocks()
{
    // Roland V-Synth XT MIDI Implementation pp. 7-17. Offsets and sizes are base 128.
    static const std::vector<Block> table = [] {
        std::vector<Block> out {
            {0, 0x72, "Common", 11}, {2 * 128, 132, "MFX", 11},
            {4 * 128, 0x33, "Chorus", 12}, {6 * 128, 0x62, "Reverb", 12},
            {8 * 128, 0x0d, "Controller", 15}, {9 * 128, 0x10, "Controller 2 (XT)", 15},
            {10 * 128, 0x62, "Step modulator", 17}
        };
        for (int zone = 0; zone < 16; ++zone)
        {
            const auto z = static_cast<Address>(zone);
            out.push_back({(0x10 + z) * 128, 0x7f, "Oscillator", 13});
            out.push_back({(0x20 + 2 * z) * 128, 169, "Envelope", 14});
            out.push_back({(0x40 + z) * 128, 0x28, "LFO", 15});
            out.push_back({(0x50 + z) * 128, 0x42, "COSM 1", 15});
            out.push_back({(0x60 + z) * 128, 0x42, "COSM 2", 15});
            out.push_back({(0x70 + z) * 128, 0x42, "Arpeggio", 16});
        }
        std::sort(out.begin(), out.end(), [](const Block& a, const Block& b) { return a.offset < b.offset; });
        return out;
    }();
    return table;
}

Bytes digits(Address a)
{
    require(a < (1u << 28), "Roland address/size overflow");
    return {uint8_t((a >> 21) & 127), uint8_t((a >> 14) & 127),
            uint8_t((a >> 7) & 127), uint8_t(a & 127)};
}
Address address(const uint8_t* b)
{
    Address a = 0;
    for (int i = 0; i < 4; ++i)
    {
        require(b[i] < 128, "Invalid Roland address digit");
        a = (a << 7) | b[i];
    }
    return a;
}
uint8_t checksum(const Bytes& b)
{
    unsigned sum = 0;
    for (auto v : b) sum += v;
    return uint8_t((128 - (sum & 127)) & 127);
}
Bytes request(uint8_t device, Address start, size_t size)
{
    require(size > 0 && size < (1u << 28), "Invalid Roland request size");
    return frame(device, 0x11, start, digits(static_cast<Address>(size)));
}
Bytes dataSet(uint8_t device, Address start, const Bytes& data)
{
    require(!data.empty() && data.size() <= 256 && sevenBit(data), "Invalid DT1 data");
    require(uint64_t(start) + data.size() <= (1u << 28), "DT1 address overflow");
    return frame(device, 0x12, start, data);
}
Message decode(const Bytes& f)
{
    require(f.size() >= 13 && f.size() <= 268, "Invalid DT1 length");
    require(f.front() == 0xf0 && f.back() == 0xf7, "Invalid SysEx framing");
    require(f[1] == 0x41 && f[3] == 0 && f[4] == 0x53 && f[5] == 0x12,
            "Not a native V-Synth DT1 message");
    deviceCheck(f[2]);
    Bytes body(f.begin() + 6, f.end() - 2);
    require(sevenBit(body) && checksum(body) == f[f.size() - 2], "Invalid DT1 checksum/data");
    Message m { f[2], address(f.data() + 6), Bytes(f.begin() + 10, f.end() - 2) };
    require(uint64_t(m.start) + m.data.size() <= (1u << 28), "DT1 address overflow");
    return m;
}

bool Patch::complete() const
{
    if (data.size() != blocks().size()) return false;
    for (const auto& b : blocks())
    {
        auto it = data.find(b.offset);
        if (it == data.end() || it->second.size() != b.size || !sevenBit(it->second)) return false;
    }
    return true;
}
std::string Patch::name() const
{
    auto it = data.find(0);
    if (it == data.end() || it->second.size() < 12) return {};
    std::string s(it->second.begin(), it->second.begin() + 12);
    while (!s.empty() && s.back() == ' ') s.pop_back();
    return s;
}
void Patch::setName(const std::string& s)
{
    require(complete(), "Fetch or import a complete patch first");
    require(s.size() <= 12 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return c >= 32 && c < 127; }),
            "Patch name must contain at most 12 printable ASCII characters");
    std::fill_n(data.at(0).begin(), 12, uint8_t(' '));
    std::copy(s.begin(), s.end(), data.at(0).begin());
}
int Patch::value(Address a) const
{
    auto it = data.upper_bound(a);
    require(it != data.begin(), "Unknown patch parameter");
    --it;
    require(a - it->first < it->second.size(), "Unknown patch parameter");
    return it->second.at(a - it->first);
}
void Patch::setValue(Address a, int v)
{
    require(complete(), "Fetch or import a complete patch first");
    const bool valid = (a == 0x0d && v >= 16 && v <= 112)
        || (a == 0x0e && v >= 14 && v <= 114) || (a == 4 * 128 && v >= 0 && v <= 8);
    require(valid, "Unsupported parameter or value");
    auto it = data.upper_bound(a);
    --it;
    it->second.at(a - it->first) = static_cast<uint8_t>(v);
}
Bytes Patch::sysex(uint8_t device) const
{
    require(complete(), "Cannot export an incomplete patch");
    Bytes out;
    for (const auto& [offset, bytes] : data)
    {
        auto f = dataSet(device, temporary + offset, bytes);
        out.insert(out.end(), f.begin(), f.end());
    }
    return out;
}
Patch Patch::fromSysex(const Bytes& bytes)
{
    require(!bytes.empty() && bytes.size() <= 65536, "Invalid patch file size");
    Patch p;
    std::vector<Receiver> receivers;
    for (const auto& b : blocks()) receivers.emplace_back(b);
    size_t pos = 0;
    int device = -1;
    while (pos < bytes.size())
    {
        require(bytes[pos] == 0xf0, "Unexpected bytes outside patch SysEx");
        auto end = std::find(bytes.begin() + static_cast<ptrdiff_t>(pos), bytes.end(), 0xf7);
        require(end != bytes.end(), "Truncated patch SysEx");
        const auto next = static_cast<size_t>(end - bytes.begin()) + 1;
        auto m = decode(Bytes(bytes.begin() + static_cast<ptrdiff_t>(pos), bytes.begin() + static_cast<ptrdiff_t>(next)));
        if (device < 0) device = m.device;
        require(device == m.device, "Mixed device IDs in patch file");
        bool accepted = false;
        for (auto& r : receivers)
            if (r.accept(m, m.device)) { accepted = true; break; }
        require(accepted, "Patch file contains unsupported addresses");
        pos = next;
    }
    for (size_t i = 0; i < receivers.size(); ++i)
    {
        require(receivers[i].complete(), "Patch file is missing required blocks");
        p.data.emplace(blocks()[i].offset, receivers[i].result());
    }
    return p;
}

Receiver::Receiver(const Block& b) : block_(b), bytes_(b.size), received_(b.size, false) {}
bool Receiver::accept(const Message& m, uint8_t device)
{
    const auto start = temporary + block_.offset;
    if (m.device != device || m.start < start || m.start >= start + block_.size) return false;
    require(!m.data.empty() && sevenBit(m.data), "Invalid response data");
    const auto offset = m.start - start;
    require(m.data.size() <= block_.size - offset, "Response crosses requested block");
    for (size_t i = 0; i < m.data.size(); ++i)
    {
        require(!received_[offset + i] || bytes_[offset + i] == m.data[i], "Conflicting duplicate patch data");
        bytes_[offset + i] = m.data[i];
        received_[offset + i] = true;
    }
    return true;
}
bool Receiver::complete() const
{
    return std::all_of(received_.begin(), received_.end(), [](bool b) { return b; });
}

struct Library::Impl { sqlite3* db = nullptr; };
namespace
{
class Statement
{
public:
    Statement(sqlite3* db, const char* sql) : db_(db)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(s); }
    void text(int i, const std::string& v) { check(sqlite3_bind_text(s, i, v.c_str(), static_cast<int>(v.size()), SQLITE_TRANSIENT)); }
    void number(int i, int64_t v) { check(sqlite3_bind_int64(s, i, v)); }
    void blob(int i, const Bytes& b) { check(sqlite3_bind_blob(s, i, b.data(), static_cast<int>(b.size()), SQLITE_TRANSIENT)); }
    int step()
    {
        int rc = sqlite3_step(s);
        if (rc != SQLITE_ROW && rc != SQLITE_DONE) throw std::runtime_error(sqlite3_errmsg(db_));
        return rc;
    }
    std::string text(int c) const
    {
        const auto* p = sqlite3_column_text(s, c);
        return p ? reinterpret_cast<const char*>(p) : "";
    }
    sqlite3_stmt* s = nullptr;
private:
    void check(int rc) { if (rc != SQLITE_OK) throw std::runtime_error(sqlite3_errmsg(db_)); }
    sqlite3* db_;
};
}
Library::Library(const std::string& path) : impl_(std::make_unique<Impl>())
{
    if (sqlite3_open(path.c_str(), &impl_->db) != SQLITE_OK)
    {
        const std::string error = sqlite3_errmsg(impl_->db);
        sqlite3_close(impl_->db);
        throw std::runtime_error(error);
    }
    sqlite3_busy_timeout(impl_->db, 3000);
    const char* sql = "PRAGMA journal_mode=WAL;"
        "CREATE TABLE IF NOT EXISTS patches(id INTEGER PRIMARY KEY, name TEXT NOT NULL,"
        "data BLOB NOT NULL UNIQUE, tags TEXT NOT NULL, notes TEXT NOT NULL, favorite INTEGER NOT NULL);";
    if (sqlite3_exec(impl_->db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
    {
        const std::string error = sqlite3_errmsg(impl_->db);
        sqlite3_close(impl_->db);
        throw std::runtime_error(error);
    }
}
Library::~Library() { sqlite3_close(impl_->db); }
int64_t Library::add(const Patch& p, const std::string& tags, const std::string& notes, bool favorite)
{
    auto bytes = p.sysex(); // Canonical address order and device ID; exact bytes deduplicate without hash collisions.
    Statement s(impl_->db, "INSERT INTO patches(name,data,tags,notes,favorite) VALUES(?,?,?,?,?) "
        "ON CONFLICT(data) DO UPDATE SET tags=excluded.tags,notes=excluded.notes,favorite=excluded.favorite;");
    s.text(1, p.name()); s.blob(2, bytes); s.text(3, tags); s.text(4, notes); s.number(5, favorite);
    s.step();
    Statement get(impl_->db, "SELECT id FROM patches WHERE data=?");
    get.blob(1, bytes);
    require(get.step() == SQLITE_ROW, "Saved patch not found");
    return sqlite3_column_int64(get.s, 0);
}
std::vector<Entry> Library::search(const std::string& text)
{
    Statement s(impl_->db, "SELECT id,name,tags,notes,favorite FROM patches WHERE "
        "instr(lower(name || ' ' || tags || ' ' || notes),lower(?))>0 ORDER BY favorite DESC,name,id LIMIT 1000");
    s.text(1, text);
    std::vector<Entry> out;
    while (s.step() == SQLITE_ROW)
        out.push_back({sqlite3_column_int64(s.s, 0), s.text(1), s.text(2), s.text(3), sqlite3_column_int(s.s, 4) != 0});
    return out;
}
Patch Library::get(int64_t id)
{
    Statement s(impl_->db, "SELECT data FROM patches WHERE id=?");
    s.number(1, id);
    require(s.step() == SQLITE_ROW, "Library patch not found");
    const auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(s.s, 0));
    const auto n = sqlite3_column_bytes(s.s, 0);
    require(p && n > 0, "Empty library patch");
    return Patch::fromSysex(Bytes(p, p + n));
}
}
