#include "NativePage.h"

namespace vsynthxt
{
NativePage::Row::Row(Processor& p, const Parameter& parameter) : info(parameter)
{
    addAndMakeVisible(label); addAndMakeVisible(raw); addAndMakeVisible(slider);
    label.setText(info.label, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 24);
    slider.setTooltip("Local edit only. Explicit Send is required to change hardware.");
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, info.key, slider);
}
void NativePage::Row::refresh(const Patch& p)
{
    if (!p.complete())
    {
        slider.setEnabled(false);
        raw.setText("Fetch/import a complete patch first.", juce::dontSendNotification);
        return;
    }
    try
    {
        const int display = parameterValue(p, info), wire = display + info.bias;
        const bool valid = wire >= info.minimum && wire <= info.maximum;
        slider.setEnabled(valid);
        const auto addressBytes = digits(temporary + info.offset);
        const auto addressText = juce::String::toHexString(addressBytes.data(), 4, 1);
        raw.setText((valid ? "" : "Out-of-range raw value retained; control disabled. ")
            + addressText + "  raw " + juce::String(wire) + "  | manual p." + juce::String(info.page),
            juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        slider.setEnabled(false);
        raw.setText(juce::String(e.what()) + " (bytes retained)", juce::dontSendNotification);
    }
}
void NativePage::Row::resized()
{
    auto r = getLocalBounds().reduced(6);
    label.setBounds(r.removeFromTop(25));
    raw.setBounds(r.removeFromBottom(22));
    slider.setBounds(r);
}
NativePage::NativePage(Processor& p) : proc_(p)
{
    for (auto* c : std::initializer_list<juce::Component*> {&group_, &zone_, &section_, &warning_, &viewport_})
        addAndMakeVisible(c);
    const juce::StringArray groups {"Oscillator", "Envelope", "LFO", "COSM", "Effects"};
    for (int i = 0; i < groups.size(); ++i) group_.addItem(groups[i], i + 1);
    for (int i = 1; i <= 16; ++i) zone_.addItem("Zone " + juce::String(i), i);
    warning_.setText("Local edits only. Sync/keyfollow/type values marked 'wire' are protocol codes. "
        "COSM/FX numbered parameters use generic signed values, not algorithm-specific units. "
        "Changing a type preserves its parameter bytes; check the XT manual before Send.",
        juce::dontSendNotification);
    viewport_.setViewedComponent(&content_, false);
    group_.setSelectedId(proc_.selectedNativeGroup.load(), juce::dontSendNotification);
    zone_.setSelectedId(proc_.selectedZone.load(), juce::dontSendNotification);
    group_.onChange = [this] {
        proc_.selectedNativeGroup.store(group_.getSelectedId());
        proc_.selectedNativeSection.store(1);
        rebuildSections();
    };
    zone_.onChange = [this] {
        proc_.selectedZone.store(zone_.getSelectedId()); rebuildRows();
    };
    section_.onChange = [this] {
        proc_.selectedNativeSection.store(section_.getSelectedId()); rebuildRows();
    };
    proc_.changed.addChangeListener(this);
    rebuildSections();
}
NativePage::~NativePage()
{
    proc_.changed.removeChangeListener(this);
    viewport_.setViewedComponent(nullptr, false);
}
void NativePage::select(int group, int zone, int section)
{
    proc_.selectedNativeGroup.store(juce::jlimit(1, 5, group));
    proc_.selectedZone.store(juce::jlimit(1, 16, zone));
    proc_.selectedNativeSection.store(juce::jmax(1, section));
    group_.setSelectedId(proc_.selectedNativeGroup.load(), juce::dontSendNotification);
    zone_.setSelectedId(proc_.selectedZone.load(), juce::dontSendNotification);
    rebuildSections();
}
void NativePage::rebuildSections()
{
    sections_.clear();
    for (const auto& p : parameters())
        if (group_.getText() == p.group && (p.zone == 0 || p.zone == zone_.getSelectedId()))
            sections_.addIfNotAlreadyThere(p.section);
    section_.clear(juce::dontSendNotification);
    for (int i = 0; i < sections_.size(); ++i) section_.addItem(sections_[i], i + 1);
    const int selected = juce::jlimit(1, juce::jmax(1, sections_.size()), proc_.selectedNativeSection.load());
    proc_.selectedNativeSection.store(selected);
    section_.setSelectedId(selected, juce::dontSendNotification);
    zone_.setEnabled(group_.getText() != "Effects");
    rebuildRows();
}
void NativePage::rebuildRows()
{
    rows_.clear();
    for (const auto& p : parameters())
        if (group_.getText() == p.group && section_.getText() == p.section
            && (p.zone == 0 || p.zone == zone_.getSelectedId()))
        {
            auto row = std::make_unique<Row>(proc_, p);
            content_.addAndMakeVisible(*row); rows_.push_back(std::move(row));
        }
    viewport_.setViewPosition(0, 0);
    resized(); refresh();
}
void NativePage::refresh()
{
    const auto patch = proc_.patch();
    for (auto& row : rows_) row->refresh(patch);
}
void NativePage::changeListenerCallback(juce::ChangeBroadcaster*)
{
    if (group_.getSelectedId() != proc_.selectedNativeGroup.load()
        || zone_.getSelectedId() != proc_.selectedZone.load()
        || section_.getSelectedId() != proc_.selectedNativeSection.load())
        select(proc_.selectedNativeGroup.load(), proc_.selectedZone.load(), proc_.selectedNativeSection.load());
    else refresh();
}
void NativePage::resized()
{
    auto r = getLocalBounds().reduced(12);
    auto top = r.removeFromTop(34);
    group_.setBounds(top.removeFromLeft(150).reduced(2));
    zone_.setBounds(top.removeFromLeft(110).reduced(2));
    section_.setBounds(top.reduced(2));
    warning_.setBounds(r.removeFromTop(66));
    viewport_.setBounds(r);
    const int width = juce::jmax(0, viewport_.getWidth() - viewport_.getScrollBarThickness() - 2);
    content_.setSize(width, static_cast<int>(rows_.size()) * 96);
    for (size_t i = 0; i < rows_.size(); ++i)
        rows_[i]->setBounds(0, static_cast<int>(i) * 96, width, 96);
}
}
