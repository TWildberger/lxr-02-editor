#pragma once

#include <optional>
#include <string>
#include <vector>

#include "kitforge/Kit.h"
#include "kitforge/Pattern.h"

namespace kitforge
{

enum class FileKind
{
    Kit,
    Pattern,
    Song,
    Globals,
    ProjectInfo,
    Unknown,
};

namespace filenames
{
/** Mirrors clearIllegalCharacters() in the firmware. */
std::string clearIllegalCharacters(const std::string& name, size_t size = 8);
/** Firmware save naming: "NN-XXXXX.ext" (first 5 chars of the cleaned name). */
std::string makeSaveName(int slot, const std::string& name, FileKind kind);
/** Legacy naming: "pNNN.ext". */
std::string makeOldName(int slot, FileKind kind);
const char* extension(FileKind kind); // "snd", "pat", "sng", ...
FileKind kindFromPath(const std::string& path);
/** Slot number parsed from "NN-*.ext" or "pNNN.ext", or -1. */
int slotFromFileName(const std::string& fileName);
} // namespace filenames

struct SdItem
{
    FileKind kind = FileKind::Unknown;
    std::string path;
    std::string project; // folder name, e.g. "proj03" ("" when loose)
    int slot = -1;
};

/** Recursively finds kits, patterns, songs and global settings below a folder (an SD card root or any folder). */
std::vector<SdItem> scanFolder(const std::string& root);

/** A device project folder ("/projNN") with up to 64 kit/pattern/song slots. */
struct Project
{
    static constexpr int kSlots = 64;

    std::string name;
    std::optional<Globals> globals;
    std::array<std::optional<Kit>, kSlots> kits;
    std::array<std::optional<Pattern>, kSlots> patterns;
    std::array<std::optional<Song>, kSlots> songs;

    static Project load(const std::string& folder, std::vector<std::string>* warnings = nullptr);
    /** Writes using the firmware's file naming. Existing files for the same slot are replaced. */
    bool save(const std::string& folder, std::string* error = nullptr) const;
};

} // namespace kitforge
