#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

// Zero-delay-feedback state variable filter (Simper / TPT)
struct Svf
{
    float ic1 = 0, ic2 = 0;
    void reset() { ic1 = ic2 = 0; }
    // mode 0 = low-pass, 1 = high-pass
    float process (float x, float g, float k, int mode)
    {
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;
        const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2.0f * v1 - ic1; ic2 = 2.0f * v2 - ic2;
        return mode == 0 ? v2 : x - k * v1 - v2;
    }
};

class VegaDJFilterProcessor : public juce::AudioProcessor
{
public:
    VegaDJFilterProcessor();
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void prepareToPlay (double, int) override;
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Vega Productions DJ Filter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    static juce::StringArray divNames();
    static double divBeats (int);
    juce::AudioProcessorValueTreeState apvts;

    // knob position (-1..1) -> filter mode and cutoff; |pos| < dead = bypass
    static void mapKnob (float pos, int& mode, float& cutoff, float& wet);

    std::atomic<float> uiPos { 0 }, uiCutoff { 1000 }, outL { 0 }, outR { 0 }, uiBrake { 1 };
    std::atomic<int> uiMode { -1 };
    std::atomic<float> uiScale { 1.0f };
    double bpm = 120;

private:
    double sr = 48000;
    Svf svf[2][2];
    std::vector<float> ring[2]; int wpos = 0;
    // beat repeat
    bool repeatWas = false; int repStart = 0, repLen = 1, repPos = 0;
    // brake / spinback
    bool brakeWas = false; double brakeRead = 0, brakeSpeed = 1, brakeT = 0; float brakeFade = 0;
    // echo out
    std::vector<float> echo[2]; int ewpos = 0; float dryGain = 1, echoIn = 0;
    // gate / lfo
    double lfoPhase = 0, gatePhase = 0; float gateEnv = 1;
    juce::SmoothedValue<float> posS, resS;
    float noiseLp = 0; juce::Random rng;
    float readRing (int c, double pos) const;
    std::atomic<float>* p[20] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VegaDJFilterProcessor)
};
