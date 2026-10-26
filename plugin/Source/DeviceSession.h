#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>

#include "kitforge/Midi.h"
#include "kitforge/ParamSync.h"
#include "kitforge/SysEx.h"

namespace kitforge
{

/**
 * Talks to the device. Owns the outgoing rate limiter, the extended-firmware handshake and the
 * half-duplex request queue, and moves bytes over one of two routes:
 *  - PluginMidi: through the plugin's MIDI output/input (processBlock), so the DAW routes it;
 *  - DirectPort: straight to a hardware MIDI port (reliable SysEx, independent of the DAW).
 *
 * All public methods are thread-safe. Listener callbacks run on the session thread; the processor
 * marshals them to the message thread.
 */
class DeviceSession : private juce::Thread, private juce::MidiInputCallback
{
public:
    enum class Route { PluginMidi = 0, DirectPort = 1 };
    enum class Mode { Disconnected, Probing, Stock, Extended };
    enum class Rate { DinSafe = 0, Fast = 1 };

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void deviceModeChanged(Mode, const ext::Identity&) {}
        virtual void deviceKitReceived(int /*slot*/, const Kit&) {}
        virtual void deviceGlobalsReceived(const Globals&) {}
        virtual void deviceParamsChanged(const std::vector<ext::ParamPair>&) {}
        virtual void deviceStatus(const ext::Status&) {}
        virtual void deviceKitChanged() {}
        virtual void deviceLog(const juce::String&) {}
        virtual void deviceUnsendable(const std::vector<int>& /*params*/) {}
    };

    using Done = std::function<void(bool ok, const juce::String& message)>;

    DeviceSession(ParamSync& sync, Listener& listener);
    ~DeviceSession() override;

    // configuration
    void setRoute(Route r);
    Route route() const { return route_.load(); }
    /** Opens direct ports by identifier; empty strings close them. */
    void setDirectPorts(const juce::String& outputId, const juce::String& inputId);
    juce::String directOutputId() const;
    juce::String directInputId() const;
    void setChannel(int channel1to16);
    int channel() const { return channel_.load(); }
    /** 1..16, or 0 for "all devices" (7F). */
    void setDeviceId(int id);
    int deviceId() const { return deviceId_.load(); }
    void setRate(Rate r) { rate_.store(r); }
    Rate rate() const { return rate_.load(); }
    /** Prefer extended SysEx when the firmware supports it (otherwise always CC/NRPN). */
    void setAllowExtended(bool b);
    bool allowExtended() const { return allowExtended_.load(); }

    // state
    Mode mode() const { return mode_.load(); }
    bool isExtended() const { return mode_.load() == Mode::Extended; }
    ext::Identity identity() const;
    juce::String describe() const;
    bool directOutputOpen() const;

    /** Sends the identity request; falls back to stock mode when nothing answers within 500 ms. */
    void detect();

    // extended-mode operations (fail immediately in stock mode)
    void requestKit(int slot, std::function<void(std::optional<Kit>, const juce::String&)> done);
    void sendKit(int slot, const Kit& kit, int flags, Done done);
    void storeKit(int slot, int flags, Done done);
    void requestGlobals(std::function<void(std::optional<Globals>, const juce::String&)> done);
    void sendGlobals(const Globals& g, int flags, Done done);
    void requestPattern(int slot, std::function<void(std::optional<Bytes>, const juce::String&)> done);
    void sendPattern(int slot, const Bytes& raw, int flags, Done done);
    void setEcho(bool on);

    /** Loads a RAM kit slot: KIT_LOAD in extended mode, CC0 bank select on stock firmware. */
    void loadKitSlot(int slot, Done done);

    /** Queue raw MIDI (e.g. notes from the UI keyboard) on the current route. */
    void sendRaw(const Bytes& msg);

    /** Called by the processor on the audio thread when route == PluginMidi. */
    void processBlock(const juce::MidiBuffer& in, juce::MidiBuffer& out);

private:
    void run() override;
    void handleIncomingMidiMessage(juce::MidiInput*, const juce::MidiMessage&) override;

    void handleFrame(const Bytes& frame, int64_t now);
    bool transmit(const Bytes& msg); // false if the route cannot take it right now
    int64_t nowMs() const;
    int devByte() const;
    void setMode(Mode m);
    void enqueue(const juce::String& label, Bytes frame, ext::RequestQueue::Matcher m,
                 std::function<void(ext::RequestQueue::Outcome, const std::vector<ext::Message>&)> done,
                 int timeoutMs = ext::kReplyTimeoutMs, int group = 0);
    bool requireExtended(const Done& done);
    static juce::String outcomeText(ext::RequestQueue::Outcome o, const std::vector<ext::Message>& replies);
    void log(const juce::String& s) { listener_.deviceLog(s); }

    ParamSync& sync_;
    Listener& listener_;
    ext::RequestQueue queue_;
    StockMidiEncoder encoder_;

    std::atomic<Route> route_ { Route::PluginMidi };
    std::atomic<Mode> mode_ { Mode::Disconnected };
    std::atomic<Rate> rate_ { Rate::DinSafe };
    std::atomic<int> channel_ { 1 };
    std::atomic<int> deviceId_ { 0 };
    std::atomic<bool> allowExtended_ { true };
    std::atomic<bool> encoderReset_ { false };

    mutable std::mutex identityLock_;
    ext::Identity identity_;

    // direct port
    mutable std::mutex portLock_;
    std::unique_ptr<juce::MidiOutput> output_;
    std::unique_ptr<juce::MidiInput> input_;
    juce::String outputId_, inputId_;

    // byte rings: plugin route out (session -> audio), plugin route in (audio -> session), port in (callback -> session)
    struct Ring
    {
        explicit Ring(int size) : fifo(size), data((size_t) size) {}
        bool write(const uint8_t* p, int n);
        int read(uint8_t* p, int max);
        int peekFrameSize() const; // framed rings: next message length or -1
        juce::AbstractFifo fifo;
        std::vector<uint8_t> data;
    };
    Ring pluginOut_ { 1 << 15 }, pluginIn_ { 1 << 16 }, portIn_ { 1 << 16 };
    std::mutex rawLock_;
    std::vector<Bytes> rawOut_;
    sysex::Assembler pluginAsm_, portAsm_;

    double credit_ = 0;
    int64_t lastTick_ = 0;
};

} // namespace kitforge
