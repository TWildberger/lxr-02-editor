#pragma once
#include "Session.h"
#include "Parameters.h"
#include <unordered_map>

namespace vsynthxt
{
class Processor : public juce::AudioProcessor,
                  private juce::AudioProcessorValueTreeState::Listener,
                  private juce::Timer
{
public:
    Processor();
    ~Processor() override;
    juce::AudioProcessorValueTreeState state;
    Session session;
    std::atomic<int> selectedTab {0};
    std::atomic<int> selectedZone {1}, selectedNativeGroup {1}, selectedNativeSection {1};
    juce::ChangeBroadcaster changed;
    Patch patch() const;
    void load(const Patch&, bool history = true);
    void rename(const juce::String&);
    void undo();
    void redo();
    void compare();
    void copyAB();
    void fetch(bool native);
    void send(bool native);
    Library& library();
    juce::String problem() const { std::lock_guard<std::recursive_mutex> g(patchMutex_); return problem_; }
    void report(const juce::String& s) { std::lock_guard<std::recursive_mutex> g(patchMutex_); problem_ = s; changed.sendChangeMessage(); }
    bool isB() const { std::lock_guard<std::recursive_mutex> g(patchMutex_); return isB_; }

    const juce::String getName() const override { return "V-Synth XT Companion"; }
    void prepareToPlay(double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& l) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
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
private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    void parameterChanged(const juce::String&, float) override;
    void timerCallback() override;
    void mirror();
    void flushEdits();
    mutable std::recursive_mutex patchMutex_;
    Patch patch_, other_;
    std::optional<Patch> transferBaseline_;
    std::vector<Patch> undo_, redo_;
    bool isB_ = false;
    std::atomic<bool> mirroring_ {false};
    std::unique_ptr<std::atomic<bool>[]> dirty_;
    std::unordered_map<std::string, size_t> parameterIndices_;
    juce::String problem_;
    std::unique_ptr<Library> library_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Processor)
};
}
