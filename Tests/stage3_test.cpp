// VANTA X - Stage 3 DSP tests (Granular + Additive). No JUCE needed:
//   g++ -std=c++17 -O2 -I Source Tests/stage3_test.cpp -o stage3_test && ./stage3_test
#include <cstdio>
#include <vector>
#include <cmath>
#include <chrono>
#include "Voices/VoiceManager.h"
using namespace vanta;

static int fails = 0, passes = 0;
#define CHECK(c, ...) do { char m_[200]; std::snprintf (m_, sizeof m_, __VA_ARGS__); \
    if (!(c)) { std::printf ("FAIL: %s\n", m_); ++fails; } else { std::printf ("ok:   %s\n", m_); ++passes; } } while (0)

static const double SR = 48000.0;
static const double PI2 = 6.283185307179586;
using Buf = std::vector<float>;

// ---- helpers ---------------------------------------------------------------------------------------------------
static double goertzel (const Buf& x, int from, int n, double f)        // Hann-windowed magnitude at f
{
    double re = 0, im = 0;
    for (int i = 0; i < n; ++i)
    {
        const double w = 0.5 - 0.5 * std::cos (PI2 * i / (n - 1));
        const double v = x[(size_t) (from + i)] * w, a = PI2 * f * i / SR;
        re += v * std::cos (a); im -= v * std::sin (a);
    }
    return std::sqrt (re * re + im * im) / n;
}
static double peakFreq (const Buf& x, int from, int n, double lo, double hi, double step = 2.0)
{
    double best = 0, bf = lo;
    for (double f = lo; f <= hi; f += step) { const double m = goertzel (x, from, n, f); if (m > best) { best = m; bf = f; } }
    return bf;
}
static double rms (const Buf& x, int from, int n) { double s = 0; for (int i = 0; i < n; ++i) s += (double) x[(size_t) (from + i)] * x[(size_t) (from + i)]; return std::sqrt (s / n); }
static double dB (double a, double b) { return 20.0 * std::log10 ((a + 1e-12) / (b + 1e-12)); }
static bool allFinite (const Buf& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }
static float maxAbs (const Buf& x) { float m = 0; for (float v : x) m = std::max (m, std::fabs (v)); return m; }
static float maxJump (const Buf& x, int from = 1) { float m = 0; for (size_t i = (size_t) from; i < x.size(); ++i) m = std::max (m, std::fabs (x[i] - x[i - 1])); return m; }

static SynthParams baseParams (int engine)
{
    SynthParams p; p.engine = engine; p.cutoff = 20000.f; p.res = 0.f; p.a = 0.0005f; p.s = 1.f; p.r = 0.05f;
    return p;
}
static SampleData makeSine (double f, double seconds, double sr = SR, float amp = 0.8f)
{
    SampleData s; s.channels = 1; s.sampleRate = sr; s.name = "sine";
    s.ch[0].resize ((size_t) (seconds * sr));
    for (size_t i = 0; i < s.ch[0].size(); ++i) s.ch[0][i] = amp * (float) std::sin (PI2 * f * (double) i / sr);
    s.finalise(); return s;
}
// render n samples of one held note through the VoiceManager (poly mode)
static void renderNote (SynthParams& p, int note, int n, Buf& L, Buf& R, int block = 256)
{
    VoiceManager vm; vm.prepare (SR); vm.noteOn (note, 1.f, p);
    L.assign ((size_t) n, 0.f); R.assign ((size_t) n, 0.f);
    for (int i = 0; i < n; i += block) { const int c = std::min (block, n - i); vm.render (L.data() + i, R.data() + i, c, p); }
}

// ======================================================================================================================
static void testWindows()
{
    std::printf ("\n--- grain windows\n");
    bool endsZero = true, bounded = true, peaky = true;
    for (int t = 0; t < grainwin::NumTypes; ++t)
        for (float w : { 0.1f, 0.5f, 1.f })
            for (float sh : { 0.f, 0.5f, 1.f })
            {
                if (grainwin::value (t, 0.f, w, sh) != 0.f || grainwin::value (t, 1.f, w, sh) != 0.f) endsZero = false;
                float pk = 0.f;
                for (int i = 0; i <= 200; ++i) { const float v = grainwin::value (t, i / 200.f, w, sh); if (v < 0.f || v > 1.0001f || ! std::isfinite (v)) bounded = false; pk = std::max (pk, v); }
                if (pk < 0.5f) peaky = false;
            }
    CHECK (endsZero, "all 6 window types are exactly 0 at both ends (no clicks) for every width/shape");
    CHECK (bounded, "windows stay within [0,1] and finite");
    CHECK (peaky, "windows reach a meaningful peak for every width/shape");
    float t[grainwin::kTable + 1]; grainwin::buildTable (t, grainwin::Gaussian, 1.f, 0.5f);
    CHECK (t[0] == 0.f && t[grainwin::kTable] == 0.f && t[grainwin::kTable / 2] > 0.95f, "window table built with centre peak and zero ends");
}

static void testGranular()
{
    std::printf ("\n--- granular engine\n");
    const SampleData builtin = SampleData::makeBuiltIn (SR);
    CHECK (builtin.valid() && builtin.builtIn && builtin.frames == (int) (SR * 4), "built-in texture generated (%d frames)", builtin.frames);

    // 1. audible + finite on the built-in texture
    {
        SynthParams p = baseParams (EngGranular); p.gran.sample = &builtin;
        Buf L, R; renderNote (p, 60, 48000, L, R);
        CHECK (allFinite (L) && allFinite (R) && maxAbs (L) > 0.02f && maxAbs (L) < 2.f, "built-in texture: finite and audible (peak %.3f)", maxAbs (L));
    }
    // 2. missing / invalid sample is silent and safe
    {
        SynthParams p = baseParams (EngGranular); p.gran.sample = nullptr;
        Buf L, R; renderNote (p, 60, 9600, L, R);
        CHECK (allFinite (L) && maxAbs (L) == 0.f, "no sample loaded: silent, no crash");
        SampleData tiny; tiny.ch[0].assign (3, 0.5f); tiny.finalise(); p.gran.sample = &tiny;
        renderNote (p, 60, 9600, L, R);
        CHECK (allFinite (L) && maxAbs (L) == 0.f, "too-short sample rejected safely");
    }
    // 3. pitch tracking: root 69 sample of 440 Hz, notes 69 / 81 / 57
    {
        const SampleData s = makeSine (440.0, 3.0);
        for (int note : { 69, 81, 57 })
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.root = 69; p.gran.density = 55.f; p.gran.sizeMs = 80.f;   // 55 Hz: 220/440/880 Hz are exact multiples of the grain rate (synchronous grain trains only have lines at k x density)
            p.gran.texture = 0.f; p.gran.spray = 0.f; p.gran.sizeRand = 0.f; p.gran.pitchRand = 0.f; p.gran.spread = 0.f; p.gran.pos = 0.3f;
            Buf L, R; renderNote (p, note, 48000, L, R);
            const double expect = 440.0 * std::pow (2.0, (note - 69) / 12.0);
            const double f = peakFreq (L, 8000, 32768, expect * 0.7, expect * 1.4, 1.0);
            CHECK (std::fabs (f / expect - 1.0) < 0.02, "grain pitch: note %d -> %.1f Hz (expected %.1f)", note, f, expect);
        }
        SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.root = 69; p.gran.pitch = 12.f; p.gran.density = 55.f; p.gran.texture = 0.f; p.gran.spray = 0.f; p.gran.pitchRand = 0.f; p.gran.pos = 0.3f;
        Buf L, R; renderNote (p, 69, 48000, L, R);
        const double f = peakFreq (L, 8000, 32768, 700, 1100, 1.0);
        CHECK (std::fabs (f / 880.0 - 1.0) < 0.02, "Pitch control +12 st -> %.1f Hz", f);
    }
    // 4. position selects source content
    {
        SampleData s; s.channels = 1; s.sampleRate = SR; s.ch[0].resize ((size_t) (SR * 2));
        for (size_t i = 0; i < s.ch[0].size(); ++i) s.ch[0][i] = 0.8f * (float) std::sin (PI2 * (i < s.ch[0].size() / 2 ? 200.0 : 800.0) * (double) i / SR);
        s.finalise();
        double fl = 0, fh = 0;
        for (int k = 0; k < 2; ++k)
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.root = 60; p.gran.sizeMs = 50.f; p.gran.density = 40.f;
            p.gran.texture = 0.f; p.gran.spray = 0.f; p.gran.pitchRand = 0.f; p.gran.pos = k == 0 ? 0.05f : 0.93f;
            Buf L, R; renderNote (p, 60, 48000, L, R);
            (k == 0 ? fl : fh) = peakFreq (L, 8000, 32768, 100, 1200, 2.0);
        }
        CHECK (std::fabs (fl - 200) < 12 && std::fabs (fh - 800) < 20, "Position selects content: pos 0.05 -> %.0f Hz, pos 0.93 -> %.0f Hz", fl, fh);
    }
    // 5. sample start / end region is respected
    {
        SampleData s; s.channels = 1; s.sampleRate = SR; s.ch[0].assign ((size_t) (SR * 2), 0.f);
        for (size_t i = s.ch[0].size() / 2; i < s.ch[0].size(); ++i) s.ch[0][i] = 0.8f * (float) std::sin (PI2 * 440.0 * (double) i / SR);
        s.finalise();
        float pk[2];
        for (int k = 0; k < 2; ++k)
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.spray = 1.f; p.gran.texture = 0.f; p.gran.sizeMs = 100.f;
            if (k == 0) { p.gran.start = 0.f; p.gran.end = 0.48f; } else { p.gran.start = 0.52f; p.gran.end = 1.f; }
            Buf L, R; renderNote (p, 60, 48000, L, R); pk[k] = maxAbs (L);
        }
        CHECK (pk[0] < 1e-4f && pk[1] > 0.05f, "region start/end: silent half -> %.5f, signal half -> %.3f", pk[0], pk[1]);
    }
    // 6. direction (forward / reverse) via a ramp source and a flat-top window
    {
        SampleData s; s.channels = 1; s.sampleRate = SR; const int N = 9600; s.ch[0].resize ((size_t) N);
        for (int i = 0; i < N; ++i) s.ch[0][(size_t) i] = (float) i / N;
        s.finalise();
        float tbl[grainwin::kTable + 1]; grainwin::buildTable (tbl, grainwin::Trapezoid, 1.f, 1.f);
        float slope[2];
        for (int d = 0; d < 2; ++d)
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.window = tbl; p.gran.dir = d; p.gran.root = 60;
            p.gran.density = 4.f; p.gran.sizeMs = 200.f; p.gran.texture = 0.f; p.gran.spray = 0.f; p.gran.sizeRand = 0.f; p.gran.pos = 0.f;
            Buf L, R; renderNote (p, 60, 12000, L, R, 128);
            const int mid = 4800; slope[d] = L[(size_t) mid + 300] - L[(size_t) mid];          // grain 1 is mid-flight at 100 ms
        }
        CHECK (slope[0] > 0.005f && slope[1] < -0.005f, "playback direction: forward slope %+.4f, reverse slope %+.4f", slope[0], slope[1]);
    }
    // 7. density -> grains spawned, texture changes timing statistics, cap respected
    {
        const SampleData s = makeSine (300.0, 2.0);
        for (float tex : { 0.f, 1.f })
        {
            GrainViz viz; SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.density = 20.f; p.gran.texture = tex; p.gran.sizeMs = 30.f; p.gran.viz = &viz;
            Buf L, R; renderNote (p, 60, 48000, L, R);
            const int n = (int) viz.counter.load();
            CHECK (tex == 0.f ? std::abs (n - 20) <= 2 : (n > 8 && n < 40), "density 20 Hz, texture %.0f -> %d grains in 1 s", tex, n);
        }
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.density = 400.f; p.gran.sizeMs = 500.f; p.gran.texture = 0.f;
        vm.noteOn (60, 1.f, p); Buf L (48000), R (48000); int maxG = 0;
        for (int i = 0; i < 48000; i += 128) { vm.render (L.data() + i, R.data() + i, 128, p); maxG = std::max (maxG, vm.grainCount()); }
        CHECK (maxG <= GranularVoice::kMaxGrains && maxG > 8 && allFinite (L), "grain pool bounded under extreme density x size (max %d active)", maxG);
    }
    // 8. stereo spread
    {
        const SampleData s = makeSine (330.0, 2.0);
        double diff[2];
        for (int k = 0; k < 2; ++k)
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.spread = k == 0 ? 0.f : 1.f; p.gran.density = 40.f;
            Buf L, R; renderNote (p, 60, 24000, L, R); diff[k] = 0; for (size_t i = 0; i < L.size(); ++i) diff[k] += std::fabs (L[i] - R[i]);
        }
        CHECK (diff[0] < 1e-3 && diff[1] > 5.0, "stereo spread: 0 -> identical channels (%.4f), 1 -> wide (%.1f)", diff[0], diff[1]);
    }
    // 9. loudness stays in a sane window across density / size
    {
        double lv[3]; int k = 0;
        for (float dens : { 8.f, 32.f, 120.f })
        {
            SynthParams p = baseParams (EngGranular); p.gran.sample = &builtin; p.gran.density = dens; p.gran.sizeMs = 100.f; p.gran.pos = 0.2f;
            Buf L, R; renderNote (p, 60, 96000, L, R); lv[k++] = rms (L, 24000, 60000);
        }
        const double span = std::max ({ dB (lv[0], lv[1]), dB (lv[1], lv[2]), dB (lv[0], lv[2]), dB (lv[1], lv[0]), dB (lv[2], lv[1]), dB (lv[2], lv[0]) });
        CHECK (span < 9.0, "output level stays within %.1f dB across density 8/32/120 Hz (rms %.3f %.3f %.3f)", span, lv[0], lv[1], lv[2]);
    }
    // 10. click-free real-time parameter changes (low-freq source: any discontinuity stands out)
    {
        const SampleData s = makeSine (100.0, 3.0);
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.root = 60;
        vm.noteOn (60, 1.f, p); const int blocks = 400, B = 120; Buf out ((size_t) blocks * B), R ((size_t) blocks * B);
        uint32_t rs = 77; auto rn = [&rs] { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.f / 16777216.f); };
        for (int b = 0; b < blocks; ++b)
        {
            p.gran.sizeMs = 10.f + 400.f * rn(); p.gran.density = 2.f + 100.f * rn(); p.gran.pos = rn(); p.gran.spray = rn(); p.gran.texture = rn();
            p.gran.pitch = -12.f + 24.f * rn(); p.gran.pitchRand = rn(); p.gran.start = 0.2f * rn(); p.gran.end = 1.f - 0.2f * rn();
            p.gran.speed = -2.f + 4.f * rn(); p.gran.dir = (int) (rn() * 3.99f); p.gran.spread = rn();
            vm.render (out.data() + b * B, R.data() + b * B, B, p);
        }
        // the signal is a 100 Hz sine pitched up to 400 Hz at most: max natural slope ~ 2*pi*400/48000 * peak
        const float pk = maxAbs (out), jump = maxJump (out);
        CHECK (allFinite (out) && jump < 0.12f * std::max (0.1f, pk) + 0.02f, "random parameter automation: finite, max sample step %.4f (peak %.3f)", jump, pk);
    }
    // 11. swapping the sample under running grains is memory-safe (ASan/UBSan guard this test)
    {
        const SampleData a = makeSine (200.0, 3.0), b = makeSine (500.0, 0.05);
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sizeMs = 400.f; p.gran.pitch = 24.f; p.gran.pitchRand = 1.f; p.gran.pos = 0.9f;
        vm.noteOn (60, 1.f, p); Buf L (256), R (256); bool ok = true;
        for (int i = 0; i < 200; ++i) { p.gran.sample = (i % 7 < 3) ? &a : ((i % 7 < 5) ? &b : nullptr); std::fill (L.begin(), L.end(), 0.f); vm.render (L.data(), R.data(), 256, p); ok = ok && allFinite (L); }
        CHECK (ok, "sample replaced / removed under running grains: no crash, finite");
    }
    // 12. polyphony
    {
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sample = &builtin;
        for (int n = 48; n < 56; ++n) vm.noteOn (n, 0.8f, p);
        Buf L (48000), R (48000); for (int i = 0; i + 256 <= 48000; i += 256) vm.render (L.data() + i, R.data() + i, 256, p);
        CHECK (vm.activeCount() == 8 && allFinite (L) && maxAbs (L) > 0.02f, "8-voice granular polyphony: %d voices, peak %.2f", vm.activeCount(), maxAbs (L));
        for (int n = 48; n < 56; ++n) vm.noteOff (n, p);
        for (int i = 0; i < 20; ++i) vm.render (L.data(), R.data(), 4800, p);
        CHECK (vm.activeCount() == 0, "granular voices release and free themselves");
    }
    // 13. CPU
    {
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sample = &builtin; p.gran.density = 60.f; p.gran.sizeMs = 200.f;
        for (int n = 40; n < 56; ++n) vm.noteOn (n, 0.8f, p);
        Buf L (512), R (512); const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 94; ++i) vm.render (L.data(), R.data(), 512, p);          // ~1 s of audio
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (sec < 0.6, "granular CPU: 16 voices, dense clouds -> %.1f%% of real time (single core, this machine)", sec * 100.0);
    }
}

// ======================================================================================================================
static AdditiveParams addP() { AdditiveParams a; a.level = 1.f; a.tone = 1.f; return a; }

static void testAdditive()
{
    std::printf ("\n--- additive engine\n");
    // shape() unit tests
    {
        AdditiveParams a = addP(); additive::shape (a);
        CHECK (std::fabs (a.shaped[0] / a.shaped[1] - 2.f) < 1e-3f && a.shaped[0] > a.shaped[31] * 20.f, "default spectrum is a 1/n saw series (ratio h1/h2 = %.3f)", a.shaped[0] / a.shaped[1]);
        AdditiveParams b = addP(); for (float& v : b.amp) v = 1.f; b.balance = 1.f; additive::shape (b);
        CHECK (b.shaped[0] == 0.f && b.shaped[1] > 0.f && b.shaped[2] == 0.f, "harmonic balance +1 keeps only even harmonics");
        b = addP(); for (float& v : b.amp) v = 1.f; b.balance = -1.f; additive::shape (b);
        CHECK (b.shaped[1] == 0.f && b.shaped[0] > 0.f, "harmonic balance -1 keeps only odd harmonics");
        b = addP(); for (float& v : b.amp) v = 1.f; additive::shape (b); const float r0 = b.shaped[3] / b.shaped[1];
        b.rolloff = 1.f; additive::shape (b); const float r1 = b.shaped[3] / b.shaped[1];
        CHECK (std::fabs (r0 - 1.f) < 1e-4f && std::fabs (r1 - 0.25f) < 1e-3f, "rolloff 1.0 -> -12 dB/oct (h4/h2 = %.3f)", r1);
        b = addP(); b.count = 4; for (float& v : b.amp) v = 1.f; additive::shape (b);
        CHECK (b.shaped[3] > 0.f && b.shaped[4] == 0.f && b.shaped[31] == 0.f, "harmonic count 4 silences harmonics 5..32");
        b = addP(); for (float& v : b.amp) v = 1.f; b.tone = 0.1f; additive::shape (b);
        CHECK (b.shaped[0] > b.shaped[3] * 3.f && b.shaped[10] < b.shaped[3] * 0.2f, "tone control darkens the spectrum");
        b = addP(); for (float& v : b.amp) v = 1.f; b.sfOn = true; b.sfPos = std::log ((float) 8) / std::log (32.f); b.sfWidth = 0.05f; b.sfGain = -1.f; additive::shape (b);
        CHECK (b.shaped[7] < 0.05f * b.shaped[5], "spectral filter notch at harmonic 8 (%.3f vs %.3f)", b.shaped[7], b.shaped[5]);
        b = addP(); b.sfOn = true; b.sfPos = 0.f; b.sfWidth = 0.1f; b.sfGain = 1.f; additive::shape (b); AdditiveParams ref = addP(); additive::shape (ref);
        CHECK (b.shaped[0] / b.shaped[1] > 3.5f * (ref.shaped[0] / ref.shaped[1]), "spectral filter boost raises the selected region (h1/h2 %.2f vs %.2f)", b.shaped[0] / b.shaped[1], ref.shaped[0] / ref.shaped[1]);
        b = addP(); for (float& v : b.amp) v = 0.f; b.amp[0] = 1.f; b.shift = 0.25f; additive::shape (b);
        CHECK (b.shaped[0] == 0.f && b.shaped[4] > 0.5f, "harmonic shift +0.25 moves the spectrum up 4 harmonics");
        b = addP(); b.morph = 1.f; b.target = 1; additive::shape (b);
        CHECK (b.shaped[1] == 0.f && b.shaped[2] > 0.f, "spectral morph to 'square' target removes even harmonics");
        b = addP(); b.level = 0.5f; additive::shape (b); AdditiveParams c = addP(); additive::shape (c);
        CHECK (std::fabs (b.shaped[0] / c.shaped[0] - 0.5f) < 1e-4f, "level scales the spectrum");
    }
    // single harmonic isolation through the real voice path
    {
        AdditiveParams base = addP(); for (float& v : base.amp) v = 0.f; base.amp[2] = 1.f;      // harmonic 3 only
        SynthParams p = baseParams (EngAdditive); p.add = base; additive::shape (p.add);
        Buf L, R; renderNote (p, 57, 48000, L, R);                                                  // A3 = 220 Hz
        const double m1 = goertzel (L, 8000, 32768, 220), m2 = goertzel (L, 8000, 32768, 440), m3 = goertzel (L, 8000, 32768, 660), m4 = goertzel (L, 8000, 32768, 880);
        CHECK (dB (m3, m1) > 50 && dB (m3, m2) > 50 && dB (m3, m4) > 50, "harmonic 3 alone: 660 Hz is %.0f/%.0f/%.0f dB above 220/440/880 Hz", dB (m3, m1), dB (m3, m2), dB (m3, m4));
        double f = peakFreq (L, 8000, 32768, 600, 720, 0.5);
        CHECK (std::fabs (f - 660.0) < 1.5, "fundamental control: note 57 -> harmonic 3 at %.1f Hz", f);
        SynthParams q = p; q.add.semi = 12.f; q.add.fine = 50.f; additive::shape (q.add); renderNote (q, 57, 48000, L, R);
        f = peakFreq (L, 8000, 32768, 1200, 1500, 0.5); const double e = 660.0 * std::pow (2.0, 1.5 / 12.0) * 2.0 / 2.0 * std::pow (2.0, 0.0);
        const double expect = 660.0 * std::pow (2.0, (12.0 + 0.5) / 12.0);
        CHECK (std::fabs (f - expect) < 3.0, "semitone/fine offset: harmonic 3 at %.1f Hz (expected %.1f)", f, expect); (void) e;
    }
    // phase control
    {
        double ph[2];
        for (int k = 0; k < 2; ++k)
        {
            AdditiveParams a = addP(); for (float& v : a.amp) v = 0.f; a.amp[0] = 1.f; a.phase[0] = k == 0 ? 0.f : 0.25f; additive::shape (a);
            SynthParams p = baseParams (EngAdditive); p.add = a; Buf L, R; renderNote (p, 57, 30000, L, R);
            double c = 0, s = 0; for (int i = 4800; i < 4800 + 22000; ++i) { c += L[(size_t) i] * std::cos (PI2 * 220.0 * i / SR); s += L[(size_t) i] * std::sin (PI2 * 220.0 * i / SR); }
            ph[k] = std::atan2 (c, s);
        }
        double d = ph[1] - ph[0]; while (d > 3.14159) d -= PI2; while (d < -3.14159) d += PI2;
        CHECK (std::fabs (std::fabs (d) - 1.5708) < 0.1, "harmonic phase 0.25 turn shifts the partial by %.2f rad (expected +-1.57)", d);
    }
    // inharmonicity
    {
        AdditiveParams a = addP(); for (float& v : a.amp) v = 0.f; a.amp[7] = 1.f; a.inharm = 1.f; additive::shape (a);
        SynthParams p = baseParams (EngAdditive); p.add = a; Buf L, R; renderNote (p, 57, 48000, L, R);
        const double expect = 220.0 * 8.0 * std::sqrt (1.0 + 0.001 * 64.0), f = peakFreq (L, 8000, 32768, 1700, 1900, 0.5);
        CHECK (std::fabs (f - expect) < 2.0, "inharmonicity stretches harmonic 8 to %.1f Hz (expected %.1f, harmonic = 1760)", f, expect);
    }
    // alias safety on a very high note
    {
        AdditiveParams a = addP(); for (float& v : a.amp) v = 1.f; additive::shape (a);
        SynthParams p = baseParams (EngAdditive); p.add = a; Buf L, R; renderNote (p, 100, 48000, L, R);    // 2637 Hz: partials >= 8 are above the limit
        const double fund = goertzel (L, 8000, 32768, 2637.0);
        // harmonic 10 = 26.4 kHz would fold to 21.6 kHz; harmonic 9 = 23.7 kHz would fold to 24.3 kHz
        const double al1 = goertzel (L, 8000, 32768, 21600.0), al2 = goertzel (L, 8000, 32768, 24300.0 - 0.0);
        CHECK (allFinite (L) && dB (fund, al1) > 55 && dB (fund, al2 > 0 ? al2 : 1e-9) > 55, "no aliasing above Nyquist on note 100 (fundamental %.0f dB over fold-backs)", std::min (dB (fund, al1), dB (fund, al2)));
    }
    // unison / stereo
    {
        AdditiveParams a = addP(); additive::shape (a);
        SynthParams p = baseParams (EngAdditive); p.add = a; Buf L, R; renderNote (p, 48, 24000, L, R);
        double dm = 0; for (size_t i = 0; i < L.size(); ++i) dm += std::fabs (L[i] - R[i]);
        CHECK (dm < 1e-6, "additive unison off: identical L/R");
        p.add.uni = true; p.add.spread = 1.f; p.add.detune = 0.5f; renderNote (p, 48, 24000, L, R);
        double du = 0; for (size_t i = 0; i < L.size(); ++i) du += std::fabs (L[i] - R[i]);
        CHECK (du > 5.0 && allFinite (L), "additive unison on: stereo width (sum |L-R| = %.1f)", du);
    }
    // harmonic envelopes: highs fade, fundamental stays
    {
        AdditiveParams a = addP(); for (float& v : a.amp) v = 1.f / 1.f; a.henv = true; a.hdecay = 0.15f; additive::shape (a);
        SynthParams p = baseParams (EngAdditive); p.add = a; Buf L, R; renderNote (p, 45, 48000, L, R);       // 110 Hz
        const double earlyH = goertzel (L, 1000, 4800, 880.0), lateH = goertzel (L, 40000, 4800, 880.0);
        const double earlyF = goertzel (L, 1000, 4800, 110.0), lateF = goertzel (L, 40000, 4800, 110.0);
        CHECK (dB (earlyH, lateH) > 20 && std::fabs (dB (earlyF, lateF)) < 3, "harmonic envelopes: h8 decays %.0f dB, fundamental changes %.1f dB", dB (earlyH, lateH), dB (earlyF, lateF));
    }
    // click-free automation of spectrum / phase / count / shift
    {
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngAdditive);
        p.add.level = 1.f; vm.noteOn (33, 1.f, p);                    // 55 Hz: slow waveform -> any step is audible in the jump metric
        const int blocks = 600, B = 100; Buf out ((size_t) blocks * B), R ((size_t) blocks * B);
        uint32_t rs = 5; auto rn = [&rs] { rs = rs * 1664525u + 1013904223u; return (rs >> 8) * (1.f / 16777216.f); };
        for (int b = 0; b < blocks; ++b)
        {
            if (b % 5 == 0) { for (int k = 0; k < 32; ++k) { p.add.amp[k] = rn() * (k < 6 ? 1.f : 0.2f); p.add.phase[k] = rn(); }
                              p.add.count = 4 + (int) (rn() * 28.f); p.add.shift = rn() * 0.2f - 0.1f; p.add.morph = rn(); p.add.balance = rn() * 2.f - 1.f; p.add.tone = 0.3f + 0.7f * rn(); }
            additive::shape (p.add); vm.render (out.data() + b * B, R.data() + b * B, B, p);
        }
        CHECK (allFinite (out) && maxJump (out) < 0.25f, "randomised spectrum/phase/count edits every 5 blocks: max sample step %.3f (peak %.2f)", maxJump (out), maxAbs (out));
    }
    // ADSR integration + release
    {
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngAdditive); p.a = 0.1f; p.r = 0.05f; additive::shape (p.add);
        vm.noteOn (60, 1.f, p); Buf L (4800), R (4800); vm.render (L.data(), R.data(), 4800, p);
        const double early = rms (L, 0, 480), late = rms (L, 4000, 800);
        CHECK (late > early * 3.0, "amp attack shapes the additive voice (rms %.4f -> %.4f)", early, late);
        vm.noteOff (60, p); for (int i = 0; i < 20; ++i) vm.render (L.data(), R.data(), 4800, p);
        CHECK (vm.activeCount() == 0, "additive voice releases and frees itself");
    }
    // extreme values stay finite
    {
        AdditiveParams a = addP(); for (float& v : a.amp) v = 1.f; for (float& v : a.phase) v = 0.99f; a.inharm = 1.f; a.sfOn = true; a.sfGain = 1.f; a.uni = true; a.henv = true; a.hdecay = 0.02f; a.shift = 1.f; a.morph = 1.f; a.target = 4;
        additive::shape (a); SynthParams p = baseParams (EngAdditive); p.add = a; p.res = 0.95f; p.drive = 1.f;
        bool ok = true; for (int note : { 0, 36, 72, 127 }) { Buf L, R; renderNote (p, note, 12000, L, R); ok = ok && allFinite (L) && allFinite (R); }
        CHECK (ok, "extreme settings (all harmonics, max inharm/shift/morph, notes 0..127): finite output");
    }
    // 8 voices polyphony + CPU
    {
        VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngAdditive); p.add.uni = true; additive::shape (p.add);
        for (int n = 36; n < 52; ++n) vm.noteOn (n, 0.8f, p);
        Buf L (512), R (512); const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 94; ++i) vm.render (L.data(), R.data(), 512, p);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        CHECK (vm.activeCount() == 16 && allFinite (L), "16 additive voices, 32 harmonics, unison on");
        CHECK (sec < 0.6, "additive CPU: 16 voices x 32 harmonics x 2 banks -> %.1f%% of real time (single core, this machine)", sec * 100.0);
    }
}

static void testEngineSwitch()
{
    std::printf ("\n--- engine integration\n");
    const SampleData s = makeSine (440.0, 2.0);
    VoiceManager vm; vm.prepare (SR); SynthParams p = baseParams (EngGranular); p.gran.sample = &s; p.gran.root = 69;
    additive::shape (p.add);
    vm.noteOn (69, 1.f, p);                                         // granular voice
    Buf L (4800), R (4800); vm.render (L.data(), R.data(), 4800, p);
    p.engine = EngAdditive; vm.noteOn (64, 1.f, p);                 // additive voice while the granular note still rings
    std::fill (L.begin(), L.end(), 0.f); vm.render (L.data(), R.data(), 4800, p);
    CHECK (vm.activeCount() == 2 && allFinite (L) && maxAbs (L) > 0.01f, "voices keep the engine they were started with (granular + additive sounding together)");
    p.engine = EngVirtualAnalog; vm.noteOn (72, 1.f, p); vm.render (L.data(), R.data(), 4800, p);
    CHECK (vm.activeCount() == 3 && allFinite (L), "VA, Granular and Additive voices coexist");
    p.engine = EngWavetable; vm.noteOn (50, 1.f, p); vm.render (L.data(), R.data(), 4800, p);
    CHECK (vm.activeCount() == 4 && allFinite (L), "not-yet-implemented engine ids fall back to Virtual Analog safely");
}

int main()
{
    testWindows();
    testGranular();
    testAdditive();
    testEngineSwitch();
    std::printf ("\n%d checks passed, %d failed\n", passes, fails);
    std::printf (fails ? "STAGE3 FAILED\n" : "STAGE3 ALL PASSED\n");
    return fails ? 1 : 0;
}
