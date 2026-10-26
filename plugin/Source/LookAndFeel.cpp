#include "LookAndFeel.h"

namespace kitforge
{

KitForgeLookAndFeel::KitForgeLookAndFeel()
{
    auto scheme = getDarkColourScheme();
    scheme.setUIColour(ColourScheme::windowBackground, colours::background);
    scheme.setUIColour(ColourScheme::widgetBackground, colours::panel);
    scheme.setUIColour(ColourScheme::menuBackground, colours::panel);
    scheme.setUIColour(ColourScheme::outline, colours::outline);
    scheme.setUIColour(ColourScheme::defaultText, colours::text);
    scheme.setUIColour(ColourScheme::defaultFill, colours::accent);
    scheme.setUIColour(ColourScheme::highlightedText, juce::Colours::black);
    scheme.setUIColour(ColourScheme::highlightedFill, colours::accent);
    scheme.setUIColour(ColourScheme::menuText, colours::text);
    setColourScheme(scheme);

    setColour(juce::Slider::rotarySliderFillColourId, colours::accent);
    setColour(juce::Slider::rotarySliderOutlineColourId, colours::outline);
    setColour(juce::Slider::backgroundColourId, colours::outline);
    setColour(juce::Slider::trackColourId, colours::accent);
    setColour(juce::Slider::thumbColourId, colours::accent);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Slider::textBoxTextColourId, colours::text);
    setColour(juce::TextButton::buttonColourId, colours::panel);
    setColour(juce::TextButton::buttonOnColourId, colours::accent);
    setColour(juce::TextButton::textColourOnId, juce::Colours::black);
    setColour(juce::ComboBox::backgroundColourId, colours::panel);
    setColour(juce::ComboBox::outlineColourId, colours::outline);
    setColour(juce::TextEditor::backgroundColourId, colours::panel.darker(0.2f));
    setColour(juce::TextEditor::outlineColourId, colours::outline);
    setColour(juce::ListBox::backgroundColourId, colours::panel.darker(0.2f));
    setColour(juce::TabbedButtonBar::tabOutlineColourId, colours::outline);
    setColour(juce::TabbedButtonBar::frontTextColourId, colours::accent);
    setColour(juce::ToggleButton::tickColourId, colours::accent);
    setColour(juce::GroupComponent::outlineColourId, colours::outline);
    setColour(juce::GroupComponent::textColourId, colours::dim);
}

void KitForgeLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end,
                                           juce::Slider& s)
{
    const auto bounds = juce::Rectangle<int>(x, y, w, h).toFloat().reduced(3.0f);
    const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) / 2.0f;
    const auto centre = bounds.getCentre();
    const float line = juce::jmax(2.0f, radius * 0.16f);
    const float arcRadius = radius - line * 0.5f;
    const float angle = start + pos * (end - start);
    const bool bipolar = s.getProperties().getWithDefault("bipolar", false);
    const float from = bipolar ? (start + end) * 0.5f : start;

    juce::Path track;
    track.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, start, end, true);
    g.setColour(s.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(track, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, juce::jmin(from, angle), juce::jmax(from, angle), true);
    g.setColour(s.isEnabled() ? s.findColour(juce::Slider::rotarySliderFillColourId) : colours::dim);
    g.strokePath(value, juce::PathStrokeType(line, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const auto tip = centre.getPointOnCircumference(arcRadius - line * 1.6f, angle);
    g.setColour(colours::text);
    g.drawLine(juce::Line<float>(centre.getPointOnCircumference(arcRadius * 0.25f, angle), tip), line * 0.8f);
}

juce::Font KitForgeLookAndFeel::getLabelFont(juce::Label& l)
{
    return juce::Font(juce::FontOptions(l.getFont().getHeight()));
}

juce::Font KitForgeLookAndFeel::getComboBoxFont(juce::ComboBox& c)
{
    return juce::Font(juce::FontOptions(juce::jmin(13.0f, (float) c.getHeight() * 0.6f)));
}

juce::Font KitForgeLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    return juce::Font(juce::FontOptions(juce::jmin(13.0f, (float) buttonHeight * 0.6f)));
}

} // namespace kitforge
