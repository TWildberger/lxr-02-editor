#include "kitforge/Librarian.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <filesystem>

#include <sqlite3.h>

namespace fs = std::filesystem;

namespace kitforge
{

const char* kindName(FileKind k)
{
    switch (k)
    {
        case FileKind::Kit: return "kit";
        case FileKind::Pattern: return "pattern";
        case FileKind::Song: return "song";
        case FileKind::Globals: return "globals";
        case FileKind::ProjectInfo: return "project";
        default: return "unknown";
    }
}

std::optional<FileKind> kindFromName(const std::string& s)
{
    for (auto k : { FileKind::Kit, FileKind::Pattern, FileKind::Song, FileKind::Globals, FileKind::ProjectInfo })
        if (s == kindName(k))
            return k;
    return std::nullopt;
}

namespace
{
class Stmt
{
public:
    Stmt(sqlite3* db, const char* sql) { sqlite3_prepare_v2(db, sql, -1, &s_, nullptr); }
    ~Stmt() { sqlite3_finalize(s_); }
    bool ok() const { return s_ != nullptr; }
    Stmt& bind(int i, int64_t v) { sqlite3_bind_int64(s_, i, v); return *this; }
    Stmt& bind(int i, const std::string& v) { sqlite3_bind_text(s_, i, v.c_str(), (int) v.size(), SQLITE_TRANSIENT); return *this; }
    Stmt& bind(int i, const Bytes& v) { sqlite3_bind_blob(s_, i, v.data(), (int) v.size(), SQLITE_TRANSIENT); return *this; }
    int step() { return sqlite3_step(s_); }
    int64_t i64(int c) const { return sqlite3_column_int64(s_, c); }
    std::string text(int c) const
    {
        const auto* t = sqlite3_column_text(s_, c);
        return t ? std::string(reinterpret_cast<const char*>(t)) : std::string();
    }
    Bytes blob(int c) const
    {
        const auto* p = static_cast<const uint8_t*>(sqlite3_column_blob(s_, c));
        return p ? Bytes(p, p + sqlite3_column_bytes(s_, c)) : Bytes();
    }

private:
    sqlite3_stmt* s_ = nullptr;
};

int64_t now()
{
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    return s;
}

std::string normaliseTag(const std::string& t)
{
    std::string s;
    for (char c : t)
        if (!std::isspace((unsigned char) c) || (!s.empty() && s.back() != ' '))
            s.push_back(std::isspace((unsigned char) c) ? ' ' : (char) std::tolower((unsigned char) c));
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

std::string nameFromData(FileKind kind, const Bytes& data, const std::string& path)
{
    if ((kind == FileKind::Kit || kind == FileKind::Pattern || kind == FileKind::Song) && data.size() >= 8)
    {
        auto n = decodeName(data.data(), 8);
        if (!n.empty())
            return n;
    }
    return fs::path(path).stem().string();
}

/** Canonical bytes used for storage/dedupe: kits padded to 255, patterns upgraded to V3. */
std::optional<Bytes> canonicalise(FileKind kind, const Bytes& data, std::string* error)
{
    switch (kind)
    {
        case FileKind::Kit:
            return Kit::fromSnd(data).toSnd();
        case FileKind::Globals:
            return Globals::fromCfg(data).toCfg();
        case FileKind::Pattern:
        {
            auto p = Pattern::fromPat(data, error);
            if (!p)
                return std::nullopt;
            return p->toPat();
        }
        case FileKind::Song:
        {
            auto s = Song::fromSng(data, error);
            if (!s)
                return std::nullopt;
            return s->toSng();
        }
        default:
            if (error)
                *error = "unsupported file type";
            return std::nullopt;
    }
}
} // namespace

Librarian::Librarian(sqlite3* db) : db_(db) {}
Librarian::~Librarian()
{
    if (db_)
        sqlite3_close(db_);
}

bool Librarian::exec(const char* sql) { return sqlite3_exec(db_, sql, nullptr, nullptr, nullptr) == SQLITE_OK; }
std::string Librarian::lastError() const { return db_ ? sqlite3_errmsg(db_) : "no database"; }

std::unique_ptr<Librarian> Librarian::open(const std::string& path, std::string* error)
{
    sqlite3* db = nullptr;
    if (sqlite3_open(path.c_str(), &db) != SQLITE_OK)
    {
        if (error)
            *error = db ? sqlite3_errmsg(db) : "cannot open database";
        sqlite3_close(db);
        return nullptr;
    }
    std::unique_ptr<Librarian> lib(new Librarian(db));
    const char* schema = R"SQL(
        PRAGMA foreign_keys = ON;
        CREATE TABLE IF NOT EXISTS items (
            id INTEGER PRIMARY KEY,
            kind TEXT NOT NULL,
            name TEXT NOT NULL,
            data BLOB NOT NULL,
            hash TEXT NOT NULL,
            source TEXT NOT NULL DEFAULT '',
            notes TEXT NOT NULL DEFAULT '',
            favorite INTEGER NOT NULL DEFAULT 0,
            rating INTEGER NOT NULL DEFAULT 0,
            created_at INTEGER NOT NULL,
            updated_at INTEGER NOT NULL,
            UNIQUE(kind, hash)
        );
        CREATE TABLE IF NOT EXISTS tags (
            id INTEGER PRIMARY KEY,
            name TEXT NOT NULL UNIQUE
        );
        CREATE TABLE IF NOT EXISTS item_tags (
            item_id INTEGER NOT NULL REFERENCES items(id) ON DELETE CASCADE,
            tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
            PRIMARY KEY (item_id, tag_id)
        );
        CREATE INDEX IF NOT EXISTS items_kind_name ON items(kind, name);
        PRAGMA user_version = 1;
    )SQL";
    if (!lib->exec(schema))
    {
        if (error)
            *error = lib->lastError();
        return nullptr;
    }
    return lib;
}

std::optional<Librarian::AddResult> Librarian::add(FileKind kind, const std::string& name, const Bytes& data,
                                                   const std::string& source, std::string* error)
{
    auto canon = canonicalise(kind, data, error);
    if (!canon)
        return std::nullopt;
    const std::string hash = toHex(fnv1a64(*canon));
    {
        Stmt q(db_, "SELECT id FROM items WHERE kind = ?1 AND hash = ?2");
        q.bind(1, std::string(kindName(kind))).bind(2, hash);
        if (q.step() == SQLITE_ROW)
            return AddResult { q.i64(0), true };
    }
    Stmt ins(db_, "INSERT INTO items(kind, name, data, hash, source, created_at, updated_at) VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?6)");
    ins.bind(1, std::string(kindName(kind))).bind(2, name).bind(3, *canon).bind(4, hash).bind(5, source).bind(6, now());
    if (ins.step() != SQLITE_DONE)
    {
        if (error)
            *error = lastError();
        return std::nullopt;
    }
    return AddResult { sqlite3_last_insert_rowid(db_), false };
}

std::optional<Librarian::AddResult> Librarian::importFile(const std::string& path, std::string* error)
{
    const auto kind = filenames::kindFromPath(path);
    if (kind == FileKind::Unknown || kind == FileKind::ProjectInfo)
    {
        if (error)
            *error = "unsupported file type";
        return std::nullopt;
    }
    auto data = readFileBytes(path);
    if (!data)
    {
        if (error)
            *error = "cannot read file";
        return std::nullopt;
    }
    return add(kind, nameFromData(kind, *data, path), *data, path, error);
}

ImportReport Librarian::importFolder(const std::string& root)
{
    ImportReport rep;
    exec("BEGIN");
    for (const auto& item : scanFolder(root))
    {
        if (item.kind == FileKind::ProjectInfo)
            continue;
        std::string err;
        auto r = importFile(item.path, &err);
        if (!r)
        {
            ++rep.failed;
            rep.messages.push_back(item.path + ": " + err);
            continue;
        }
        (r->duplicate ? rep.duplicates : rep.added)++;
        rep.ids.push_back(r->id);
        if (!r->duplicate && !item.project.empty())
            addTag(r->id, item.project);
    }
    exec("COMMIT");
    return rep;
}

std::vector<std::string> Librarian::tagsFor(int64_t id) const
{
    std::vector<std::string> out;
    Stmt q(db_, "SELECT t.name FROM tags t JOIN item_tags it ON it.tag_id = t.id WHERE it.item_id = ?1 ORDER BY t.name");
    q.bind(1, id);
    while (q.step() == SQLITE_ROW)
        out.push_back(q.text(0));
    return out;
}

std::optional<LibraryItem> Librarian::get(int64_t id) const
{
    Stmt q(db_, "SELECT id, kind, name, hash, source, notes, favorite, rating, created_at, data FROM items WHERE id = ?1");
    q.bind(1, id);
    if (q.step() != SQLITE_ROW)
        return std::nullopt;
    LibraryItem it;
    it.id = q.i64(0);
    it.kind = kindFromName(q.text(1)).value_or(FileKind::Unknown);
    it.name = q.text(2);
    it.hash = q.text(3);
    it.source = q.text(4);
    it.notes = q.text(5);
    it.favorite = q.i64(6) != 0;
    it.rating = (int) q.i64(7);
    it.createdAt = q.i64(8);
    it.data = q.blob(9);
    it.tags = tagsFor(id);
    return it;
}

std::vector<LibraryItem> Librarian::search(const SearchQuery& sq) const
{
    std::string sql = "SELECT i.id, i.kind, i.name, i.hash, i.source, i.notes, i.favorite, i.rating, i.created_at";
    sql += sq.includeData ? ", i.data" : ", NULL";
    sql += " FROM items i WHERE 1";
    if (sq.kind)
        sql += " AND i.kind = ?1";
    if (sq.favoritesOnly)
        sql += " AND i.favorite = 1";
    if (!sq.text.empty())
        sql += " AND (lower(i.name) LIKE ?2 ESCAPE '\\' OR lower(i.notes) LIKE ?2 ESCAPE '\\' OR lower(i.source) LIKE ?2 ESCAPE '\\'"
               " OR EXISTS (SELECT 1 FROM item_tags it JOIN tags t ON t.id = it.tag_id WHERE it.item_id = i.id AND t.name LIKE ?2 ESCAPE '\\'))";
    for (size_t k = 0; k < sq.tags.size(); ++k)
        sql += " AND EXISTS (SELECT 1 FROM item_tags it JOIN tags t ON t.id = it.tag_id WHERE it.item_id = i.id AND t.name = ?"
               + std::to_string(10 + k) + ")";
    sql += " ORDER BY i.favorite DESC, lower(i.name), i.id LIMIT ?3";

    Stmt q(db_, sql.c_str());
    if (sq.kind)
        q.bind(1, std::string(kindName(*sq.kind)));
    if (!sq.text.empty())
    {
        std::string pat;
        for (char c : lower(sq.text))
        {
            if (c == '%' || c == '_' || c == '\\')
                pat.push_back('\\');
            pat.push_back(c);
        }
        q.bind(2, "%" + pat + "%");
    }
    q.bind(3, (int64_t) sq.limit);
    for (size_t k = 0; k < sq.tags.size(); ++k)
        q.bind((int) (10 + k), normaliseTag(sq.tags[k]));

    std::vector<LibraryItem> out;
    while (q.step() == SQLITE_ROW)
    {
        LibraryItem it;
        it.id = q.i64(0);
        it.kind = kindFromName(q.text(1)).value_or(FileKind::Unknown);
        it.name = q.text(2);
        it.hash = q.text(3);
        it.source = q.text(4);
        it.notes = q.text(5);
        it.favorite = q.i64(6) != 0;
        it.rating = (int) q.i64(7);
        it.createdAt = q.i64(8);
        if (sq.includeData)
            it.data = q.blob(9);
        out.push_back(std::move(it));
    }
    for (auto& it : out)
        it.tags = tagsFor(it.id);
    return out;
}

int Librarian::count(std::optional<FileKind> kind) const
{
    Stmt q(db_, kind ? "SELECT COUNT(*) FROM items WHERE kind = ?1" : "SELECT COUNT(*) FROM items");
    if (kind)
        q.bind(1, std::string(kindName(*kind)));
    return q.step() == SQLITE_ROW ? (int) q.i64(0) : 0;
}

bool Librarian::rename(int64_t id, const std::string& name)
{
    Stmt q(db_, "UPDATE items SET name = ?2, updated_at = ?3 WHERE id = ?1");
    q.bind(1, id).bind(2, name).bind(3, now());
    return q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Librarian::setFavorite(int64_t id, bool fav)
{
    Stmt q(db_, "UPDATE items SET favorite = ?2 WHERE id = ?1");
    q.bind(1, id).bind(2, (int64_t) (fav ? 1 : 0));
    return q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Librarian::setRating(int64_t id, int rating)
{
    Stmt q(db_, "UPDATE items SET rating = ?2 WHERE id = ?1");
    q.bind(1, id).bind(2, (int64_t) std::clamp(rating, 0, 5));
    return q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Librarian::setNotes(int64_t id, const std::string& notes)
{
    Stmt q(db_, "UPDATE items SET notes = ?2, updated_at = ?3 WHERE id = ?1");
    q.bind(1, id).bind(2, notes).bind(3, now());
    return q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Librarian::updateData(int64_t id, const Bytes& data)
{
    auto item = get(id);
    if (!item)
        return false;
    auto canon = canonicalise(item->kind, data, nullptr);
    if (!canon)
        return false;
    Stmt q(db_, "UPDATE items SET data = ?2, hash = ?3, updated_at = ?4 WHERE id = ?1");
    q.bind(1, id).bind(2, *canon).bind(3, toHex(fnv1a64(*canon))).bind(4, now());
    return q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
}

bool Librarian::addTag(int64_t id, const std::string& tag)
{
    const auto t = normaliseTag(tag);
    if (t.empty())
        return false;
    {
        Stmt q(db_, "INSERT OR IGNORE INTO tags(name) VALUES (?1)");
        q.bind(1, t);
        q.step();
    }
    Stmt q(db_, "INSERT OR IGNORE INTO item_tags(item_id, tag_id) SELECT ?1, id FROM tags WHERE name = ?2");
    q.bind(1, id).bind(2, t);
    return q.step() == SQLITE_DONE;
}

bool Librarian::removeTag(int64_t id, const std::string& tag)
{
    Stmt q(db_, "DELETE FROM item_tags WHERE item_id = ?1 AND tag_id = (SELECT id FROM tags WHERE name = ?2)");
    q.bind(1, id).bind(2, normaliseTag(tag));
    const bool ok = q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
    exec("DELETE FROM tags WHERE id NOT IN (SELECT tag_id FROM item_tags)");
    return ok;
}

std::vector<std::string> Librarian::allTags() const
{
    std::vector<std::string> out;
    Stmt q(db_, "SELECT name FROM tags ORDER BY name");
    while (q.step() == SQLITE_ROW)
        out.push_back(q.text(0));
    return out;
}

bool Librarian::remove(int64_t id)
{
    Stmt q(db_, "DELETE FROM items WHERE id = ?1");
    q.bind(1, id);
    const bool ok = q.step() == SQLITE_DONE && sqlite3_changes(db_) > 0;
    exec("DELETE FROM tags WHERE id NOT IN (SELECT tag_id FROM item_tags)");
    return ok;
}

std::optional<std::string> Librarian::exportItem(int64_t id, const std::string& folder, int slot, std::string* error) const
{
    auto item = get(id);
    if (!item)
    {
        if (error)
            *error = "no such item";
        return std::nullopt;
    }
    std::string fileName;
    if (item->kind == FileKind::Globals)
        fileName = "glo.cfg";
    else
        fileName = filenames::makeSaveName(slot, item->name, item->kind);

    Bytes data = item->data;
    if (item->kind == FileKind::Kit || item->kind == FileKind::Pattern || item->kind == FileKind::Song)
        if (data.size() >= 8)
            encodeName(item->name, data.data(), 8); // reflect renames in the file header

    std::error_code ec;
    fs::create_directories(folder, ec);
    const auto path = (fs::path(folder) / fileName).string();
    if (!writeFileBytes(path, data))
    {
        if (error)
            *error = "cannot write " + path;
        return std::nullopt;
    }
    return path;
}

} // namespace kitforge
