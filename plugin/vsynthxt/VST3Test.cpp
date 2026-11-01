#include <juce_audio_utils/juce_audio_utils.h>
#include "Parameters.h"
#include <iostream>

namespace
{
void check(bool ok, const char* message)
{
    if (!ok) throw std::runtime_error(message);
}
void processSilently(juce::AudioPluginInstance& plugin, int blockSize)
{
    juce::AudioBuffer<float> audio(2, blockSize);
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(audio.getWritePointer(channel), 0.5f, blockSize);
    juce::MidiBuffer midi;
    plugin.processBlock(audio, midi);
    check(midi.isEmpty(), "Loaded VST3 emitted MIDI without explicit Fetch/Send");
    for (int channel = 0; channel < audio.getNumChannels(); ++channel)
        check(audio.getMagnitude(channel, 0, blockSize) == 0.0f, "Hardware companion VST3 produced audio");
}
juce::MemoryBlock fixtureState()
{
    vsynthxt::Patch patch;
    for (const auto& block : vsynthxt::blocks()) patch.data[block.offset] = vsynthxt::Bytes(block.size, 0);
    for (const auto& info : vsynthxt::parameters())
        vsynthxt::setParameter(patch, info, info.minimum - info.bias);
    patch.setName("HOST TEST");
    const auto bytes = patch.sysex();
    juce::ValueTree state("VSynthXTState");
    state.setProperty("version", 1, nullptr);
    state.setProperty("patch", juce::MemoryBlock(bytes.data(), bytes.size()).toBase64Encoding(), nullptr);
    state.setProperty("tab", 3, nullptr);
    state.setProperty("zone", 16, nullptr);
    state.setProperty("nativeGroup", 4, nullptr);
    state.setProperty("nativeSection", 1, nullptr);
    juce::MemoryBlock result;
    juce::AudioProcessor::copyXmlToBinary(*state.createXml(), result);
    juce::XmlElement hostState("VST3PluginState");
    hostState.createNewChildElement("IComponent")->addTextElement(result.toBase64Encoding());
    juce::MemoryBlock wrapped;
    juce::AudioProcessor::copyXmlToBinary(hostState, wrapped);
    return wrapped;
}
vsynthxt::Patch archivedPatch(const juce::MemoryBlock& hostState)
{
    auto hostXml = juce::AudioProcessor::getXmlFromBinary(hostState.getData(), static_cast<int>(hostState.getSize()));
    check(hostXml != nullptr, "VST3 host state XML missing");
    auto* component = hostXml->getChildByName("IComponent");
    check(component != nullptr, "VST3 component state missing");
    juce::MemoryBlock componentBytes;
    check(componentBytes.fromBase64Encoding(component->getAllSubText()), "Invalid VST3 component encoding");
    auto xml = juce::AudioProcessor::getXmlFromBinary(componentBytes.getData(), static_cast<int>(componentBytes.getSize()));
    check(xml != nullptr && xml->hasTagName("VSynthXTState"), "Loaded VST3 did not retain XT state");
    juce::MemoryBlock patch;
    check(patch.fromBase64Encoding(xml->getStringAttribute("patch")), "Loaded VST3 patch encoding invalid");
    const auto* raw = static_cast<const uint8_t*>(patch.getData());
    return vsynthxt::Patch::fromSysex(vsynthxt::Bytes(raw, raw + patch.getSize()));
}
}

int main(int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI gui;
    try
    {
        check(argc == 2, "Usage: VSynthXTVST3Test path/to/companion.vst3");
        const auto bundle = juce::File(juce::String(argv[1]));
        check(bundle.isDirectory(), "VST3 bundle not found");
        juce::VST3PluginFormat format;
        juce::OwnedArray<juce::PluginDescription> descriptions;
        format.findAllTypesForFile(descriptions, bundle.getFullPathName());
        check(descriptions.size() == 1, "Expected one loadable XT VST3 instrument");
        check(descriptions[0]->name == "V-Synth XT Companion", "Unexpected VST3 product");
        juce::String error;
        auto plugin = format.createInstanceFromDescription(*descriptions[0], 48000.0, 256, error);
        if (!plugin) throw std::runtime_error("VST3 instantiation failed: " + error.toStdString());
        check(plugin->getBypassParameter() != nullptr, "VST3 wrapper bypass parameter missing");
        // JUCE's host additionally exposes 130 MIDI controller proxies per channel.
        constexpr int wrapperParameters = 1 + 16 * 130;
        check(plugin->getParameters().size() == static_cast<int>(vsynthxt::parameters().size()) + wrapperParameters,
              "Loaded VST3 automation parameter count changed");
        for (size_t i = 0; i < vsynthxt::parameters().size(); ++i)
        {
            const auto& info = vsynthxt::parameters()[i];
            const auto name = info.zone ? "Z" + juce::String(info.zone) + " " + info.section + " " + info.label
                                       : juce::String(info.section) + " " + info.label;
            check(plugin->getParameters()[static_cast<int>(i)]->getName(256) == name,
                  "Actual VST3 parameter name/order differs from the stable registry");
        }
        std::vector<float> normalized;
        const auto state = fixtureState();
        plugin->prepareToPlay(48000.0, 256);
        processSilently(*plugin, 256);
        plugin->setStateInformation(state.getData(), static_cast<int>(state.getSize()));
        for (int blockSize : {1, 64, 256}) processSilently(*plugin, blockSize);
        juce::MemoryBlock loaded;
        plugin->getStateInformation(loaded);
        check(archivedPatch(loaded) == archivedPatch(state), "Actual VST3 did not load the complete fixture patch");

        // VST3 host wrappers expose SDK parameter IDs, not APVTS strings.
        juce::AudioProcessorParameter* coarse = nullptr;
        for (auto* parameter : plugin->getParameters())
            if (parameter->getName(128) == "Common Coarse tune") coarse = parameter;
        check(coarse != nullptr, "Original coarse automation parameter missing from VST3");
        coarse->setValueNotifyingHost(0.75f);
        for (auto* parameter : plugin->getParameters()) normalized.push_back(parameter->getValue());
        juce::MemoryBlock saved;
        plugin->getStateInformation(saved);
        check(saved.getSize() > 0, "Loaded VST3 returned empty state");
        auto expected = archivedPatch(state);
        vsynthxt::setParameter(expected, vsynthxt::parameter("xt.common.coarse"), 24);
        check(archivedPatch(saved) == expected, "VST3 coarse automation changed unexpected bytes or failed to persist");
        plugin->releaseResources();

        auto restored = format.createInstanceFromDescription(*descriptions[0], 44100.0, 128, error);
        if (!restored) throw std::runtime_error("Second VST3 instantiation failed: " + error.toStdString());
        restored->setStateInformation(saved.getData(), static_cast<int>(saved.getSize()));
        restored->prepareToPlay(44100.0, 128);
        check(restored->getParameters().size() == static_cast<int>(normalized.size()), "VST3 parameter count changed on recall");
        for (size_t i = 0; i < normalized.size(); ++i)
            check(juce::approximatelyEqual(restored->getParameters()[static_cast<int>(i)]->getValue(), normalized[i]),
                  "Loaded VST3 automation state did not survive instance recall");
        for (int block = 0; block < 8; ++block) processSilently(*restored, 128);
        juce::MemoryBlock recalled;
        restored->getStateInformation(recalled);
        check(archivedPatch(recalled) == expected, "Actual VST3 patch bytes changed on recall");
        {
            std::unique_ptr<juce::AudioProcessorEditor> editor(restored->createEditorAndMakeActive());
            check(editor != nullptr, "Loaded VST3 could not create its editor");
            editor->setSize(820, 600);
            juce::Image image(juce::Image::ARGB, 820, 600, true);
            juce::Graphics graphics(image);
            editor->paintEntireComponent(graphics, true);
        }
        processSilently(*restored, 128);
        restored->releaseResources();
        std::cout << "Actual VST3 loaded: " << bundle.getFullPathName()
                  << "\n" << vsynthxt::parameters().size() << " XT parameters + wrapper bypass/MIDI proxies; two instances; state/automation recall; "
                  "48/44.1 kHz processing; silent audio/MIDI; editor creation/render passed.\n";
        return 0;
    }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
