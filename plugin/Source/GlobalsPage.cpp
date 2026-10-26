#include "GlobalsPage.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

GlobalsPage::GlobalsPage(KitForgeProcessor& p) : proc_(p)
{
    std::vector<int> params;
    for (int i = kFirstGlobal; i < kNumParams; ++i)
        if (ParamTable::get(i).isUserFacing())
            params.push_back(i);
    std::stable_sort(params.begin(), params.end(), [](int a, int b) {
        const auto &pa = ParamTable::get(a), &pb = ParamTable::get(b);
        const bool ma = juce::String(pa.section) == "MIDI", mb = juce::String(pb.section) == "MIDI";
        return ma != mb ? mb : pa.order < pb.order;
    });

    for (int i : params)
    {
        const auto& info = ParamTable::get(i);
        Row row { i, std::make_unique<juce::Label>(), nullptr, nullptr };
        row.label->setText(ParamTable::fullName(i), juce::dontSendNotification);
        row.label->setFont(juce::FontOptions(13.0f));
        row.label->setTooltip(info.id);
        addAndMakeVisible(*row.label);
        const int count = info.maxValue - info.minValue + 1;
        if (count <= 32)
        {
            row.combo = std::make_unique<juce::ComboBox>();
            for (int v = info.minValue; v <= info.maxValue; ++v)
                row.combo->addItem(ParamTable::formatValue(i, v), v - info.minValue + 1);
            row.combo->onChange = [this, i, c = row.combo.get(), min = info.minValue] {
                if (c->getSelectedId() > 0)
                    proc_.setGlobal(i, c->getSelectedId() - 1 + min);
            };
            addAndMakeVisible(*row.combo);
        }
        else
        {
            row.slider = std::make_unique<juce::Slider>(juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight);
            row.slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 20);
            row.slider->setRange(info.minValue, info.maxValue, 1);
            row.slider->textFromValueFunction = [i](double v) { return juce::String(ParamTable::formatValue(i, (int) v)); };
            row.slider->onValueChange = [this, i, s = row.slider.get()] { proc_.setGlobal(i, (int) s->getValue()); };
            addAndMakeVisible(*row.slider);
        }
        rows_.push_back(std::move(row));
    }

    get_.onClick = [this] { proc_.fetchGlobals(); };
    send_.onClick = [this] { send(false); };
    sendSave_.onClick = [this] { send(true); };
    open_.onClick = [this] { openFile(); };
    save_.onClick = [this] { saveFile(); };
    for (auto* b : { &get_, &send_, &sendSave_, &open_, &save_ })
        addAndMakeVisible(*b);
    note_.setColour(juce::Label::textColourId, colours::dim);
    note_.setFont(juce::FontOptions(12.0f));
    addAndMakeVisible(note_);

    proc_.kitChanged.addChangeListener(this);
    proc_.deviceChanged.addChangeListener(this);
    refresh();
}

GlobalsPage::~GlobalsPage()
{
    proc_.kitChanged.removeChangeListener(this);
    proc_.deviceChanged.removeChangeListener(this);
}

void GlobalsPage::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }

void GlobalsPage::refresh()
{
    const auto& g = proc_.globals();
    for (auto& r : rows_)
    {
        const int v = g.get(r.param);
        if (r.combo)
            r.combo->setSelectedId(v - ParamTable::get(r.param).minValue + 1, juce::dontSendNotification);
        if (r.slider)
            r.slider->setValue(v, juce::dontSendNotification);
    }
    const bool ext = proc_.device().isExtended();
    for (auto* b : { &get_, &send_, &sendSave_ })
        b->setEnabled(ext);
    note_.setText(ext ? "Changes are sent to the device as you edit; use \"Send & save\" to make them permanent."
                      : "Stock firmware cannot transfer global settings over MIDI: save a glo.cfg file and copy it to the SD card. "
                        "SysEx, Device ID and Param Echo only exist on the extended firmware.",
                  juce::dontSendNotification);
}

void GlobalsPage::send(bool save) { proc_.pushGlobals(save); }

void GlobalsPage::openFile()
{
    chooser_ = std::make_unique<juce::FileChooser>("Open global settings", juce::File(), "*.cfg");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        const auto f = fc.getResult();
        if (!f.existsAsFile())
            return;
        juce::MemoryBlock mb;
        f.loadFileAsData(mb);
        const auto* d = static_cast<const uint8_t*>(mb.getData());
        std::string warning;
        proc_.loadGlobals(Globals::fromCfg(Bytes(d, d + mb.getSize()), &warning), false);
        proc_.addLog("Loaded " + f.getFileName() + (warning.empty() ? juce::String() : " (" + juce::String(warning) + ")"));
    });
}

void GlobalsPage::saveFile()
{
    chooser_ = std::make_unique<juce::FileChooser>("Save global settings", juce::File().getChildFile("glo.cfg"), "*.cfg");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              const auto b = proc_.globals().toCfg();
                              if (f.replaceWithData(b.data(), b.size()))
                                  proc_.addLog("Saved " + f.getFullPathName());
                              else
                                  proc_.addLog("Could not write " + f.getFullPathName());
                          });
}

void GlobalsPage::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto buttons = r.removeFromTop(28);
    for (auto* b : { &get_, &send_, &sendSave_ })
    {
        b->setBounds(buttons.removeFromLeft(140));
        buttons.removeFromLeft(6);
    }
    for (auto* b : { &save_, &open_ })
    {
        b->setBounds(buttons.removeFromRight(130));
        buttons.removeFromRight(6);
    }
    r.removeFromTop(4);
    note_.setBounds(r.removeFromTop(34));
    r.removeFromTop(6);
    constexpr int cols = 3, rowH = 28;
    const int colW = r.getWidth() / cols;
    const int perCol = ((int) rows_.size() + cols - 1) / cols;
    for (int i = 0; i < (int) rows_.size(); ++i)
    {
        auto cell = juce::Rectangle<int>(r.getX() + (i / perCol) * colW, r.getY() + (i % perCol) * rowH, colW - 12, rowH - 4);
        rows_[(size_t) i].label->setBounds(cell.removeFromLeft(cell.getWidth() / 2));
        if (rows_[(size_t) i].combo)
            rows_[(size_t) i].combo->setBounds(cell);
        if (rows_[(size_t) i].slider)
            rows_[(size_t) i].slider->setBounds(cell);
    }
}

} // namespace kitforge
