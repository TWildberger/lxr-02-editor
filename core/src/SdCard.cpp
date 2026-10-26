#include "kitforge/SdCard.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>

namespace fs = std::filesystem;

namespace kitforge
{

namespace filenames
{

std::string clearIllegalCharacters(const std::string& name, size_t size)
{
    std::string out;
    for (size_t i = 0; i < size; ++i)
    {
        const char c = i < name.size() ? name[i] : 0;
        if (c == 0)
            break;
        if (std::isalnum((unsigned char) c) && (unsigned char) c < 0x80)
            out.push_back(c);
        else if (c == ' ')
            out.push_back('_');
        else if (std::string("!#$%&()-@^_`{}~").find(c) != std::string::npos)
            out.push_back(c);
        else
            out.push_back('_');
    }
    return out;
}

const char* extension(FileKind kind)
{
    switch (kind)
    {
        case FileKind::Kit: return "snd";
        case FileKind::Pattern: return "pat";
        case FileKind::Song: return "sng";
        case FileKind::Globals: return "cfg";
        case FileKind::ProjectInfo: return "nfo";
        default: return "";
    }
}

std::string makeSaveName(int slot, const std::string& name, FileKind kind)
{
    char num[4];
    std::snprintf(num, sizeof(num), "%02d", std::clamp(slot, 0, 99));
    return std::string(num) + "-" + clearIllegalCharacters(name).substr(0, 5) + "." + extension(kind);
}

std::string makeOldName(int slot, FileKind kind)
{
    char buf[16];
    std::snprintf(buf, sizeof(buf), "p%03d.%s", std::clamp(slot, 0, 999), extension(kind));
    return buf;
}

FileKind kindFromPath(const std::string& path)
{
    const fs::path p(path);
    std::string fn = p.filename().string();
    std::transform(fn.begin(), fn.end(), fn.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    if (fn == "glo.cfg")
        return FileKind::Globals;
    if (fn == "prj.nfo")
        return FileKind::ProjectInfo;
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char) std::tolower(c); });
    if (ext == ".snd")
        return FileKind::Kit;
    if (ext == ".pat")
        return FileKind::Pattern;
    if (ext == ".sng")
        return FileKind::Song;
    return FileKind::Unknown;
}

int slotFromFileName(const std::string& fileName)
{
    const std::string fn = fs::path(fileName).filename().string();
    if (fn.size() >= 3 && std::isdigit((unsigned char) fn[0]) && std::isdigit((unsigned char) fn[1]) && fn[2] == '-')
        return (fn[0] - '0') * 10 + (fn[1] - '0');
    if (fn.size() >= 4 && (fn[0] == 'p' || fn[0] == 'P') && std::isdigit((unsigned char) fn[1])
        && std::isdigit((unsigned char) fn[2]) && std::isdigit((unsigned char) fn[3]))
        return (fn[1] - '0') * 100 + (fn[2] - '0') * 10 + (fn[3] - '0');
    return -1;
}

} // namespace filenames

std::vector<SdItem> scanFolder(const std::string& root)
{
    std::vector<SdItem> items;
    std::error_code ec;
    if (!fs::is_directory(root, ec))
        return items;
    for (auto it = fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec);
         it != fs::recursive_directory_iterator(); it.increment(ec))
    {
        if (ec)
            break;
        if (!it->is_regular_file(ec))
            continue;
        const auto& path = it->path();
        if (path.filename().string().rfind("._", 0) == 0) // macOS resource forks on FAT cards
            continue;
        const auto kind = filenames::kindFromPath(path.string());
        if (kind == FileKind::Unknown)
            continue;
        SdItem item;
        item.kind = kind;
        item.path = path.string();
        item.slot = filenames::slotFromFileName(path.string());
        const auto rel = fs::relative(path.parent_path(), root, ec);
        item.project = (ec || rel == ".") ? std::string() : rel.generic_string();
        items.push_back(std::move(item));
    }
    std::sort(items.begin(), items.end(), [](const SdItem& a, const SdItem& b) { return a.path < b.path; });
    return items;
}

Project Project::load(const std::string& folder, std::vector<std::string>* warnings)
{
    Project prj;
    prj.name = fs::path(folder).filename().string();
    auto warn = [&](const std::string& w) {
        if (warnings)
            warnings->push_back(w);
    };
    std::error_code ec;
    std::vector<fs::path> files;
    for (const auto& e : fs::directory_iterator(folder, ec))
        if (e.is_regular_file(ec))
            files.push_back(e.path());
    // the firmware loads the first match of "NN-*.ext"; sort for deterministic results
    std::sort(files.begin(), files.end());

    for (const auto& path : files)
    {
        const auto kind = filenames::kindFromPath(path.string());
        const int slot = filenames::slotFromFileName(path.string());
        if (path.filename().string().rfind("._", 0) == 0)
            continue;
        auto data = readFileBytes(path.string());
        if (!data)
            continue;
        std::string msg;
        switch (kind)
        {
            case FileKind::Globals:
                prj.globals = Globals::fromCfg(*data, &msg);
                break;
            case FileKind::ProjectInfo:
                if (data->size() >= 8)
                    prj.name = decodeName(data->data(), 8);
                break;
            case FileKind::Kit:
                if (slot >= 0 && slot < kSlots && !prj.kits[(size_t) slot])
                    prj.kits[(size_t) slot] = Kit::fromSnd(*data, &msg);
                break;
            case FileKind::Pattern:
                if (slot >= 0 && slot < kSlots && !prj.patterns[(size_t) slot])
                {
                    auto p = Pattern::fromPat(*data, &msg);
                    if (p)
                        prj.patterns[(size_t) slot] = *p;
                }
                break;
            case FileKind::Song:
                if (slot >= 0 && slot < kSlots && !prj.songs[(size_t) slot])
                {
                    auto s = Song::fromSng(*data, &msg);
                    if (s)
                        prj.songs[(size_t) slot] = *s;
                }
                break;
            default:
                break;
        }
        if (!msg.empty())
            warn(path.filename().string() + ": " + msg);
    }
    return prj;
}

bool Project::save(const std::string& folder, std::string* error) const
{
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec)
    {
        if (error)
            *error = ec.message();
        return false;
    }

    auto removeSlot = [&](int slot, FileKind kind) {
        for (const auto& e : fs::directory_iterator(folder, ec))
            if (e.is_regular_file() && filenames::kindFromPath(e.path().string()) == kind
                && filenames::slotFromFileName(e.path().string()) == slot)
                fs::remove(e.path(), ec);
    };

    bool ok = true;
    auto put = [&](const std::string& fileName, const Bytes& b) { ok = writeFileBytes((fs::path(folder) / fileName).string(), b) && ok; };

    if (globals)
        put("glo.cfg", globals->toCfg());
    {
        Bytes nfo(8, 0);
        encodeName(name, nfo.data(), 8);
        put("prj.nfo", nfo);
    }
    for (int i = 0; i < kSlots; ++i)
    {
        if (kits[(size_t) i])
        {
            removeSlot(i, FileKind::Kit);
            put(filenames::makeSaveName(i, kits[(size_t) i]->name(), FileKind::Kit), kits[(size_t) i]->toSnd());
        }
        if (patterns[(size_t) i])
        {
            removeSlot(i, FileKind::Pattern);
            put(filenames::makeSaveName(i, patterns[(size_t) i]->name(), FileKind::Pattern), patterns[(size_t) i]->toPat());
        }
        if (songs[(size_t) i])
        {
            removeSlot(i, FileKind::Song);
            put(filenames::makeSaveName(i, songs[(size_t) i]->name(), FileKind::Song), songs[(size_t) i]->toSng());
        }
    }
    if (!ok && error)
        *error = "failed to write one or more files";
    return ok;
}

} // namespace kitforge
