#include "../Source/PluginProcessor.h"
#include <iostream>
static int fails = 0;
static void check (bool ok, const juce::String& m) { std::cout << (ok ? "PASS " : "FAIL ") << m << "\n"; if (! ok) ++fails; }
static void set (VegaDJFilterProcessor& p, const char* id, float v) { auto* q = p.apvts.getParameter (id); q->setValueNotifyingHost (q->convertTo0to1 (v)); }
struct Head : juce::AudioPlayHead { double bpm = 120; juce::Optional<PositionInfo> getPosition() const override { PositionInfo i; i.setBpm (bpm); return i; } };
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
    // find the strongest echo after the dry click: returns its sample position
    auto echoPeak = [] (const std::vector<float>& o, int from) { int at = from; float best = 0; for (int i = from; i < (int) o.size(); ++i) if (std::abs (o[(size_t) i]) > best) { best = std::abs (o[(size_t) i]); at = i; } return at; };
    auto click = [] (int at) { return [at] (int i) { return i == at ? 0.8f : 0.0f; }; };
    {   // SYNC on: 1/2 beat at 120 BPM = 12000 samples; follows a tempo change to 100 BPM = 14400
        auto p = make(); set (*p, "echoBeat", 3); set (*p, "echoFeedback", 0); set (*p, "echoOut", 1);
        run (*p, 0.1, [] (int) { return 0.0f; });
        auto o = run (*p, 0.5, click (100));
        check (std::abs (o[100] - 0.8f) < 1e-4, "echo keeps the dry signal at full level (" + juce::String (o[100], 4) + ")");
        check (echoPeak (o, 200) == 100 + 12000, "sync: 1/2 beat at 120 BPM repeats after 12000 samples (got " + juce::String (echoPeak (o, 200) - 100) + ")");
        head.bpm = 100; run (*p, 0.6, [] (int) { return 0.0f; });
        auto o2 = run (*p, 0.5, click (100));
        check (echoPeak (o2, 200) == 100 + 14400, "sync follows the tempo: 100 BPM -> 14400 samples (got " + juce::String (echoPeak (o2, 200) - 100) + ")");
        head.bpm = 120; p->setPlayHead (nullptr);
    }
    {   // SYNC off: free time in ms, ignores tempo
        auto p = make(); set (*p, "echoSync", 0); set (*p, "echoMs", 300); set (*p, "echoFeedback", 0); set (*p, "echoOut", 1);
        head.bpm = 87; run (*p, 0.1, [] (int) { return 0.0f; });
        auto o = run (*p, 0.5, click (100));
        const int d = echoPeak (o, 200) - 100;
        check (std::abs (d - 14400) <= 2, "sync off: 300 ms = 14400 samples regardless of tempo (got " + juce::String (d) + ")");
        head.bpm = 120; p->setPlayHead (nullptr);
    }
    {   // 1 beat repeats line up with the beat, each one quieter
        auto p = make(); set (*p, "echoBeat", 5); set (*p, "echoFeedback", 60); set (*p, "echoOut", 1);
        run (*p, 0.1, [] (int) { return 0.0f; });
        auto o = run (*p, 2.2, click (100));
        auto peakNear = [&] (int c) { float b = 0; for (int i = c - 50; i <= c + 50; ++i) b = std::max (b, std::abs (o[(size_t) i])); return b; };
        const float e1 = peakNear (100 + 24000), e2 = peakNear (100 + 48000), e3 = peakNear (100 + 72000), mid = peakNear (100 + 36000);
        check (e1 > 0.2f && e2 < e1 && e3 < e2 && e3 > 0.02f && mid < 0.01f, "1-beat repeats on the beat, decaying (" + juce::String (e1, 3) + ", " + juce::String (e2, 3) + ", " + juce::String (e3, 3) + "), silent between");
        p->setPlayHead (nullptr);
    }
    {   // trail: switching ECHO off keeps the repeats going, dry is untouched, then it dies away
        auto p = make(); set (*p, "echoBeat", 3); set (*p, "echoFeedback", 60);
        juce::Random r (9); std::vector<float> in; auto noise = [&] (int) { const float v = 0.3f * (r.nextFloat() - 0.5f); in.push_back (v); return v; };
        run (*p, 0.2, noise); in.clear();
        set (*p, "echoOut", 1); run (*p, 1.0, noise); in.clear();
        set (*p, "echoOut", 0); auto o = run (*p, 4.0, noise);
        std::vector<float> wet (o.size()); for (size_t i = 0; i < o.size(); ++i) wet[i] = o[i] - in[i];
        const double after = rms (wet, (int) (0.05 * fs), (int) (0.5 * fs)), later = rms (wet, (int) (1.5 * fs), (int) (2.0 * fs)), gone = rms (wet, (int) (3.5 * fs), (int) (4.0 * fs));
        check (after > 0.02 && later < after && gone < 0.001, "trail: echoes keep ringing after OFF (" + juce::String (after, 4) + ") and fade (" + juce::String (later, 4) + " -> " + juce::String (gone, 5) + ")");
        p->setPlayHead (nullptr);
    }
    {   // max feedback with loud music on stays bounded
        auto p = make(); set (*p, "echoFeedback", 95); set (*p, "echoLevel", 100); set (*p, "echoBeat", 0); set (*p, "echoOut", 1);
        auto o = run (*p, 4.0, sine (220, 0.9f)); float worst = 0; bool fin = true; for (float v : o) { fin &= std::isfinite (v); worst = std::max (worst, std::abs (v)); }
        check (fin && worst < 2.5f, "95% feedback on loud input stays bounded (peak " + juce::String (worst, 2) + ")");
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
