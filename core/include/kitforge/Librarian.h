#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "kitforge/SdCard.h"

struct sqlite3;

namespace kitforge
{

struct LibraryItem
{
    int64_t id = 0;
    FileKind kind = FileKind::Kit;
    std::string name;
    std::string hash;
    std::string source; // original path or "device", "editor", ...
    std::string notes;
    bool favorite = false;
    int rating = 0;
    int64_t createdAt = 0;
    std::vector<std::string> tags;
    Bytes data; // filled by get(); search() leaves it empty unless requested
};

struct SearchQuery
{
    std::string text;                 // matches name, notes, source and tags (case-insensitive substring)
    std::optional<FileKind> kind;
    bool favoritesOnly = false;
    std::vector<std::string> tags;    // all must be present
    bool includeData = false;
    int limit = 1000;
};

struct ImportReport
{
    int added = 0;
    int duplicates = 0;
    int failed = 0;
    std::vector<int64_t> ids;
    std::vector<std::string> messages;
};

class Librarian
{
public:
    /** Opens (and migrates) a library database. Use ":memory:" for a temporary library. */
    static std::unique_ptr<Librarian> open(const std::string& path, std::string* error = nullptr);
    ~Librarian();

    Librarian(const Librarian&) = delete;
    Librarian& operator=(const Librarian&) = delete;

    struct AddResult
    {
        int64_t id = 0;
        bool duplicate = false;
    };

    /** Adds an item; identical content of the same kind is de-duplicated by hash. */
    std::optional<AddResult> add(FileKind kind, const std::string& name, const Bytes& data, const std::string& source,
                                 std::string* error = nullptr);
    std::optional<AddResult> addKit(const Kit& kit, const std::string& source) { return add(FileKind::Kit, kit.name(), kit.toSnd(), source); }

    /** Imports every kit/pattern/song/glo.cfg below a folder (e.g. a mounted SD card). Tags items with their project folder. */
    ImportReport importFolder(const std::string& root);
    std::optional<AddResult> importFile(const std::string& path, std::string* error = nullptr);

    std::optional<LibraryItem> get(int64_t id) const;
    std::vector<LibraryItem> search(const SearchQuery& q) const;
    int count(std::optional<FileKind> kind = std::nullopt) const;

    bool rename(int64_t id, const std::string& name);
    bool setFavorite(int64_t id, bool fav);
    bool setRating(int64_t id, int rating);
    bool setNotes(int64_t id, const std::string& notes);
    bool updateData(int64_t id, const Bytes& data);
    bool addTag(int64_t id, const std::string& tag);
    bool removeTag(int64_t id, const std::string& tag);
    std::vector<std::string> allTags() const;
    bool remove(int64_t id);

    /** Writes an item to `folder` using the firmware naming for `slot`. Returns the written path. */
    std::optional<std::string> exportItem(int64_t id, const std::string& folder, int slot, std::string* error = nullptr) const;

    std::string lastError() const;

private:
    explicit Librarian(sqlite3* db);
    bool exec(const char* sql);
    std::vector<std::string> tagsFor(int64_t id) const;

    sqlite3* db_ = nullptr;
};

const char* kindName(FileKind k);
std::optional<FileKind> kindFromName(const std::string& s);

} // namespace kitforge
