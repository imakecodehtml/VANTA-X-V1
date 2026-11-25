#pragma once
// VANTA X - Granular engine (JUCE-free, allocation-free in the audio path).
// One GranularVoice lives inside each Voice. Grains are pre-allocated in a fixed pool.
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "SampleData.h"

namespace vanta
{
// ------------------------------------------------------------------ grain windows
namespace grainwin
{
enum Type { Hann = 0, Gaussian, Triangle, Trapezoid, Sine, Blackman, NumTypes };
static constexpr int kTable = 1024;                 // table has kTable + 1 entries

inline float smooth01 (float x) noexcept { x = std::min (1.f, std::max (0.f, x)); return x * x * (3.f - 2.f * x); }

// x in [0,1] over the grain, width in (0,1] = fraction of the grain that is "open", shape in [0,1] (type specific).
// Every window is forced to exactly 0 at both ends (short raised edge) so a grain can never click.
inline float value (int type, float x, float width, float shape) noexcept
{
    width = std::min (1.f, std::max (0.05f, width));
    shape = std::min (1.f, std::max (0.f, shape));
    x = (x - (1.f - width) * 0.5f) / width;
    if (x <= 0.f || x >= 1.f) return 0.f;
    float w = 0.f;
    const float pi = 3.14159265f;
    switch (type)
    {
        case Gaussian:
        {   const float sigma = 0.35f - 0.27f * shape; const float d = (x - 0.5f) / sigma; w = std::exp (-0.5f * d * d); break; }
        case Triangle:
        {   const float pk = 0.1f + 0.8f * shape; w = x < pk ? x / pk : (1.f - x) / (1.f - pk); break; }
        case Trapezoid:
        {   const float ramp = 0.02f + 0.48f * (1.f - shape); w = std::min (1.f, std::min (x / ramp, (1.f - x) / ramp)); break; }
        case Sine:
        {   w = std::pow (std::sin (pi * x), 1.f + 2.f * shape); break; }
        case Blackman:
        {   const float b = 0.42f - 0.5f * std::cos (2.f * pi * x) + 0.08f * std::cos (4.f * pi * x); w = std::pow (std::max (0.f, b), 1.f + 2.f * shape); break; }
        case Hann:
        default:
        {   const float T = 1.f - 0.9f * shape;               // taper fraction: 1 = pure Hann, small = flat top
            const float e = std::min (x, 1.f - x);
            w = e >= T * 0.5f ? 1.f : 0.5f * (1.f - std::cos (2.f * pi * e / T)); break; }
    }
    const float edge = std::min (smooth01 (x / 0.03f), smooth01 ((1.f - x) / 0.03f));
    return std::max (0.f, std::min (1.f, w)) * edge;
}

inline void buildTable (float* t, int type, float width, float shape) noexcept
{ for (int i = 0; i <= kTable; ++i) t[i] = value (type, (float) i / (float) kTable, width, shape); }
} // namespace grainwin

// ------------------------------------------------------------------ UI visualisation ring (written by audio thread)
struct GrainViz
{
    static constexpr int kN = 64;
    std::atomic<uint32_t> counter { 0 };
    std::atomic<float> pos[kN];        // 0..1 over the whole sample
    std::atomic<float> pitch[kN];      // semitones relative to root
    std::atomic<float> size[kN];       // seconds
    GrainViz() { for (int i = 0; i < kN; ++i) { pos[i].store (0.f); pitch[i].store (0.f); size[i].store (0.f); } }
};

// ------------------------------------------------------------------ parameters (snapshot per block)
struct GranularParams
{
    const SampleData* sample = nullptr;      // may be null: engine outputs silence
    float sizeMs = 120.f, sizeRand = 0.1f, density = 32.f;
    float pos = 0.42f, spray = 0.15f, speed = 0.f;
    float pitch = 0.f, pitchRand = 0.f, spread = 0.75f, texture = 0.3f;
    float start = 0.f, end = 1.f;
    int   dir = 0;                           // 0 forward, 1 reverse, 2 alternate, 3 random
    int   root = 60;
    const float* window = nullptr;           // table of grainwin::kTable + 1 entries (null -> built-in Hann)
    int   grainCap = 32;                     // max simultaneous grains for this voice (CPU governor)
    GrainViz* viz = nullptr;
};

// ------------------------------------------------------------------ voice
class GranularVoice
{
public:
    static constexpr int kMaxGrains = 32;

    struct Frame            // per-block cache
    {
        const SampleData* s = nullptr;
        int   rs = 0, rlen = 8;
        float srcRatio = 1.f, sr = 44100.f;
        float normTarget = 1.f;
        float meanInterval = 1000.f;
        float sizeSamples = 1000.f;
        float scanInc = 0.f;
        bool  mono = true;
        int   cap = 32;
    };

    void reset (uint32_t seed) noexcept
    {
        for (auto& g : grains) g.active = false;
        nActive = 0; timer = 0.f; scan = 0.f; altDir = false; normSm = 1.f; fresh = true;
        rng = seed * 2654435761u + 12345u; if (rng == 0) rng = 1;
    }
    int activeGrains() const noexcept { return nActive; }

    Frame setup (const GranularParams& p, float sr) const noexcept
    {
        Frame f; f.sr = sr; f.s = (p.sample != nullptr && p.sample->valid()) ? p.sample : nullptr;
        f.cap = std::max (1, std::min (kMaxGrains, p.grainCap));
        if (f.s == nullptr) return f;
        const int frames = f.s->frames;
        f.rs = std::max (0, std::min (frames - 8, (int) std::lround (std::min (p.start, p.end) * (float) frames)));
        const int re = std::max (f.rs + 8, std::min (frames, (int) std::lround (std::max (p.start, p.end) * (float) frames)));
        f.rlen = re - f.rs;
        f.srcRatio = (float) (f.s->sampleRate / sr);
        f.mono = f.s->channels < 2;
        f.sizeSamples = std::max (16.f, p.sizeMs * 0.001f * sr);
        const float dens = std::max (0.2f, std::min (400.f, p.density));
        f.meanInterval = std::max (1.f, sr / dens);
        const float overlap = std::min ((float) f.cap, f.sizeSamples / f.meanInterval);
        f.normTarget = 1.f / std::sqrt (std::max (1.f, 0.75f * overlap));
        f.scanInc = p.speed * f.srcRatio / (float) f.rlen;
        return f;
    }

    // Generates one output sample pair and ADDS nothing: returns the grain sum. `note` = MIDI note incl. glide + bend.
    inline void tick (const Frame& f, const GranularParams& p, float note, float& outL, float& outR) noexcept
    {
        outL = outR = 0.f;
        if (f.s == nullptr) { for (auto& g : grains) g.active = false; nActive = 0; curSample = nullptr; return; }
        if (f.s != curSample)                                  // sample was replaced: in-flight grains refer to old data
        { for (auto& g : grains) g.active = false; nActive = 0; curSample = f.s; }

        scan += f.scanInc; scan -= std::floor (scan);

        timer -= 1.f;
        if (fresh) { timer = 0.f; fresh = false; }
        while (timer <= 0.f)
        {
            spawn (f, p, note);
            const float u = std::max (1e-3f, rnd01());
            const float irregular = -std::log (u);                       // exponential (Poisson) timing, mean 1
            timer += f.meanInterval * ((1.f - p.texture) + p.texture * irregular);
            if (timer < 1.f) timer = 1.f;
        }

        normSm += (f.normTarget - normSm) * 0.002f;

        const float* win = p.window;
        float l = 0.f, r = 0.f;
        int cnt = 0;
        for (auto& g : grains)
        {
            if (! g.active) continue;
            ++cnt;
            const float w = win != nullptr ? tableLookup (win, g.ph) : hannLookup (g.ph);
            const float a = w * g.amp;
            if (f.mono)
            {
                const float v = hermite (f.s->ch[0].data(), g.pos, g.rs, g.rlen) * a;
                l += v * g.gl; r += v * g.gr;
            }
            else
            {
                l += hermite (f.s->ch[0].data(), g.pos, g.rs, g.rlen) * a * g.gl;
                r += hermite (f.s->ch[1].data(), g.pos, g.rs, g.rlen) * a * g.gr;
            }
            g.pos += g.inc;
            const double rend = (double) (g.rs + g.rlen);       // region snapshot taken at spawn: editing start/end never makes a running grain jump
            if (g.pos >= rend || g.pos < (double) g.rs)
            {
                g.pos = (double) g.rs + std::fmod (g.pos - (double) g.rs, (double) g.rlen);
                if (g.pos < (double) g.rs) g.pos += (double) g.rlen;
            }
            g.ph += g.phInc;
            if (g.ph >= 1.f) { g.active = false; --nActive; }
        }
        (void) cnt;
        outL = l * normSm; outR = r * normSm;
    }

private:
    struct Grain { double pos = 0.0; float inc = 1.f, ph = 0.f, phInc = 0.f, gl = 1.f, gr = 1.f, amp = 1.f; int rs = 0, rlen = 8; bool active = false; };

    Grain grains[kMaxGrains];
    int nActive = 0; float timer = 0.f, scan = 0.f, normSm = 1.f; bool altDir = false, fresh = true;
    const SampleData* curSample = nullptr;
    uint32_t rng = 1;

    float rnd01() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (rng >> 8) * (1.f / 16777216.f); }
    float rndS() noexcept { return rnd01() * 2.f - 1.f; }

    static inline float tableLookup (const float* t, float ph) noexcept
    {
        const float x = ph * (float) grainwin::kTable; const int i = std::min (grainwin::kTable - 1, (int) x); const float fr = x - (float) i;
        return t[i] + (t[i + 1] - t[i]) * fr;
    }
    static inline float hannLookup (float ph) noexcept { return 0.5f - 0.5f * std::cos (6.2831853f * ph); }

    static inline int wrapIdx (int j, int rs, int rlen) noexcept { if (j < rs) j += rlen; else if (j >= rs + rlen) j -= rlen; return j; }
    static inline float hermite (const float* d, double pos, int rs, int rlen) noexcept
    {
        const int i0 = (int) pos; const float t = (float) (pos - (double) i0);
        const float y0 = d[wrapIdx (i0 - 1, rs, rlen)], y1 = d[wrapIdx (i0, rs, rlen)], y2 = d[wrapIdx (i0 + 1, rs, rlen)], y3 = d[wrapIdx (i0 + 2, rs, rlen)];
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * t + c2) * t + c1) * t + y1;
    }

    void spawn (const Frame& f, const GranularParams& p, float note) noexcept
    {
        if (nActive >= f.cap) return;                       // never steal a running grain (would click)
        Grain* g = nullptr;
        for (auto& c : grains) if (! c.active) { g = &c; break; }
        if (g == nullptr) return;

        // position inside the region
        float u = p.pos + scan + p.spray * 0.5f * rndS();
        u -= std::floor (u);
        const double startPos = (double) f.rs + (double) u * (double) f.rlen;

        // direction
        float sign = 1.f;
        switch (p.dir)
        {
            case 1: sign = -1.f; break;
            case 2: sign = altDir ? -1.f : 1.f; altDir = ! altDir; break;
            case 3: sign = rnd01() < 0.5f ? -1.f : 1.f; break;
            default: break;
        }
        const float semis = note - (float) p.root + p.pitch + p.pitchRand * 12.f * rndS();
        const float rate = f.srcRatio * std::exp2 (semis / 12.f);

        const float sizeVar = 1.f + p.sizeRand * 0.8f * rndS();
        const float len = std::max (16.f, f.sizeSamples * sizeVar);

        g->rs = f.rs; g->rlen = f.rlen;
        g->pos = startPos; g->inc = sign * rate; g->ph = 0.f; g->phInc = 1.f / len;
        g->amp = 1.f - p.texture * 0.6f * rnd01();
        const float pan = std::max (-1.f, std::min (1.f, p.spread * rndS()));
        const float ang = (pan + 1.f) * 0.78539816f;
        g->gl = std::cos (ang) * 1.41421356f; g->gr = std::sin (ang) * 1.41421356f;
        g->active = true; ++nActive;

        if (p.viz != nullptr)
        {
            const uint32_t i = p.viz->counter.fetch_add (1u, std::memory_order_relaxed) & (uint32_t) (GrainViz::kN - 1);
            p.viz->pos[i].store ((float) (startPos / (double) f.s->frames), std::memory_order_relaxed);
            p.viz->pitch[i].store (semis, std::memory_order_relaxed);
            p.viz->size[i].store (len / f.sr, std::memory_order_relaxed);
        }
    }
};
} // namespace vanta
