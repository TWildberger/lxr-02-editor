#include "NativePage.h"
#include <algorithm>

namespace vsynthxt
{
NativePage::Row::Row(Processor& p, const Parameter& parameter) : info(parameter)
{
    addAndMakeVisible(label); addAndMakeVisible(raw); addAndMakeVisible(slider);
    label.setText(info.label, juce::dontSendNotification);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 85, 24);
    slider.setTooltip("Local edit only. Explicit Send is required to change hardware.");
    const auto& types = algorithms(algorithmFamily(info));
    if (!types.empty() && info.offset == algorithmTypeOffset(info))
    {
        addAndMakeVisible(type);
        slider.setVisible(false);
        for (size_t i = 0; i < types.size(); ++i)
            type.addItem(juce::String(static_cast<int>(i)) + ": " + types[i].name, static_cast<int>(i + 1));
        typeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state, info.key, type);
        type.setTooltip("Algorithm code; changes are local only. Existing parameter bytes are retained.");
        label.setText("Algorithm type (local)", juce::dontSendNotification);
    }
    else attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, info.key, slider);
}
void NativePage::Row::refresh(const Patch& p)
{
    if (!p.complete())
    {
        slider.setEnabled(false);
        type.setEnabled(false);
        raw.setText("Fetch/import a complete patch first.", juce::dontSendNotification);
        return;
    }
    try
    {
        const int display = parameterValue(p, info), wire = display + info.bias;
        const bool valid = wire >= info.minimum && wire <= info.maximum;
        slider.setEnabled(valid);
        type.setEnabled(valid);
        if (typeAttachment)
        {
            type.setTextWhenNothingSelected("Unknown wire code " + juce::String(wire));
            type.setSelectedId(valid ? wire + 1 : 0, juce::dontSendNotification);
        }
        const auto* algorithm = selectedAlgorithm(p, info);
        if (!typeAttachment)
        {
            const bool generic = info.width == 4 && !algorithmFamily(info).empty();
            label.setText(generic ? juce::String(info.label) + " (unmapped wire slot)" : juce::String(info.label),
                          juce::dontSendNotification);
            label.setTooltip(algorithm ? algorithm->name : "Algorithm code unresolved");
        }
        const auto addressBytes = digits(temporary + info.offset);
        const auto addressText = juce::String::toHexString(addressBytes.data(), 4, 1);
        raw.setText((valid ? "" : "Out-of-range raw value retained; control disabled. ")
            + addressText + "  raw " + juce::String(wire) + "  | manual p." + juce::String(info.page),
            juce::dontSendNotification);
    }
    catch (const std::exception& e)
    {
        slider.setEnabled(false);
        type.setEnabled(false);
        raw.setText(juce::String(e.what()) + " (bytes retained)", juce::dontSendNotification);
    }
}
void NativePage::Row::resized()
{
    auto r = getLocalBounds().reduced(6);
    label.setBounds(r.removeFromTop(25));
    raw.setBounds(r.removeFromBottom(22));
    slider.setBounds(r);
    type.setBounds(r);
}
NativePage::NativePage(Processor& p) : proc_(p)
{
    for (auto* c : std::initializer_list<juce::Component*> {&group_, &zone_, &section_, &warning_, &viewport_, &algorithmReference_, &manual_})
        addAndMakeVisible(c);
    const juce::StringArray groups {"Oscillator", "Envelope", "LFO", "COSM", "Effects"};
    for (int i = 0; i < groups.size(); ++i) group_.addItem(groups[i], i + 1);
    for (int i = 1; i <= 16; ++i) zone_.addItem("Zone " + juce::String(i), i);
    warning_.setText("Local edits only. Sync/keyfollow/type values marked 'wire' are protocol codes. "
        "Named COSM/FX types use official codes; numbered slots remain generic where wire mapping is unresolved. "
        "Changing a type preserves its parameter bytes; check the XT manual before Send.",
        juce::dontSendNotification);
    viewport_.setViewedComponent(&content_, false);
    algorithmReference_.setMultiLine(true);
    algorithmReference_.setReadOnly(true);
    algorithmReference_.setScrollbarsShown(true);
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
    std::stable_partition(rows_.begin(), rows_.end(), [](const auto& row) { return row->typeAttachment != nullptr; });
    viewport_.setViewPosition(0, 0);
    resized(); refresh();
}
void NativePage::refresh()
{
    const auto patch = proc_.patch();
    for (auto& row : rows_) row->refresh(patch);
    const auto* algorithm = rows_.empty() ? nullptr : selectedAlgorithm(patch, rows_.front()->info);
    algorithmReference_.setVisible(algorithm != nullptr);
    manual_.setVisible(algorithm != nullptr);
    if (algorithm)
    {
        algorithmReference_.setText(juce::String(algorithm->name) + " | Owner's Manual p." + juce::String(algorithm->page)
            + "\n" + algorithm->reference
            + "\nReference only: physical units/ranges are not bound to numbered SysEx slots. Raw editing remains unchanged.",
            false);
        manual_.setURL(juce::URL("https://static.roland.com/assets/media/pdf/V-SYNTH_XT_OM.pdf#page="
            + juce::String(algorithm->page + 1)));
    }
    resized();
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
    if (algorithmReference_.isVisible())
    {
        manual_.setBounds(r.removeFromBottom(24));
        algorithmReference_.setBounds(r.removeFromBottom(96));
    }
    viewport_.setBounds(r);
    const int width = juce::jmax(0, viewport_.getWidth() - viewport_.getScrollBarThickness() - 2);
    content_.setSize(width, static_cast<int>(rows_.size()) * 96);
    for (size_t i = 0; i < rows_.size(); ++i)
        rows_[i]->setBounds(0, static_cast<int>(i) * 96, width, 96);
}
}
