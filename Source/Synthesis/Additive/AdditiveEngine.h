#pragma once
// VANTA X - Additive engine (JUCE-free, allocation-free in the audio path).
// 32 harmonics per voice, each a recursively-rotated phasor (4 multiplies / sample / harmonic), amplitude and phase
// changes are interpolated per 16-sample block so editing controls never clicks.
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace vanta
{
constexpr int kAddHarm = 32;
constexpr int kAddBlock = 16;
constexpr float kAmpSlew = 0.15f;      // fraction of the remaining amplitude step applied per block

struct AdditiveParams
{
    float amp[kAddHarm];                 // editor amplitudes 0..1 (harmonic 1..32)
    float phase[kAddHarm];               // editor phases 0..1 (turns)
    int   count = 32;                    // active harmonics 1..32
    float level = 0.8f, shift = 0.f, morph = 0.f;
    int   target = 0;                    // morph target spectrum
    float tone = 1.f;                    // 1 = open, 0 = dark
    float inharm = 0.f;
    float balance = 0.f;                 // -1 = odd only .. +1 = even only
    float rolloff = 0.f;                 // extra spectral slope, 0..1
    float semi = 0.f, fine = 0.f;        // fundamental offset (semitones, cents)
    bool  sfOn = false; float sfPos = 0.5f, sfWidth = 0.4f, sfGain = 0.f;
    bool  henv = false; float hdecay = 1.f;
    bool  uni = false; float spread = 0.25f, detune = 0.25f;   // detune 0..1 -> 0..25 cents each side
    float shaped[kAddHarm];              // result of additive::shape() (computed once per block)

    AdditiveParams()
    {
        for (int k = 0; k < kAddHarm; ++k) { amp[k] = 1.f / (float) (k + 1); phase[k] = 0.f; shaped[k] = amp[k]; }
    }
};

namespace additive
{
inline float targetAmp (int target, int h) noexcept        // h = 1..32
{
    const float fh = (float) h; const bool odd = (h & 1) != 0;
    switch (target)
    {
        case 1:  return odd ? 1.f / fh : 0.f;                                    // square
        case 2:  return odd ? 1.f / (fh * fh) : 0.f;                             // triangle
        case 3:  { static const int d[] { 1, 2, 3, 4, 6, 8 };                                  // organ drawbars
                   for (int i = 0; i < 6; ++i) if (d[i] == h) return 0.8f - 0.08f * (float) i;
                   return 0.f; }
        case 4:  { const float a = std::exp (-0.5f * std::pow ((std::log2 (fh) - 1.f) / 0.55f, 2.f));       // formant "ah"
                   const float b = std::exp (-0.5f * std::pow ((std::log2 (fh) - 3.4f) / 0.45f, 2.f)); return 0.9f * a + 0.6f * b; }
        default: return 1.f / fh;                                                // saw
    }
}

// Gain applied to harmonic h (1..32) by the spectral filter (also used by the editor to draw the filter curve).
inline float spectralFilterGain (float pos, float width, float gain, int h) noexcept
{
    const float c = std::pow ((float) kAddHarm, std::min (1.f, std::max (0.f, pos)));          // centre harmonic 1..32
    const float bw = 0.15f + std::min (1.f, std::max (0.f, width)) * 2.5f;
    const float d = std::log2 ((float) h / c) / bw; const float bump = std::exp (-2.f * d * d);
    return gain >= 0.f ? 1.f + 3.f * gain * bump : 1.f + gain * bump;
}

// Full static spectral shaping chain (editor -> morph -> shift -> balance -> rolloff -> tone -> spectral filter -> count -> level).
inline void shape (AdditiveParams& p) noexcept
{
    float a[kAddHarm];
    for (int k = 0; k < kAddHarm; ++k)
        a[k] = p.amp[k] + (targetAmp (p.target, k + 1) - p.amp[k]) * p.morph;

    float b[kAddHarm];
    const float sh = p.shift * 16.f;
    for (int k = 0; k < kAddHarm; ++k)
    {
        const float src = (float) k - sh;
        if (src < 0.f || src > (float) (kAddHarm - 1)) { b[k] = 0.f; continue; }
        const int i0 = (int) src; const int i1 = std::min (kAddHarm - 1, i0 + 1); const float f = src - (float) i0;
        b[k] = a[i0] + (a[i1] - a[i0]) * f;
    }

    const int cnt = std::max (1, std::min (kAddHarm, p.count));
    const float kc = 1.f + 63.f * p.tone * p.tone;
    float sum = 0.f;
    for (int k = 0; k < kAddHarm; ++k)
    {
        const int h = k + 1; float v = b[k];
        if (p.balance > 0.f && (h & 1)) v *= 1.f - p.balance;
        else if (p.balance < 0.f && ! (h & 1)) v *= 1.f + p.balance;
        if (p.rolloff > 0.f) v *= std::pow ((float) h, -2.f * p.rolloff);
        if (p.tone < 0.999f) { const float q = (float) h / kc; v /= std::sqrt (1.f + q * q * q * q); }
        if (p.sfOn) v *= spectralFilterGain (p.sfPos, p.sfWidth, p.sfGain, h);
        if (h > cnt) v = 0.f;
        v = std::max (0.f, v);
        p.shaped[k] = v; sum += v;
    }
    // keep dense spectra from overloading: soft normalisation, then user level
    const float norm = 1.f / std::max (1.f, 0.5f * sum);
    for (int k = 0; k < kAddHarm; ++k) p.shaped[k] *= norm * p.level;
}
} // namespace additive

class AdditiveVoice
{
public:
    void start (const AdditiveParams& p) noexcept
    {
        for (int b = 0; b < 2; ++b)
            for (int k = 0; k < kAddHarm; ++k)
            {
                const float ph = p.phase[k] * 6.2831853f;
                re[b][k] = std::cos (ph); im[b][k] = std::sin (ph); amp[b][k] = 0.f;
            }
        for (int k = 0; k < kAddHarm; ++k) applied[k] = p.phase[k] * 6.2831853f;
        t = 0.f;
    }

    // Renders `cnt` (<= kAddBlock) samples into outL/outR (overwrites).
    void renderBlock (float* outL, float* outR, int cnt, const AdditiveParams& p, float f0, float sr) noexcept
    {
        for (int i = 0; i < cnt; ++i) outL[i] = outR[i] = 0.f;
        const int banks = p.uni ? 2 : 1;
        const float detCents = p.detune * 25.f;
        const float invCnt = 1.f / (float) cnt;
        const float bankNorm = banks == 2 ? 0.70710678f : 1.f;
        const float tNow = t;

        // slew phase offsets towards the editor target (click-free phase edits)
        float dPhase[kAddHarm];
        for (int k = 0; k < kAddHarm; ++k)
        {
            const float tgt = p.phase[k] * 6.2831853f;
            float diff = tgt - applied[k];
            diff = std::max (-0.12f, std::min (0.12f, diff));
            applied[k] += diff; dPhase[k] = diff;
        }

        const float nyq = 0.46f * sr, band = 0.06f * sr;
        const float B = p.inharm * p.inharm * 0.001f;
        const int cntH = std::max (1, std::min (kAddHarm, p.count));

        for (int b = 0; b < banks; ++b)
        {
            const float det = banks == 2 ? (b == 0 ? -detCents : detCents) : 0.f;
            const float ratio = std::exp2 (det / 1200.f);
            const float pan = banks == 2 ? (b == 0 ? -p.spread : p.spread) : 0.f;
            const float ang = (pan + 1.f) * 0.78539816f;
            const float gl = std::cos (ang) * 1.41421356f * bankNorm, gr = std::sin (ang) * 1.41421356f * bankNorm;
            float acc[kAddBlock];
            for (int i = 0; i < cnt; ++i) acc[i] = 0.f;

            for (int k = 0; k < kAddHarm; ++k)
            {
                const int h = k + 1;
                float target = p.shaped[k];
                const float f = f0 * ratio * (float) h * std::sqrt (1.f + B * (float) (h * h));
                if (h > cntH || target <= 0.f) target = 0.f;
                if (f >= nyq) target = 0.f; else if (f > nyq - band) target *= (nyq - f) / band;
                if (target > 0.f && p.henv) target *= std::exp (-3.f * (float) (h - 1) / 31.f * tNow / std::max (0.02f, p.hdecay));

                float a = amp[b][k];
                if (target <= 0.f && a < 1e-5f) { amp[b][k] = 0.f; continue; }            // silent harmonic: skip work
                const float da = (target - a) * kAmpSlew * invCnt;                          // one-pole slew (~2 ms) -> click-free edits

                float zr = re[b][k], zi = im[b][k];
                if (dPhase[k] != 0.f)
                {
                    const float cd = std::cos (dPhase[k]), sd = std::sin (dPhase[k]);
                    const float nr = zr * cd - zi * sd; zi = zr * sd + zi * cd; zr = nr;
                }
                const float w = 6.2831853f * std::min (f, nyq) / sr;
                const float c = std::cos (w), s = std::sin (w);
                for (int i = 0; i < cnt; ++i)
                {
                    a += da; acc[i] += a * zi;
                    const float nr = zr * c - zi * s; zi = zr * s + zi * c; zr = nr;
                }
                const float m = 1.5f - 0.5f * (zr * zr + zi * zi); zr *= m; zi *= m;          // renormalise |z|
                re[b][k] = zr; im[b][k] = zi; amp[b][k] = a;
            }
            for (int i = 0; i < cnt; ++i) { outL[i] += acc[i] * gl; outR[i] += acc[i] * gr; }
        }
        t += (float) cnt / sr;
    }

private:
    float re[2][kAddHarm] {}, im[2][kAddHarm] {}, amp[2][kAddHarm] {}, applied[kAddHarm] {};
    float t = 0.f;
};
} // namespace vanta
