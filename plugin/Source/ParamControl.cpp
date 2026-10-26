#include "ParamControl.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/KitOps.h"
#include "kitforge/Midi.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

namespace
{
int voiceLfoParamFor(int targetParam)
{
    const int voice = ParamTable::get(targetParam).voice;
    for (int i : ParamTable::paramsForVoice(voice))
        if (ParamTable::get(i).dtype == DType::VoiceLfo)
            return i;
    return -1;
}

juce::String midiInfo(int param)
{
    const auto& p = ParamTable::get(param);
    juce::String s;
    switch (p.midiKind)
    {
        case MidiKind::CC: s = "CC " + juce::String(p.midiNumber); break;
        case MidiKind::NRPN: s = "NRPN " + juce::String(p.midiNumber); break;
        case MidiKind::None: s = "no MIDI control"; break;
    }
    if (p.stockLimitation && *p.stockLimitation)
        s << " - stock firmware: " << p.stockLimitation;
    return s;
}
} // namespace

ParamControl::ParamControl(KitForgeProcessor& p, int param, bool showLabel) : proc_(p), param_(param), showLabel_(showLabel)
{
    const auto& info = ParamTable::get(param);
    min_ = info.minValue;
    auto* rp = proc_.parameterFor(param);
    jassert(rp != nullptr);

    const int count = info.maxValue - info.minValue + 1;
    if (info.dtype == DType::LfoTarget || info.dtype == DType::VeloTarget)
        kind_ = Kind::Target;
    else if ((info.dtype == DType::Menu || info.dtype == DType::OnOff || info.dtype == DType::VoiceLfo) && count <= 80)
        kind_ = Kind::Combo;

    label_.setJustificationType(juce::Justification::centred);
    label_.setFont(juce::FontOptions(11.5f));
    label_.setColour(juce::Label::textColourId, colours::dim);
    label_.setInterceptsMouseClicks(false, false);
    label_.setMinimumHorizontalScale(0.7f);
    if (showLabel_)
        addAndMakeVisible(label_);

    const juce::String tip = juce::String(ParamTable::fullName(param)) + "\n" + midiInfo(param);
    switch (kind_)
    {
        case Kind::Rotary:
            slider_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            slider_.setTextBoxStyle(juce::Slider::TextBoxBelow, false, kWidth - 6, 16);
            slider_.setTooltip(tip);
            slider_.getProperties().set("bipolar", info.dtype == DType::PM63 || info.dtype == DType::PM100);
            sliderAttachment_ = std::make_unique<juce::SliderParameterAttachment>(*rp, slider_);
            slider_.setDoubleClickReturnValue(true, ParamTable::clamp(param, info.defaultValue));
            addAndMakeVisible(slider_);
            slider_.addMouseListener(this, true);
            break;
        case Kind::Combo:
            for (int v = info.minValue; v <= info.maxValue; ++v)
                combo_.addItem(ParamTable::formatValue(param, v), v - info.minValue + 1);
            combo_.setTooltip(tip);
            attachment_ = std::make_unique<juce::ParameterAttachment>(*rp, [this](float v) {
                combo_.setSelectedId(juce::roundToInt(v) - min_ + 1, juce::dontSendNotification);
            });
            combo_.onChange = [this] {
                if (combo_.getSelectedId() > 0)
                {
                    const int value = combo_.getSelectedId() - 1 + min_;
                    if (param_ == pid::PAR_FX_TYPE && proc_.rawValue(param_) > ParamTable::get(param_).maxValue)
                        proc_.setValue(param_, value);
                    else
                        attachment_->setValueAsCompleteGesture((float) value);
                }
            };
            attachment_->sendInitialUpdate();
            addAndMakeVisible(combo_);
            combo_.addMouseListener(this, true);
            break;
        case Kind::Target:
            target_.setTooltip(tip);
            attachment_ = std::make_unique<juce::ParameterAttachment>(*rp, [this](float v) {
                target_.setButtonText(ParamTable::formatValue(param_, juce::roundToInt(v)));
            });
            target_.onClick = [this] { showTargetMenu(proc_, param_, &target_); };
            attachment_->sendInitialUpdate();
            addAndMakeVisible(target_);
            target_.addMouseListener(this, true);
            break;
    }
    refresh();
}

ParamControl::~ParamControl() = default;

juce::String ParamControl::labelText() const
{
    const auto& info = ParamTable::get(param_);
    if (param_ >= pid::PAR_FX_P1 && param_ <= pid::PAR_FX_P1 + 3)
        if (const char* l = ParamTable::fxParamLabel(proc_.rawValue(pid::PAR_FX_TYPE), param_ - pid::PAR_FX_P1))
            return l;
    return info.label ? info.label : info.id;
}

void ParamControl::refresh()
{
    if (param_ == pid::PAR_FX_TYPE && kind_ == Kind::Combo)
    {
        const int raw = proc_.rawValue(param_);
        if (raw > ParamTable::get(param_).maxValue)
            combo_.setText(raw == 6 ? "frz (extended)" : "FX " + juce::String(raw) + " (raw)", juce::dontSendNotification);
        else
            combo_.setSelectedId(raw - min_ + 1, juce::dontSendNotification);
    }
    label_.setText(labelText(), juce::dontSendNotification);
    const bool locked = proc_.isLocked(param_);
    label_.setColour(juce::Label::textColourId, locked ? colours::locked : colours::dim);
    slider_.setColour(juce::Slider::rotarySliderFillColourId, locked ? colours::locked : colours::accent);
    if (kind_ == Kind::Target && attachment_)
        target_.setButtonText(ParamTable::formatValue(param_, proc_.rawValue(param_)));
    repaint();
}

void ParamControl::paint(juce::Graphics& g)
{
    if (proc_.isLocked(param_))
    {
        g.setColour(colours::locked.withAlpha(0.12f));
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(1.0f), 4.0f);
    }
}

void ParamControl::resized()
{
    auto r = getLocalBounds().reduced(2);
    if (showLabel_)
        label_.setBounds(r.removeFromTop(16));
    switch (kind_)
    {
        case Kind::Rotary: slider_.setBounds(r); break;
        case Kind::Combo: combo_.setBounds(r.withSizeKeepingCentre(r.getWidth(), juce::jmin(24, r.getHeight()))); break;
        case Kind::Target: target_.setBounds(r.withSizeKeepingCentre(r.getWidth(), juce::jmin(24, r.getHeight()))); break;
    }
}

void ParamControl::mouseDown(const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showContextMenu();
}

void ParamControl::showContextMenu()
{
    const auto& info = ParamTable::get(param_);
    juce::PopupMenu m;
    m.addSectionHeader(ParamTable::fullName(param_));
    m.addItem(1, "Lock (keep when randomizing)", true, proc_.isLocked(param_));
    m.addItem(2, "Reset to default (" + juce::String(ParamTable::formatValue(param_, info.defaultValue)) + ")");
    if (info.voice >= 1)
        m.addItem(3, "Apply this value to all voices");
    m.addSeparator();
    m.addItem(4, midiInfo(param_), false);
    juce::Component::SafePointer<ParamControl> safe(this);
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(this), [safe](int r) {
        if (!safe)
            return;
        auto& proc = safe->proc_;
        const int p = safe->param_;
        if (r == 1)
            proc.setLocked(p, !proc.isLocked(p));
        else if (r == 2)
            proc.setValue(p, ParamTable::clamp(p, ParamTable::get(p).defaultValue));
        else if (r == 3)
        {
            Kit k = proc.currentKit();
            const int v = k.get(p);
            const auto& info2 = ParamTable::get(p);
            for (int voice = 1; voice <= kNumVoices; ++voice)
                if (voice != info2.voice)
                    if (const int q = equivalentParam(p, voice); q >= 0 && !proc.isLocked(q)
                                                                && info2.dtype != DType::LfoTarget && info2.dtype != DType::VeloTarget)
                        k.set(q, ParamTable::clamp(q, v));
            proc.setValues(k, {});
        }
    });
}

void ParamControl::showTargetMenu(KitForgeProcessor& proc, int param, juce::Component* target)
{
    const auto& info = ParamTable::get(param);
    const int current = proc.rawValue(param);
    const auto* targets = ParamTable::modTargets();
    const int n = ParamTable::numModTargets();
    juce::PopupMenu m;
    m.addItem(1, "none", true, current == 0);
    if (info.dtype == DType::VeloTarget)
    {
        const auto range = ParamTable::modTargetRange(info.voice);
        m.addSeparator();
        for (int t = range.first; t <= range.last && t < n; ++t)
            m.addItem(t + 1, targets[t].label, true, t == current);
    }
    else
    {
        for (int voice = 0; voice <= kNumVoices; ++voice)
        {
            juce::PopupMenu sub;
            bool any = false, ticked = false;
            for (int t = 1; t < n; ++t)
                if (targets[t].voice == voice)
                {
                    sub.addItem(t + 1, targets[t].label, true, t == current);
                    any = true;
                    ticked |= t == current;
                }
            if (any)
                m.addSubMenu(voice == 0 ? juce::String("Kit") : juce::String(ParamTable::voiceName(voice)), sub, true, nullptr, ticked);
        }
    }
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(target), [&proc, param](int r) {
        if (r <= 0)
            return;
        const int t = r - 1;
        Kit k = proc.currentKit();
        k.set(param, t);
        if (ParamTable::get(param).dtype == DType::LfoTarget)
        {
            const int tv = ParamTable::modTargets()[t].voice;
            if (const int vp = voiceLfoParamFor(param); vp >= 0 && tv >= 1)
                k.set(vp, tv);
        }
        proc.setValues(k, {});
    });
}

} // namespace kitforge
