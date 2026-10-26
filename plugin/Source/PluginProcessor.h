#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <functional>
#include <optional>
#include <unordered_map>
#include <atomic>
#include <memory>
#include <mutex>
#include <set>

#include "DeviceSession.h"
#include "kitforge/Kit.h"
#include "kitforge/Librarian.h"
#include "kitforge/Midi.h"
#include "kitforge/ParamSync.h"

namespace kitforge
{

/**
 * Holds the kit being edited (raw bytes are the source of truth, mirrored into one AudioParameterInt per
 * sound parameter for DAW automation), the global settings, undo/redo, A/B, locks, and the device session.
 */
class KitForgeProcessor : public juce::AudioProcessor,
                          private juce::AudioProcessorValueTreeState::Listener,
                          private juce::AudioProcessorListener,
                          private DeviceSession::Listener,
                          private juce::Timer
{
public:
    KitForgeProcessor();
    ~KitForgeProcessor() override;

    // AudioProcessor
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*, int) override;

    // kit model (message thread unless noted)
    juce::AudioProcessorValueTreeState& state() { return apvts_; }
    juce::RangedAudioParameter* parameterFor(int param) const;
    Kit currentKit() const; // any thread
    int rawValue(int param) const { return raw_[(size_t) param].load(); }
    void setValue(int param, int value);     // one undoable edit
    void setValues(const Kit& kit, const juce::String& undoLabel); // bulk edit (randomize, copy voice, ...)
    enum class LoadOrigin { User, Device, StateRecall, History };
    void loadKit(const Kit& kit, LoadOrigin origin);
    void setKitName(const juce::String& name);
    juce::String kitName() const;

    // undo / A-B / locks
    void pushUndo();
    bool canUndo() const;
    bool canRedo() const;
    void undo();
    void redo();
    bool isB() const { return isB_; }
    void toggleAB();
    void copyCurrentToOther();
    Kit otherKit() const { return other_; }
    bool isLocked(int param) const { return locks_.count(param) > 0; }
    void setLocked(int param, bool locked);
    const std::set<int>& locks() const { return locks_; }

    // globals (not automatable)
    const Globals& globals() const { return globals_; }
    void setGlobal(int param, int value);
    void loadGlobals(const Globals& g, bool fromDevice);

    // device
    DeviceSession& device() { return *device_; }
    void pushKitToDevice(); // full resend (kit dump in extended mode)
    void pullKitFromDevice();
    void fetchGlobals();
    void pushGlobals(bool save);
    /** Runs fn on the message thread (from the processor timer); safe to call from any thread. */
    void post(std::function<void()> fn);
    bool passThrough() const { return passThrough_.load(); }
    void setPassThrough(bool b) { passThrough_.store(b); }
    bool sendOnLoad() const { return sendOnLoad_; }
    void setSendOnLoad(bool b) { sendOnLoad_ = b; }
    bool pullOnConnect() const { return pullOnConnect_; }
    void setPullOnConnect(bool b) { pullOnConnect_ = b; }
    void saveDeviceSettings(); // remembered as defaults for new instances

    // library (opened lazily; nullptr if the database cannot be opened)
    Librarian* library();
    juce::String libraryPath() const;

    // UI notifications (message thread)
    juce::ChangeBroadcaster kitChanged;     // kit values / name replaced or edited in bulk
    juce::ChangeBroadcaster historyChanged; // undo/redo/A-B state
    juce::ChangeBroadcaster deviceChanged;  // mode, log, status
    juce::StringArray logLines() const;
    juce::String lastProblem() const;
    void addLog(const juce::String& s);

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void parameterChanged(const juce::String& id, float value) override;
    void audioProcessorParameterChanged(juce::AudioProcessor*, int, float) override {}
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails&) override {}
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) override;
    void timerCallback() override;

    // DeviceSession::Listener (session thread)
    void deviceModeChanged(DeviceSession::Mode, const ext::Identity&) override;
    void deviceKitReceived(int slot, const Kit&) override;
    void deviceGlobalsReceived(const Globals&) override;
    void deviceParamsChanged(const std::vector<ext::ParamPair>&) override;
    void deviceStatus(const ext::Status&) override;
    void deviceKitChanged() override;
    void deviceLog(const juce::String&) override;
    void deviceUnsendable(const std::vector<int>&) override;

    void applyKitToParameters(const Kit& kit);
    void applyDeviceSettings(const juce::ValueTree& v);
    juce::ValueTree deviceSettings() const;

    juce::AudioProcessorValueTreeState apvts_;
    std::array<juce::RangedAudioParameter*, kNumSoundParams> params_ {};
    std::array<std::atomic<int>, kNumSoundParams> raw_;
    mutable std::mutex nameLock_;
    std::array<uint8_t, Kit::kNameBytes> name_ {};
    Bytes extension_; // guarded by nameLock_, like the raw kit name
    std::atomic<bool> loading_ { false };

    Globals globals_;
    ParamSync sync_;
    std::unique_ptr<DeviceSession> device_;
    std::mutex syncLock_;

    mutable std::mutex historyLock_;
    std::vector<Kit> undo_, redo_;
    Kit other_;
    bool isB_ = false;
    std::set<int> locks_;

    std::atomic<bool> passThrough_ { true };
    bool sendOnLoad_ = true;
    bool pullOnConnect_ = false;

    std::mutex inboxLock_;
    std::vector<std::function<void()>> inbox_;

    mutable std::mutex logLock_;
    juce::StringArray log_;
    juce::String lastProblem_;
    std::set<int> reportedUnsendable_;

    std::unique_ptr<Librarian> library_;
    bool libraryTried_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitForgeProcessor)
};

} // namespace kitforge
