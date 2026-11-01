#include "Processor.h"
#include "NativePage.h"
#include <iostream>

namespace xt = vsynthxt;
namespace
{
void check(bool ok, const char* text) { if (!ok) throw std::runtime_error(text); }
xt::Patch fixture()
{
    xt::Patch p;
    for (const auto& b : xt::blocks()) p.data[b.offset] = xt::Bytes(b.size, 0);
    for (const auto& info : xt::parameters()) xt::setParameter(p, info, info.minimum - info.bias);
    p.setName("SMOKE TEST"); p.setValue(13, 64); p.setValue(14, 64);
    return p;
}
void silence(xt::Processor& p)
{
    juce::AudioBuffer<float> audio(2, 64);
    juce::MidiBuffer midi;
    p.processBlock(audio, midi);
    check(midi.isEmpty(), "Unexpected MIDI emitted without an explicit action");
}
juce::ComboBox* algorithmMenu(juce::Component& parent, int count)
{
    for (auto* child : parent.getChildren())
    {
        if (auto* menu = dynamic_cast<juce::ComboBox*>(child))
            if (menu->getNumItems() == count) return menu;
        if (auto* menu = algorithmMenu(*child, count)) return menu;
    }
    return nullptr;
}
void verifyTypeMenu(xt::Processor& p, xt::NativePage& page, const char* key, int count)
{
    juce::ComboBox* menu = nullptr;
    for (auto* child : page.getChildren())
        if (auto* viewport = dynamic_cast<juce::Viewport*>(child)) menu = algorithmMenu(*viewport, count);
    check(menu != nullptr, "Named algorithm selector is missing");
    const auto before = p.patch();
    const auto& info = xt::parameter(key);
    menu->setSelectedId(count, juce::sendNotificationSync);
    auto expected = before;
    xt::setParameter(expected, info, count - 1);
    const auto deadline = juce::Time::getMillisecondCounterHiRes() + 1000;
    while (!(p.patch() == expected) && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        juce::Timer::callPendingTimersSynchronously();
        juce::Thread::sleep(20);
    }
    silence(p);
    check(p.patch() == expected, "Algorithm selector modified bytes beyond its type");
    page.refresh();
    check(menu->getSelectedId() == count, "Algorithm selector did not refresh");
    juce::MemoryBlock state;
    p.getStateInformation(state);
    xt::Processor restored;
    restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
    check(restored.patch() == expected, "Algorithm type/raw bytes did not survive DAW state");
    silence(restored);
    auto unknown = expected;
    auto block = unknown.data.upper_bound(info.offset);
    --block;
    block->second.at(info.offset - block->first) = 127;
    p.load(unknown);
    page.refresh();
    check(!menu->isEnabled() && menu->getSelectedId() == 0
        && menu->getTextWhenNothingSelected().contains("127"), "Unknown algorithm code was silently replaced");
    check(p.patch() == unknown, "Unknown algorithm raw bytes were modified by presentation");
    p.load(before);
    silence(p);
    page.refresh();
}
xt::Patch transfer(xt::Session& session, const xt::Patch& source, bool send, bool consumeResult = true)
{
    session.start(send, source, true);
    xt::Patch hardware = source;
    juce::MidiBuffer midi;
    int writes = 0, reads = 0;
    auto deadline = juce::Time::getMillisecondCounterHiRes() + 20000;
    while (session.busy() && juce::Time::getMillisecondCounterHiRes() < deadline)
    {
        session.process(midi);
        juce::MidiBuffer reply;
        for (const auto meta : midi)
        {
            const auto m = meta.getMessage();
            const auto* raw = m.getRawData();
            check(m.isSysEx() && raw[4] == 0x53, "Non-XT traffic emitted");
            if (raw[5] == 0x11)
            {
                ++reads;
                check(m.getRawDataSize() == 16, "Invalid RQ1 length");
                const auto a = xt::address(raw + 6);
                const auto offset = a - xt::temporary;
                check(hardware.data.count(offset) == 1, "Request outside part-1 temporary patch");
                const auto& b = hardware.data.at(offset);
                check(xt::address(raw + 10) == b.size(), "Incorrect block request size");
                const auto f = xt::dataSet(0x10, a, b);
                reply.addEvent(f.data(), static_cast<int>(f.size()), 0);
            }
            else
            {
                check(send && raw[5] == 0x12, "Unexpected write during read-only fetch");
                const xt::Bytes f(raw, raw + m.getRawDataSize());
                const auto decoded = xt::decode(f);
                check(decoded.start >= xt::temporary && decoded.start < xt::temporary + 16384, "Write outside temporary memory");
                hardware.data.at(decoded.start - xt::temporary) = decoded.data;
                ++writes;
            }
        }
        midi.swapWith(reply);
        juce::Thread::sleep(5);
    }
    check(!session.busy(), "Fake-device transfer did not finish");
    if (!consumeResult)
    {
        check(session.status().startsWith("Complete patch fetched"), "Fetch did not complete");
        return source;
    }
    const auto result = session.takeResult();
    if (!result) throw std::runtime_error(session.status().toStdString());
    check(*result == source, "Fake-device round-trip mismatch");
    check(reads == 103 && writes == (send ? 103 : 0), "Incorrect transfer block count");
    return *result;
}
}
int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        if (argc == 2 && juce::String(argv[1]) == "--list-midi")
        {
            for (const auto& d : juce::MidiInput::getAvailableDevices())
                std::cout << "Input: " << d.name << " [" << d.identifier << "]\n";
            for (const auto& d : juce::MidiOutput::getAvailableDevices())
                std::cout << "Output: " << d.name << " [" << d.identifier << "]\n";
            return 0;
        }
        if (argc == 6 && juce::String(argv[1]) == "--fetch-native")
        {
            const auto destination = juce::File(juce::String(argv[5]));
            check(!destination.exists(), "Backup destination already exists; choose a new path");
            xt::Session session;
            session.configure(false, argv[2], argv[3], juce::String(argv[4]).getIntValue());
            session.start(false, {}, true);
            const auto until = juce::Time::getMillisecondCounterHiRes() + 120000;
            while (session.busy() && juce::Time::getMillisecondCounterHiRes() < until)
                juce::Thread::sleep(10);
            if (session.busy()) { session.cancel(); throw std::runtime_error("Read-only hardware fetch exceeded overall deadline"); }
            const auto result = session.takeResult();
            if (!result) throw std::runtime_error(session.status().toStdString());
            const auto b = result->sysex();
            check(destination.replaceWithData(b.data(), b.size()), "Could not write fetched snapshot");
            juce::MemoryBlock archived;
            check(destination.loadFileAsData(archived), "Could not reopen fetched snapshot");
            const auto* raw = static_cast<const uint8_t*>(archived.getData());
            check(xt::Patch::fromSysex(xt::Bytes(raw, raw + archived.getSize())) == *result,
                  "Fetched snapshot failed on-disk archive round-trip");
            std::cout << session.status() << " Saved " << b.size() << " bytes to " << destination.getFullPathName() << '\n';
            return 0;
        }
        if (argc == 8 && juce::String(argv[1]) == "--verify-temporary")
        {
            const auto backupFile = juce::File(juce::String(argv[5]));
            const auto readbackFile = juce::File(juce::String(argv[6]));
            const auto auditFile = juce::File(juce::String(argv[7]));
            check(!readbackFile.exists() && !auditFile.exists(), "Use new readback and audit paths");
            juce::MemoryBlock backupBytes;
            check(backupFile.getSize() > 0 && backupFile.getSize() <= 65536
                  && backupFile.loadFileAsData(backupBytes), "Cannot read backup");
            const auto* raw = static_cast<const uint8_t*>(backupBytes.getData());
            const auto backup = xt::Patch::fromSysex(xt::Bytes(raw, raw + backupBytes.getSize()));
            xt::Session session;
            session.configure(false, argv[2], argv[3], juce::String(argv[4]).getIntValue());
            const auto waitFor = [&]() {
                const auto until = juce::Time::getMillisecondCounterHiRes() + 120000;
                while (session.busy() && juce::Time::getMillisecondCounterHiRes() < until)
                    juce::Thread::sleep(10);
                if (session.busy()) { session.cancel(); throw std::runtime_error("Transfer exceeded overall deadline"); }
                auto result = session.takeResult();
                if (!result) throw std::runtime_error(session.status().toStdString());
                return *result;
            };
            const auto saveAudit = [&]() {
                const auto sent = session.takeTransmitted();
                check(auditFile.replaceWithData(sent.data(), sent.size()), "Could not save transmission audit");
            };
            session.beginCapture();
            std::exception_ptr failure;
            try
            {
                session.start(false, {}, true);
                check(waitFor() == backup, "Current edit buffer differs from authorized backup; no writes performed");
                session.start(true, backup, true);
                const auto result = waitFor();
                check(result == backup, "Temporary-buffer readback mismatch");
                const auto b = result.sysex();
                check(readbackFile.replaceWithData(b.data(), b.size()), "Could not save readback");
                std::cout << session.status() << " All 103 blocks matched. Audit includes preflight RQ1, "
                    "temporary DT1 and verification RQ1 only.\n";
            }
            catch (...) { failure = std::current_exception(); }
            saveAudit();
            if (failure) std::rethrow_exception(failure);
            return 0;
        }
        const bool screenshots = argc == 3 && juce::String(argv[1]) == "--screenshots";
        check(argc == 1 || screenshots, "Usage: VSynthXTSmokeTest [--list-midi | --fetch-native inputID outputID deviceID newBackup.syx | --verify-temporary inputID outputID deviceID backup.syx newReadback.syx newAudit.syx | --screenshots folder]");
        auto source = fixture();
        const auto snapshot = juce::SystemStats::getEnvironmentVariable("VSYNTHXT_TEST_SNAPSHOT", {});
        if (snapshot.isNotEmpty())
        {
            juce::MemoryBlock bytes;
            const auto file = juce::File(snapshot);
            check(file.getSize() > 0 && file.getSize() <= 65536 && file.loadFileAsData(bytes), "Cannot read offline test snapshot");
            const auto* raw = static_cast<const uint8_t*>(bytes.getData());
            source = xt::Patch::fromSysex(xt::Bytes(raw, raw + bytes.getSize()));
        }
        xt::Processor p;
        silence(p);
        p.load(source);
        silence(p);
        p.rename("RENAMED");
        p.undo(); check(p.patch() == source, "Undo failed");
        p.redo(); check(p.patch().name() == "RENAMED", "Redo failed");
        p.copyAB(); p.rename("BUFFER A"); p.compare();
        check(p.patch().name() == "RENAMED", "A/B failed");
        p.selectedTab.store(1);
        p.selectedZone.store(16); p.selectedNativeGroup.store(3); p.selectedNativeSection.store(5);
        juce::MemoryBlock state;
        p.getStateInformation(state);
        xt::Processor restored;
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(restored.patch() == p.patch(), "DAW patch state did not round-trip");
        check(restored.isB() == p.isB(), "DAW A/B state did not round-trip");
        check(restored.selectedTab.load() == 1, "DAW UI state did not round-trip");
        check(restored.selectedZone.load() == 16 && restored.selectedNativeGroup.load() == 3
            && restored.selectedNativeSection.load() == 5, "Native view state did not round-trip");
        silence(restored);
        std::unique_ptr<juce::AudioProcessorEditor> editor(restored.createEditor());
        editor->setSize(820, 600);
        juce::TabbedComponent* tabs = nullptr;
        for (auto* child : editor->getChildren())
            if (auto* t = dynamic_cast<juce::TabbedComponent*>(child)) tabs = t;
        check(tabs != nullptr, "XT editor has no tabs");
        for (int i = 0; i < tabs->getNumTabs(); ++i)
        {
            tabs->setCurrentTabIndex(i);
            auto* panel = tabs->getTabContentComponent(i);
            if (auto* native = dynamic_cast<xt::NativePage*>(panel))
            {
                for (int group = 1; group <= 5; ++group)
                    for (int zone : {1, 16})
                    {
                        native->select(group, zone, 1);
                        check(native->rowCount() > 0, "Native group has no controls");
                        const int count = native->sectionCount();
                        for (int section = 1; section <= count; ++section)
                        {
                            native->select(group, zone, section);
                            check(native->rowCount() > 0, "Native section has no controls");
                        }
                        native->select(group, zone, 1);
                        if (screenshots && zone == 1)
                        {
                            juce::Image image(juce::Image::ARGB, 820, 600, true);
                            juce::Graphics graphics(image); editor->paintEntireComponent(graphics, true);
                            const auto folder = juce::File(juce::String(argv[2]));
                            check(folder.createDirectory().wasOk(), "Could not create native screenshot directory");
                            juce::FileOutputStream stream(folder.getChildFile("Native-" + juce::String(group) + ".png"));
                            check(stream.openedOk(), "Could not open native screenshot");
                            juce::PNGImageFormat png;
                            check(png.writeImageToStream(image, stream), "Could not encode native screenshot");
                        }
                    }
                native->select(3, 16, 5);
                check(native->rowCount() == 7, "LFO 5 controls missing");
                native->select(4, 16, 1);
                verifyTypeMenu(restored, *native, "xt.native.cosm.zone16.cosm1.type", 17);
                native->select(5, 1, 1);
                const int effectsSections = native->sectionCount();
                int verifiedEffects = 0;
                for (int section = 1; section <= effectsSections; ++section)
                {
                    native->select(5, 1, section);
                    for (auto* child : native->getChildren())
                        if (auto* viewport = dynamic_cast<juce::Viewport*>(child))
                        {
                            if (algorithmMenu(*viewport, 42))
                            {
                                verifyTypeMenu(restored, *native, "xt.native.effects.mfx.type", 42);
                                ++verifiedEffects;
                            }
                            if (algorithmMenu(*viewport, 9))
                            {
                                verifyTypeMenu(restored, *native, "xt.chorus.type", 9);
                                ++verifiedEffects;
                            }
                            if (algorithmMenu(*viewport, 11))
                            {
                                verifyTypeMenu(restored, *native, "xt.native.effects.reverb.type", 11);
                                ++verifiedEffects;
                            }
                        }
                }
                check(verifiedEffects == 3, "Not all effect type selectors were exercised");
                native->select(3, 16, 5);
            }
            for (auto* child : panel->getChildren())
                check(!child->getBounds().isEmpty() && panel->getLocalBounds().contains(child->getBounds()),
                      "XT page has an empty or clipped control");
            juce::Image image(juce::Image::ARGB, 820, 600, true);
            juce::Graphics graphics(image); editor->paintEntireComponent(graphics, true);
            if (screenshots)
            {
                const auto folder = juce::File(juce::String(argv[2]));
                check(folder.createDirectory().wasOk(), "Could not create screenshot directory");
                juce::FileOutputStream stream(folder.getChildFile(tabs->getTabNames()[i] + ".png"));
                check(stream.openedOk(), "Could not open screenshot");
                juce::PNGImageFormat png;
                check(png.writeImageToStream(image, stream), "Could not encode screenshot");
            }
        }
        restored.session.configure(true, {}, {}, 17);
        silence(restored);
        restored.session.start(false, {}, false);
        check(!restored.session.busy(), "Native-mode safety gate failed");
        transfer(restored.session, source, false);
        transfer(restored.session, source, true);
        transfer(restored.session, source, false, false);
        restored.setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        check(!restored.session.takeResult(), "DAW recall retained a stale transfer result");
        check(restored.patch() == p.patch(), "Stale transfer replaced restored local state");
        silence(restored);
        auto* coarse = restored.state.getParameter("xt.common.coarse");
        coarse->setValueNotifyingHost(coarse->convertTo0to1(12.0f));
        juce::MemoryBlock automatedState;
        restored.getStateInformation(automatedState);
        xt::Processor automated;
        automated.setStateInformation(automatedState.getData(), static_cast<int>(automatedState.getSize()));
        check(automated.patch().value(13) == 76, "State serialization lost pending host automation");
        silence(automated);
        const auto& env = xt::parameter("xt.native.envelope.zone16.env13.attack");
        auto* envParam = restored.state.getParameter(env.key);
        check(envParam != nullptr, "Native host parameter is missing");
        envParam->setValueNotifyingHost(envParam->convertTo0to1(144.0f));
        juce::MemoryBlock nativeState;
        restored.getStateInformation(nativeState);
        automated.setStateInformation(nativeState.getData(), static_cast<int>(nativeState.getSize()));
        check(xt::parameterValue(automated.patch(), env) == 144, "Native nibble automation lost on recall");
        silence(automated);
        const auto retained = automated.patch();
        const auto retainedB = automated.isB();
        automated.session.configure(true, {}, {}, 17);
        const auto rejectState = [&](const juce::MemoryBlock& invalid) {
            automated.session.start(true, retained, true);
            check(automated.session.busy(), "Malformed-state cancellation test did not start its fake host transfer");
            automated.setStateInformation(invalid.getData(), static_cast<int>(invalid.getSize()));
            check(!automated.session.busy() && !automated.session.takeResult(),
                  "Malformed state retained a queued transfer or stale result");
            check(automated.problem().startsWith("State restore failed:"), "Malformed state did not report an error");
            check(automated.patch() == retained && automated.isB() == retainedB,
                  "Malformed state partially replaced valid local patch/A-B state");
            silence(automated);
        };
        rejectState(juce::MemoryBlock("invalid", 7));
        for (const char* corruption : {"version", "patch", "other"})
        {
            auto xml = juce::AudioProcessor::getXmlFromBinary(nativeState.getData(), static_cast<int>(nativeState.getSize()));
            check(xml != nullptr, "Could not inspect valid state fixture");
            auto invalid = juce::ValueTree::fromXml(*xml);
            if (juce::String(corruption) == "version") invalid.setProperty("version", 999, nullptr);
            else invalid.setProperty(corruption, "not-a-patch", nullptr);
            juce::MemoryBlock invalidBytes;
            juce::AudioProcessor::copyXmlToBinary(*invalid.createXml(), invalidBytes);
            rejectState(invalidBytes);
        }
        restored.session.start(true, source, true);
        juce::Thread::sleep(20);
        restored.session.cancel();
        silence(restored);
        restored.session.start(true, source, true);
        juce::Thread::sleep(3100); // No audio blocks: queued host writes must expire, not replay.
        check(!restored.session.busy(), "Stopped-host timeout failed");
        silence(restored);
        std::cout << "XT state, UI, safety gates and fake-device fetch/send/readback passed\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
