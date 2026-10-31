#pragma once
#include "VSynthXT.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <atomic>
#include <mutex>

namespace vsynthxt
{
class Session : private juce::Thread, private juce::MidiInputCallback
{
public:
    Session();
    ~Session() override;
    void configure(bool hostRoute, const juce::String& input, const juce::String& output, int device);
    void start(bool send, const Patch&, bool nativeConfirmed);
    void cancel();
    void process(juce::MidiBuffer&);
    juce::String status() const;
    std::optional<Patch> takeResult();
    bool busy() const;
    Bytes takeTransmitted();
    void beginCapture();

private:
    struct Packet { std::array<uint8_t, 268> bytes {}; int size = 0; unsigned generation = 0; };
    struct Queue
    {
        juce::AbstractFifo fifo {256};
        std::array<Packet, 256> packets;
        bool put(const uint8_t*, int size, unsigned);
        bool get(Packet&);
    };
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;
    void run() override;
    void transmit(const Bytes&);
    void finish(const juce::String&);
    void beginFetch();
    void nextRequest();
    void releasePorts();

    mutable std::mutex mutex_;
    std::unique_ptr<juce::MidiInput> input_;
    std::unique_ptr<juce::MidiOutput> output_;
    std::unique_ptr<juce::InterProcessLock> portLease_;
    juce::String leaseKey_;
    Queue hostIn_, directIn_, hostOut_;
    std::atomic<bool> hostRoute_ {true}, overflow_ {false};
    std::atomic<unsigned> generation_ {1};
    std::atomic<double> hostSentAt_ {-1};
    bool waitingHost_ = false;
    double hostDeadline_ = 0, hostDelay_ = 0;
    bool configured_ = false, active_ = false, writing_ = false, verifying_ = false;
    uint8_t device_ = 0x10;
    size_t index_ = 0;
    Patch target_, fetched_;
    std::optional<Patch> result_;
    std::unique_ptr<Receiver> receiver_;
    double nextAt_ = 0, deadline_ = 0;
    juce::String status_ = "Offline. Select a route, confirm native mode, then Fetch.";
    Bytes transmitted_;
    bool capture_ = false;
};
}
