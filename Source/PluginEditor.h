#pragma once
#include "PluginProcessor.h"
#include "VegaUI.h"

class VegaDJFilterEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit VegaDJFilterEditor (VegaDJFilterProcessor&);
    ~VegaDJFilterEditor() override;
    void paint (juce::Graphics&) override {}
    void resized() override;
    static constexpr int W = 1000, H = 600;
    class Main;
private:
    void timerCallback() override;
    VegaDJFilterProcessor& proc;
    VegaLookAndFeel lnf;
    std::unique_ptr<Main> main;
    juce::ComponentBoundsConstrainer constrainer;
};
