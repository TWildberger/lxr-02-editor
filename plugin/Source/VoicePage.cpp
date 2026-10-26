#include "VoicePage.h"

#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "kitforge/KitOps.h"
#include "kitforge/ParamTable.h"

namespace kitforge
{

SectionBox::SectionBox(const juce::String& title, int maxColumns) : title_(title), maxColumns_(maxColumns) {}

void SectionBox::add(std::unique_ptr<ParamControl> c)
{
    addAndMakeVisible(*c);
    controls_.push_back(std::move(c));
}

juce::Point<int> SectionBox::preferredSize() const
{
    const int n = (int) controls_.size();
    const int cols = juce::jmax(1, juce::jmin(n, maxColumns_));
    const int rows = (n + cols - 1) / cols;
    return { cols * ParamControl::kWidth + 12, rows * ParamControl::kHeight + 30 };
}

void SectionBox::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(2.0f);
    g.setColour(colours::panel);
    g.fillRoundedRectangle(r, 6.0f);
    g.setColour(colours::outline);
    g.drawRoundedRectangle(r, 6.0f, 1.0f);
    g.setColour(colours::accent);
    g.setFont(juce::FontOptions(13.0f, juce::Font::bold));
    g.drawText(title_.toUpperCase(), getLocalBounds().reduced(10, 6).removeFromTop(16), juce::Justification::centredLeft);
}

void SectionBox::resized()
{
    const int n = (int) controls_.size();
    const int cols = juce::jmax(1, juce::jmin(n, maxColumns_));
    for (int i = 0; i < n; ++i)
        controls_[(size_t) i]->setBounds(6 + (i % cols) * ParamControl::kWidth, 26 + (i / cols) * ParamControl::kHeight,
                                         ParamControl::kWidth, ParamControl::kHeight);
}

int VoicePage::Content::layout(int width)
{
    int x = 0, y = 0, rowHeight = 0;
    for (auto& b : boxes)
    {
        const auto s = b->preferredSize();
        if (x > 0 && x + s.x > width)
        {
            x = 0;
            y += rowHeight + 6;
            rowHeight = 0;
        }
        b->setBounds(x, y, s.x, s.y);
        x += s.x + 6;
        rowHeight = juce::jmax(rowHeight, s.y);
    }
    return y + rowHeight;
}

VoicePage::VoicePage(KitForgeProcessor& p, int voice) : proc_(p), voice_(voice)
{
    title_.setText(voice == 0 ? juce::String("FX & Kit") : juce::String(ParamTable::voiceName(voice)), juce::dontSendNotification);
    title_.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    addAndMakeVisible(title_);

    std::vector<int> params;
    for (int i : ParamTable::paramsForVoice(voice))
        if (ParamTable::get(i).isUserFacing() && proc_.parameterFor(i))
            params.push_back(i);
    std::stable_sort(params.begin(), params.end(), [](int a, int b) { return ParamTable::get(a).order < ParamTable::get(b).order; });

    std::vector<juce::String> order;
    std::map<juce::String, std::vector<int>> bySection;
    for (int i : params)
    {
        const juce::String s = ParamTable::get(i).section;
        if (!bySection.count(s))
            order.push_back(s);
        bySection[s].push_back(i);
    }
    for (const auto& s : order)
    {
        auto box = std::make_unique<SectionBox>(s, s == "FX" ? 6 : 4);
        for (int i : bySection[s])
            box->add(std::make_unique<ParamControl>(proc_, i));
        content_.addAndMakeVisible(*box);
        content_.boxes.push_back(std::move(box));
    }
    viewport_.setViewedComponent(&content_, false);
    viewport_.setScrollBarsShown(true, false);
    addAndMakeVisible(viewport_);

    if (voice >= 1)
    {
        copy_.onClick = [this] { showCopyMenu(); };
        addAndMakeVisible(copy_);
    }
    randomize_.onClick = [this] { showRandomizeMenu(); };
    reset_.onClick = [this] { resetVoice(); };
    reset_.setButtonText(voice == 0 ? "Reset FX" : "Reset voice");
    addAndMakeVisible(randomize_);
    addAndMakeVisible(reset_);

    if (voice == 0)
        if (auto* fx = proc_.parameterFor(pid::PAR_FX_TYPE))
            fxTypeWatch_ = std::make_unique<juce::ParameterAttachment>(*fx, [this](float) { refreshControls(); });
    if (voice >= 1 && voice <= 3)
    {
        engine_.addItem("Legacy", 1);
        engine_.addItem("Feedback FM", 2);
        engine_.onChange = [this] { editEngine(); };
        timbreDecay_.setRange(0, 127, 1);
        timbreDecay_.setSliderStyle(juce::Slider::LinearHorizontal);
        timbreDecay_.setChangeNotificationOnlyOnRelease(true);
        timbreDecay_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);
        timbreDecay_.onDragStart = [this] { proc_.pushUndo(); };
        timbreDecay_.onValueChange = [this] { editEngine(); };
        engine_.setTooltip("Explicit EngV01 opt-in for Drum 1-3. Older firmware plays the legacy engine.");
        timbreDecay_.setTooltip("Static feedback-FM timbre envelope decay, 0-127; not host automated.");
        addAndMakeVisible(engineLabel_);
        addAndMakeVisible(engine_);
        addAndMakeVisible(timbreLabel_);
        addAndMakeVisible(timbreDecay_);
    }
    proc_.kitChanged.addChangeListener(this);
    refreshControls();
}

VoicePage::~VoicePage() { proc_.kitChanged.removeChangeListener(this); }

void VoicePage::changeListenerCallback(juce::ChangeBroadcaster*) { refreshControls(); }

void VoicePage::refreshControls()
{
    if (voice_ >= 1 && voice_ <= 3)
    {
        const auto k = proc_.currentKit();
        const bool known = k.extension().empty() || k.hasEngineExtension();
        engine_.setEnabled(known);
        timbreDecay_.setEnabled(known);
        engine_.setSelectedId(k.hasEngineExtension() ? k.get(Kit::kEngineFirst + voice_ - 1) + 1 : 1,
                              juce::dontSendNotification);
        timbreDecay_.setValue(k.hasEngineExtension() ? k.get(Kit::kTimbreDecayFirst + voice_ - 1) : 0,
                              juce::dontSendNotification);
    }
    for (auto& b : content_.boxes)
        for (auto& c : b->controls())
            c->refresh();
}

void VoicePage::resized()
{
    auto r = getLocalBounds().reduced(8);
    auto top = r.removeFromTop(28);
    title_.setBounds(top.removeFromLeft(200));
    reset_.setBounds(top.removeFromRight(110));
    top.removeFromRight(6);
    randomize_.setBounds(top.removeFromRight(110));
    top.removeFromRight(6);
    if (voice_ >= 1)
        copy_.setBounds(top.removeFromRight(130));
    r.removeFromTop(6);
    if (voice_ >= 1 && voice_ <= 3)
    {
        auto engineRow = r.removeFromTop(30);
        engineLabel_.setBounds(engineRow.removeFromLeft(60));
        engine_.setBounds(engineRow.removeFromLeft(150));
        engineRow.removeFromLeft(14);
        timbreLabel_.setBounds(engineRow.removeFromLeft(100));
        timbreDecay_.setBounds(engineRow.removeFromLeft(250));
        r.removeFromTop(6);
    }
    viewport_.setBounds(r);
    const int w = r.getWidth() - viewport_.getScrollBarThickness();
    content_.setSize(w, content_.layout(w));
}

void VoicePage::showCopyMenu()
{
    juce::PopupMenu m;
    for (int v = 1; v <= kNumVoices; ++v)
        if (v != voice_)
            m.addItem(v, ParamTable::voiceName(v));
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&copy_), [this](int to) {
        if (to <= 0)
            return;
        Kit k = proc_.currentKit();
        const int n = copyVoice(k, voice_, to);
        if (k.hasEngineExtension() && voice_ <= 3 && to <= 3)
            k.setEngineSettings(to, k.get(Kit::kEngineFirst + voice_ - 1), k.get(Kit::kTimbreDecayFirst + voice_ - 1));
        proc_.setValues(k, "Copied " + juce::String(n) + " parameters from " + ParamTable::voiceName(voice_) + " to "
                               + ParamTable::voiceName(to));
    });
}

void VoicePage::showRandomizeMenu()
{
    juce::PopupMenu m;
    m.addItem(1, "Subtle (10%)");
    m.addItem(2, "Moderate (30%)");
    m.addItem(3, "Wild (60%)");
    m.addItem(4, "Fully random");
    m.addSeparator();
    m.addItem(5, "Fully random, including modulation targets");
    m.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&randomize_), [this](int r) {
        if (r <= 0)
            return;
        static constexpr double amounts[] = { 0.0, 0.1, 0.3, 0.6, 1.0, 1.0 };
        RandomizeOptions o;
        o.voices = { voice_ };
        o.amount = amounts[r];
        o.includeModTargets = r == 5;
        o.lockedParams = proc_.locks();
        Kit k = proc_.currentKit();
        randomizeKit(k, o, rng_);
        proc_.setValues(k, {});
    });
}

void VoicePage::resetVoice()
{
    Kit k = proc_.currentKit();
    const Kit init = Kit::init();
    for (int i : ParamTable::paramsForVoice(voice_))
        if (!proc_.isLocked(i))
            k.set(i, init.get(i));
    if (k.hasEngineExtension() && voice_ >= 1 && voice_ <= 3)
        k.setEngineSettings(voice_, 0, 0);
    proc_.setValues(k, {});
}

void VoicePage::editEngine()
{
    auto k = proc_.currentKit();
    std::string error;
    if (!k.setEngineSettings(voice_, engine_.getSelectedId() - 1, (int) timbreDecay_.getValue(), &error))
    {
        proc_.addLog("Engine edit failed: " + juce::String(error));
        return;
    }
    proc_.setValues(k, {});
}

} // namespace kitforge
