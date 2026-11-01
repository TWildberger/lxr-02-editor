#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <array>
#include <map>
#include <memory>
#include <stdexcept>

namespace vsynthxt
{
using Bytes = std::vector<uint8_t>;
using Address = uint32_t; // Four base-128 digits, represented as a linear offset.
constexpr Address temporary = 0x10u << 21;

struct Block
{
    Address offset;
    size_t size;
    const char* name;
    int page;
};
const std::vector<Block>& blocks();
Bytes digits(Address);
Address address(const uint8_t*);
uint8_t checksum(const Bytes&);
Bytes request(uint8_t device, Address, size_t);
Bytes dataSet(uint8_t device, Address, const Bytes&);
struct Message { uint8_t device; Address start; Bytes data; };
Message decode(const Bytes&);

class Patch
{
public:
    std::map<Address, Bytes> data;
    bool complete() const;
    std::string name() const;
    void setName(const std::string&);
    int value(Address) const;
    void setValue(Address, int);
    Bytes sysex(uint8_t device = 0x10) const;
    static Patch fromSysex(const Bytes&);
    bool operator==(const Patch& other) const { return data == other.data; }
};

class Receiver
{
public:
    explicit Receiver(const Block&);
    bool accept(const Message&, uint8_t device);
    bool complete() const;
    const Bytes& result() const { return bytes_; }
private:
    Block block_;
    Bytes bytes_;
    std::vector<bool> received_;
};

struct Entry
{
    int64_t id;
    std::string name, tags, notes;
    bool favorite;
};

class Library
{
public:
    explicit Library(const std::string& path);
    ~Library();
    Library(const Library&) = delete;
    Library& operator=(const Library&) = delete;
    int64_t add(const Patch&, const std::string& tags, const std::string& notes, bool favorite);
    std::vector<Entry> search(const std::string&);
    Patch get(int64_t);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
