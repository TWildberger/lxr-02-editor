#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kitforge
{

class KitForgeProcessor;

/** Global settings (glo.cfg): edit, load/save files, transfer to the device (extended firmware). */
class GlobalsPage : public juce::Component, private juce::ChangeListener
{
public:
    explicit GlobalsPage(KitForgeProcessor& p);
    ~GlobalsPage() override;
    void resized() override;

private:
    struct Row
    {
        int param;
        std::unique_ptr<juce::Label> label;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<juce::Slider> slider;
    };
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refresh();
    void openFile();
    void saveFile();
    void send(bool save);

    KitForgeProcessor& proc_;
    std::vector<Row> rows_;
    juce::TextButton get_ { "Get from device" }, send_ { "Send to device" }, sendSave_ { "Send & save to SD" },
        open_ { "Open glo.cfg..." }, save_ { "Save glo.cfg..." };
    juce::Label note_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace kitforge
