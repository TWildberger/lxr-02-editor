#pragma once
#include "Processor.h"
#include "NativePage.h"

namespace vsynthxt
{
class Editor : public juce::AudioProcessorEditor, private juce::Timer, private juce::ChangeListener
{
public:
    explicit Editor(Processor&);
    ~Editor() override;
    void resized() override;
    void paint(juce::Graphics&) override;
private:
    void timerCallback() override;
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();
    void search();
    void file(bool save);
    void act(const std::function<void()>&);
    void add(juce::Component&, juce::Component&);
    Processor& proc_;
    struct Tabs : juce::TabbedComponent
    {
        Tabs() : TabbedComponent(juce::TabbedButtonBar::TabsAtTop) {}
        std::function<void(int)> changed;
        void currentTabChanged(int index, const juce::String&) override
        {
            if (changed) changed(index);
        }
    };
    Tabs tabs_;
    juce::Component device_, patch_, library_;
    NativePage nativePage_;
    juce::Label heading_, warning_, status_, problem_;
    juce::ComboBox route_, input_, output_, deviceId_;
    juce::ToggleButton native_ {"I confirm the XT is in native V-Synth mode (not VC-1/VC-2)"};
    juce::TextButton connect_ {"Apply route"}, fetch_ {"Fetch part 1"}, send_ {"Send to temporary buffer"},
        cancel_ {"Cancel"}, undo_ {"Undo"}, redo_ {"Redo"}, ab_ {"Compare A/B"}, copy_ {"Copy to other A/B"},
        rename_ {"Apply name"}, import_ {"Import .syx"}, export_ {"Export .syx"},
        save_ {"Archive current / update metadata"}, load_ {"Load selected locally"};
    juce::TextEditor name_, query_, tagsText_, notes_;
    juce::ToggleButton favorite_ {"Favorite"};
    juce::ComboBox entries_;
    std::array<juce::Slider, 2> sliders_;
    std::array<juce::Label, 3> labels_;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, 2> attachments_;
    juce::ComboBox chorusType_;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> chorusAttachment_;
    std::vector<juce::MidiDeviceInfo> ins_, outs_;
    std::vector<Entry> rows_;
    std::unique_ptr<juce::FileChooser> chooser_;
    bool routeApplied_ = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Editor)
};
}
