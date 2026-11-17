#include "PluginEditor.h"

namespace
{
// Response display: draws the current filter curve, plus the big state readout
class FilterView : public juce::Component
{
public:
    explicit FilterView (VegaDJFilterProcessor& p) : proc (p) {}
    void paint (juce::Graphics& g) override
    {
        vegaui::drawScreen (g, getLocalBounds().toFloat());
        auto r = getLocalBounds().toFloat().reduced (12.0f, 10.0f);
        auto& a = proc.apvts;
        const float pos = a.getRawParameterValue ("filter")->load() * 0.01f, res = a.getRawParameterValue ("resonance")->load() * 0.01f;
        const bool steep = a.getRawParameterValue ("slope")->load() > 0.5f;
        int mode; float cut, wet; VegaDJFilterProcessor::mapKnob (pos, mode, cut, wet);
        auto top = r.removeFromTop (22.0f);
        g.setFont (VegaFonts::mono (13.0f)); g.setColour (VegaColours::accent);
        g.drawText (mode < 0 ? juce::String ("FILTER OFF") : (mode == 0 ? "LOW-PASS  " : "HIGH-PASS  ") + (cut >= 1000 ? juce::String (cut / 1000, 2) + " kHz" : juce::String (juce::roundToInt (cut)) + " Hz"),
                    top, juce::Justification::centredLeft);
        g.setColour (VegaColours::textDim); g.drawText (juce::String (proc.bpm, 1) + " BPM", top, juce::Justification::centredRight);
        auto fx = [&] (float f) { return r.getX() + r.getWidth() * std::log (f / 20.0f) / std::log (1000.0f); };
        auto dy = [&] (float db) { return r.getY() + r.getHeight() * (1.0f - (db + 36.0f) / 54.0f); };
        g.setFont (VegaFonts::ui (9.0f));
        for (float f : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
        {
            g.setColour (juce::Colour (0xff252525)); g.drawVerticalLine ((int) fx (f), r.getY(), r.getBottom());
            g.setColour (VegaColours::textDim.withAlpha (0.5f)); g.drawText (f >= 1000 ? juce::String ((int) f / 1000) + "k" : juce::String ((int) f), (int) fx (f) + 2, (int) r.getBottom() - 11, 30, 11, juce::Justification::left);
        }
        g.setColour (juce::Colour (0xff303030)); g.drawHorizontalLine ((int) dy (0), r.getX(), r.getRight());
        const float k = 2.0f - 1.9f * res;
        juce::Path path;
        for (int i = 0; i <= 300; ++i)
        {
            const float f = 20.0f * std::pow (1000.0f, i / 300.0f);
            float db = 0;
            if (mode >= 0)
            {   // analog prototype magnitude of the SVF
                const float w = f / cut, w2 = w * w;
                const float den = std::sqrt ((1 - w2) * (1 - w2) + k * k * w2);
                float mag = mode == 0 ? 1.0f / den : w2 / den;
                if (steep) { const float k2 = std::max (k, 1.0f), den2 = std::sqrt ((1 - w2) * (1 - w2) + k2 * k2 * w2); mag *= mode == 0 ? 1.0f / den2 : w2 / den2; }
                mag = 1.0f + (mag - 1.0f) * wet;
                db = juce::jlimit (-36.0f, 18.0f, juce::Decibels::gainToDecibels (mag, -60.0f));
            }
            const juce::Point<float> pt (fx (f), dy (db));
            if (i == 0) path.startNewSubPath (pt); else path.lineTo (pt);
        }
        juce::Path fill (path); fill.lineTo (r.getRight(), r.getBottom()); fill.lineTo (r.getX(), r.getBottom()); fill.closeSubPath();
        g.setColour (VegaColours::accent.withAlpha (0.12f)); g.fillPath (fill);
        g.setColour (VegaColours::accent); g.strokePath (path, juce::PathStrokeType (2.0f));
    }
private:
    VegaDJFilterProcessor& proc;
};

// Big latching performance pad bound to a bool parameter
class FxPad : public juce::Component, private juce::AudioProcessorValueTreeState::Listener
{
public:
    FxPad (juce::AudioProcessorValueTreeState& s, const juce::String& pid, const juce::String& t, const juce::String& sub) : state (s), id (pid), title (t), subtitle (sub)
    {
        state.addParameterListener (id, this);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }
    ~FxPad() override { state.removeParameterListener (id, this); }
    bool momentary = false;
    void paint (juce::Graphics& g) override
    {
        const bool on = state.getRawParameterValue (id)->load() > 0.5f;
        auto b = getLocalBounds().toFloat().reduced (3.0f);
        g.setColour (juce::Colour (on ? 0xff1d3b3a : 0xff1f1f1f)); g.fillRoundedRectangle (b, 6.0f);
        if (on) { g.setColour (VegaColours::accent.withAlpha (0.25f)); g.drawRoundedRectangle (b.expanded (1.5f), 7.0f, 3.0f); }
        g.setColour (on ? VegaColours::accent : VegaColours::line); g.drawRoundedRectangle (b, 6.0f, 1.5f);
        g.setColour (on ? VegaColours::accent : VegaColours::text); g.setFont (VegaFonts::ui (15.0f, true));
        g.drawText (title, b.withTrimmedBottom (b.getHeight() * 0.4f).toNearestInt(), juce::Justification::centredBottom);
        g.setColour (VegaColours::textDim); g.setFont (VegaFonts::ui (10.0f));
        g.drawText (momentary ? subtitle + "  (hold)" : subtitle, b.withTrimmedTop (b.getHeight() * 0.62f).toNearestInt(), juce::Justification::centredTop);
    }
    void mouseDown (const juce::MouseEvent&) override
    {
        auto* prm = state.getParameter (id);
        const bool on = prm->getValue() > 0.5f;
        prm->beginChangeGesture(); prm->setValueNotifyingHost (momentary ? 1.0f : (on ? 0.0f : 1.0f)); prm->endChangeGesture();
    }
    void mouseUp (const juce::MouseEvent&) override
    {
        if (! momentary) return;
        auto* prm = state.getParameter (id); prm->beginChangeGesture(); prm->setValueNotifyingHost (0.0f); prm->endChangeGesture();
    }
private:
    void parameterChanged (const juce::String&, float) override { juce::MessageManager::callAsync ([sp = juce::Component::SafePointer<FxPad> (this)] { if (sp != nullptr) sp->repaint(); }); }
    juce::AudioProcessorValueTreeState& state; juce::String id, title, subtitle;
};
}

class VegaDJFilterEditor::Main : public juce::Component
{
public:
    explicit Main (VegaDJFilterProcessor& p) : proc (p), a (p.apvts), view (p),
        filter (a, "filter", "FILTER", true), res (a, "resonance", "Resonance"), slope (a, "slope", "Slope"), drive (a, "drive", "Drive"),
        lfoDepth (a, "lfoDepth", "LFO Depth"), lfoRate (a, "lfoRate", "LFO Rate"), noise (a, "noise", "Riser Noise"), output (a, "output", "Output"),
        repLen (a, "repeatLen", "Length"), gateRate (a, "gateRate", "Rate"), gateDepth (a, "gateDepth", "Depth"),
        echoTime (a, "echoTime", "Time"), echoFb (a, "echoFeedback", "Feedback"), brakeStyle (a, "brakeStyle", "Style"), brakeTime (a, "brakeTime", "Time"),
        repeatPad (a, "repeat", "BEAT REPEAT", "loops the last slice"), gatePad (a, "gate", "GATE", "trance chop"),
        echoPad (a, "echoOut", "ECHO OUT", "dry out, echoes ring"), brakePad (a, "brake", "STOP", "vinyl / spinback / tape")
    {
        for (auto* c : std::initializer_list<juce::Component*> { &view, &filter, &res, &slope, &drive, &lfoDepth, &lfoRate, &noise, &output, &repLen, &gateRate, &gateDepth,
                                                                 &echoTime, &echoFb, &brakeStyle, &brakeTime, &repeatPad, &gatePad, &echoPad, &brakePad, &meter })
            addAndMakeVisible (c);
        filter.slider.setMouseDragSensitivity (420);
        holdMode.setButtonText ("HOLD TO PLAY"); holdMode.setClickingTogglesState (true);
        holdMode.onClick = [this] { for (auto* pd : { &repeatPad, &gatePad, &echoPad, &brakePad }) { pd->momentary = holdMode.getToggleState(); pd->repaint(); } };
        addAndMakeVisible (holdMode);
    }
    void refresh()
    {
        view.repaint();
        meter.setLevels (proc.outL.exchange (0.0f), proc.outR.exchange (0.0f));
    }
    void paint (juce::Graphics& g) override
    {
        g.fillAll (VegaColours::background);
        vegaui::drawTopBar (g, W, "DJ FILTER  VP-13");
        vegaui::drawBody (g, { 0, 35, W, H - 65 });
        g.setFont (VegaFonts::ui (12.0f, true)); g.setColour (VegaColours::textDim);
        g.drawText ("LP", 40, 300, 40, 16, juce::Justification::left); g.drawText ("HP", 232, 300, 40, 16, juce::Justification::right);
        vegaui::drawSection (g, { 310, 204, 380, 140 }, "FILTER");
        vegaui::drawSection (g, { 690, 204, W - 690, 140 }, "MOVEMENT");
        g.setColour (VegaColours::line); g.drawHorizontalLine (348, 0, (float) W);
        vegaui::drawSection (g, { 0, 352, 250, 218 }, "BEAT REPEAT", false);
        vegaui::drawSection (g, { 250, 352, 250, 218 }, "GATE");
        vegaui::drawSection (g, { 500, 352, 250, 218 }, "ECHO OUT");
        vegaui::drawSection (g, { 750, 352, 250, 218 }, "STOP");
        vegaui::drawBottomBar (g, { 0, H - 30, W, 30 }, "LEFT = LOW-PASS   CENTRE = OFF   RIGHT = HIGH-PASS   -   PADS ARE AUTOMATABLE IN FL STUDIO");
    }
    void resized() override
    {
        view.setBounds (12, 44, 900, 150); meter.setBounds (924, 40, 64, 160);
        filter.setBounds (70, 198, 170, 150); filter.slider.setMouseDragSensitivity (420);
        res.setBounds (322, 238, 80, 104); slope.setBounds (406, 250, 100, 46); drive.setBounds (512, 238, 80, 104); output.setBounds (600, 238, 80, 104);
        lfoDepth.setBounds (700, 238, 80, 104); lfoRate.setBounds (784, 250, 100, 46); noise.setBounds (896, 238, 92, 104);
        holdMode.setBounds (784, 306, 100, 22);
        auto padRow = [&] (FxPad& pad, int x, juce::Component& k1, juce::Component& k2)
        {
            pad.setBounds (x + 14, 372, 222, 84);
            k1.setBounds (x + 14, 466, 110, 46); k2.setBounds (x + 130, 458, 100, 104);
        };
        padRow (repeatPad, 0, repLen, dummy);
        padRow (gatePad, 250, gateRate, gateDepth);
        padRow (echoPad, 500, echoTime, echoFb);
        padRow (brakePad, 750, brakeStyle, brakeTime);
    }
private:
    VegaDJFilterProcessor& proc; juce::AudioProcessorValueTreeState& a;
    FilterView view;
    vegaui::Knob filter, res; vegaui::Choice slope; vegaui::Knob drive, lfoDepth; vegaui::Choice lfoRate; vegaui::Knob noise, output;
    vegaui::Choice repLen, gateRate; vegaui::Knob gateDepth; vegaui::Choice echoTime; vegaui::Knob echoFb; vegaui::Choice brakeStyle; vegaui::Knob brakeTime;
    FxPad repeatPad, gatePad, echoPad, brakePad;
    juce::TextButton holdMode; juce::Component dummy;
    vegaui::StereoMeter meter;
};

VegaDJFilterEditor::VegaDJFilterEditor (VegaDJFilterProcessor& p) : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);
    main = std::make_unique<Main> (p);
    addAndMakeVisible (*main);
    constrainer.setFixedAspectRatio ((double) W / H);
    constrainer.setSizeLimits (W / 2, H / 2, W * 2, H * 2);
    setConstrainer (&constrainer);
    setResizable (true, true);
    const float s = juce::jlimit (0.5f, 2.0f, proc.uiScale.load());
    setSize (juce::roundToInt (W * s), juce::roundToInt (H * s));
    startTimerHz (30);
}
VegaDJFilterEditor::~VegaDJFilterEditor() { stopTimer(); setConstrainer (nullptr); main.reset(); setLookAndFeel (nullptr); }
void VegaDJFilterEditor::timerCallback() { main->refresh(); }
void VegaDJFilterEditor::resized()
{
    main->setBounds (0, 0, W, H);
    const float s = (float) getWidth() / (float) W;
    main->setTransform (juce::AffineTransform::scale (s));
    proc.uiScale.store (s);
}
juce::AudioProcessorEditor* VegaDJFilterProcessor::createEditor() { return new VegaDJFilterEditor (*this); }
