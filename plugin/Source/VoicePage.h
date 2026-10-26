#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <random>

#include "ParamControl.h"

namespace kitforge
{

class KitForgeProcessor;

/** Titled group of parameter controls laid out in a grid. */
class SectionBox : public juce::Component
{
public:
    SectionBox(const juce::String& title, int maxColumns);
    void add(std::unique_ptr<ParamControl> c);
    juce::Point<int> preferredSize() const;
    void paint(juce::Graphics&) override;
    void resized() override;
    std::vector<std::unique_ptr<ParamControl>>& controls() { return controls_; }

private:
    juce::String title_;
    int maxColumns_;
    std::vector<std::unique_ptr<ParamControl>> controls_;
};

/** Every parameter of one voice (1..6), or the kit-wide FX & Mix parameters (voice 0). */
class VoicePage : public juce::Component, private juce::ChangeListener
{
public:
    VoicePage(KitForgeProcessor& p, int voice);
    ~VoicePage() override;
    void resized() override;
    void refreshControls();

private:
    class Content : public juce::Component
    {
    public:
        std::vector<std::unique_ptr<SectionBox>> boxes;
        int layout(int width); // returns the height needed
    };

    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void showCopyMenu();
    void showRandomizeMenu();
    void resetVoice();

    KitForgeProcessor& proc_;
    const int voice_;
    juce::Label title_;
    juce::TextButton copy_ { "Copy voice to..." }, randomize_ { "Randomize..." }, reset_ { "Reset voice" };
    juce::Viewport viewport_;
    Content content_;
    std::unique_ptr<juce::ParameterAttachment> fxTypeWatch_;
    std::mt19937 rng_ { std::random_device {}() };
};

} // namespace kitforge
