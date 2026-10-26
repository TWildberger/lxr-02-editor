// Headless checks of the plugin processor and editor (no audio device, no window).
// Usage: KitForgeSmokeTest [snapshot-output-dir]

#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdlib>
#include <iostream>

#include "../Source/PluginProcessor.h"
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
        proc.loadKit(kit, KitForgeProcessor::LoadOrigin::User);
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
        out.clear();
        runBlocks(proc, 4000, [&](const auto&) { return false; }, out); // drain the initial kit resend
        out.clear();
        proc.setValue(pid::PAR_COARSE1, 77);
        StockMidiEncoder enc(1);
        const auto expected = StockMidiEncoder::toBytes(enc.encodeParam(pid::PAR_COARSE1, 77));
        check(runBlocks(proc, 1000, [&](const auto& o) { return contains(o, expected); }, out), "edit sent as CC on stock firmware");

        // 6. An extended identity reply switches to extended mode.
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
        editor.reset();
    }
    tmp.deleteRecursively();
    std::cout << (failures ? "FAILED: " + std::to_string(failures) : std::string("all plugin checks passed")) << std::endl;
    return failures ? 1 : 0;
}
