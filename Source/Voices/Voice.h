#pragma once
#include <cmath>
#include <algorithm>
#include <cstdint>
#include "SynthParams.h"
#include "Synthesis/VirtualAnalog/VAOscillator.h"

namespace vanta
{
struct Ladder   // 4-pole TPT ladder, tanh input saturation, LP24/LP12/BP/HP taps
{
    float s[4] {}, y[4] {};
    void reset() { for (int i = 0; i < 4; ++i) s[i] = y[i] = 0.f; }
    float process (float x, float Gp, float k, float drv, int type) noexcept
    {
        float u = std::tanh ((x - k * y[3]) * drv);
        float in = u;
        for (int i = 0; i < 4; ++i)
        {
            const float v = (in - s[i]) * Gp;
            const float out = v + s[i];
            s[i] = out + v; y[i] = out; in = out;
        }
        float o;
        switch (type)
        {
            case 1:  o = y[1]; break;
            case 2:  o = 2.f * (y[1] - y[3]); break;
            case 3:  o = u - 4.f * y[0] + 6.f * y[1] - 4.f * y[2] + y[3]; break;
            default: o = y[3]; break;
        }
        return o;
    }
};

class Voice
{
public:
    bool active = false, gate = false, sustained = false;
    int  note = -1; float vel = 0.f; uint32_t order = 0;
    int  engine = EngVirtualAnalog;          // engine this voice was started with (set by VoiceManager at note-on)
    int  grainCount() const noexcept { return granV.activeGrains(); }
    float curNote = 60.f, tgtNote = 60.f;
    float envLevel() const noexcept { return env; }

    void start (int n, float v, float glideFrom, uint32_t ord, bool retrigger = true) noexcept
    {
        note = n; tgtNote = (float) n; vel = v; order = ord; sustained = false;
        curNote = glideFrom < 0.f ? tgtNote : glideFrom;
        if (retrigger) { stage = Attack; gate = true; needInit = true; mv.noteOn ((uint32_t) (ord * 2654435761u) ^ ((uint32_t) n * 977u)); }
        active = true;
    }
    void changeNote (int n, uint32_t ord) noexcept { note = n; tgtNote = (float) n; order = ord; }
    void release() noexcept { gate = false; sustained = false; if (stage != Idle) stage = Release; }
    void kill() noexcept { active = false; gate = false; stage = Idle; env = 0.f; fl.reset(); fr.reset(); needInit = true; mv.reset(); modGain = gainSm = modWidth = 1.f; modPanOn = false; panL = panR = 1.f; }

    void prepare (double sr) { sampleRate = (float) sr; rng = 0x1234567u + (uint32_t) (reinterpret_cast<std::uintptr_t> (this) >> 4); }

    // grainCap: CPU governor for the granular engine (max simultaneous grains for this voice).
    void render (float* L, float* R, int n, const SynthParams& p, float bendSemis, int grainCap = GranularVoice::kMaxGrains) noexcept
    {
        if (! active) return;
        const mod::ModContext* mc = p.mod;
        if (mc == nullptr || ! mc->active) { modGain = modWidth = 1.f; modPanOn = false; renderEngine (L, R, n, p, bendSemis, grainCap); return; }   // legacy path
        for (int i = 0; i < n && active; i += mod::kChunk)
        {
            const int cnt = std::min (mod::kChunk, n - i);
            mod::ModOut mo; mv.update (*mc, cnt, i, (double) sampleRate, note, vel, gate, mo);
            SynthParams pm = p; float bend = bendSemis;
            applyMod (pm, bend, mo);
            renderEngine (L + i, R + i, cnt, pm, bend, grainCap);
        }
    }

private:
    void renderEngine (float* L, float* R, int n, const SynthParams& p, float bendSemis, int grainCap) noexcept
    {
        if (engine == EngGranular)  { renderGranular (L, R, n, p, bendSemis, grainCap); return; }
        if (engine == EngAdditive)  { renderAdditive (L, R, n, p, bendSemis); return; }
        renderVA (L, R, n, p, bendSemis);
    }
    static float clampf (float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    // Stage 4: apply the smoothed matrix output to a per-chunk copy of the parameters (and to the voice's output stage).
    void applyMod (SynthParams& pm, float& bend, const mod::ModOut& o) noexcept
    {
        using namespace mod; const float* d = o.d;
        bend += d[DstPitch] * 24.f;
        modGain = clampf (1.f + d[DstAmp], 0.f, 2.f); modWidth = clampf (1.f + d[DstWidth], 0.f, 2.f);
        const float pan = clampf (d[DstPan], -1.f, 1.f); modPanOn = std::fabs (pan) > 1e-4f;
        if (modPanOn) { const float ang = (pan + 1.f) * 0.78539816f; panL = std::cos (ang) * 1.41421356f; panR = std::sin (ang) * 1.41421356f; }
        else panL = panR = 1.f;
        if (d[DstCutoff] != 0.f) pm.cutoff = clampf (pm.cutoff * std::exp2 (d[DstCutoff] * 5.f), 20.f, 20000.f);
        pm.res = clampf (pm.res + d[DstRes], 0.f, 1.f); pm.drive = clampf (pm.drive + d[DstDrive], 0.f, 1.f);
        pm.pw = clampf (pm.pw + d[DstPW] * 0.45f, 0.05f, 0.95f);
        pm.mix[0] = clampf (pm.mix[0] + d[DstOsc1], 0.f, 1.f); pm.mix[1] = clampf (pm.mix[1] + d[DstOsc2], 0.f, 1.f); pm.mix[2] = clampf (pm.mix[2] + d[DstOsc3], 0.f, 1.f);
        pm.uniDetune = clampf (pm.uniDetune + d[DstUniDetune], 0.f, 1.f);
        auto& g = pm.gran;
        g.pos = clampf (g.pos + d[DstGrPos], 0.f, 1.f);
        if (d[DstGrSize] != 0.f)    g.sizeMs = clampf (g.sizeMs * std::exp2 (d[DstGrSize] * 2.f), 2.f, 2000.f);
        if (d[DstGrDensity] != 0.f) g.density = clampf (g.density * std::exp2 (d[DstGrDensity] * 2.f), 0.2f, 300.f);
        g.spray = clampf (g.spray + d[DstGrSpray], 0.f, 1.f); g.pitch = clampf (g.pitch + d[DstGrPitch] * 24.f, -48.f, 48.f);
        g.texture = clampf (g.texture + d[DstGrTexture], 0.f, 1.f); g.spread = clampf (g.spread + d[DstGrSpread], 0.f, 1.f);
        auto& a = pm.add;
        if (d[DstAddBalance] != 0.f || d[DstAddMorph] != 0.f || d[DstAddTone] != 0.f || d[DstAddShift] != 0.f || d[DstAddInharm] != 0.f || d[DstAddRolloff] != 0.f)
        {
            a.balance = clampf (a.balance + d[DstAddBalance], -1.f, 1.f); a.morph = clampf (a.morph + d[DstAddMorph], 0.f, 1.f);
            a.tone = clampf (a.tone + d[DstAddTone], 0.f, 1.f); a.shift = clampf (a.shift + d[DstAddShift], -1.f, 1.f);
            a.inharm = clampf (a.inharm + d[DstAddInharm], 0.f, 1.f); a.rolloff = clampf (a.rolloff + d[DstAddRolloff], 0.f, 1.f);
            additive::shape (a);
        }
    }

    // ------------------------------------------------------------ shared amp-envelope + ladder-filter stage
    struct Common { float aInc, dCoef, rCoef, cutTarget, kRes, drv, resComp, drvComp; };
    Common makeCommon (const SynthParams& p) const noexcept
    {
        Common c; const float sr = sampleRate;
        c.aInc  = 1.f / std::max (1.f, p.a * sr);
        c.dCoef = 1.f - std::exp (-4.6f / std::max (1.f, p.d * sr));
        c.rCoef = 1.f - std::exp (-4.6f / std::max (1.f, p.r * sr));
        c.cutTarget = std::min (p.cutoff, 0.45f * sr);
        c.kRes = p.res * 3.9f; c.drv = 1.f + p.drive * 4.f;
        c.resComp = 1.f + 0.5f * c.kRes; c.drvComp = 1.f / (1.f + 0.5f * p.drive * 4.f);
        return c;
    }
    // advances envelope + filter by one sample and ADDS the result to oL/oR. Returns false when the voice ended.
    inline bool stepOut (float sl, float sr_, float& oL, float& oR, const Common& c, const SynthParams& p) noexcept
    {
        switch (stage)
        {
            case Attack:  env += c.aInc; if (env >= 1.f) { env = 1.f; stage = Decay; } break;
            case Decay:   env += (p.s - env) * c.dCoef; if (std::fabs (env - p.s) < 1e-4f) { env = p.s; stage = Sustain; } break;
            case Sustain: env = p.s; break;
            case Release: env += (-0.001f - env) * c.rCoef; if (env <= 0.f) { env = 0.f; stage = Idle; active = false; } break;
            default: break;
        }
        cutSm += (c.cutTarget - cutSm) * 0.003f;
        const float G = std::tan (3.14159265f * std::min (cutSm, 0.45f * sampleRate) / sampleRate);
        const float Gp = G / (1.f + G);
        gainSm += (modGain - gainSm) * 0.02f;                       // exactly 1.0 when no modulation is routed to Amp
        const float amp = env * vel * gainSm;
        float yl = fl.process (sl,  Gp, c.kRes, c.drv, p.fltType) * c.resComp * c.drvComp * amp;
        float yr = fr.process (sr_, Gp, c.kRes, c.drv, p.fltType) * c.resComp * c.drvComp * amp;
        if (modWidth != 1.f) { const float m = (yl + yr) * 0.5f, sd = (yl - yr) * 0.5f * modWidth; yl = m + sd; yr = m - sd; }
        if (modPanOn) { yl *= panL; yr *= panR; }
        oL += yl; oR += yr;
        return active;
    }
    inline void glideStep (const SynthParams& p, bool glideOn, float linRate, float expCoef) noexcept
    {
        if (glideOn)
        {
            const float diff = tgtNote - curNote;
            const float lin = std::max (-linRate, std::min (linRate, diff));
            curNote += (1.f - p.glideCurve) * lin + p.glideCurve * diff * expCoef;
        }
        else curNote = tgtNote;
    }

    // ------------------------------------------------------------ GRANULAR
    void renderGranular (float* L, float* R, int n, const SynthParams& p, float bendSemis, int grainCap) noexcept
    {
        if (needInit) { granV.reset ((uint32_t) (order * 2654435761u) ^ (uint32_t) note * 977u); needInit = false; }
        const float sr = sampleRate;
        GranularParams gp = p.gran; gp.grainCap = grainCap;
        const auto frame = granV.setup (gp, sr);
        const Common cm = makeCommon (p);
        const bool  glideOn = p.glide > 0.0005f;
        const float linRate = 12.f / std::max (1.f, p.glide * sr);
        const float expCoef = 1.f - std::exp (-4.6f / std::max (1.f, p.glide * sr));
        for (int i = 0; i < n; ++i)
        {
            glideStep (p, glideOn, linRate, expCoef);
            float sl, sr_; granV.tick (frame, gp, curNote + bendSemis, sl, sr_);
            if (! stepOut (sl, sr_, L[i], R[i], cm, p)) break;
        }
    }

    // ------------------------------------------------------------ ADDITIVE
    void renderAdditive (float* L, float* R, int n, const SynthParams& p, float bendSemis) noexcept
    {
        if (needInit) { addV.start (p.add); needInit = false; }
        const float sr = sampleRate;
        const Common cm = makeCommon (p);
        const bool  glideOn = p.glide > 0.0005f;
        const float linRate = 12.f / std::max (1.f, p.glide * sr);
        const float expCoef = 1.f - std::exp (-4.6f / std::max (1.f, p.glide * sr));
        float bl[kAddBlock], br[kAddBlock];
        int i = 0;
        while (i < n)
        {
            const int cnt = std::min (kAddBlock, n - i);
            for (int j = 0; j < cnt; ++j) glideStep (p, glideOn, linRate, expCoef);
            const float nn = curNote + bendSemis + p.add.semi + p.add.fine * 0.01f;
            const float f0 = 440.f * std::exp2 ((nn - 69.f) / 12.f);
            addV.renderBlock (bl, br, cnt, p.add, f0, sr);
            for (int j = 0; j < cnt; ++j)
                if (! stepOut (bl[j], br[j], L[i + j], R[i + j], cm, p)) return;
            i += cnt;
        }
    }

    // ------------------------------------------------------------ VIRTUAL ANALOG (unchanged from Stage 2)
    void renderVA (float* L, float* R, int n, const SynthParams& p, float bendSemis) noexcept
    {
        const float sr = sampleRate;
        const int nU = std::max (1, std::min (8, p.uniVoices));
        const float uniNorm = 1.f / std::sqrt ((float) nU);

        float ratio[3][8], gl[8], gr[8];
        for (int u = 0; u < nU; ++u)
        {
            const float pos = nU > 1 ? (2.f * u / (float) (nU - 1) - 1.f) : 0.f;
            const float det = pos * p.uniDetune * 50.f;
            ratio[0][u] = std::exp2 (det / 1200.f);
            ratio[1][u] = std::exp2 ((det + p.tune2) / 1200.f);
            ratio[2][u] = std::exp2 ((det + p.tune3) / 1200.f);
            const float ang = (pos * p.uniSpread + 1.f) * 0.78539816f;
            gl[u] = std::cos (ang) * 1.41421356f; gr[u] = std::sin (ang) * 1.41421356f;
        }
        const Common cm = makeCommon (p);
        const bool  glideOn = p.glide > 0.0005f;
        const float linRate = 12.f / std::max (1.f, p.glide * sr);
        const float expCoef = 1.f - std::exp (-4.6f / std::max (1.f, p.glide * sr));
        const float subRatio = std::exp2 ((float) p.subOct);
        const float nk = 0.02f + p.noiseColor * 0.98f;

        for (int i = 0; i < n; ++i)
        {
            if (glideOn)
            {
                const float diff = tgtNote - curNote;
                const float lin = std::max (-linRate, std::min (linRate, diff));
                curNote += (1.f - p.glideCurve) * lin + p.glideCurve * diff * expCoef;
            }
            else curNote = tgtNote;

            // slow analog drift (random walk, smoothed)
            if (--driftCount <= 0) { driftTarget = rnd(); driftCount = (int) (sr * 0.05f); }
            driftVal += (driftTarget - driftVal) * 0.0008f;

            const float nn = curNote + p.oct * 12.f + (float) p.semi + p.fine + bendSemis + driftVal * p.drift * 0.25f;
            const float f0 = 440.f * std::exp2 ((nn - 69.f) / 12.f);

            float sl = 0.f, sr_ = 0.f;
            for (int u = 0; u < nU; ++u)
            {
                float su = 0.f;
                const float dt0 = std::min (0.45f, f0 * ratio[0][u] / sr);
                ph[0][u] += dt0;
                bool wrapped = false; float frac = 0.f;
                if (ph[0][u] >= 1.f) { ph[0][u] -= 1.f; wrapped = true; frac = ph[0][u] / dt0; }
                if (p.mix[0] > 0.f) su += p.mix[0] * oscSample (p.wave, ph[0][u], dt0, p.pw);
                for (int o = 1; o < 3; ++o)
                {
                    const float dt = std::min (0.45f, f0 * ratio[o][u] / sr);
                    ph[o][u] += dt;
                    if (p.sync && wrapped) ph[o][u] = frac * (dt / dt0);
                    if (ph[o][u] >= 1.f) ph[o][u] -= std::floor (ph[o][u]);
                    if (p.mix[o] > 0.f) su += p.mix[o] * oscSample (p.wave, ph[o][u], dt, p.pw);
                }
                sl += su * gl[u]; sr_ += su * gr[u];
            }
            sl *= uniNorm * 0.5f; sr_ *= uniNorm * 0.5f;

            if (p.subLevel > 0.f)
            {
                const float dts = std::min (0.45f, f0 * subRatio / sr);
                subPh += dts; if (subPh >= 1.f) subPh -= 1.f;
                const int w = p.subWave == 0 ? Sine : (p.subWave == 1 ? Square : Triangle);
                const float s = p.subLevel * 0.5f * oscSample (w, subPh, dts, 0.5f);
                sl += s; sr_ += s;
            }
            if (p.noiseLevel > 0.f)
            {
                const float white = rnd();
                float x = white;
                if (p.noiseType == 1)
                {
                    b0 = 0.99765f * b0 + white * 0.0990460f; b1 = 0.96300f * b1 + white * 0.2965164f;
                    b2 = 0.57000f * b2 + white * 1.0526913f;
                    x = (b0 + b1 + b2 + white * 0.1848f) * 0.25f;
                }
                nl += (x - nl) * nk;
                const float s = nl * p.noiseLevel * 0.4f;
                sl += s; sr_ += s;
            }

            if (! stepOut (sl, sr_, L[i], R[i], cm, p)) break;
        }
    }

    enum Stage { Idle, Attack, Decay, Sustain, Release };
    Stage stage = Idle; float env = 0.f;
    float sampleRate = 44100.f;
    float ph[3][8] {}; float subPh = 0.f;
    float b0 = 0, b1 = 0, b2 = 0, nl = 0;
    float driftVal = 0, driftTarget = 0; int driftCount = 0;
    float cutSm = 8000.f;
    Ladder fl, fr;
    mod::VoiceMod mv; float modGain = 1.f, gainSm = 1.f, modWidth = 1.f, panL = 1.f, panR = 1.f; bool modPanOn = false;
    GranularVoice granV; AdditiveVoice addV; bool needInit = true;
    uint32_t rng = 1;
    float rnd() noexcept { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (float) (int32_t) rng * (1.f / 2147483648.f); }
};
} // namespace vanta
