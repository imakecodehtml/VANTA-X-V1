// Standalone DSP sanity test (no JUCE needed): g++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp
#include <cstdio>
#include <vector>
#include <cmath>
#include "Voices/VoiceManager.h"
using namespace vanta;

static int fails = 0;
#define CHECK(c, msg) do { if (!(c)) { std::printf("FAIL: %s\n", msg); ++fails; } else std::printf("ok:   %s\n", msg); } while (0)

static float zeroCrossFreq (const std::vector<float>& x, double sr)
{ int z = 0; for (size_t i = 1; i < x.size(); ++i) if (x[i-1] < 0 && x[i] >= 0) ++z; return (float) (z / (x.size() / sr)); }

int main()
{
    const double sr = 48000; const int N = 48000;
    SynthParams p; p.cutoff = 20000; p.mix[0] = 1.f;
    for (int wave = 0; wave < 5; ++wave)
    {
        VoiceManager vm; vm.prepare (sr); p.wave = wave;
        vm.noteOn (69, 1.f, p);   // A4 = 440 Hz
        std::vector<float> L (N, 0.f), R (N, 0.f);
        vm.render (L.data(), R.data(), N, p);
        float pk = 0; bool finite = true;
        for (float v : L) { pk = std::max (pk, std::fabs (v)); if (! std::isfinite (v)) finite = false; }
        char m[96];
        std::snprintf (m, sizeof m, "wave %d finite & audible (peak %.3f)", wave, pk); CHECK (finite && pk > 0.05f && pk < 2.f, m);
        std::vector<float> tail (L.begin() + 4800, L.end());
        if (wave != 4) { float f = zeroCrossFreq (tail, sr); std::snprintf (m, sizeof m, "wave %d pitch ~440 Hz (got %.1f)", wave, f); CHECK (std::fabs (f - 440.f) < 8.f, m); }
    }
    { // release ends voice, stereo unison, noise + sub, filter types all finite
        VoiceManager vm; vm.prepare (sr);
        SynthParams q; q.uniVoices = 8; q.uniDetune = 0.5f; q.uniSpread = 1.f; q.subLevel = 0.5f; q.noiseLevel = 0.3f; q.noiseType = 1;
        q.res = 0.95f; q.drive = 0.8f; q.mix[1] = 0.8f; q.mix[2] = 0.8f; q.sync = true; q.r = 0.05f;
        bool finite = true; float diff = 0.f;
        for (int t = 0; t < 4; ++t)
        {
            q.fltType = t; vm.noteOn (48 + t, 0.9f, q);
            std::vector<float> L (4800, 0.f), R (4800, 0.f); vm.render (L.data(), R.data(), 4800, q);
            for (int i = 0; i < 4800; ++i) { if (! std::isfinite (L[i]) || ! std::isfinite (R[i])) finite = false; diff += std::fabs (L[i] - R[i]); }
        }
        CHECK (finite, "all filter types / unison / sync / noise stay finite at high resonance");
        CHECK (diff > 1.f, "unison spread produces stereo difference");
        for (int n = 48; n < 52; ++n) vm.noteOff (n, q);
        std::vector<float> L (48000, 0.f), R (48000, 0.f); vm.render (L.data(), R.data(), 48000, q);
        CHECK (vm.activeCount() == 0, "voices free themselves after release");
    }
    { // polyphony limit & stealing
        VoiceManager vm; vm.prepare (sr); SynthParams q; q.polyLimit = 4;
        for (int n = 60; n < 70; ++n) vm.noteOn (n, 1.f, q);
        std::vector<float> L (256, 0.f), R (256, 0.f); vm.render (L.data(), R.data(), 256, q);
        CHECK (vm.activeCount() == 4, "polyphony limit 4 respected with voice stealing");
    }
    { // mono / legato
        VoiceManager vm; vm.prepare (sr); SynthParams q; q.mode = 1;
        vm.noteOn (60, 1.f, q); vm.noteOn (64, 1.f, q);
        std::vector<float> L (256, 0.f), R (256, 0.f); vm.render (L.data(), R.data(), 256, q);
        CHECK (vm.activeCount() == 1, "mono mode keeps a single voice");
        vm.noteOff (64, q); vm.render (L.data(), R.data(), 256, q);
        CHECK (vm.activeCount() == 1, "mono note-stack returns to previous held note");
    }
    { // glide moves pitch gradually
        VoiceManager vm; vm.prepare (sr); SynthParams q; q.mode = 2; q.glide = 0.5f; q.glideCurve = 0.f; q.wave = Sine; q.cutoff = 20000; q.mix[0] = 1;
        vm.noteOn (57, 1.f, q); std::vector<float> L (4800, 0.f), R (4800, 0.f); vm.render (L.data(), R.data(), 4800, q);
        vm.noteOn (69, 1.f, q);
        std::vector<float> A (4800, 0.f), B (4800, 0.f); vm.render (A.data(), B.data(), 4800, q);
        std::vector<float> C (24000, 0.f), D (24000, 0.f); vm.render (C.data(), D.data(), 24000, q);
        float f1 = zeroCrossFreq (A, sr), f2 = zeroCrossFreq (std::vector<float> (C.end() - 4800, C.end()), sr);
        CHECK (f1 < 400.f && f1 > 200.f, "portamento: pitch glides (not an instant jump)");
        CHECK (std::fabs (f2 - 440.f) < 12.f, "portamento: pitch settles on target note");
    }
    std::printf (fails ? "\n%d FAILED\n" : "\nALL PASSED\n", fails);
    return fails ? 1 : 0;
}
