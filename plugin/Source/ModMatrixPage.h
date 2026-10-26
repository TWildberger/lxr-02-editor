#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "ParamControl.h"

namespace kitforge
{

class KitForgeProcessor;

/** LFO and velocity modulation of all six voices in one grid. */
class ModMatrixPage : public juce::Component, private juce::ChangeListener
{
public:
    explicit ModMatrixPage(KitForgeProcessor& p);
    ~ModMatrixPage() override;
    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    struct Column
    {
        const char* section;
        const char* label;
        const char* heading;
        int width;
    };
    static const std::vector<Column>& columns();

    KitForgeProcessor& proc_;
    std::vector<std::vector<std::unique_ptr<ParamControl>>> rows_; // [voice-1][column], may contain nullptr
    juce::Label hint_;
};

} // namespace kitforge
