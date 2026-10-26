// Headless checks of the plugin processor and editor (no audio device, no window).
// Usage: KitForgeSmokeTest [snapshot-output-dir]

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>
#include <iostream>

#include "../Source/PluginProcessor.h"
#include "../Source/ParamControl.h"
#include "kitforge/Midi.h"
#include "kitforge/ParamTable.h"

using namespace kitforge;

namespace
{
int failures = 0;

void check(bool ok, const juce::String& what)
{
    std::cout << (ok ? "ok   " : "FAIL ") << what << std::endl;
    if (!ok)
        ++failures;
}

void pump(int ms) { juce::MessageManager::getInstance()->runDispatchLoopUntil(ms); }

// Runs audio blocks until pred(bytes of all output so far) is true or the timeout expires.
template <typename Pred>
bool runBlocks(KitForgeProcessor& p, int timeoutMs, Pred pred, std::vector<uint8_t>& out)
{
    juce::AudioBuffer<float> audio(2, 256);
    const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
    while (juce::Time::getMillisecondCounter() < end)
    {
        juce::MidiBuffer midi;
        p.processBlock(audio, midi);
        for (const auto m : midi)
            out.insert(out.end(), m.data, m.data + m.numBytes);
        if (pred(out))
            return true;
        pump(5);
    }
    return false;
}

bool contains(const std::vector<uint8_t>& hay, const std::vector<uint8_t>& needle)
{
    return std::search(hay.begin(), hay.end(), needle.begin(), needle.end()) != hay.end();
}

juce::TextButton* findButton(juce::Component& root, const juce::String& text)
{
    if (auto* b = dynamic_cast<juce::TextButton*>(&root); b && b->getButtonText() == text)
        return b;
    for (auto* child : root.getChildren())
        if (auto* b = findButton(*child, text))
            return b;
    return nullptr;
}

// Minimal device-side kit RAM, not a DSP/hardware simulation.
struct KitDevice
{
    Bytes edit = Kit().toSnd();
    std::array<Bytes, ext::kNumSlots> slots;
    std::vector<ext::Message> received;
    juce::MidiBuffer midi;

    void run(KitForgeProcessor& proc, int timeoutMs, std::function<bool()> done)
    {
        juce::AudioBuffer<float> audio(2, 256);
        const auto end = juce::Time::getMillisecondCounter() + (juce::uint32) timeoutMs;
        while (juce::Time::getMillisecondCounter() < end)
        {
            proc.processBlock(audio, midi);
            juce::MidiBuffer replies;
            for (const auto meta : midi)
            {
                const auto m = ext::parse(Bytes(meta.data, meta.data + meta.numBytes));
                if (!m)
                    continue;
                received.push_back(*m);
                Bytes reply;
                switch (m->command)
                {
                    case ext::Command::EchoSet:
                        reply = ext::ack(0, m->command, m->on ? 1 : 0);
                        break;
                    case ext::Command::KitDump:
                        if (m->slot == ext::kEditBuffer)
                            edit = m->data;
                        else
                            slots[(size_t) m->slot] = m->data;
                        reply = ext::ack(0, m->command, m->slot);
                        break;
                    case ext::Command::KitStore:
                        slots[(size_t) m->slot] = edit;
                        reply = ext::ack(0, m->command, m->slot);
                        break;
                    case ext::Command::KitLoad:
                        edit = slots[(size_t) m->slot];
                        reply = ext::ack(0, m->command, m->slot);
                        break;
                    case ext::Command::KitRequest:
                        reply = ext::kitDump(0, m->slot, 0, m->slot == ext::kEditBuffer ? edit : slots[(size_t) m->slot]);
                        break;
                    case ext::Command::GlobalsRequest:
                        reply = ext::globalsDump(0, 0, kFirstGlobal, Globals().toCfg());
                        break;
                    default: break;
                }
                if (!reply.empty())
                    replies.addEvent(reply.data(), (int) reply.size(), 0);
            }
            midi.swapWith(replies);
            if (done())
                return;
            pump(5);
        }
    }
};
} // namespace

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    const auto tmp = juce::File::createTempFile("kitforge-smoke");
    tmp.createDirectory();
    setenv("KITFORGE_LIBRARY_PATH", tmp.getChildFile("library.sqlite").getFullPathName().toRawUTF8(), 1);

    {
        KitForgeProcessor proc;
        proc.device().setRoute(DeviceSession::Route::PluginMidi);
        proc.device().setChannel(1);

        // 1. Out-of-range bytes from real kits survive a load / save cycle.
        Kit kit = Kit::init();
        kit.setName("Smoke");
        kit.set(pid::PAR_COARSE1, 70);
        const int transWave = pid::PAR_TRANS6_WAVE;
        kit.set(transWave, ParamTable::get(transWave).maxValue + 11);
        const auto legacyKit = kit;
        check(kit.setEngineSettings(1, 1, 100), "feedback-FM opt-in");
        proc.loadKit(legacyKit, KitForgeProcessor::LoadOrigin::User);
        proc.setValues(kit, {});
        proc.undo();
        check(proc.currentKit() == legacyKit, "undo engine opt-in restores untagged bytes exactly");
        proc.redo();
        check(proc.currentKit() == kit, "redo engine opt-in restores all tagged bytes");
        check(proc.currentKit() == kit, "kit bytes preserved after load (including an out-of-range byte)");
        check(proc.kitName() == "Smoke", "kit name");
        auto* coarse = proc.parameterFor(pid::PAR_COARSE1);
        check(coarse && juce::roundToInt(coarse->convertFrom0to1(coarse->getValue())) == 70, "APVTS parameter follows the kit");

        // 2. Undo / redo / A-B.
        proc.setValue(pid::PAR_COARSE1, 90);
        check(proc.rawValue(pid::PAR_COARSE1) == 90, "setValue");
        proc.undo();
        check(proc.rawValue(pid::PAR_COARSE1) == 70, "undo");
        proc.redo();
        check(proc.rawValue(pid::PAR_COARSE1) == 90, "redo");
        const Kit a = proc.currentKit();
        proc.toggleAB();
        check(proc.isB() && proc.currentKit() != a, "A/B switches to the other kit");
        proc.toggleAB();
        check(!proc.isB() && proc.currentKit() == a, "A/B switches back");

        // 3. Host automation writes the raw value.
        coarse->setValueNotifyingHost(coarse->convertTo0to1(33.0f));
        check(proc.rawValue(pid::PAR_COARSE1) == 33, "automation updates the kit");

        // 4. State round trip into a fresh instance.
        Globals g;
        g.set(pid::PAR_BPM, 133);
        proc.loadGlobals(g, false);
        proc.setLocked(pid::PAR_FINE1, true);
        juce::MemoryBlock state;
        proc.getStateInformation(state);
        {
            KitForgeProcessor other;
            other.setStateInformation(state.getData(), (int) state.getSize());
            check(other.currentKit() == proc.currentKit(), "state restores the kit byte-exactly");
            check(other.globals() == proc.globals(), "state restores globals");
            check(other.isLocked(pid::PAR_FINE1), "state restores locks");
        }

        // 5. Device detection over the plugin MIDI route times out to stock mode; edits go out as CC.
        std::vector<uint8_t> out;
        proc.device().detect();
        const std::vector<uint8_t> probe { 0xF0, 0x7D, 0x4C, 0x7F, 0x01, 0x01, 0x7F, 0xF7 };
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, probe); }, out), "identity request sent on plugin MIDI out");
        runBlocks(proc, 1500, [&](const auto&) { return proc.device().mode() == DeviceSession::Mode::Stock; }, out);
        check(proc.device().mode() == DeviceSession::Mode::Stock, "no reply -> stock firmware mode");
        check(!proc.device().supportsFreeze(), "freeze unavailable on stock firmware");
        proc.device().sendFreeze(5);
        out.clear();
        runBlocks(proc, 4000, [&](const auto&) { return false; }, out); // drain the initial kit resend
        out.clear();
        proc.setValue(pid::PAR_COARSE1, 77);
        StockMidiEncoder enc(1);
        const auto expected = StockMidiEncoder::toBytes(enc.encodeParam(pid::PAR_COARSE1, 77));
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, expected); }, out), "edit sent as CC on stock firmware");

        // 6. An extended identity reply switches to extended mode.
        proc.setSendOnLoad(false);
        proc.setPullOnConnect(false);
        ext::Identity id;
        id.fwMajor = 1;
        id.fwMinor = 8;
        id.fwString = "smoke";
        id.numParams = kNumParams;
        id.endOfSound = kNumSoundParams;
        id.firstGlobal = kFirstGlobal;
        id.kitSlots = id.patternSlots = 64;
        id.tracks = 7;
        id.steps = 128;
        id.patternLength = ext::kPatternRawLength;
        id.maxFrame = ext::kMaxFrameLength;
        id.capabilities = 0x7F;
        id.deviceId = 1;
        proc.device().detect();
        juce::AudioBuffer<float> audio(2, 256);
        for (int i = 0; i < 5; ++i)
        {
            juce::MidiBuffer m;
            proc.processBlock(audio, m);
            pump(5);
        }
        const auto reply = ext::identityReply(id);
        juce::MidiBuffer in;
        in.addEvent(juce::MidiMessage(reply.data(), (int) reply.size()), 0);
        proc.processBlock(audio, in);
        out.clear();
        runBlocks(proc, 1000, [&](const auto&) { return proc.device().isExtended(); }, out);
        check(proc.device().isExtended(), "identity reply -> extended mode (" + proc.device().describe() + ")");
        check(!proc.device().supportsFreeze(), "freeze unavailable when identity bit 8 is absent");
        KitDevice simulated;
        std::atomic<bool> sent { false };
        proc.device().sendKit(4, kit, 0, [&](bool ok, const auto&) { sent = ok; });
        simulated.run(proc, 2000, [&] { return sent.load(); });
        check(sent && simulated.slots[4].size() == 255, "old extended firmware receives a legacy kit body");
        check(proc.currentKit().hasEngineExtension(), "old-device transfer does not truncate editor kit");
        out.clear();
        proc.device().sendFreeze(5);
        runBlocks(proc, 100, [&](const auto&) { return false; }, out);
        check(!contains(out, ext::fxFreeze(0x7F, 5)), "unsupported freeze sends no command");

        // Both optional features live above the original capability byte.
        id.capabilities = 0x1FF;
        proc.device().detect();
        out.clear();
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, probe); }, out), "second identity request");
        const auto extendedReply = ext::identityReply(id);
        in.clear();
        in.addEvent(extendedReply.data(), (int) extendedReply.size(), 0);
        proc.processBlock(audio, in);
        runBlocks(proc, 1000, [&](const auto&) { return proc.device().supportsFreeze(); }, out);
        check(proc.device().supportsFreeze(), "identity bit 8 enables freeze");
        sent = false;
        proc.device().sendKit(ext::kEditBuffer, kit, 0, [&](bool ok, const auto&) { sent = ok; });
        simulated.run(proc, 2000, [&] { return sent.load(); });
        check(sent && simulated.edit == kit.toSnd(), "new firmware receives full tagged kit with n=247");
        std::atomic<bool> stored { false }, loaded { false }, fetched { false };
        proc.device().storeKit(5, ext::kFlagSave, [&](bool ok, const auto&) { stored = ok; });
        proc.device().loadKitSlot(5, [&](bool ok, const auto&) { loaded = ok; });
        proc.device().requestKit(ext::kEditBuffer, [&](std::optional<Kit> back, const auto&) {
            fetched = back && back->toSnd() == kit.toSnd();
        });
        simulated.run(proc, 2000, [&] { return stored && loaded && fetched; });
        check(stored && loaded && fetched && simulated.slots[5] == kit.toSnd(),
              "kit dump/store/load/request preserves all extension bytes");
        auto oversized = kit.toSnd();
        oversized.resize(300, 0xFF);
        bool rejected = false;
        proc.device().sendKit(6, Kit::fromSnd(oversized), 0, [&](bool ok, const auto& msg) {
            rejected = !ok && msg.contains("maximum frame length");
        });
        check(rejected, "oversized opaque extension reports transfer failure instead of truncating");
        proc.device().sendFreeze(5);
        out.clear();
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, ext::fxFreeze(0x7F, 5)); }, out),
              "freeze rate frame sent without waiting for ACK");
        proc.device().releaseFreeze();
        out.clear();
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, ext::fxFreeze(0x7F, 0)); }, out),
              "freeze release frame sent");
        const auto nak = ext::nak(0, ext::Command::FxFreeze, ext::Error::Range, 3);
        in.clear();
        in.addEvent(nak.data(), (int) nak.size(), 0);
        proc.processBlock(audio, in);
        runBlocks(proc, 500, [&](const auto&) { return proc.logLines().joinIntoString("\n").contains("Device rejected FX_FREEZE"); }, out);
        check(proc.logLines().joinIntoString("\n").contains("Device rejected FX_FREEZE")
                  && proc.logLines().joinIntoString("\n").contains(", 3)"),
              "device freeze NAK is reported");

        // 7. Library + editor snapshots.
        if (auto* lib = proc.library())
        {
            check(lib->addKit(proc.currentKit(), "smoke").has_value(), "library add");
            Kit k2 = Kit::init();
            k2.setName("Init");
            lib->addKit(k2, "smoke");
        }
        else
            check(false, "library opens");

        std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
        editor->setSize(1200, 800);
        juce::TabbedComponent* tabs = nullptr;
        for (auto* c : editor->getChildren())
            if (auto* t = dynamic_cast<juce::TabbedComponent*>(c))
                tabs = t;
        check(tabs != nullptr && tabs->getNumTabs() == 11, "editor has 11 pages");
        if (tabs)
            tabs->setCurrentTabIndex(10);
        pump(20);
        auto* freezeButton = findButton(*editor, "Hold freeze");
        check(freezeButton && freezeButton->isEnabled(), "Device page freeze control enabled by capability bit 8");
        auto* selectFreeze = findButton(*editor, "Select frz FX");
        check(selectFreeze != nullptr, "Device page offers explicit freeze FX selection");
        if (selectFreeze)
        {
            selectFreeze->triggerClick();
            pump(100);
            simulated.run(proc, 1000, [&] { return simulated.edit == proc.currentKit().toSnd(); });
            check(proc.rawValue(pid::PAR_FX_TYPE) == 6 && Kit::fromSnd(simulated.edit).get(pid::PAR_FX_TYPE) == 6,
                  "freeze FX selection reaches the device without clamping type 6");
            ParamControl fxControl(proc, pid::PAR_FX_TYPE);
            fxControl.refresh();
            juce::ComboBox* fxCombo = nullptr;
            for (auto* child : fxControl.getChildren())
                if (auto* box = dynamic_cast<juce::ComboBox*>(child))
                    fxCombo = box;
            check(fxCombo && fxCombo->getText() == "frz (extended)", "FX control shows raw frz instead of a clamped legacy type");
            if (fxCombo)
            {
                fxCombo->setSelectedId(ParamTable::get(pid::PAR_FX_TYPE).maxValue + 1, juce::sendNotificationSync);
                check(proc.rawValue(pid::PAR_FX_TYPE) == ParamTable::get(pid::PAR_FX_TYPE).maxValue,
                      "legacy FX choice replaces frz even when the host parameter was already clamped to that choice");
            }
            selectFreeze->triggerClick();
            pump(100);
            simulated.run(proc, 1000, [&] { return simulated.edit == proc.currentKit().toSnd(); });
        }
        if (freezeButton)
        {
            freezeButton->setState(juce::Button::buttonDown);
            out.clear();
            check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, ext::fxFreeze(0x7F, 5)); }, out),
                  "hold button sends selected freeze rate");
            freezeButton->setState(juce::Button::buttonNormal);
            out.clear();
            check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, ext::fxFreeze(0x7F, 0)); }, out),
                  "hold button release sends freeze off");
        }
        const juce::File outDir = argc > 1 ? juce::File::getCurrentWorkingDirectory().getChildFile(argv[1]) : juce::File();
        if (outDir != juce::File())
            outDir.createDirectory();
        for (int i = 0; tabs && i < tabs->getNumTabs(); ++i)
        {
            tabs->setCurrentTabIndex(i);
            pump(20);
            const auto img = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
            check(img.isValid(), "render page " + tabs->getTabNames()[i]);
            if (outDir != juce::File())
            {
                auto f = outDir.getChildFile(juce::String(i).paddedLeft('0', 2) + "-"
                                             + tabs->getTabNames()[i].replaceCharacters(" &/", "___") + ".png");
                f.deleteFile();
                juce::FileOutputStream os(f);
                juce::PNGImageFormat().writeImageToStream(img, os);
            }
        }
        proc.device().setAllowExtended(false);
        pump(100);
        check(freezeButton && !freezeButton->isEnabled(), "freeze UI becomes unavailable without extended identity support");
        editor.reset();
    }
    tmp.deleteRecursively();
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all plugin checks passed")) << std::endl;
    return failures ? 1 : 0;
}
