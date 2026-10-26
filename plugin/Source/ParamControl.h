#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace kitforge
{

class KitForgeProcessor;

/** One sound parameter: rotary knob, menu, or modulation-target picker, with a right-click menu. */
class ParamControl : public juce::Component
{
public:
    ParamControl(KitForgeProcessor& p, int param, bool showLabel = true);
    ~ParamControl() override;

    static constexpr int kWidth = 82;
    static constexpr int kHeight = 96;

    int param() const { return param_; }
    void refresh(); // FX parameter labels, lock state
    void resized() override;
    void paint(juce::Graphics&) override;
    void mouseDown(const juce::MouseEvent&) override;

    /** Shows the modulation target menu for an LFO or velocity target parameter. */
    static void showTargetMenu(KitForgeProcessor& p, int param, juce::Component* target);

private:
    enum class Kind { Rotary, Combo, Target };
    void showContextMenu();
    juce::String labelText() const;

    KitForgeProcessor& proc_;
    const int param_;
    const bool showLabel_;
    Kind kind_ = Kind::Rotary;
    int min_ = 0;
    juce::Label label_;
    juce::Slider slider_;
    juce::ComboBox combo_;
    juce::TextButton target_;
    std::unique_ptr<juce::SliderParameterAttachment> sliderAttachment_;
    std::unique_ptr<juce::ParameterAttachment> attachment_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ParamControl)
};

} // namespace kitforge
