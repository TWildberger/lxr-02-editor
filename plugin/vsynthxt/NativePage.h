#pragma once
#include "Processor.h"
#include "Algorithms.h"

namespace vsynthxt
{
class NativePage : public juce::Component, private juce::ChangeListener
{
public:
    explicit NativePage(Processor&);
    ~NativePage() override;
    void resized() override;
    void select(int group, int zone, int section);
    int rowCount() const { return static_cast<int>(rows_.size()); }
    int sectionCount() const { return sections_.size(); }
    void refresh();
private:
    struct Row : juce::Component
    {
        Row(Processor&, const Parameter&);
        void refresh(const Patch&);
        void resized() override;
        const Parameter& info;
        juce::Label label, raw;
        juce::Slider slider;
        juce::ComboBox type;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> typeAttachment;
    };
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void rebuildSections();
    void rebuildRows();
    Processor& proc_;
    juce::ComboBox group_, zone_, section_;
    juce::Label warning_;
    juce::TextEditor algorithmReference_;
    juce::HyperlinkButton manual_ {"Official algorithm reference", juce::URL()};
    juce::Viewport viewport_;
    juce::Component content_;
    std::vector<std::unique_ptr<Row>> rows_;
    juce::StringArray sections_;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NativePage)
};
}
