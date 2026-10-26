#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

namespace
{
constexpr size_t kMaxUndo = 200;
constexpr int kMaxLogLines = 200;

juce::String toBase64(const Bytes& b)
{
    return juce::Base64::toBase64(b.data(), b.size());
}

Bytes fromBase64(const juce::String& s)
{
    juce::MemoryOutputStream out;
    if (!juce::Base64::convertFromBase64(out, s))
        return {};
    const auto* p = static_cast<const uint8_t*>(out.getData());
    return Bytes(p, p + out.getDataSize());
}

std::unique_ptr<juce::PropertiesFile> openSettings()
{
    juce::PropertiesFile::Options o;
    o.applicationName = "KitForge";
    o.filenameSuffix = ".settings";
    o.folderName = "KitForge";
    o.osxLibrarySubFolder = "Application Support";
    return std::make_unique<juce::PropertiesFile>(o);
}

std::unordered_map<std::string, int>& keyIndex()
{
    static std::unordered_map<std::string, int> m = [] {
        std::unordered_map<std::string, int> r;
        for (int i = 0; i < kNumSoundParams; ++i)
            r[ParamTable::get(i).key] = i;
        return r;
    }();
    return m;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout KitForgeProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    for (int i : ParamTable::soundParamsInOrder())
    {
        const auto& p = ParamTable::get(i);
        auto attrs = juce::AudioParameterIntAttributes()
                         .withStringFromValueFunction([i](int v, int) { return juce::String(ParamTable::formatValue(i, v)); })
                         .withValueFromStringFunction([i](const juce::String& text) {
                             const auto& info = ParamTable::get(i);
                             for (int v = info.minValue; v <= info.maxValue; ++v)
                                 if (text.trim().equalsIgnoreCase(juce::String(ParamTable::formatValue(i, v))))
                                     return v;
                             return juce::jlimit(info.minValue, info.maxValue, text.getIntValue());
                         })
                         .withLabel(p.section ? p.section : "");
        layout.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID { p.key, 1 }, ParamTable::fullName(i), p.minValue,
                                                             p.maxValue, ParamTable::clamp(i, p.defaultValue), attrs));
    }
    return layout;
}

KitForgeProcessor::KitForgeProcessor()
    : juce::AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts_(*this, nullptr, "KitForge", createLayout())
{
    keyIndex();
    const Kit init = Kit::init();
    for (int i = 0; i < kNumSoundParams; ++i)
    {
        raw_[(size_t) i].store(init.get(i));
        const auto& p = ParamTable::get(i);
        if (auto* param = apvts_.getParameter(p.key))
        {
            params_[(size_t) i] = param;
            apvts_.addParameterListener(p.key, this);
        }
    }
    name_ = init.rawName();
    addListener(this);
    sync_.setTargets(init);

    const bool standalone = wrapperType == wrapperType_Standalone;
    pullOnConnect_ = standalone;
    device_ = std::make_unique<DeviceSession>(sync_, static_cast<DeviceSession::Listener&>(*this));

    juce::ValueTree defaults("Device");
    if (auto settings = openSettings())
        if (auto xml = settings->getXmlValue("device"))
            defaults = juce::ValueTree::fromXml(*xml);
    if (!defaults.hasProperty("route"))
        defaults.setProperty("route", standalone ? (int) DeviceSession::Route::DirectPort : (int) DeviceSession::Route::PluginMidi, nullptr);
    applyDeviceSettings(defaults);
    startTimerHz(30);
}

KitForgeProcessor::~KitForgeProcessor()
{
    stopTimer();
    removeListener(this);
    device_.reset();
    for (int i = 0; i < kNumSoundParams; ++i)
        if (params_[(size_t) i])
            apvts_.removeParameterListener(ParamTable::get(i).key, this);
}

bool KitForgeProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled();
}

void KitForgeProcessor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    juce::MidiBuffer out;
    if (passThrough_.load())
        for (const auto meta : midi)
            if (meta.numBytes > 0 && meta.data[0] < 0xF0)
                out.addEvent(meta.data, meta.numBytes, meta.samplePosition);
    device_->processBlock(midi, out);
    midi.swapWith(out);
}

juce::AudioProcessorEditor* KitForgeProcessor::createEditor() { return new KitForgeEditor(*this); }

// ---------------------------------------------------------------- kit model

juce::RangedAudioParameter* KitForgeProcessor::parameterFor(int param) const
{
    return param >= 0 && param < kNumSoundParams ? params_[(size_t) param] : nullptr;
}

Kit KitForgeProcessor::currentKit() const
{
    Kit k;
    for (int i = 0; i < kNumSoundParams; ++i)
        k.set(i, raw_[(size_t) i].load());
    std::lock_guard<std::mutex> g(nameLock_);
    Bytes snd = k.toSnd();
    std::copy(name_.begin(), name_.end(), snd.begin());
    snd.insert(snd.end(), extension_.begin(), extension_.end());
    return Kit::fromSnd(snd);
}

juce::String KitForgeProcessor::kitName() const { return juce::String(currentKit().name()); }

void KitForgeProcessor::setKitName(const juce::String& name)
{
    pushUndo();
    Kit k;
    k.setName(name.toStdString());
    {
        std::lock_guard<std::mutex> g(nameLock_);
        name_ = k.rawName();
    }
    kitChanged.sendChangeMessage();
}

void KitForgeProcessor::parameterChanged(const juce::String& id, float value)
{
    const auto it = keyIndex().find(id.toStdString());
    if (it == keyIndex().end())
        return;
    const int i = it->second;
    const int v = juce::roundToInt(value);
    auto& raw = raw_[(size_t) i];
    // keep out-of-range bytes from kits (the parameter can only show the clamped value)
    if (ParamTable::clamp(i, raw.load()) != v)
        raw.store(v);
    sync_.setTarget(i, raw.load());
}

void KitForgeProcessor::setValue(int param, int value)
{
    auto* p = parameterFor(param);
    if (!p)
        return;
    pushUndo();
    raw_[(size_t) param].store(value);
    sync_.setTarget(param, value);
    p->beginChangeGesture();
    p->setValueNotifyingHost(p->convertTo0to1((float) ParamTable::clamp(param, value)));
    p->endChangeGesture();
}

void KitForgeProcessor::applyKitToParameters(const Kit& kit)
{
    for (int i = 0; i < kNumSoundParams; ++i)
        raw_[(size_t) i].store(kit.get(i));
    {
        std::lock_guard<std::mutex> g(nameLock_);
        name_ = kit.rawName();
        extension_ = kit.extension();
    }
    for (int i = 0; i < kNumSoundParams; ++i)
        if (auto* p = params_[(size_t) i])
        {
            const float norm = p->convertTo0to1((float) ParamTable::clamp(i, kit.get(i)));
            if (std::abs(p->getValue() - norm) > 1.0e-6f)
                p->setValueNotifyingHost(norm);
        }
    sync_.setTargets(kit);
    kitChanged.sendChangeMessage();
}

void KitForgeProcessor::setValues(const Kit& kit, const juce::String& undoLabel)
{
    const auto before = currentKit();
    pushUndo();
    applyKitToParameters(kit);
    // Engine controls are deliberately not legacy host parameters. Send the versioned container.
    if (before.extension() != kit.extension()
        || !std::equal(before.params().begin() + Kit::kEngineFirst,
                       before.params().begin() + Kit::kTimbreDecayFirst + 3,
                       kit.params().begin() + Kit::kEngineFirst))
        if (device_->isExtended())
            pushKitToDevice();
    if (undoLabel.isNotEmpty())
        addLog(undoLabel);
}

void KitForgeProcessor::loadKit(const Kit& kit, LoadOrigin origin)
{
    const auto before = currentKit();
    if (origin == LoadOrigin::User || origin == LoadOrigin::Device)
        pushUndo();
    applyKitToParameters(kit);
    switch (origin)
    {
        case LoadOrigin::Device:
            sync_.markInSync(kit);
            break;
        case LoadOrigin::User:
            if (device_->isExtended())
                pushKitToDevice();
            break;
        case LoadOrigin::StateRecall:
            if (sendOnLoad_)
                pushKitToDevice();
            else
                sync_.markInSync(kit);
            break;
        case LoadOrigin::History:
            if (before.extension() != kit.extension()
                || !std::equal(before.params().begin() + Kit::kEngineFirst,
                               before.params().begin() + Kit::kTimbreDecayFirst + 3,
                               kit.params().begin() + Kit::kEngineFirst))
                if (device_->isExtended())
                    pushKitToDevice();
            break; // other differences are streamed as parameter changes
    }
    historyChanged.sendChangeMessage();
}

void KitForgeProcessor::pushKitToDevice()
{
    if (!device_->isExtended())
    {
        sync_.invalidateDevice();
        return;
    }
    const auto kit = currentKit();
    sync_.markInSync(kit);
    device_->sendKit(ext::kEditBuffer, kit, 0, [this](bool ok, const juce::String& msg) {
        if (!ok)
            post([this, msg] {
                addLog("Kit dump failed (" + msg + "), resending parameters individually");
                sync_.invalidateDevice();
            });
    });
}

void KitForgeProcessor::pullKitFromDevice()
{
    device_->requestKit(ext::kEditBuffer, [this](std::optional<Kit> kit, const juce::String& msg) {
        post([this, kit, msg] {
            if (kit)
            {
                loadKit(*kit, LoadOrigin::Device);
                addLog("Received kit from device");
            }
            else
                addLog("Kit request failed: " + msg);
        });
    });
    fetchGlobals();
}

void KitForgeProcessor::fetchGlobals()
{
    device_->requestGlobals([this](std::optional<Globals> g, const juce::String& msg) {
        post([this, g, msg] {
            if (g)
                loadGlobals(*g, true);
            else
                addLog("Globals request failed: " + msg);
        });
    });
}

void KitForgeProcessor::pushGlobals(bool save)
{
    device_->sendGlobals(globals_, save ? ext::kFlagSave : 0, [this, save](bool ok, const juce::String& msg) {
        addLog(ok ? juce::String(save ? "Globals sent and saved to SD" : "Globals sent") : "Sending globals failed: " + msg);
    });
}

// ---------------------------------------------------------------- history

void KitForgeProcessor::audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int) { pushUndo(); }

void KitForgeProcessor::pushUndo()
{
    const auto k = currentKit();
    {
        std::lock_guard<std::mutex> g(historyLock_);
        if (!undo_.empty() && undo_.back() == k)
            return;
        undo_.push_back(k);
        if (undo_.size() > kMaxUndo)
            undo_.erase(undo_.begin());
        redo_.clear();
    }
    historyChanged.sendChangeMessage();
}

bool KitForgeProcessor::canUndo() const
{
    std::lock_guard<std::mutex> g(historyLock_);
    return !undo_.empty();
}

bool KitForgeProcessor::canRedo() const
{
    std::lock_guard<std::mutex> g(historyLock_);
    return !redo_.empty();
}

void KitForgeProcessor::undo()
{
    const auto cur = currentKit();
    std::optional<Kit> prev;
    {
        std::lock_guard<std::mutex> g(historyLock_);
        while (!undo_.empty() && undo_.back() == cur)
            undo_.pop_back();
        if (undo_.empty())
            return;
        prev = undo_.back();
        undo_.pop_back();
        redo_.push_back(cur);
    }
    loadKit(*prev, LoadOrigin::History);
}

void KitForgeProcessor::redo()
{
    const auto cur = currentKit();
    std::optional<Kit> next;
    {
        std::lock_guard<std::mutex> g(historyLock_);
        if (redo_.empty())
            return;
        next = redo_.back();
        redo_.pop_back();
        undo_.push_back(cur);
    }
    loadKit(*next, LoadOrigin::History);
}

void KitForgeProcessor::toggleAB()
{
    const auto cur = currentKit();
    const auto o = other_;
    other_ = cur;
    isB_ = !isB_;
    loadKit(o, LoadOrigin::History);
}

void KitForgeProcessor::copyCurrentToOther()
{
    other_ = currentKit();
    addLog(juce::String("Copied ") + (isB_ ? "B to A" : "A to B"));
    historyChanged.sendChangeMessage();
}

void KitForgeProcessor::setLocked(int param, bool locked)
{
    if (locked)
        locks_.insert(param);
    else
        locks_.erase(param);
    kitChanged.sendChangeMessage();
}

// ---------------------------------------------------------------- globals

void KitForgeProcessor::setGlobal(int param, int value)
{
    globals_.set(param, value);
    sync_.setTarget(param, globals_.get(param));
}

void KitForgeProcessor::loadGlobals(const Globals& g, bool fromDevice)
{
    globals_ = g;
    if (fromDevice)
        sync_.markInSync(g);
    else
        sync_.setTargets(g);
    kitChanged.sendChangeMessage();
}

// ---------------------------------------------------------------- device callbacks

void KitForgeProcessor::post(std::function<void()> fn)
{
    std::lock_guard<std::mutex> g(inboxLock_);
    inbox_.push_back(std::move(fn));
}

void KitForgeProcessor::timerCallback()
{
    std::vector<std::function<void()>> work;
    {
        std::lock_guard<std::mutex> g(inboxLock_);
        work.swap(inbox_);
    }
    for (auto& fn : work)
        fn();
}

void KitForgeProcessor::deviceModeChanged(DeviceSession::Mode mode, const ext::Identity&)
{
    post([this, mode] {
        addLog("Device: " + device_->describe());
        reportedUnsendable_.clear();
        if (mode == DeviceSession::Mode::Extended)
        {
            if (pullOnConnect_)
                pullKitFromDevice();
            else
            {
                if (sendOnLoad_)
                    pushKitToDevice();
                else
                    sync_.markInSync(currentKit());
                fetchGlobals();
            }
        }
        else if (mode == DeviceSession::Mode::Stock)
        {
            if (sendOnLoad_)
                sync_.invalidateDevice();
            else
                sync_.markInSync(currentKit());
        }
        deviceChanged.sendChangeMessage();
    });
}

void KitForgeProcessor::deviceKitReceived(int, const Kit& kit)
{
    post([this, kit] { loadKit(kit, LoadOrigin::Device); });
}

void KitForgeProcessor::deviceGlobalsReceived(const Globals& g)
{
    post([this, g] { loadGlobals(g, true); });
}

void KitForgeProcessor::deviceParamsChanged(const std::vector<ext::ParamPair>& pairs)
{
    post([this, pairs] {
        bool globalsChanged = false;
        bool engineChanged = false;
        for (const auto& pr : pairs)
        {
            if (pr.id < 0 || pr.id >= kNumParams)
                continue;
            sync_.noteDeviceValue(pr.id, pr.value);
            if (pr.id < kNumSoundParams)
            {
                engineChanged |= pr.id >= Kit::kEngineFirst && pr.id < Kit::kTimbreDecayFirst + 3;
                raw_[(size_t) pr.id].store(pr.value);
                if (auto* p = params_[(size_t) pr.id])
                    p->setValueNotifyingHost(p->convertTo0to1((float) ParamTable::clamp(pr.id, pr.value)));
            }
            else if (pr.id >= kFirstGlobal)
            {
                globals_.set(pr.id, pr.value);
                globalsChanged = true;
            }
        }
        if (engineChanged && device_->identity().has(ext::CapEngineTrailer))
            pullKitFromDevice(); // only a container readback can establish the engine version tag
        if (globalsChanged || engineChanged)
            kitChanged.sendChangeMessage();
    });
}

void KitForgeProcessor::deviceStatus(const ext::Status& s)
{
    post([this, s] { addLog("Device status: kit " + juce::String(s.kitSlot + 1) + ", pattern " + juce::String(s.pattern + 1)
                            + (s.running ? ", running" : ", stopped")); });
}

void KitForgeProcessor::deviceKitChanged()
{
    post([this] {
        addLog("Kit changed on the device");
        pullKitFromDevice();
    });
}

void KitForgeProcessor::deviceLog(const juce::String& s)
{
    post([this, s] { addLog(s); });
}

void KitForgeProcessor::deviceUnsendable(const std::vector<int>& params)
{
    post([this, params] {
        juce::StringArray names;
        for (int p : params)
            if (reportedUnsendable_.insert(p).second)
                names.add(juce::String(ParamTable::fullName(p)) + " ("
                          + juce::String(StockMidiEncoder::canSend(p, raw_[(size_t) p].load()).reason) + ")");
        if (names.isEmpty())
            return;
        std::lock_guard<std::mutex> g(logLock_);
        lastProblem_ = "Stock firmware cannot receive: " + names.joinIntoString(", ") + ". Save the kit as .snd to transfer it.";
        log_.add(lastProblem_);
        deviceChanged.sendChangeMessage();
    });
}

void KitForgeProcessor::addLog(const juce::String& s)
{
    {
        std::lock_guard<std::mutex> g(logLock_);
        log_.add(juce::Time::getCurrentTime().toString(false, true, true, true) + "  " + s);
        if (log_.size() > kMaxLogLines)
            log_.removeRange(0, log_.size() - kMaxLogLines);
    }
    deviceChanged.sendChangeMessage();
}

juce::StringArray KitForgeProcessor::logLines() const
{
    std::lock_guard<std::mutex> g(logLock_);
    return log_;
}

juce::String KitForgeProcessor::lastProblem() const
{
    std::lock_guard<std::mutex> g(logLock_);
    return lastProblem_;
}

// ---------------------------------------------------------------- settings & state

juce::ValueTree KitForgeProcessor::deviceSettings() const
{
    juce::ValueTree v("Device");
    v.setProperty("route", (int) device_->route(), nullptr);
    v.setProperty("out", device_->directOutputId(), nullptr);
    v.setProperty("in", device_->directInputId(), nullptr);
    v.setProperty("channel", device_->channel(), nullptr);
    v.setProperty("deviceId", device_->deviceId(), nullptr);
    v.setProperty("rate", (int) device_->rate(), nullptr);
    v.setProperty("extended", device_->allowExtended(), nullptr);
    v.setProperty("passThrough", passThrough_.load(), nullptr);
    v.setProperty("sendOnLoad", sendOnLoad_, nullptr);
    v.setProperty("pullOnConnect", pullOnConnect_, nullptr);
    return v;
}

void KitForgeProcessor::applyDeviceSettings(const juce::ValueTree& v)
{
    device_->setChannel(v.getProperty("channel", 1));
    device_->setDeviceId(v.getProperty("deviceId", 0));
    device_->setRate((DeviceSession::Rate) (int) v.getProperty("rate", 0));
    device_->setAllowExtended(v.getProperty("extended", true));
    passThrough_.store(v.getProperty("passThrough", true));
    sendOnLoad_ = v.getProperty("sendOnLoad", true);
    pullOnConnect_ = v.getProperty("pullOnConnect", pullOnConnect_);
    device_->setDirectPorts(v.getProperty("out").toString(), v.getProperty("in").toString());
    device_->setRoute((DeviceSession::Route) (int) v.getProperty("route", 0));
}

void KitForgeProcessor::saveDeviceSettings()
{
    if (auto settings = openSettings())
        if (auto xml = deviceSettings().createXml())
        {
            settings->setValue("device", xml.get());
            settings->saveIfNeeded();
        }
}

void KitForgeProcessor::getStateInformation(juce::MemoryBlock& dest)
{
    juce::ValueTree v("KitForgeState");
    v.setProperty("version", 1, nullptr);
    v.setProperty("kit", toBase64(currentKit().toSnd()), nullptr);
    v.setProperty("globals", toBase64(globals_.toCfg()), nullptr);
    v.setProperty("other", toBase64(other_.toSnd()), nullptr);
    v.setProperty("isB", isB_, nullptr);
    juce::StringArray locks;
    for (int p : locks_)
        locks.add(ParamTable::get(p).key);
    v.setProperty("locks", locks.joinIntoString(","), nullptr);
    v.appendChild(deviceSettings(), nullptr);
    if (auto xml = v.createXml())
        copyXmlToBinary(*xml, dest);
}

void KitForgeProcessor::setStateInformation(const void* data, int size)
{
    auto xml = getXmlFromBinary(data, size);
    if (!xml)
        return;
    auto v = juce::ValueTree::fromXml(*xml);
    if (!v.hasType("KitForgeState"))
        return;
    auto apply = [this, v] {
        const auto kitBytes = fromBase64(v.getProperty("kit").toString());
        if (kitBytes.size() >= (size_t) Kit::kNameBytes)
        {
            {
                std::lock_guard<std::mutex> g(historyLock_);
                undo_.clear();
                redo_.clear();
            }
            const auto otherBytes = fromBase64(v.getProperty("other").toString());
            other_ = otherBytes.empty() ? Kit::init() : Kit::fromSnd(otherBytes);
            isB_ = v.getProperty("isB", false);
            locks_.clear();
            for (const auto& key : juce::StringArray::fromTokens(v.getProperty("locks").toString(), ",", ""))
                if (const auto* p = ParamTable::findByKey(key.toStdString()))
                    locks_.insert(p->index);
            if (const auto glo = fromBase64(v.getProperty("globals").toString()); !glo.empty())
                globals_ = Globals::fromCfg(glo);
            const auto dev = v.getChildWithName("Device");
            if (dev.isValid())
                applyDeviceSettings(dev);
            loadKit(Kit::fromSnd(kitBytes), LoadOrigin::StateRecall);
        }
    };
    if (juce::MessageManager::getInstance()->isThisTheMessageThread())
        apply();
    else
        post(apply);
}

// ---------------------------------------------------------------- library

juce::String KitForgeProcessor::libraryPath() const
{
    if (const auto env = juce::SystemStats::getEnvironmentVariable("KITFORGE_LIBRARY_PATH", {}); env.isNotEmpty())
        return env;
    return juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile(juce::SystemStats::getOperatingSystemType() & juce::SystemStats::MacOSX ? "Application Support/KitForge" : "KitForge")
        .getChildFile("library.sqlite")
        .getFullPathName();
}

Librarian* KitForgeProcessor::library()
{
    if (!library_ && !libraryTried_)
    {
        libraryTried_ = true;
        const juce::File f(libraryPath());
        f.getParentDirectory().createDirectory();
        std::string err;
        library_ = Librarian::open(f.getFullPathName().toStdString(), &err);
        if (!library_)
            addLog("Could not open library " + f.getFullPathName() + ": " + juce::String(err));
    }
    return library_.get();
}

} // namespace kitforge

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new kitforge::KitForgeProcessor(); }
