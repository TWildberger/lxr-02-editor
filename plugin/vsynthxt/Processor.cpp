#include "Processor.h"
#include "Editor.h"

namespace vsynthxt
{
juce::AudioProcessorValueTreeState::ParameterLayout Processor::layout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout l;
    for (const auto& p : parameters())
    {
        const auto name = p.zone ? "Z" + juce::String(p.zone) + " " + p.section + " " + p.label
                                 : juce::String(p.section) + " " + p.label;
        const int low = p.minimum - p.bias, high = p.maximum - p.bias;
        l.add(std::make_unique<juce::AudioParameterInt>(juce::ParameterID(p.key, 1), name,
            low, high, juce::jlimit(low, high, 0)));
    }
    return l;
}
Processor::Processor() : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
    state(*this, nullptr, "VSynthXTParameters", layout())
{
    dirty_ = std::make_unique<std::atomic<bool>[]>(parameters().size());
    for (size_t i = 0; i < parameters().size(); ++i)
    {
        const auto key = parameters()[i].key;
        parameterIndices_.emplace(key, i);
        dirty_[i].store(false);
        state.addParameterListener(key, this);
    }
    startTimerHz(30);
}
Processor::~Processor()
{
    stopTimer();
    for (const auto& p : parameters()) state.removeParameterListener(p.key, this);
}
bool Processor::isBusesLayoutSupported(const BusesLayout& l) const
{
    const auto out = l.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono() || out.isDisabled();
}
void Processor::processBlock(juce::AudioBuffer<float>& audio, juce::MidiBuffer& midi)
{
    audio.clear();
    session.process(midi);
}
juce::AudioProcessorEditor* Processor::createEditor() { return new Editor(*this); }
Patch Processor::patch() const { std::lock_guard<std::recursive_mutex> g(patchMutex_); return patch_; }
void Processor::mirror()
{
    const auto p = patch();
    if (!p.complete()) return;
    mirroring_.store(true);
    juce::String invalid;
    for (size_t i = 0; i < parameters().size(); ++i)
    {
        const auto& info = parameters()[i];
        auto* param = state.getParameter(info.key);
        try
        {
            const auto v = param->convertTo0to1(static_cast<float>(parameterValue(p, info)));
            if (!juce::approximatelyEqual(param->getValue(), v)) param->setValueNotifyingHost(v);
        }
        catch (const std::exception& e) { if (invalid.isEmpty()) invalid = e.what(); }
        dirty_[i].store(false);
    }
    mirroring_.store(false);
    if (invalid.isNotEmpty()) report(invalid + " (raw bytes retained; affected controls disabled)");
}
void Processor::load(const Patch& p, bool history)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    if (!p.complete()) throw std::runtime_error("Cannot load an incomplete XT patch");
    {
        if (history && patch_.complete())
        {
            if (undo_.size() == 128) undo_.erase(undo_.begin());
            undo_.push_back(patch_); redo_.clear();
        }
        patch_ = p;
    }
    mirror(); changed.sendChangeMessage();
}
void Processor::rename(const juce::String& name)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    flushEdits();
    auto p = patch(); p.setName(name.toStdString()); load(p);
}
void Processor::parameterChanged(const juce::String& id, float)
{
    if (mirroring_.load()) return;
    const auto it = parameterIndices_.find(id.toStdString());
    if (it != parameterIndices_.end()) dirty_[it->second].store(true);
}
void Processor::flushEdits()
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    auto p = patch();
    bool edited = false;
    for (size_t i = 0; i < parameters().size(); ++i)
        if (dirty_[i].exchange(false))
        {
            edited = true;
            if (p.complete())
            {
                const auto& info = parameters()[i];
                vsynthxt::setParameter(p, info, juce::roundToInt(state.getRawParameterValue(info.key)->load()));
            }
        }
    if (!edited) return;
    if (!p.complete()) { report("Automation changed locally, but no complete patch is loaded. Fetch/import first."); return; }
    if (!(p == patch())) load(p);
}
void Processor::timerCallback()
{
    try
    {
        flushEdits();
        if (auto result = session.takeResult())
        {
            std::lock_guard<std::recursive_mutex> lock(patchMutex_);
            if (transferBaseline_ && !(*transferBaseline_ == patch_))
                report("Transfer finished, but local edits changed during it. Local edits were retained; Fetch again to refresh.");
            else
                load(*result);
            transferBaseline_.reset();
        }
    }
    catch (const std::exception& e) { report(e.what()); }
}
void Processor::undo()
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    flushEdits();
    if (undo_.empty()) { report("Nothing to undo."); return; }
    const auto p = undo_.back(); undo_.pop_back(); redo_.push_back(patch()); load(p, false);
}
void Processor::redo()
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    flushEdits();
    if (redo_.empty()) { report("Nothing to redo."); return; }
    const auto p = redo_.back(); redo_.pop_back(); undo_.push_back(patch()); load(p, false);
}
void Processor::compare()
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    flushEdits();
    if (!other_.complete()) { report("Copy the current patch to the other A/B buffer first."); return; }
    auto current = patch(); load(other_); other_ = std::move(current); isB_ = !isB_;
}
void Processor::copyAB()
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    flushEdits(); other_ = patch(); report("Current patch copied to the other A/B buffer (local only).");
}
void Processor::fetch(bool native)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    if (session.busy()) { report("Transfer already running; cancel before Fetch."); return; }
    flushEdits(); transferBaseline_ = patch_;
    session.start(false, patch_, native);
}
void Processor::send(bool native)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    if (session.busy()) { report("Transfer already running; cancel before Send."); return; }
    flushEdits(); transferBaseline_ = patch_;
    session.start(true, patch_, native);
}
Library& Processor::library()
{
    if (!library_)
    {
        const auto parent = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
            .getChildFile("Application Support/VSynthXTCompanion");
        const auto override = juce::SystemStats::getEnvironmentVariable("VSYNTHXT_LIBRARY_PATH", {});
        const auto file = override.isNotEmpty() ? juce::File(override) : parent.getChildFile("library.sqlite");
        const auto result = file.getParentDirectory().createDirectory();
        if (result.failed()) throw std::runtime_error(result.getErrorMessage().toStdString());
        library_ = std::make_unique<Library>(file.getFullPathName().toStdString());
    }
    return *library_;
}
void Processor::getStateInformation(juce::MemoryBlock& dest)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    juce::ValueTree root("VSynthXTState");
    root.setProperty("version", 1, nullptr);
    auto p = patch();
    // Include automation changes even if the message-thread timer has not run yet.
    if (p.complete())
    {
        for (size_t i = 0; i < parameters().size(); ++i)
            if (dirty_[i].load())
            {
                const auto& info = parameters()[i];
                vsynthxt::setParameter(p, info, juce::roundToInt(state.getRawParameterValue(info.key)->load()));
            }
        auto b = p.sysex(); root.setProperty("patch", juce::MemoryBlock(b.data(), b.size()).toBase64Encoding(), nullptr);
    }
    if (other_.complete())
    {
        const auto b = other_.sysex();
        root.setProperty("other", juce::MemoryBlock(b.data(), b.size()).toBase64Encoding(), nullptr);
    }
    root.setProperty("isB", isB_, nullptr);
    root.setProperty("tab", selectedTab.load(), nullptr);
    root.setProperty("zone", selectedZone.load(), nullptr);
    root.setProperty("nativeGroup", selectedNativeGroup.load(), nullptr);
    root.setProperty("nativeSection", selectedNativeSection.load(), nullptr);
    if (auto xml = root.createXml()) copyXmlToBinary(*xml, dest);
}
void Processor::setStateInformation(const void* bytes, int size)
{
    std::lock_guard<std::recursive_mutex> lock(patchMutex_);
    session.cancel(); // Invalidates queued host writes; restored state never configures or sends MIDI.
    try
    {
        auto xml = getXmlFromBinary(bytes, size);
        if (!xml) throw std::runtime_error("Invalid XT state XML");
        auto root = juce::ValueTree::fromXml(*xml);
        if (!root.hasType("VSynthXTState") || int(root.getProperty("version", 0)) != 1)
            throw std::runtime_error("Unsupported XT state version/type");
        const auto read = [&](const char* key) {
            juce::MemoryBlock b;
            if (!b.fromBase64Encoding(root.getProperty(key).toString())) throw std::runtime_error("Invalid XT state encoding");
            const auto* p = static_cast<const uint8_t*>(b.getData());
            return Patch::fromSysex(Bytes(p, p + b.getSize()));
        };
        Patch p, other;
        if (root.hasProperty("patch")) p = read("patch");
        if (root.hasProperty("other")) other = read("other");
        {
            patch_ = std::move(p); other_ = std::move(other);
            isB_ = root.getProperty("isB", false);
            undo_.clear(); redo_.clear();
        }
        for (size_t i = 0; i < parameters().size(); ++i) dirty_[i].store(false);
        mirror();
        transferBaseline_.reset();
        selectedTab.store(juce::jlimit(0, 3, int(root.getProperty("tab", 0))));
        selectedZone.store(juce::jlimit(1, 16, int(root.getProperty("zone", 1))));
        selectedNativeGroup.store(juce::jlimit(1, 5, int(root.getProperty("nativeGroup", 1))));
        selectedNativeSection.store(juce::jmax(1, int(root.getProperty("nativeSection", 1))));
        changed.sendChangeMessage();
    }
    catch (const std::exception& e) { report("State restore failed: " + juce::String(e.what())); }
}
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new vsynthxt::Processor(); }
