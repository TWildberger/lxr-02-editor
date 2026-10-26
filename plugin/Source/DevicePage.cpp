#include "DevicePage.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/Pattern.h"
#include "kitforge/SysEx.h"

namespace kitforge
{

DevicePage::DevicePage(KitForgeProcessor& p) : proc_(p)
{
    auto& dev = proc_.device();
    route_.addItem("Plugin MIDI output (route it to the device in your DAW)", 1);
    route_.addItem("Direct MIDI port (recommended for SysEx)", 2);
    route_.setSelectedId((int) dev.route() + 1, juce::dontSendNotification);
    route_.onChange = [this] {
        proc_.device().setRoute((DeviceSession::Route) (route_.getSelectedId() - 1));
        refreshStatus();
    };
    for (int c = 1; c <= 16; ++c)
        channel_.addItem(juce::String(c), c);
    channel_.setSelectedId(dev.channel(), juce::dontSendNotification);
    channel_.onChange = [this] { proc_.device().setChannel(channel_.getSelectedId()); };
    deviceId_.addItem("Any (7F)", 1);
    for (int i = 1; i <= 16; ++i)
        deviceId_.addItem("ID " + juce::String(i), i + 1);
    deviceId_.setSelectedId(dev.deviceId() + 1, juce::dontSendNotification);
    deviceId_.onChange = [this] { proc_.device().setDeviceId(deviceId_.getSelectedId() - 1); };
    rate_.addItem("DIN-safe (31.25 kbit/s)", 1);
    rate_.addItem("Fast (USB)", 2);
    rate_.setSelectedId((int) dev.rate() + 1, juce::dontSendNotification);
    rate_.onChange = [this] { proc_.device().setRate((DeviceSession::Rate) (rate_.getSelectedId() - 1)); };
    for (int s = 1; s <= ext::kNumSlots; ++s)
        slot_.addItem(juce::String(s), s);
    slot_.setSelectedId(1, juce::dontSendNotification);
    out_.onChange = [this] { applyPorts(); };
    in_.onChange = [this] { applyPorts(); };
    refreshPorts_.onClick = [this] { refreshPorts(); };

    extended_.setToggleState(dev.allowExtended(), juce::dontSendNotification);
    extended_.onClick = [this] { proc_.device().setAllowExtended(extended_.getToggleState()); };
    passThrough_.setToggleState(proc_.passThrough(), juce::dontSendNotification);
    passThrough_.onClick = [this] { proc_.setPassThrough(passThrough_.getToggleState()); };
    sendOnLoad_.setToggleState(proc_.sendOnLoad(), juce::dontSendNotification);
    sendOnLoad_.onClick = [this] { proc_.setSendOnLoad(sendOnLoad_.getToggleState()); };
    pullOnConnect_.setToggleState(proc_.pullOnConnect(), juce::dontSendNotification);
    pullOnConnect_.onClick = [this] { proc_.setPullOnConnect(pullOnConnect_.getToggleState()); };
    echo_.setToggleState(true, juce::dontSendNotification);
    echo_.onClick = [this] { proc_.device().setEcho(echo_.getToggleState()); };

    detect_.onClick = [this] { proc_.device().detect(); };
    sendKit_.onClick = [this] {
        if (proc_.device().isExtended())
            proc_.pushKitToDevice();
        else
        {
            proc_.pushKitToDevice(); // stock: resend every parameter over CC/NRPN
            proc_.addLog("Resending all parameters over CC/NRPN");
        }
    };
    getKit_.onClick = [this] { proc_.pullKitFromDevice(); };
    store_.onClick = [this] {
        auto& proc = proc_;
        const int s = slot();
        proc_.pushKitToDevice();
        proc_.device().storeKit(s, ext::kFlagSave, [&proc, s](bool ok, const juce::String& msg) {
            proc.addLog(ok ? "Stored kit in slot " + juce::String(s + 1) + " and saved it to the SD card" : "Store failed: " + msg);
        });
    };
    loadSlot_.onClick = [this] {
        auto& proc = proc_;
        const int s = slot();
        proc_.device().loadKitSlot(s, [&proc, s](bool ok, const juce::String& msg) {
            proc.addLog(ok ? "Loaded kit slot " + juce::String(s + 1) : "Loading slot failed: " + msg);
        });
    };
    getPattern_.onClick = [this] {
        auto& proc = proc_;
        const int s = slot();
        proc_.device().requestPattern(s, [&proc, s](std::optional<Bytes> raw, const juce::String& msg) {
            if (!raw)
            {
                proc.addLog("Pattern request failed: " + msg);
                return;
            }
            proc.post([&proc, s, data = ext::patFromPatternRaw(*raw)] {
                auto* lib = proc.library();
                if (!lib)
                    return;
                const auto pat = Pattern::fromPat(data);
                const std::string name = pat ? pat->name() : "pattern";
                if (auto r = lib->add(FileKind::Pattern, name, data, "device slot " + std::to_string(s + 1)))
                    proc.addLog(r->duplicate ? "Pattern " + juce::String(s + 1) + " is already in the library"
                                             : "Added pattern " + juce::String(s + 1) + " (\"" + juce::String(name) + "\") to the library");
            });
        });
    };
    saveDefaults_.onClick = [this] {
        proc_.saveDeviceSettings();
        proc_.addLog("Saved these settings as the default for new instances");
    };
    saveDefaults_.setTooltip("Use this MIDI setup for new plugin instances and the standalone app");

    int rateId = 1;
    for (const auto* rate : { "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32", "1/32T" })
        freezeRate_.addItem(rate, rateId++);
    freezeRate_.addItem("Use P1", 127);
    freezeRate_.setSelectedId(5, juce::dontSendNotification);
    freezeFx_.onClick = [this] {
        auto k = proc_.currentKit();
        k.set(pid::PAR_FX_TYPE, 6);
        proc_.loadKit(k, KitForgeProcessor::LoadOrigin::User);
    };
    freezeHold_.onStateChange = [this] {
        const bool down = freezeHold_.isDown();
        if (down == freezeDown_)
            return;
        freezeDown_ = down;
        if (down)
            proc_.device().sendFreeze(freezeRate_.getSelectedId());
        else
            proc_.device().releaseFreeze();
    };
    freezeRelease_.onClick = [this] { proc_.device().sendFreeze(0); };
    freezeHold_.setTooltip("Momentary audio freeze. Select frz FX first. NAKs/errors appear in the Device log.");
    addAndMakeVisible(freezeLabel_);
    addAndMakeVisible(freezeRate_);
    for (auto* b : { &freezeFx_, &freezeHold_, &freezeRelease_ })
        addAndMakeVisible(*b);

    for (auto* l : { &routeLabel_, &outLabel_, &inLabel_, &channelLabel_, &idLabel_, &rateLabel_, &slotLabel_ })
    {
        l->setColour(juce::Label::textColourId, colours::dim);
        addAndMakeVisible(*l);
    }
    for (auto* c : { &route_, &out_, &in_, &channel_, &deviceId_, &rate_, &slot_ })
        addAndMakeVisible(*c);
    for (auto* b : { &extended_, &passThrough_, &sendOnLoad_, &pullOnConnect_, &echo_ })
        addAndMakeVisible(*b);
    for (auto* b : { &refreshPorts_, &detect_, &sendKit_, &getKit_, &store_, &loadSlot_, &getPattern_, &saveDefaults_ })
        addAndMakeVisible(*b);
    status_.setFont(juce::FontOptions(15.0f, juce::Font::bold));
    problem_.setColour(juce::Label::textColourId, colours::warn);
    problem_.setFont(juce::FontOptions(12.5f));
    problem_.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(status_);
    addAndMakeVisible(problem_);
    log_.setMultiLine(true);
    log_.setReadOnly(true);
    log_.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::plain));
    addAndMakeVisible(log_);

    refreshPorts();
    refreshStatus();
    proc_.deviceChanged.addChangeListener(this);
}

DevicePage::~DevicePage()
{
    proc_.device().releaseFreeze();
    proc_.deviceChanged.removeChangeListener(this);
}

void DevicePage::visibilityChanged()
{
    if (!isShowing())
    {
        freezeDown_ = false;
        proc_.device().releaseFreeze();
    }
}

void DevicePage::changeListenerCallback(juce::ChangeBroadcaster*) { refreshStatus(); }

void DevicePage::refreshPorts()
{
    auto fill = [](juce::ComboBox& box, juce::StringArray& ids, const juce::Array<juce::MidiDeviceInfo>& devices, const juce::String& current) {
        box.clear(juce::dontSendNotification);
        ids.clear();
        box.addItem("(none)", 1);
        ids.add({});
        int sel = 1;
        for (const auto& d : devices)
        {
            ids.add(d.identifier);
            box.addItem(d.name, ids.size());
            if (d.identifier == current)
                sel = ids.size();
        }
        if (sel == 1 && current.isNotEmpty())
        {
            ids.add(current);
            box.addItem("(unavailable) " + current, ids.size());
            sel = ids.size();
        }
        box.setSelectedId(sel, juce::dontSendNotification);
    };
    fill(out_, outIds_, juce::MidiOutput::getAvailableDevices(), proc_.device().directOutputId());
    fill(in_, inIds_, juce::MidiInput::getAvailableDevices(), proc_.device().directInputId());
}

void DevicePage::applyPorts()
{
    const auto o = outIds_[out_.getSelectedId() - 1];
    const auto i = inIds_[in_.getSelectedId() - 1];
    proc_.device().setDirectPorts(o, i);
    refreshStatus();
}

void DevicePage::refreshStatus()
{
    auto& dev = proc_.device();
    const bool direct = dev.route() == DeviceSession::Route::DirectPort;
    for (juce::Component* c : { (juce::Component*) &out_, (juce::Component*) &in_, (juce::Component*) &refreshPorts_ })
        c->setEnabled(direct);
    juce::Colour colour = colours::dim;
    switch (dev.mode())
    {
        case DeviceSession::Mode::Extended: colour = colours::ok; break;
        case DeviceSession::Mode::Stock: colour = colours::accent; break;
        case DeviceSession::Mode::Probing: colour = colours::text; break;
        case DeviceSession::Mode::Disconnected: colour = colours::warn; break;
    }
    status_.setColour(juce::Label::textColourId, colour);
    status_.setText(dev.describe(), juce::dontSendNotification);
    problem_.setText(proc_.lastProblem(), juce::dontSendNotification);
    const bool ext = dev.isExtended();
    for (auto* b : { &getKit_, &store_, &getPattern_ })
        b->setEnabled(ext);
    echo_.setEnabled(ext && (dev.identity().capabilities & ext::CapEcho));
    const bool freeze = dev.supportsFreeze();
    freezeLabel_.setText(freeze ? "Audio freeze / beat repeat" : "Freeze unavailable: needs firmware capability bit 8",
                         juce::dontSendNotification);
    freezeRate_.setEnabled(freeze);
    for (auto* b : { &freezeFx_, &freezeHold_, &freezeRelease_ })
        b->setEnabled(freeze);
    const bool live = dev.mode() == DeviceSession::Mode::Stock || ext;
    sendKit_.setEnabled(live);
    loadSlot_.setEnabled(live);
    log_.setText(proc_.logLines().joinIntoString("\n"), false);
    log_.moveCaretToEnd();
}

void DevicePage::resized()
{
    auto r = getLocalBounds().reduced(10);
    auto left = r.removeFromLeft(juce::jmin(560, r.getWidth() / 2));
    r.removeFromLeft(10);

    auto row = [&left](juce::Label& l, juce::Component& c, juce::Component* extra = nullptr) {
        auto line = left.removeFromTop(26);
        l.setBounds(line.removeFromLeft(100));
        if (extra)
        {
            extra->setBounds(line.removeFromRight(70));
            line.removeFromRight(4);
        }
        c.setBounds(line);
        left.removeFromTop(4);
    };
    row(routeLabel_, route_);
    row(outLabel_, out_, &refreshPorts_);
    row(inLabel_, in_);
    row(channelLabel_, channel_);
    row(idLabel_, deviceId_);
    row(rateLabel_, rate_);
    left.removeFromTop(4);
    for (auto* b : { &extended_, &passThrough_, &sendOnLoad_, &pullOnConnect_, &echo_ })
    {
        b->setBounds(left.removeFromTop(24));
        left.removeFromTop(2);
    }
    left.removeFromTop(8);
    auto buttons = left.removeFromTop(28);
    for (auto* b : { &detect_, &sendKit_, &getKit_, &saveDefaults_ })
    {
        b->setBounds(buttons.removeFromLeft(110));
        buttons.removeFromLeft(6);
    }
    left.removeFromTop(6);
    auto slotRow = left.removeFromTop(28);
    slotLabel_.setBounds(slotRow.removeFromLeft(40));
    slot_.setBounds(slotRow.removeFromLeft(70));
    slotRow.removeFromLeft(6);
    for (auto* b : { &store_, &loadSlot_, &getPattern_ })
    {
        b->setBounds(slotRow.removeFromLeft(130));
        slotRow.removeFromLeft(6);
    }
    left.removeFromTop(8);
    freezeLabel_.setBounds(left.removeFromTop(24));
    auto freezeRow = left.removeFromTop(28);
    freezeRate_.setBounds(freezeRow.removeFromLeft(100));
    for (auto* b : { &freezeFx_, &freezeHold_, &freezeRelease_ })
    {
        freezeRow.removeFromLeft(6);
        b->setBounds(freezeRow.removeFromLeft(110));
    }

    status_.setBounds(r.removeFromTop(28));
    problem_.setBounds(r.removeFromTop(48));
    log_.setBounds(r);
}

} // namespace kitforge
