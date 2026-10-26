#include "PluginEditor.h"

#include "DevicePage.h"
#include "GlobalsPage.h"
#include "LibraryPage.h"
#include "ModMatrixPage.h"
#include "PluginProcessor.h"
#include "VoicePage.h"
#include "kitforge/KitOps.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

KitForgeEditor::KitForgeEditor(KitForgeProcessor& p) : juce::AudioProcessorEditor(p), proc_(p)
{
    setLookAndFeel(&lnf_);
    product_.setText("KitForge  for LXR-02", juce::dontSendNotification);
    product_.setFont(juce::FontOptions(17.0f, juce::Font::bold));
    product_.setColour(juce::Label::textColourId, colours::accent);
    addAndMakeVisible(product_);

    name_.setInputRestrictions(8);
    name_.setTooltip("Kit name (8 characters)");
    name_.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    name_.setJustification(juce::Justification::centred);
    auto commitName = [this] {
        if (name_.getText() != proc_.kitName())
            proc_.setKitName(name_.getText());
    };
    name_.onReturnKey = commitName;
    name_.onFocusLost = commitName;
    addAndMakeVisible(name_);

    undo_.onClick = [this] { proc_.undo(); };
    redo_.onClick = [this] { proc_.redo(); };
    ab_.setTooltip("Switch between two kits for comparison");
    ab_.onClick = [this] { proc_.toggleAB(); };
    copyAB_.onClick = [this] { proc_.copyCurrentToOther(); };
    random_.onClick = [this] { randomizeKit(); };
    open_.onClick = [this] { openKit(); };
    save_.onClick = [this] { saveKit(); };
    addLib_.onClick = [this] {
        if (library_)
            library_->addCurrentKit();
    };
    send_.setTooltip("Send the whole kit to the device");
    send_.onClick = [this] { proc_.pushKitToDevice(); };
    undo_.setTooltip("Undo (Cmd/Ctrl+Z)");
    redo_.setTooltip("Redo (Shift+Cmd/Ctrl+Z)");
    for (auto* b : { &undo_, &redo_, &ab_, &copyAB_, &random_, &open_, &save_, &addLib_, &send_ })
        addAndMakeVisible(*b);
    status_.setJustificationType(juce::Justification::centredRight);
    status_.setFont(juce::FontOptions(12.5f));
    addAndMakeVisible(status_);

    const auto tabColour = colours::panel;
    for (int v = 1; v <= kNumVoices; ++v)
        tabs_.addTab(ParamTable::voiceName(v), tabColour, new VoicePage(proc_, v), true);
    tabs_.addTab("FX & Kit", tabColour, new VoicePage(proc_, 0), true);
    tabs_.addTab("Mod Matrix", tabColour, new ModMatrixPage(proc_), true);
    tabs_.addTab("Globals", tabColour, new GlobalsPage(proc_), true);
    library_ = new LibraryPage(proc_);
    tabs_.addTab("Library", tabColour, library_, true);
    tabs_.addTab("Device", tabColour, new DevicePage(proc_), true);
    tabs_.setTabBarDepth(30);
    tabs_.setOutline(0);
    addAndMakeVisible(tabs_);

    proc_.kitChanged.addChangeListener(this);
    proc_.historyChanged.addChangeListener(this);
    proc_.deviceChanged.addChangeListener(this);
    refreshHeader();

    setWantsKeyboardFocus(true);
    setResizable(true, true);
    setResizeLimits(1000, 640, 2400, 1600);
    setSize(1200, 800);
}

KitForgeEditor::~KitForgeEditor()
{
    proc_.kitChanged.removeChangeListener(this);
    proc_.historyChanged.removeChangeListener(this);
    proc_.deviceChanged.removeChangeListener(this);
    setLookAndFeel(nullptr);
}

void KitForgeEditor::changeListenerCallback(juce::ChangeBroadcaster*) { refreshHeader(); }

void KitForgeEditor::refreshHeader()
{
    if (!name_.hasKeyboardFocus(true))
        name_.setText(proc_.kitName(), juce::dontSendNotification);
    undo_.setEnabled(proc_.canUndo());
    redo_.setEnabled(proc_.canRedo());
    ab_.setButtonText(proc_.isB() ? "B" : "A");
    ab_.setToggleState(proc_.isB(), juce::dontSendNotification);
    copyAB_.setButtonText(proc_.isB() ? "Copy to A" : "Copy to B");
    auto& dev = proc_.device();
    juce::Colour c = colours::dim;
    juce::String s;
    switch (dev.mode())
    {
        case DeviceSession::Mode::Extended: c = colours::ok; s = "Extended firmware"; break;
        case DeviceSession::Mode::Stock: c = colours::accent; s = "Stock firmware (CC/NRPN)"; break;
        case DeviceSession::Mode::Probing: c = colours::text; s = "Detecting..."; break;
        case DeviceSession::Mode::Disconnected: c = colours::warn; s = "Not connected"; break;
    }
    status_.setText(s, juce::dontSendNotification);
    status_.setColour(juce::Label::textColourId, c);
    status_.setTooltip(dev.describe());
    send_.setEnabled(dev.mode() == DeviceSession::Mode::Extended || dev.mode() == DeviceSession::Mode::Stock);
}

void KitForgeEditor::paint(juce::Graphics& g) { g.fillAll(colours::background); }

void KitForgeEditor::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop(44).reduced(8, 8);
    product_.setBounds(header.removeFromLeft(180));
    name_.setBounds(header.removeFromLeft(110));
    header.removeFromLeft(10);
    for (auto* b : { &undo_, &redo_ })
    {
        b->setBounds(header.removeFromLeft(54));
        header.removeFromLeft(4);
    }
    header.removeFromLeft(8);
    ab_.setBounds(header.removeFromLeft(32));
    header.removeFromLeft(4);
    copyAB_.setBounds(header.removeFromLeft(80));
    header.removeFromLeft(8);
    random_.setBounds(header.removeFromLeft(120));
    header.removeFromLeft(8);
    for (auto* b : { &open_, &save_, &addLib_ })
    {
        b->setBounds(header.removeFromLeft(92));
        header.removeFromLeft(4);
    }
    header.removeFromLeft(4);
    send_.setBounds(header.removeFromLeft(60));
    status_.setBounds(header);
    tabs_.setBounds(r);
}

bool KitForgeEditor::keyPressed(const juce::KeyPress& k)
{
    if (k == juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0))
    {
        proc_.undo();
        return true;
    }
    if (k == juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)
        || k == juce::KeyPress('y', juce::ModifierKeys::commandModifier, 0))
    {
        proc_.redo();
        return true;
    }
    return false;
}

void KitForgeEditor::randomizeKit()
{
    juce::PopupMenu m;
    m.addItem(1, "Subtle (10%)");
    m.addItem(2, "Moderate (30%)");
    m.addItem(3, "Wild (60%)");
    m.addItem(4, "Fully random");
    m.addSeparator();
    m.addItem(5, "Fully random, including FX and modulation targets");
    m.addSeparator();
    m.addItem(6, "Morph halfway towards the other A/B kit");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&random_), [this](int r) {
        if (r <= 0)
            return;
        Kit k = proc_.currentKit();
        if (r == 6)
        {
            Kit m2 = morphKits(k, proc_.otherKit(), 0.5);
            for (int p : proc_.locks())
                m2.set(p, k.get(p));
            m2.setName(k.name());
            proc_.setValues(m2, "Morphed towards the other kit");
            return;
        }
        static constexpr double amounts[] = { 0.0, 0.1, 0.3, 0.6, 1.0, 1.0 };
        RandomizeOptions o;
        o.amount = amounts[r];
        if (r == 5)
        {
            o.voices.insert(0);
            o.includeModTargets = true;
        }
        o.lockedParams = proc_.locks();
        kitforge::randomizeKit(k, o, rng_);
        proc_.setValues(k, {});
    });
}

void KitForgeEditor::loadKitFile(const juce::File& f)
{
    juce::MemoryBlock mb;
    if (!f.loadFileAsData(mb))
        return;
    const auto* d = static_cast<const uint8_t*>(mb.getData());
    std::string warning;
    if (f.getFileName().equalsIgnoreCase("tmp.snd"))
    {
        proc_.addLog("Cannot open tmp.snd as a named kit: it is a slot-prefixed autosave, not a normal .snd file");
        return;
    }
    const Kit k = Kit::fromSnd(Bytes(d, d + mb.getSize()), &warning);
    proc_.loadKit(k, KitForgeProcessor::LoadOrigin::User);
    proc_.addLog("Opened " + f.getFileName() + (warning.empty() ? juce::String() : " (" + juce::String(warning) + ")"));
}

void KitForgeEditor::openKit()
{
    chooser_ = std::make_unique<juce::FileChooser>("Open kit", juce::File(), "*.snd");
    chooser_->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this](const juce::FileChooser& fc) {
        if (fc.getResult().existsAsFile())
            loadKitFile(fc.getResult());
    });
}

void KitForgeEditor::saveKit()
{
    const auto name = proc_.kitName().trim();
    chooser_ = std::make_unique<juce::FileChooser>("Save kit", juce::File().getChildFile((name.isEmpty() ? "kit" : name) + ".snd"), "*.snd");
    chooser_->launchAsync(juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                          [this](const juce::FileChooser& fc) {
                              auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              f = f.withFileExtension("snd");
                              const auto b = proc_.currentKit().toSnd();
                              proc_.addLog(f.replaceWithData(b.data(), b.size()) ? "Saved " + f.getFullPathName()
                                                                                  : "Could not write " + f.getFullPathName());
                          });
}

bool KitForgeEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for (const auto& f : files)
        if (f.endsWithIgnoreCase(".snd") || f.endsWithIgnoreCase(".pat") || f.endsWithIgnoreCase(".sng") || f.endsWithIgnoreCase(".cfg")
            || juce::File(f).isDirectory())
            return true;
    return false;
}

void KitForgeEditor::filesDropped(const juce::StringArray& files, int, int)
{
    if (files.size() == 1 && files[0].endsWithIgnoreCase(".snd"))
    {
        loadKitFile(juce::File(files[0]));
        return;
    }
    auto* lib = proc_.library();
    if (!lib)
        return;
    int added = 0;
    for (const auto& path : files)
    {
        const juce::File f(path);
        if (f.isDirectory())
            added += lib->importFolder(path.toStdString()).added;
        else if (auto r = lib->importFile(path.toStdString()); r && !r->duplicate)
            ++added;
    }
    proc_.addLog("Imported " + juce::String(added) + " new item(s) into the library");
    if (library_)
        library_->refreshList();
}

} // namespace kitforge
