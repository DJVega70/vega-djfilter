#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaDJFilterProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
struct Head : juce::AudioPlayHead { juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (120); return i; } };
const double fs = 48000; const int blk = 480;

// process `secs` of a signal generator, return output channel 0
template <typename Gen> static std::vector<float> run (VegaDJFilterProcessor& p, double secs, Gen gen, int startSample = 0)
{
    const int n = (int) (secs * fs); std::vector<float> out ((size_t) n);
    juce::AudioBuffer<float> b (2, blk); juce::MidiBuffer m;
    for (int pos = 0; pos < n; pos += blk)
    {
        for (int i = 0; i < blk; ++i) { const float x = gen (startSample + pos + i); b.setSample (0, i, x); b.setSample (1, i, x); }
        p.processBlock (b, m);
        for (int i = 0; i < blk && pos + i < n; ++i) out[(size_t) (pos + i)] = b.getSample (0, i);
    }
    return out;
}
static double rms (const std::vector<float>& v, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += v[(size_t) i] * v[(size_t) i]; return std::sqrt (s / std::max (1, b - a)); }
static auto sine (float f, float amp = 0.5f) { return [f, amp] (int i) { return amp * (float) std::sin (2 * juce::MathConstants<double>::pi * f * i / fs); }; }

int main()
{
    juce::ScopedJuceInitialiser_GUI init;
    Head head;
    auto make = [&] { auto* p = new VegaDJFilterProcessor(); p->setPlayConfigDetails (2, 2, fs, blk); p->prepareToPlay (fs, blk); p->setPlayHead (&head); return std::unique_ptr<VegaDJFilterProcessor> (p); };
    {
        auto p = make(); auto o = run (*p, 0.5, sine (1000));
        double err = 0; for (int i = 0; i < (int) o.size(); ++i) err = std::max (err, (double) std::abs (o[(size_t) i] - sine (1000) (i)));
        check (err < 1e-5, "centre position is a perfect bypass (max error " + juce::String (err, 8) + ")");
        p->setPlayHead (nullptr);
    }
    {
        auto p = make(); set (*p, "filter", -70); set (*p, "resonance", 0);
        run (*p, 0.2, sine (5000));
        auto hi = run (*p, 0.5, sine (5000)); auto lo = run (*p, 0.5, sine (50));
        check (rms (hi, 4800, 24000) < 0.01 && rms (lo, 4800, 24000) > 0.25, "low-pass at -70 cuts 5 kHz (" + juce::String (rms (hi, 4800, 24000), 4) + "), keeps 50 Hz (" + juce::String (rms (lo, 4800, 24000), 3) + ")");
        p->setPlayHead (nullptr);
    }
    {
        auto p = make(); set (*p, "filter", 60); set (*p, "resonance", 0);
        run (*p, 0.2, sine (60));
        auto lo = run (*p, 0.5, sine (60)); auto hi = run (*p, 0.5, sine (8000));
        check (rms (lo, 4800, 24000) < 0.01 && rms (hi, 4800, 24000) > 0.3, "high-pass at +60 cuts 60 Hz (" + juce::String (rms (lo, 4800, 24000), 4) + "), keeps 8 kHz");
        p->setPlayHead (nullptr);
    }
    {   // max resonance sweep stays bounded
        auto p = make(); set (*p, "resonance", 100); set (*p, "drive", 100);
        float worst = 0; bool fin = true;
        for (int k = -100; k <= 100; k += 10) { set (*p, "filter", (float) k); auto o = run (*p, 0.1, sine (300, 0.8f)); for (float v : o) { fin &= std::isfinite (v); worst = std::max (worst, std::abs (v)); } }
        check (fin && worst < 4.0f, "full sweep at max resonance + drive stays stable (peak " + juce::String (worst, 2) + ")");
        p->setPlayHead (nullptr);
    }
    {   // beat repeat: output repeats with the slice period (1/16 at 120 = 6000 samples)
        auto p = make(); set (*p, "repeatLen", 1);
        juce::Random r (3); auto noise = [&r] (int) { return r.nextFloat() - 0.5f; };
        run (*p, 0.5, noise);
        set (*p, "repeat", 1);
        auto o = run (*p, 0.5, noise);
        double diff = 0, sig = 0;
        for (int i = 6000; i < 18000; ++i) { if (i % 6000 < 200 || i % 6000 > 5800) continue; diff += std::pow (o[(size_t) i] - o[(size_t) (i - 6000)], 2); sig += o[(size_t) i] * o[(size_t) i]; }
        check (sig > 0 && diff / sig < 1e-6, "beat repeat loops a 1/16 slice exactly (residual " + juce::String (diff / sig, 8) + ")");
        set (*p, "repeat", 0); p->setPlayHead (nullptr);
    }
    {   // vinyl stop slows and goes silent
        auto p = make(); set (*p, "brakeTime", 0.5f); set (*p, "brakeStyle", 0);
        run (*p, 0.5, sine (440));
        set (*p, "brake", 1);
        auto o = run (*p, 1.0, sine (440));
        // zero crossings early vs late: frequency should drop
        auto zc = [&] (int a, int b) { int z = 0; for (int i = a + 1; i < b; ++i) z += (o[(size_t) i - 1] < 0) != (o[(size_t) i] < 0); return z; };
        const int early = zc (0, 4800), late = zc (14400, 19200);
        check (late < early * 0.75 && rms (o, 26000, 48000) < 0.001, "vinyl stop: pitch drops (" + juce::String (early) + " -> " + juce::String (late) + " crossings) then silence");
        set (*p, "brake", 0); auto back = run (*p, 0.2, sine (440));
        check (rms (back, 2400, 9600) > 0.3, "music returns when stop is released");
        p->setPlayHead (nullptr);
    }
    {   // echo out: dry disappears, echoes continue, then decay
        auto p = make(); set (*p, "echoTime", 4); set (*p, "echoFeedback", 60);
        run (*p, 0.5, sine (440));
        set (*p, "echoOut", 1);
        auto o = run (*p, 3.0, sine (440));
        const double tail = rms (o, (int) (0.3 * fs), (int) (0.8 * fs)), late = rms (o, (int) (2.6 * fs), (int) (3.0 * fs));
        check (tail > 0.05 && late < tail * 0.3, "echo out: echoes ring (" + juce::String (tail, 3) + ") and fade (" + juce::String (late, 4) + ") while input continues");
        p->setPlayHead (nullptr);
    }
    {   // gate halves the energy at 100% depth
        auto p = make(); set (*p, "gate", 1); set (*p, "gateDepth", 100);
        auto o = run (*p, 1.0, sine (440));
        const double r = rms (o, 4800, 48000) / (0.5 / std::sqrt (2.0));
        check (r > 0.6 && r < 0.8, "gate at 100% removes about half the energy (rms ratio " + juce::String (r, 2) + ")");
        p->setPlayHead (nullptr);
    }
    {
        auto p = make(); set (*p, "filter", -42); juce::MemoryBlock mb; p->getStateInformation (mb);
        auto q = make(); q->setStateInformation (mb.getData(), (int) mb.getSize());
        check (std::abs (q->apvts.getRawParameterValue ("filter")->load() + 42) < 0.5f, "state round trip");
        p->setPlayHead (nullptr); q->setPlayHead (nullptr);
    }
    std::cout << (fails == 0 ? "ALL PASSED" : "FAILURES: " + std::to_string (fails)) << "\n";
    return fails == 0 ? 0 : 1;
}
