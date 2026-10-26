#include "kitforge/Bytes.h"

#include <cstdio>
#include <fstream>
#include <iterator>

namespace kitforge
{

std::optional<Bytes> readFileBytes(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return std::nullopt;
    return Bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

bool writeFileBytes(const std::string& path, const Bytes& data)
{
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char*>(data.data()), (std::streamsize) data.size());
    return (bool) out;
}

uint64_t fnv1a64(const uint8_t* data, size_t size)
{
    uint64_t h = 0xcbf29ce484222325ull;
    for (size_t i = 0; i < size; ++i)
    {
        h ^= data[i];
        h *= 0x100000001b3ull;
    }
    return h;
}

std::string toHex(uint64_t v)
{
    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016llx", (unsigned long long) v);
    return buf;
}

std::string decodeName(const uint8_t* raw, size_t size)
{
    std::string s;
    for (size_t i = 0; i < size && raw[i] != 0; ++i)
        s.push_back((raw[i] >= 0x20 && raw[i] < 0x7F) ? (char) raw[i] : '?');
    while (!s.empty() && s.back() == ' ')
        s.pop_back();
    return s;
}

void encodeName(const std::string& name, uint8_t* raw, size_t size)
{
    for (size_t i = 0; i < size; ++i)
    {
        if (i < name.size())
        {
            const auto c = (unsigned char) name[i];
            raw[i] = (c >= 0x20 && c < 0x7F) ? c : (uint8_t) '_';
        }
        else
            raw[i] = 0;
    }
}

} // namespace kitforge
