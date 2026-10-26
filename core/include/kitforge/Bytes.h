#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace kitforge
{

using Bytes = std::vector<uint8_t>;

std::optional<Bytes> readFileBytes(const std::string& path);
bool writeFileBytes(const std::string& path, const Bytes& data);

/** 64-bit FNV-1a, used for content de-duplication in the librarian. */
uint64_t fnv1a64(const uint8_t* data, size_t size);
inline uint64_t fnv1a64(const Bytes& b) { return fnv1a64(b.data(), b.size()); }
std::string toHex(uint64_t v);

/** Decode a fixed 8-byte device name: stops at NUL, trims trailing spaces. */
std::string decodeName(const uint8_t* raw, size_t size = 8);
/** Encode to a fixed 8-byte NUL-padded device name (truncates, replaces non-printables). */
void encodeName(const std::string& name, uint8_t* raw, size_t size = 8);

} // namespace kitforge
