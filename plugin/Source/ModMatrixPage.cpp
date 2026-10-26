#include "ModMatrixPage.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

namespace
{
constexpr int kHeaderHeight = 26;
constexpr int kRowHeight = 72;
constexpr int kNameWidth = 110;

int findParam(int voice, const char* section, const char* label)
{
    for (int i : ParamTable::paramsForVoice(voice))
    {
        const auto& p = ParamTable::get(i);
        if (p.section && p.label && juce::String(p.section) == section && juce::String(p.label) == label)
            return i;
    }
    return -1;
}
} // namespace

const std::vector<ModMatrixPage::Column>& ModMatrixPage::columns()
{
    static const std::vector<Column> c {
        { "LFO", "LFO Target", "LFO target", 190 },        { "LFO", "LFO Amount", "Amount", 70 },
        { "LFO", "LFO Rate", "Rate", 70 },                 { "LFO", "LFO Sync", "Sync", 90 },
        { "LFO", "LFO Wave", "Wave", 90 },                 { "LFO", "LFO Retrigger", "Retrigger", 90 },
        { "LFO", "LFO Phase", "Phase", 70 },               { "Mod", "Velocity Target", "Velocity target", 170 },
        { "Mod", "Velocity Amount", "Vel. amount", 70 },   { "Mod", "Velocity -> Volume", "Vel. > vol", 80 },
    };
    return c;
}

ModMatrixPage::ModMatrixPage(KitForgeProcessor& p) : proc_(p)
{
    for (int v = 1; v <= kNumVoices; ++v)
    {
        auto& row = rows_.emplace_back();
        for (const auto& c : columns())
        {
            const int param = findParam(v, c.section, c.label);
            if (param >= 0 && proc_.parameterFor(param))
            {
                auto ctl = std::make_unique<ParamControl>(proc_, param, false);
                addAndMakeVisible(*ctl);
                row.push_back(std::move(ctl));
            }
            else
                row.push_back(nullptr);
        }
    }
    hint_.setText("Choosing an LFO target on another voice also sets the LFO's target voice. Velocity can only modulate "
                  "its own voice. Right-click a control to lock it.",
                  juce::dontSendNotification);
    hint_.setColour(juce::Label::textColourId, colours::dim);
    hint_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(hint_);
    proc_.kitChanged.addChangeListener(this);
}

ModMatrixPage::~ModMatrixPage() { proc_.kitChanged.removeChangeListener(this); }

void ModMatrixPage::changeListenerCallback(juce::ChangeBroadcaster*)
{
    for (auto& row : rows_)
        for (auto& c : row)
            if (c)
                c->refresh();
}

void ModMatrixPage::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().reduced(8);
    g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
    g.setColour(colours::accent);
    int x = r.getX() + kNameWidth;
    for (const auto& c : columns())
    {
        g.drawText(c.heading, x, r.getY(), c.width, kHeaderHeight, juce::Justification::centred);
        x += c.width;
    }
    for (int v = 0; v < kNumVoices; ++v)
    {
        const int y = r.getY() + kHeaderHeight + v * kRowHeight;
        g.setColour((v % 2) ? colours::panel : colours::panel.darker(0.15f));
        g.fillRect(r.getX(), y, x - r.getX(), kRowHeight);
        g.setColour(colours::text);
        g.setFont(juce::FontOptions(14.0f, juce::Font::bold));
        g.drawText(ParamTable::voiceName(v + 1), r.getX() + 6, y, kNameWidth - 6, kRowHeight, juce::Justification::centredLeft);
    }
}

void ModMatrixPage::resized()
{
    auto r = getLocalBounds().reduced(8);
    for (int v = 0; v < kNumVoices; ++v)
    {
        int x = r.getX() + kNameWidth;
        const int y = r.getY() + kHeaderHeight + v * kRowHeight;
        for (size_t c = 0; c < columns().size(); ++c)
        {
            if (auto& ctl = rows_[(size_t) v][c])
                ctl->setBounds(x, y + 2, columns()[c].width, kRowHeight - 4);
            x += columns()[c].width;
        }
    }
    hint_.setBounds(r.getX(), r.getY() + kHeaderHeight + kNumVoices * kRowHeight + 8, r.getWidth(), 20);
}

} // namespace kitforge
