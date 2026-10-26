#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kitforge
{

class KitForgeProcessor;

/** MIDI routing, connection status, device transfers and the log. */
class DevicePage : public juce::Component, private juce::ChangeListener
{
public:
    explicit DevicePage(KitForgeProcessor& p);
    ~DevicePage() override;
    void resized() override;
    void visibilityChanged() override;

private:
    void changeListenerCallback(juce::ChangeBroadcaster*) override;
    void refreshPorts();
    void refreshStatus();
    void applyPorts();
    int slot() const { return slot_.getSelectedId() - 1; }

    KitForgeProcessor& proc_;
    juce::Label routeLabel_ { {}, "Route" }, outLabel_ { {}, "Output" }, inLabel_ { {}, "Input" }, channelLabel_ { {}, "Channel" },
        idLabel_ { {}, "SysEx device" }, rateLabel_ { {}, "Rate" }, slotLabel_ { {}, "Slot" };
    juce::ComboBox route_, out_, in_, channel_, deviceId_, rate_, slot_;
    juce::TextButton refreshPorts_ { "Rescan" };
    juce::ToggleButton extended_ { "Use extended firmware when detected" }, passThrough_ { "Pass incoming notes through" },
        sendOnLoad_ { "Send kit to device on connect / session load" }, pullOnConnect_ { "Fetch kit from device on connect (extended)" },
        echo_ { "Device reports knob changes (Param Echo)" };
    juce::TextButton detect_ { "Detect" }, sendKit_ { "Send kit" }, getKit_ { "Get kit" }, store_ { "Store kit to slot" },
        loadSlot_ { "Load slot" }, getPattern_ { "Pattern -> library" }, saveDefaults_ { "Save as default" };
    juce::Label status_, problem_;
    juce::Label freezeLabel_ { {}, "Freeze (needs capability bit 8)" };
    juce::ComboBox freezeRate_;
    juce::TextButton freezeFx_ { "Select frz FX" }, freezeHold_ { "Hold freeze" }, freezeRelease_ { "Release" };
    bool freezeDown_ = false;
    juce::TextEditor log_;
    juce::StringArray outIds_, inIds_;
};

} // namespace kitforge
