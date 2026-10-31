#include "Editor.h"

namespace vsynthxt
{
void Editor::add(juce::Component& parent, juce::Component& child) { parent.addAndMakeVisible(child); }
void Editor::act(const std::function<void()>& fn)
{
    try { fn(); }
    catch (const std::exception& e) { proc_.report(e.what()); }
}
Editor::Editor(Processor& p) : AudioProcessorEditor(p), proc_(p), nativePage_(p)
{
    heading_.setText("V-Synth XT Companion", juce::dontSendNotification);
    heading_.setFont(juce::Font(juce::FontOptions(24.0f)));
    warning_.setText("Hardware required. Patch snapshots do not include samples; keep the matching project/waves on the XT.\n"
                     "Edits and DAW recall are local only. Fetch/Send are explicit. No user-slot writes.",
                     juce::dontSendNotification);
    for (auto* c : std::initializer_list<juce::Component*> {&heading_, &warning_, &status_, &problem_, &tabs_}) add(*this, *c);
    tabs_.addTab("Device", juce::Colour(0xff252b35), &device_, false);
    tabs_.addTab("Patch", juce::Colour(0xff252b35), &patch_, false);
    tabs_.addTab("Library", juce::Colour(0xff252b35), &library_, false);
    tabs_.addTab("Native", juce::Colour(0xff252b35), &nativePage_, false);
    for (auto* c : std::initializer_list<juce::Component*> {
        &route_, &input_, &output_, &deviceId_, &native_, &connect_, &fetch_, &send_, &cancel_}) add(device_, *c);
    route_.addItem("Direct MIDI (standalone / recommended for Logic)", 1);
    route_.addItem("Host MIDI (DAW must route SysEx both ways)", 2);
    route_.setSelectedId(1);
    input_.setTextWhenNothingSelected("Select MIDI input");
    output_.setTextWhenNothingSelected("Select MIDI output");
    const auto ins = juce::MidiInput::getAvailableDevices(), outs = juce::MidiOutput::getAvailableDevices();
    ins_.assign(ins.begin(), ins.end()); outs_.assign(outs.begin(), outs.end());
    for (size_t i = 0; i < ins_.size(); ++i) input_.addItem(ins_[i].name, static_cast<int>(i + 1));
    for (size_t i = 0; i < outs_.size(); ++i) output_.addItem(outs_[i].name, static_cast<int>(i + 1));
    for (int id = 17; id <= 32; ++id) deviceId_.addItem("Device ID " + juce::String(id), id);
    deviceId_.setSelectedId(17);
    const auto routeChanged = [this] {
        proc_.session.cancel(); native_.setToggleState(false, juce::dontSendNotification);
        routeApplied_ = false;
        connect_.setButtonText("Apply route (changed)");
    };
    route_.onChange = routeChanged; input_.onChange = routeChanged;
    output_.onChange = routeChanged; deviceId_.onChange = routeChanged;
    connect_.onClick = [this] {
        const auto in = input_.getSelectedItemIndex(), out = output_.getSelectedItemIndex();
        proc_.session.configure(route_.getSelectedId() == 2,
            in >= 0 ? ins_.at(static_cast<size_t>(in)).identifier : juce::String(),
            out >= 0 ? outs_.at(static_cast<size_t>(out)).identifier : juce::String(),
            deviceId_.getSelectedId());
        connect_.setButtonText("Apply route");
        routeApplied_ = true;
    };
    fetch_.onClick = [this] { act([this] { proc_.fetch(native_.getToggleState()); }); };
    send_.onClick = [this] {
        juce::Component::SafePointer<Editor> safe(this);
        juce::AlertWindow::showOkCancelBox(juce::MessageBoxIconType::WarningIcon,
            "Replace the XT temporary patch?",
            "This replaces unsaved part-1 edits on the hardware. Back them up first. "
            "Matching samples/project must already be loaded. User slots are not written.",
            "Send and verify", "Cancel", this, juce::ModalCallbackFunction::create([safe](int result) {
                if (safe && result) safe->act([safe] { safe->proc_.send(safe->native_.getToggleState()); });
            }));
    };
    cancel_.onClick = [this] { proc_.session.cancel(); };
    native_.onClick = [this] { if (!native_.getToggleState()) proc_.session.cancel(); };

    for (auto* c : std::initializer_list<juce::Component*> {
        &name_, &rename_, &undo_, &redo_, &ab_, &copy_, &import_, &export_}) add(patch_, *c);
    name_.setInputRestrictions(12); name_.setTextToShowWhenEmpty("Patch name (12 ASCII characters)", juce::Colours::grey);
    const std::array<const char*, 3> ids {"xt.common.coarse", "xt.common.fine", "xt.chorus.type"};
    const std::array<const char*, 3> names {"Coarse tune (semitones)", "Fine tune (cents)", "Chorus type (0-8)"};
    for (size_t i = 0; i < 3; ++i)
    {
        add(patch_, sliders_[i]); add(patch_, labels_[i]);
        labels_[i].setText(names[i], juce::dontSendNotification);
        sliders_[i].setSliderStyle(juce::Slider::LinearHorizontal);
        sliders_[i].setTextBoxStyle(juce::Slider::TextBoxRight, false, 70, 24);
        attachments_[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state, ids[i], sliders_[i]);
    }
    rename_.onClick = [this] { act([this] { proc_.rename(name_.getText()); }); };
    undo_.onClick = [this] { act([this] { proc_.undo(); }); };
    redo_.onClick = [this] { act([this] { proc_.redo(); }); };
    ab_.onClick = [this] { act([this] { proc_.compare(); }); };
    copy_.onClick = [this] { act([this] { proc_.copyAB(); }); };
    import_.onClick = [this] { file(false); }; export_.onClick = [this] { file(true); };

    for (auto* c : std::initializer_list<juce::Component*> {
        &query_, &entries_, &tagsText_, &notes_, &favorite_, &save_, &load_}) add(library_, *c);
    query_.setTextToShowWhenEmpty("Search names, tags and notes", juce::Colours::grey);
    tagsText_.setTextToShowWhenEmpty("Tags (space/comma separated text)", juce::Colours::grey);
    notes_.setTextToShowWhenEmpty("Project / sample dependencies and notes", juce::Colours::grey);
    notes_.setMultiLine(true);
    entries_.setTextWhenNothingSelected("Select an archived patch");
    query_.onTextChange = [this] { search(); };
    entries_.onChange = [this] {
        const int i = entries_.getSelectedItemIndex();
        if (i < 0 || static_cast<size_t>(i) >= rows_.size()) return;
        const auto& row = rows_[static_cast<size_t>(i)];
        tagsText_.setText(row.tags); notes_.setText(row.notes);
        favorite_.setToggleState(row.favorite, juce::dontSendNotification);
    };
    save_.onClick = [this] { act([this] {
        proc_.library().add(proc_.patch(), tagsText_.getText().toStdString(), notes_.getText().toStdString(), favorite_.getToggleState());
        search(); proc_.report("Patch archived locally (identical snapshots update metadata).");
    }); };
    load_.onClick = [this] { act([this] {
        const int i = entries_.getSelectedItemIndex();
        if (i < 0) throw std::runtime_error("Select a library patch first");
        proc_.load(proc_.library().get(rows_.at(static_cast<size_t>(i)).id));
    }); };
    proc_.changed.addChangeListener(this);
    tabs_.setCurrentTabIndex(proc_.selectedTab.load());
    tabs_.changed = [this](int index) { proc_.selectedTab.store(index); };
    setSize(820, 600);
    refresh(); search(); timerCallback(); startTimerHz(5);
}
Editor::~Editor() { stopTimer(); proc_.changed.removeChangeListener(this); }
void Editor::file(bool save)
{
    chooser_ = std::make_unique<juce::FileChooser>(save ? "Export complete native patch snapshot" : "Import native part-1 patch snapshot",
                                                  juce::File(), "*.syx");
    juce::Component::SafePointer<Editor> safe(this);
    chooser_->launchAsync((save ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                               : juce::FileBrowserComponent::openMode) | juce::FileBrowserComponent::canSelectFiles,
        [safe, save](const juce::FileChooser& chooser) {
            if (!safe || chooser.getResult() == juce::File()) return;
            safe->act([&] {
                if (save)
                {
                    const auto b = safe->proc_.patch().sysex();
                    if (!chooser.getResult().replaceWithData(b.data(), b.size())) throw std::runtime_error("Could not export patch file");
                }
                else
                {
                    if (chooser.getResult().getSize() > 65536) throw std::runtime_error("Patch file exceeds 64 KiB");
                    juce::MemoryBlock b;
                    if (!chooser.getResult().loadFileAsData(b)) throw std::runtime_error("Could not read patch file");
                    const auto* p = static_cast<const uint8_t*>(b.getData());
                    safe->proc_.load(Patch::fromSysex(Bytes(p, p + b.getSize())));
                }
            });
        });
}
void Editor::search()
{
    act([this] {
        rows_ = proc_.library().search(query_.getText().toStdString());
        entries_.clear(juce::dontSendNotification);
        for (size_t i = 0; i < rows_.size(); ++i)
            entries_.addItem((rows_[i].favorite ? "* " : "") + juce::String(rows_[i].name), static_cast<int>(i + 1));
    });
}
void Editor::refresh()
{
    tabs_.setCurrentTabIndex(proc_.selectedTab.load());
    const auto p = proc_.patch();
    name_.setText(p.name(), false);
    for (auto& s : sliders_) s.setEnabled(p.complete());
    rename_.setEnabled(p.complete()); export_.setEnabled(p.complete()); save_.setEnabled(p.complete());
    ab_.setButtonText(proc_.isB() ? "Compare A/B (B)" : "Compare A/B (A)");
    problem_.setText(proc_.problem(), juce::dontSendNotification);
}
void Editor::changeListenerCallback(juce::ChangeBroadcaster*) { refresh(); }
void Editor::timerCallback()
{
    status_.setText(proc_.session.status(), juce::dontSendNotification);
    const bool ready = routeApplied_ && native_.getToggleState() && !proc_.session.busy()
        && connect_.getButtonText() == "Apply route";
    fetch_.setEnabled(ready); send_.setEnabled(ready && proc_.patch().complete());
}
void Editor::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff19202a)); }
void Editor::resized()
{
    auto r = getLocalBounds().reduced(18);
    heading_.setBounds(r.removeFromTop(34)); warning_.setBounds(r.removeFromTop(52));
    problem_.setBounds(r.removeFromBottom(42)); status_.setBounds(r.removeFromBottom(36));
    tabs_.setBounds(r);
    auto d = device_.getLocalBounds().reduced(18);
    route_.setBounds(d.removeFromTop(32)); d.removeFromTop(10);
    input_.setBounds(d.removeFromTop(32)); d.removeFromTop(10);
    output_.setBounds(d.removeFromTop(32)); d.removeFromTop(10);
    deviceId_.setBounds(d.removeFromTop(32)); d.removeFromTop(10);
    native_.setBounds(d.removeFromTop(32)); d.removeFromTop(10);
    auto buttons = d.removeFromTop(36);
    connect_.setBounds(buttons.removeFromLeft(155).reduced(2));
    fetch_.setBounds(buttons.removeFromLeft(125).reduced(2));
    send_.setBounds(buttons.removeFromLeft(230).reduced(2));
    cancel_.setBounds(buttons.removeFromLeft(90).reduced(2));
    auto p = patch_.getLocalBounds().reduced(18);
    auto row = p.removeFromTop(34);
    name_.setBounds(row.removeFromLeft(330)); rename_.setBounds(row.removeFromLeft(140).reduced(3));
    for (size_t i = 0; i < 3; ++i)
    {
        p.removeFromTop(12); row = p.removeFromTop(38);
        labels_[i].setBounds(row.removeFromLeft(210)); sliders_[i].setBounds(row);
    }
    p.removeFromTop(16); row = p.removeFromTop(36);
    for (auto* b : {&undo_, &redo_, &ab_, &copy_}) b->setBounds(row.removeFromLeft(170).reduced(3));
    row = p.removeFromTop(40);
    import_.setBounds(row.removeFromLeft(170).reduced(3)); export_.setBounds(row.removeFromLeft(170).reduced(3));
    auto l = library_.getLocalBounds().reduced(18);
    query_.setBounds(l.removeFromTop(32)); l.removeFromTop(8);
    entries_.setBounds(l.removeFromTop(32)); l.removeFromTop(8);
    tagsText_.setBounds(l.removeFromTop(32)); l.removeFromTop(8);
    notes_.setBounds(l.removeFromTop(100)); favorite_.setBounds(l.removeFromTop(30));
    row = l.removeFromTop(36);
    save_.setBounds(row.removeFromLeft(240).reduced(3)); load_.setBounds(row.removeFromLeft(240).reduced(3));
}
}
