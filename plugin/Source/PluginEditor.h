#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <random>

#include "LookAndFeel.h"

namespace kitforge
{

class KitForgeProcessor;
class LibraryPage;

class KitForgeEditor : public juce::AudioProcessorEditor,
                       public juce::FileDragAndDropTarget,
                       private juce::ChangeListener
{
public:
    explicit KitForgeEditor(KitForgeProcessor& p);
    ~KitForgeEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int, int) override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshHeader();
    void openKit();
    void saveKit();
    void randomizeKit();
    void loadKitFile(const juce::File& f);

    KitForgeProcessor& proc_;
    KitForgeLookAndFeel lnf_;
    juce::TooltipWindow tooltips_ { this, 600 };
    juce::Label product_;
    juce::TextEditor name_;
    juce::TextButton undo_ { "Undo" }, redo_ { "Redo" }, ab_ { "A" }, copyAB_ { "Copy to B" }, random_ { "Randomize kit..." },
        open_ { "Open .snd..." }, save_ { "Save .snd..." }, addLib_ { "+ Library" }, send_ { "Send" };
    juce::Label status_;
    juce::TabbedComponent tabs_ { juce::TabbedButtonBar::TabsAtTop };
    LibraryPage* library_ = nullptr;
    std::unique_ptr<juce::FileChooser> chooser_;
    std::mt19937 rng_ { std::random_device {}() };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KitForgeEditor)
};

} // namespace kitforge
