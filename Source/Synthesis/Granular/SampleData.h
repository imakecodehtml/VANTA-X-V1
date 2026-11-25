#pragma once
// Immutable audio source for the granular engine. JUCE-free so it can be unit tested.
// A SampleData object is built on the message thread and only READ by the audio thread.
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vanta
{
struct SampleData
{
    static constexpr int kOverview = 1024;     // peak pairs used for drawing

    std::vector<float> ch[2];                  // ch[1] is a copy of ch[0] for mono sources
    int    channels   = 1;                     // 1 or 2 (source channel count)
    int    frames     = 0;
    double sampleRate = 44100.0;
    std::string name;                          // display name
    std::string path;                          // empty for the built-in texture
    bool   builtIn    = false;
    std::vector<float> overview;               // [min0,max0,min1,max1,...] over the whole sample
    uint32_t version  = 0;

    bool valid() const noexcept { return frames >= 8 && (int) ch[0].size() >= frames && (int) ch[1].size() >= frames; }

    // Call once after filling ch[0] (and ch[1] for stereo).
    void finalise()
    {
        frames = (int) ch[0].size();
        if (channels < 2 || ch[1].size() != ch[0].size()) { ch[1] = ch[0]; if (channels > 2) channels = 2; if (channels < 1) channels = 1; }
        overview.assign ((size_t) kOverview * 2, 0.f);
        if (frames <= 0) return;
        for (int i = 0; i < kOverview; ++i)
        {
            const int a = (int) ((int64_t) frames * i / kOverview), b = std::max (a + 1, (int) ((int64_t) frames * (i + 1) / kOverview));
            float lo = 0.f, hi = 0.f;
            for (int s = a; s < b && s < frames; ++s)
            {
                const float v = 0.5f * (ch[0][(size_t) s] + ch[1][(size_t) s]);
                lo = std::min (lo, v); hi = std::max (hi, v);
            }
            overview[(size_t) i * 2] = lo; overview[(size_t) i * 2 + 1] = hi;
        }
    }

    // Deterministic procedural source used when the user has not loaded a file (or the file is missing).
    // It is generated, not a recording: 4 evolving sections (drone, bright harmonic pad, noisy air, bell-like chimes)
    // so that the Position control audibly changes the timbre.
    static SampleData makeBuiltIn (double sr = 44100.0)
    {
        SampleData s; s.channels = 2; s.sampleRate = sr; s.name = "Built-in Texture"; s.builtIn = true;
        const int N = (int) (sr * 4.0);
        s.ch[0].assign ((size_t) N, 0.f); s.ch[1].assign ((size_t) N, 0.f);
        uint32_t rng = 0xC0FFEEu;
        auto rnd = [&rng] { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (int32_t) rng * (1.f / 2147483648.f); };
        const double tp = 6.283185307179586;
        float nzL = 0.f, nzR = 0.f;
        for (int i = 0; i < N; ++i)
        {
            const double t = i / sr; const double u = t / 4.0;           // 0..1
            float l = 0.f, r = 0.f;
            // 0.00-0.25 : low drone (110 Hz + sub), slightly detuned L/R
            const float w0 = (float) std::max (0.0, 1.0 - std::fabs (u - 0.125) / 0.175);
            l += w0 * 0.5f * (float) (std::sin (tp * 110.0 * t) + 0.5 * std::sin (tp * 220.5 * t));
            r += w0 * 0.5f * (float) (std::sin (tp * 110.4 * t) + 0.5 * std::sin (tp * 219.5 * t));
            // 0.25-0.50 : bright harmonic pad (220 Hz, 8 harmonics, slow vibrato)
            const float w1 = (float) std::max (0.0, 1.0 - std::fabs (u - 0.375) / 0.175);
            for (int h = 1; h <= 8; ++h)
            {
                const double f = 220.0 * h * (1.0 + 0.002 * std::sin (tp * 4.5 * t));
                const float a = w1 * 0.35f / (float) h;
                l += a * (float) std::sin (tp * f * t); r += a * (float) std::sin (tp * f * 1.0015 * t);
            }
            // 0.50-0.75 : filtered noise "air" with slow movement
            const float w2 = (float) std::max (0.0, 1.0 - std::fabs (u - 0.625) / 0.175);
            nzL += (rnd() - nzL) * 0.25f; nzR += (rnd() - nzR) * 0.25f;
            const float mv = 0.6f + 0.4f * (float) std::sin (tp * 3.0 * t);
            l += w2 * mv * nzL * 0.9f; r += w2 * mv * nzR * 0.9f;
            // 0.75-1.00 : inharmonic chimes, decaying pings every 0.5 s
            const float w3 = (float) std::max (0.0, 1.0 - std::fabs (u - 0.875) / 0.175);
            const double tc = std::fmod (t, 0.5); const float ce = (float) std::exp (-tc * 7.0);
            const double base = (((int) (t / 0.5)) % 2) ? 880.0 : 659.3;
            const float ch1 = (float) (std::sin (tp * base * t) + 0.45 * std::sin (tp * base * 2.756 * t) + 0.25 * std::sin (tp * base * 5.404 * t));
            l += w3 * ce * 0.5f * ch1; r += w3 * ce * 0.5f * ch1 * 0.9f;
            s.ch[0][(size_t) i] = l * 0.6f; s.ch[1][(size_t) i] = r * 0.6f;
        }
        // loop-friendly edges
        const int fade = (int) (sr * 0.01);
        for (int i = 0; i < fade; ++i) { const float g = (float) i / fade; for (int c = 0; c < 2; ++c) { s.ch[c][(size_t) i] *= g; s.ch[c][(size_t) (N - 1 - i)] *= g; } }
        s.finalise();
        return s;
    }
};
} // namespace vanta
