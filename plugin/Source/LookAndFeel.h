#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kitforge
{

namespace colours
{
const juce::Colour background { 0xff1b1d21 };
const juce::Colour panel { 0xff25282e };
const juce::Colour outline { 0xff3a3e46 };
const juce::Colour text { 0xffe4e6ea };
const juce::Colour dim { 0xff9097a3 };
const juce::Colour accent { 0xffe8a33d };
const juce::Colour locked { 0xff6fa8dc };
const juce::Colour warn { 0xffe06c5f };
const juce::Colour ok { 0xff7cc47f };
} // namespace colours

class KitForgeLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KitForgeLookAndFeel();

    void drawRotarySlider(juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    juce::Font getLabelFont(juce::Label&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override;
};

} // namespace kitforge
