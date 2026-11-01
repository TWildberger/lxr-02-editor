#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace companion
{
/** Splits MIDI F0..F7 messages, retaining fragments and ignoring interleaved realtime bytes. */
class SysExAssembler
{
public:
    explicit SysExAssembler(std::size_t maxLength = 4096) : maxLength_(maxLength) {}
    std::vector<std::vector<uint8_t>> feed(const uint8_t* data, std::size_t size)
    {
        std::vector<std::vector<uint8_t>> out;
        for (std::size_t i = 0; i < size; ++i)
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
                reset();
            }
            else if (b >= 0xF8)
                continue;
            else if ((b & 0x80) || buf_.size() >= maxLength_)
                reset();
            else
                buf_.push_back(b);
        }
        return out;
    }
    void reset() { buf_.clear(); inSysex_ = false; }
private:
    std::size_t maxLength_;
    std::vector<uint8_t> buf_;
    bool inSysex_ = false;
};
}
