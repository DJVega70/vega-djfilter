#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
enum { kFilter, kRes, kSlope, kDrive, kLfoDepth, kLfoRate, kNoise, kRepeat, kRepLen, kGate, kGateRate, kGateDepth, kEcho, kEchoTime, kEchoFb, kBrake, kBrakeStyle, kBrakeTime, kOut };
const char* ids[] = { "filter", "resonance", "slope", "drive", "lfoDepth", "lfoRate", "noise", "repeat", "repeatLen", "gate", "gateRate", "gateDepth", "echoOut", "echoTime", "echoFeedback", "brake", "brakeStyle", "brakeTime", "output" };
constexpr int numP = 19;
}

juce::StringArray VegaDJFilterProcessor::divNames() { return { "1/32", "1/16", "1/8T", "1/8", "1/8D", "1/4", "1/2", "1 Bar" }; }
double VegaDJFilterProcessor::divBeats (int i) { static const double b[] { 0.125, 0.25, 1.0 / 3, 0.5, 0.75, 1, 2, 4 }; return b[juce::jlimit (0, 7, i)]; }

void VegaDJFilterProcessor::mapKnob (float pos, int& mode, float& cutoff, float& wet)
{
    constexpr float dead = 0.03f, fade = 0.10f;
    const float a = std::abs (pos);
    if (a <= dead) { mode = -1; cutoff = pos < 0 ? 20000.0f : 20.0f; wet = 0; return; }
    mode = pos < 0 ? 0 : 1;
    const float t = juce::jlimit (0.0f, 1.0f, (a - dead) / (1.0f - dead));
    cutoff = mode == 0 ? 20000.0f * std::pow (40.0f / 20000.0f, t) : 20.0f * std::pow (12000.0f / 20.0f, t);
    wet = juce::jlimit (0.0f, 1.0f, (a - dead) / (fade - dead));
}

juce::AudioProcessorValueTreeState::ParameterLayout VegaDJFilterProcessor::layout()
{
    using namespace juce;
    std::vector<std::unique_ptr<RangedAudioParameter>> v;
    auto pct = [] (float x, int) { return String (roundToInt (x)) + "%"; };
    auto F = [&] (int id, const char* name, NormalisableRange<float> r, float def, std::function<String (float, int)> f)
    { v.push_back (std::make_unique<AudioParameterFloat> (ParameterID { ids[id], 1 }, name, r, def, AudioParameterFloatAttributes().withStringFromValueFunction (f))); };
    auto B = [&] (int id, const char* name) { v.push_back (std::make_unique<AudioParameterBool> (ParameterID { ids[id], 1 }, name, false)); };
    auto C = [&] (int id, const char* name, StringArray s, int def) { v.push_back (std::make_unique<AudioParameterChoice> (ParameterID { ids[id], 1 }, name, s, def)); };
    F (kFilter, "Filter", NormalisableRange<float> (-100, 100), 0, [] (float x, int) { return std::abs (x) < 3 ? String ("OFF") : (x < 0 ? "LP " : "HP ") + String (roundToInt (std::abs (x))); });
    F (kRes, "Resonance", NormalisableRange<float> (0, 100), 35, pct);
    C (kSlope, "Slope", { "12 dB", "24 dB" }, 1);
    F (kDrive, "Drive", NormalisableRange<float> (0, 100), 10, pct);
    F (kLfoDepth, "LFO Depth", NormalisableRange<float> (0, 100), 0, pct);
    C (kLfoRate, "LFO Rate", divNames(), 6);
    F (kNoise, "Riser Noise", NormalisableRange<float> (0, 100), 0, pct);
    B (kRepeat, "Beat Repeat"); C (kRepLen, "Repeat Length", divNames(), 1);
    B (kGate, "Gate"); C (kGateRate, "Gate Rate", divNames(), 1); F (kGateDepth, "Gate Depth", NormalisableRange<float> (0, 100), 100, pct);
    B (kEcho, "Echo Out"); C (kEchoTime, "Echo Time", divNames(), 4); F (kEchoFb, "Echo Feedback", NormalisableRange<float> (0, 95), 65, pct);
    B (kBrake, "Stop"); C (kBrakeStyle, "Stop Style", { "Vinyl Stop", "Spinback", "Tape Stop" }, 0);
    F (kBrakeTime, "Stop Time", NormalisableRange<float> (0.1f, 4.0f, 0, 0.5f), 1.0f, [] (float x, int) { return String (x, 2) + " s"; });
    F (kOut, "Output", NormalisableRange<float> (-24, 12), 0, [] (float x, int) { return String (x, 1) + " dB"; });
    return { v.begin(), v.end() };
}

VegaDJFilterProcessor::VegaDJFilterProcessor()
    : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true).withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", layout())
{
    for (int i = 0; i < numP; ++i) p[i] = apvts.getRawParameterValue (ids[i]);
}

bool VegaDJFilterProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return l.getMainInputChannelSet() == o && (o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono());
}

void VegaDJFilterProcessor::prepareToPlay (double s, int)
{
    sr = s;
    for (auto& r : ring) r.assign ((size_t) (s * 8.5), 0.0f);
    for (auto& e : echo) e.assign ((size_t) (s * 4.5), 0.0f);
    wpos = ewpos = 0;
    for (auto& a : svf) for (auto& f : a) f.reset();
    posS.reset (s, 0.04); posS.setCurrentAndTargetValue (p[kFilter]->load() * 0.01f);
    resS.reset (s, 0.04); resS.setCurrentAndTargetValue (p[kRes]->load() * 0.01f);
    repeatWas = brakeWas = false; dryGain = 1; echoIn = 0; gateEnv = 1; brakeFade = 0;
}

float VegaDJFilterProcessor::readRing (int c, double pos) const
{
    const auto& r = ring[c]; const int n = (int) r.size();
    while (pos < 0) pos += n; while (pos >= n) pos -= n;
    const int i = (int) pos; const float f = (float) (pos - i);
    return r[(size_t) i] + (r[(size_t) ((i + 1) % n)] - r[(size_t) i]) * f;
}

void VegaDJFilterProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals nd;
    const int n = buffer.getNumSamples(), nch = std::min (2, buffer.getNumChannels());
    if (n == 0 || nch == 0) return;
    double ppq = 0; bool hasPpq = false;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition())
    {
        if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 400.0, *b);
        if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) { ppq = *q; hasPpq = true; }
    }
    const double spb = sr * 60.0 / bpm;
    const int rn = (int) ring[0].size(), en = (int) echo[0].size();

    const bool repeatOn = p[kRepeat]->load() > 0.5f, gateOn = p[kGate]->load() > 0.5f, echoOn = p[kEcho]->load() > 0.5f, brakeOn = p[kBrake]->load() > 0.5f;
    const int slope = (int) p[kSlope]->load();
    const float drive = p[kDrive]->load() * 0.01f, driveG = 1.0f + drive * 5.0f;
    const float lfoDepth = p[kLfoDepth]->load() * 0.01f; const double lfoInc = 1.0 / (divBeats ((int) p[kLfoRate]->load()) * spb);
    const float noise = p[kNoise]->load() * 0.01f;
    const double gateBeats = divBeats ((int) p[kGateRate]->load()); const float gateDepth = p[kGateDepth]->load() * 0.01f;
    const int echoLen = juce::jlimit (16, en - 4, (int) (divBeats ((int) p[kEchoTime]->load()) * spb));
    const float echoFb = p[kEchoFb]->load() * 0.01f;
    const int brakeStyle = (int) p[kBrakeStyle]->load(); const double brakeTime = p[kBrakeTime]->load();
    const float outG = juce::Decibels::decibelsToGain (p[kOut]->load());
    posS.setTargetValue (p[kFilter]->load() * 0.01f); resS.setTargetValue (p[kRes]->load() * 0.01f);

    // edge-triggered events
    if (repeatOn && ! repeatWas)
    {
        repLen = juce::jlimit (32, rn / 2, (int) (divBeats ((int) p[kRepLen]->load()) * spb));
        repStart = wpos - repLen; if (repStart < 0) repStart += rn; repPos = 0;
    }
    if (brakeOn && ! brakeWas) { brakeRead = wpos; brakeSpeed = brakeStyle == 1 ? -3.0 : 1.0; brakeT = 0; }
    if (hasPpq) gatePhase = ppq / gateBeats;

    float* ch[2] { buffer.getWritePointer (0), buffer.getWritePointer (nch > 1 ? 1 : 0) };
    float pk[2] {}; int lastMode = -1; float lastCut = 1000, lastPos = 0;
    for (int i = 0; i < n; ++i)
    {
        float x[2] { ch[0][i], ch[1][i] };
        for (int c = 0; c < 2; ++c) ring[c][(size_t) wpos] = x[c];

        // ---- beat repeat (loops the last slice, 2 ms crossfade at the seam)
        if (repeatOn)
        {
            const int fadeN = (int) (0.002 * sr);
            for (int c = 0; c < 2; ++c)
            {
                float y = readRing (c, repStart + repPos);
                if (repPos >= repLen - fadeN) { const float t = (float) (repPos - (repLen - fadeN)) / fadeN; y = y * (1 - t) + readRing (c, repStart + repPos - repLen) * t; }
                x[c] = y;
            }
            if (++repPos >= repLen) repPos = 0;
        }
        // ---- vinyl stop / spinback / tape stop
        if (brakeOn)
        {
            brakeT += 1.0 / sr;
            const double t = juce::jlimit (0.0, 1.0, brakeT / brakeTime);
            if (brakeStyle == 0)      brakeSpeed = std::pow (1.0 - t, 1.6);            // vinyl: motor slows
            else if (brakeStyle == 2) brakeSpeed = 1.0 - t;                            // tape: linear
            else                      brakeSpeed = -3.0 * (1.0 - t) * (1.0 - t);       // spinback: fast reverse that slows down
            const float lvl = (float) std::min (1.0, std::abs (brakeSpeed) * 4.0);
            for (int c = 0; c < 2; ++c) x[c] = readRing (c, brakeRead) * lvl;
            brakeRead += brakeSpeed;
            brakeFade = 1.0f;
        }
        else if (brakeFade > 0) { brakeFade = std::max (0.0f, brakeFade - 1.0f / (float) (0.01 * sr)); for (auto& v : x) v *= 1.0f - brakeFade; }

        // ---- riser noise
        if (noise > 0) { const float w = rng.nextFloat() * 2.0f - 1.0f; noiseLp += 0.5f * (w - noiseLp); for (auto& v : x) v += noise * 0.25f * (w - 0.3f * noiseLp); }

        // ---- filter
        float pos = posS.getNextValue();
        if (lfoDepth > 0)
        {
            lfoPhase += lfoInc; if (lfoPhase >= 1) lfoPhase -= 1;
            if (std::abs (pos) > 0.03f) pos = juce::jlimit (-1.0f, 1.0f, pos + (pos < 0 ? -1.0f : 1.0f) * lfoDepth * 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * lfoPhase));
        }
        int mode; float cut, wet; mapKnob (pos, mode, cut, wet);
        lastMode = mode; lastCut = cut; lastPos = pos;
        const float res = resS.getNextValue();
        const float k = 2.0f - 1.9f * res;              // damping: 2 (Q 0.5) .. 0.1 (Q 10)
        const float g = std::tan (juce::MathConstants<float>::pi * std::min (cut, (float) sr * 0.45f) / (float) sr);
        for (int c = 0; c < 2; ++c)
        {
            float d = x[c];
            if (drive > 0 && wet > 0) d += (std::tanh (d * driveG) / driveG - d) * wet;
            float y = svf[0][c].process (d, g, k, std::max (0, mode));
            if (slope == 1) y = svf[1][c].process (y, g, std::max (k, 1.0f), std::max (0, mode));
            if (res > 0.6f) y = std::tanh (y * 0.8f) / 0.8f;          // tame screaming resonance
            x[c] = d + (y - d) * wet;
            if (mode < 0) x[c] = d;
        }

        // ---- trance gate
        if (gateOn)
        {
            gatePhase += 1.0 / (gateBeats * spb);
            const double ph = gatePhase - std::floor (gatePhase);
            const float target = ph < 0.5 ? 1.0f : 1.0f - gateDepth;
            gateEnv += (target - gateEnv) * 0.02f;
            for (auto& v : x) v *= gateEnv;
        }
        else gateEnv += (1.0f - gateEnv) * 0.02f;

        // ---- echo out: dry fades, echoes keep going
        dryGain += ((echoOn ? 0.0f : 1.0f) - dryGain) * (float) (1.0 / (0.04 * sr));
        echoIn = echoOn ? std::min (1.0f, echoIn + (float) (1.0 / (0.005 * sr))) : std::max (0.0f, echoIn - (float) (1.0 / (0.02 * sr)));
        int rp = ewpos - echoLen; if (rp < 0) rp += en;
        for (int c = 0; c < 2; ++c)
        {
            const float e = echo[c][(size_t) rp];
            const float other = echo[1 - c][(size_t) rp];
            const float feed = (x[c] * dryGain) * echoIn * (dryGain > 0.02f ? 1.0f : 0.0f) + (0.7f * e + 0.3f * other) * echoFb;
            echo[c][(size_t) ewpos] = std::tanh (feed);
            x[c] = x[c] * dryGain + e;
        }
        if (++ewpos >= en) ewpos = 0;
        if (++wpos >= rn) wpos = 0;

        for (int c = 0; c < nch; ++c) { ch[c][i] = x[c] * outG; pk[c] = std::max (pk[c], std::abs (ch[c][i])); }
    }
    repeatWas = repeatOn; brakeWas = brakeOn;
    uiPos.store (lastPos); uiMode.store (lastMode); uiCutoff.store (lastCut); uiBrake.store ((float) brakeSpeed);
    outL.store (std::max (outL.load(), pk[0])); outR.store (std::max (outR.load(), nch > 1 ? pk[1] : pk[0]));
}

void VegaDJFilterProcessor::getStateInformation (juce::MemoryBlock& d)
{
    auto s = apvts.copyState(); s.setProperty ("uiScale", uiScale.load(), nullptr);
    if (auto x = s.createXml()) copyXmlToBinary (*x, d);
}
void VegaDJFilterProcessor::setStateInformation (const void* data, int size)
{
    if (auto x = getXmlFromBinary (data, size))
    {
        auto s = juce::ValueTree::fromXml (*x);
        if (! s.hasType (apvts.state.getType())) return;
        uiScale.store ((float) s.getProperty ("uiScale", 1.0f));
        apvts.replaceState (s);
    }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VegaDJFilterProcessor(); }
