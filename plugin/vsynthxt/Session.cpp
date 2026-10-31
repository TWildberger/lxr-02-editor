#include "Session.h"
#include <set>

namespace vsynthxt
{
namespace
{
std::mutex leasesMutex;
std::set<juce::String> leases;
}
bool Session::Queue::put(const uint8_t* bytes, int size, unsigned generation)
{
    if (size <= 0 || size > 268) return false;
    int a, n, b, k;
    fifo.prepareToWrite(1, a, n, b, k);
    if (n + k == 0) return false;
    auto& p = packets[static_cast<size_t>(n ? a : b)];
    p.size = size; p.generation = generation;
    std::copy_n(bytes, size, p.bytes.begin());
    fifo.finishedWrite(1);
    return true;
}
bool Session::Queue::get(Packet& p)
{
    int a, n, b, k;
    fifo.prepareToRead(1, a, n, b, k);
    if (n + k == 0) return false;
    p = packets[static_cast<size_t>(n ? a : b)];
    fifo.finishedRead(1);
    return true;
}
Session::Session() : Thread("V-Synth XT MIDI") { startThread(); }
Session::~Session()
{
    stopThread(-1);
    releasePorts();
}
void Session::releasePorts()
{
    // The callback only touches its SPSC queue, never mutex_.
    if (input_) input_->stop();
    input_.reset(); output_.reset();
    if (portLease_) { portLease_->exit(); portLease_.reset(); }
    if (leaseKey_.isNotEmpty())
    {
        std::lock_guard<std::mutex> guard(leasesMutex);
        leases.erase(leaseKey_);
        leaseKey_.clear();
    }
}
void Session::configure(bool host, const juce::String& in, const juce::String& out, int device)
{
    std::lock_guard<std::mutex> guard(mutex_);
    ++generation_;
    active_ = false; result_.reset(); receiver_.reset(); configured_ = false;
    waitingHost_ = false;
    releasePorts();
    try
    {
        if (device < 17 || device > 32) throw std::runtime_error("Device ID must be 17-32");
        device_ = static_cast<uint8_t>(device - 1);
        hostRoute_.store(host);
        if (!host)
        {
            if (in.isEmpty() || out.isEmpty()) throw std::runtime_error("Select both MIDI input and output");
            const auto key = "VSynthXT-" + juce::String::toHexString(out.hashCode64());
            {
                std::lock_guard<std::mutex> lock(leasesMutex);
                if (!leases.insert(key).second) throw std::runtime_error("MIDI output is owned by another XT instance");
                leaseKey_ = key;
            }
            portLease_ = std::make_unique<juce::InterProcessLock>(key);
            if (!portLease_->enter(0)) throw std::runtime_error("MIDI output is owned by another XT process");
            output_ = juce::MidiOutput::openDevice(out);
            input_ = juce::MidiInput::openDevice(in, this);
            if (!input_ || !output_) throw std::runtime_error("Could not open both MIDI ports; check connection and other applications");
            input_->start();
        }
        configured_ = true;
        status_ = "Route ready (not device verification). Explicit Fetch/Send only.";
    }
    catch (const std::exception& e)
    {
        releasePorts();
        status_ = "Connection error: " + juce::String(e.what());
    }
}
void Session::start(bool send, const Patch& patch, bool native)
{
    std::lock_guard<std::mutex> guard(mutex_);
    if (active_) { status_ = "Transfer already running; cancel before starting another."; return; }
    if (!configured_ || !native) { status_ = "Configure MIDI and confirm the XT is in native mode first."; return; }
    if (send && !patch.complete()) { status_ = "Fetch or import a complete patch before Send."; return; }
    ++generation_;
    result_.reset(); target_ = patch; active_ = true; index_ = 0;
    waitingHost_ = false;
    verifying_ = send; writing_ = send; nextAt_ = 0;
    if (!send) beginFetch();
}
void Session::cancel()
{
    std::lock_guard<std::mutex> guard(mutex_);
    result_.reset();
    finish(active_ ? "Cancelled. Already transmitted edits cannot be undone; Fetch to check hardware."
                   : "Pending transfers cleared. Explicit Fetch/Send only.");
}
void Session::finish(const juce::String& message)
{
    ++generation_;
    active_ = false; receiver_.reset();
    waitingHost_ = false;
    status_ = message;
}
juce::String Session::status() const { std::lock_guard<std::mutex> g(mutex_); return status_; }
bool Session::busy() const { std::lock_guard<std::mutex> g(mutex_); return active_; }
Bytes Session::takeTransmitted()
{
    std::lock_guard<std::mutex> g(mutex_);
    capture_ = false;
    auto bytes = std::move(transmitted_); transmitted_.clear(); return bytes;
}
void Session::beginCapture()
{
    std::lock_guard<std::mutex> g(mutex_);
    transmitted_.clear(); capture_ = true;
}
std::optional<Patch> Session::takeResult()
{
    std::lock_guard<std::mutex> g(mutex_);
    auto result = std::move(result_); result_.reset(); return result;
}
void Session::handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage& m)
{
    if (m.isSysEx() && !directIn_.put(m.getRawData(), m.getRawDataSize(), generation_.load())) overflow_.store(true);
}
void Session::process(juce::MidiBuffer& midi)
{
    if (!hostRoute_.load()) { midi.clear(); return; }
    for (const auto meta : midi)
    {
        if (meta.numBytes > 0 && meta.data[0] == 0xf0
            && !hostIn_.put(meta.data, meta.numBytes, generation_.load())) overflow_.store(true);
    }
    midi.clear(); // The companion does not forward notes or unrelated SysEx.
    Packet p;
    while (hostOut_.get(p))
        if (p.generation == generation_.load())
        {
            midi.addEvent(p.bytes.data(), p.size, 0);
            hostSentAt_.store(juce::Time::getMillisecondCounterHiRes());
        }
}
void Session::transmit(const Bytes& b)
{
    const juce::MidiMessage m(b.data(), static_cast<int>(b.size()));
    if (hostRoute_.load())
    {
        hostSentAt_.store(-1);
        waitingHost_ = true;
        hostDeadline_ = juce::Time::getMillisecondCounterHiRes() + 3000;
        hostDelay_ = b.size() * 0.32 + 25;
        if (!hostOut_.put(b.data(), static_cast<int>(b.size()), generation_.load()))
            throw std::runtime_error("Host MIDI queue full; host may not be processing");
    }
    else
    {
        if (!output_ || !input_) throw std::runtime_error("Direct MIDI route is closed");
        const auto ins = juce::MidiInput::getAvailableDevices();
        const auto outs = juce::MidiOutput::getAvailableDevices();
        const auto has = [](const auto& devices, const auto& id) {
            return std::any_of(devices.begin(), devices.end(), [&](const auto& d) { return d.identifier == id; });
        };
        if (!has(ins, input_->getIdentifier()) || !has(outs, output_->getIdentifier()))
            throw std::runtime_error("MIDI device disconnected");
        if (capture_ && transmitted_.size() + b.size() > 65536)
            throw std::runtime_error("Diagnostic capture limit exceeded; transfer stopped before next message");
        output_->sendMessageNow(m);
        // Bounded diagnostic capture of submitted direct-route messages, not a hardware ACK.
        if (capture_)
            transmitted_.insert(transmitted_.end(), b.begin(), b.end());
    }
}
void Session::nextRequest()
{
    const auto& b = blocks().at(index_);
    receiver_ = std::make_unique<Receiver>(b);
    transmit(request(device_, temporary + b.offset, b.size));
    deadline_ = juce::Time::getMillisecondCounterHiRes() + 3000;
    status_ = (verifying_ ? "Verifying " : "Fetching ") + juce::String(static_cast<int>(index_ + 1))
        + "/" + juce::String(static_cast<int>(blocks().size())) + " " + b.name;
}
void Session::beginFetch()
{
    writing_ = false; index_ = 0; fetched_.data.clear(); receiver_.reset();
    nextAt_ = juce::Time::getMillisecondCounterHiRes() + 25;
}
void Session::run()
{
    while (!threadShouldExit())
    {
        {
            std::lock_guard<std::mutex> guard(mutex_);
            try
            {
                if (overflow_.exchange(false)) throw std::runtime_error("Incoming MIDI exceeded the bounded queue or packet limit");
                const double now = juce::Time::getMillisecondCounterHiRes();
                if (waitingHost_)
                {
                    const double sent = hostSentAt_.load();
                    if (sent >= 0)
                    {
                        waitingHost_ = false;
                        nextAt_ = sent + hostDelay_;
                        deadline_ = sent + 3000;
                    }
                    else if (now > hostDeadline_)
                        throw std::runtime_error("Host stopped processing MIDI; queued writes cancelled. Fetch before retrying");
                }
                Packet packet;
                auto& input = hostRoute_.load() ? hostIn_ : directIn_;
                while (input.get(packet))
                {
                    if (!active_ || writing_ || !receiver_ || packet.generation != generation_.load()) continue;
                    const Bytes f(packet.bytes.begin(), packet.bytes.begin() + packet.size);
                    // Ignore unrelated manufacturers/models; matching malformed XT replies are errors.
                    if (f.size() < 6 || f[1] != 0x41 || f[2] != device_ || f[3] != 0 || f[4] != 0x53 || f[5] != 0x12) continue;
                    const auto m = decode(f);
                    if (!receiver_->accept(m, device_)) continue;
                    if (receiver_->complete())
                    {
                        fetched_.data.emplace(blocks()[index_].offset, receiver_->result());
                        receiver_.reset();
                        ++index_;
                        nextAt_ = now + 25;
                        if (index_ == blocks().size())
                        {
                            if (!fetched_.complete()) throw std::runtime_error("Incomplete patch");
                            if (verifying_ && !(fetched_ == target_)) throw std::runtime_error("Readback mismatch; hardware patch differs from sent snapshot");
                            result_ = fetched_;
                            finish(verifying_ ? "Temporary patch verified by readback. No user slots written."
                                              : "Complete patch fetched. No writes performed.");
                        }
                    }
                }
                if (active_ && !waitingHost_ && now >= nextAt_)
                {
                    if (writing_)
                    {
                        const auto& b = blocks().at(index_);
                        transmit(dataSet(device_, temporary + b.offset, target_.data.at(b.offset)));
                        status_ = "Sending temporary block " + juce::String(static_cast<int>(index_ + 1));
                        // DIN wire time (10 bits/byte at 31250 baud), plus the documented inter-packet gap.
                        nextAt_ = now + (b.size + 12) * 0.32 + 25;
                        if (++index_ == blocks().size())
                        {
                            writing_ = false; index_ = 0; fetched_.data.clear();
                            receiver_.reset();
                        }
                    }
                    else if (!receiver_) nextRequest();
                    else if (now > deadline_) throw std::runtime_error("Timeout/incomplete reply. Check native mode, Receive Exclusive, device ID and return MIDI route");
                }
            }
            catch (const std::exception& e) { finish("Transfer error: " + juce::String(e.what())); }
        }
        wait(5);
    }
}
}
