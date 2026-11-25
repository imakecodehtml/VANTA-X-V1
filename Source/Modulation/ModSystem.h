#pragma once
// VANTA X - Stage 4: modulation core (JUCE-free, allocation-free, lock-free).
//   * 8 LFOs, 8 envelopes, 1 step modulator, 8 macros, MIDI sources -> 32-slot matrix -> per-voice destination values.
//   * ModContext = block-constant configuration + global controller values + free-running clock (written by the audio thread only).
//   * VoiceMod   = per-voice state (note-retriggered LFO/step phases, envelopes, smoothing).
//   * Evaluation runs at control rate (kChunk samples) inside Voice::render; results are smoothed per destination.
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <string>

namespace vanta { namespace mod
{
constexpr int kLfos = 8, kEnvs = 8, kSteps = 16, kMacros = 8, kSlots = 32, kChunk = 32;

enum Src : int
{
    SrcNone = 0, SrcLfo1 = 1 /*..8*/, SrcEnv1 = 9 /*..16*/, SrcVelocity = 17, SrcModWheel, SrcPitchBend, SrcAftertouch,
    SrcKeyTrack, SrcExpression, SrcRandom, SrcStep, SrcMacro1 = 25 /*..32*/, kNumSrc = 33
};
enum Dst : int
{
    DstNone = 0, DstPitch, DstAmp, DstPan, DstWidth, DstCutoff, DstRes, DstDrive,
    DstPW, DstOsc1, DstOsc2, DstOsc3, DstUniDetune,
    DstGrPos, DstGrSize, DstGrDensity, DstGrSpray, DstGrPitch, DstGrTexture, DstGrSpread,
    DstAddBalance, DstAddMorph, DstAddTone, DstAddShift, DstAddInharm, DstAddRolloff,
    kNumDst
};

inline const char* const kSrcNames[kNumSrc] = {
    "None", "LFO 1", "LFO 2", "LFO 3", "LFO 4", "LFO 5", "LFO 6", "LFO 7", "LFO 8",
    "Env 1", "Env 2", "Env 3", "Env 4", "Env 5", "Env 6", "Env 7", "Env 8",
    "Velocity", "Mod Wheel", "Pitch Bend", "Aftertouch", "Key Track", "Expression", "Random", "Step Mod",
    "Macro 1", "Macro 2", "Macro 3", "Macro 4", "Macro 5", "Macro 6", "Macro 7", "Macro 8" };
inline const char* const kDstNames[kNumDst] = {
    "None", "Pitch", "Amp Level", "Pan", "Stereo Width", "Filter Cutoff", "Filter Resonance", "Filter Drive",
    "VA Pulse Width", "VA Osc 1 Level", "VA Osc 2 Level", "VA Osc 3 Level", "Unison Detune",
    "Grain Position", "Grain Size", "Grain Density", "Grain Spray", "Grain Pitch", "Grain Texture", "Grain Spread",
    "Add Odd/Even Balance", "Add Spectral Morph", "Add Tone", "Add Harmonic Shift", "Add Inharmonicity", "Add Rolloff" };

inline const char* const kLfoWaves[7] = { "Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Sample & Hold", "Random" };
inline const char* const kStepDirs[4] = { "Forward", "Backward", "Ping-Pong", "Random" };
constexpr int kNumDivs = 16;
inline const char* const kDivNames[kNumDivs] = { "1/32", "1/16T", "1/16", "1/16.", "1/8T", "1/8", "1/8.", "1/4T", "1/4", "1/4.", "1/2T", "1/2", "1/2.", "1 bar", "2 bars", "4 bars" };
inline const float kDivBeats[kNumDivs] = { 0.125f, 1.f / 6.f, 0.25f, 0.375f, 1.f / 3.f, 0.5f, 0.75f, 2.f / 3.f, 1.f, 1.5f, 4.f / 3.f, 2.f, 3.f, 4.f, 8.f, 16.f };

// ---- parameter IDs (1-based module numbers)
inline std::string lfoId (int n, const char* f)  { return "lfo" + std::to_string (n) + "_" + f; }     // wave rate sync div phase retrig fade bipolar depth
inline std::string envId (int n, const char* f)  { return "env" + std::to_string (n) + "_" + f; }     // a h d s r curve vel loop bipolar retrig
inline std::string stepValId (int i)             { return std::string ("stp_v") + (i < 9 ? "0" : "") + std::to_string (i + 1); }
inline std::string stepId (const char* f)        { return std::string ("stp_") + f; }                  // count rate sync div dir smooth retrig bipolar
inline std::string macroId (int n)               { return "mac" + std::to_string (n); }
inline std::string slotId (int n, const char* f) { return std::string ("mm") + (n < 10 ? "0" : "") + std::to_string (n) + "_" + f; }   // src dst amt on

inline const char* const kLfoFields[9]  = { "wave", "rate", "sync", "div", "phase", "retrig", "fade", "bipolar", "depth" };
inline const char* const kEnvFields[10] = { "a", "h", "d", "s", "r", "curve", "vel", "loop", "bipolar", "retrig" };
inline const char* const kStepFields[8] = { "count", "rate", "sync", "div", "dir", "smooth", "retrig", "bipolar" };
inline const char* const kSlotFields[4] = { "src", "dst", "amt", "on" };

inline uint32_t hash32 (uint32_t x) noexcept { x ^= x >> 16; x *= 0x7feb352dU; x ^= x >> 15; x *= 0x846ca68bU; x ^= x >> 16; return x; }
inline float hashf (uint32_t x) noexcept { return (float) (int32_t) hash32 (x) * (1.f / 2147483648.f); }     // -1..1

// ---- stateless waveform / step functions (phase in cycles, may exceed 1)
inline float lfoShape (int wave, double ph, uint32_t seed) noexcept
{
    const double cyc = std::floor (ph); const float f = (float) (ph - cyc); const uint32_t c = (uint32_t) (int64_t) cyc;
    switch (wave)
    {
        case 0:  return std::sin (6.28318531f * f);
        case 1:  return f < 0.25f ? 4.f * f : (f < 0.75f ? 2.f - 4.f * f : 4.f * f - 4.f);
        case 2:  return 2.f * f - 1.f;
        case 3:  return 1.f - 2.f * f;
        case 4:  return f < 0.5f ? 1.f : -1.f;
        case 5:  return hashf (seed * 0x9E3779B1u + c);
        default: { const float a = hashf (seed * 0x9E3779B1u + c), b = hashf (seed * 0x9E3779B1u + c + 1u); const float t = f * f * (3.f - 2.f * f); return a + (b - a) * t; }
    }
}
inline int stepIndex (int dir, int count, int64_t n, uint32_t seed) noexcept
{
    count = std::max (1, std::min (kSteps, count));
    auto md = [] (int64_t a, int64_t m) { const int64_t r = a % m; return (int) (r < 0 ? r + m : r); };
    switch (dir)
    {
        case 1:  return count - 1 - md (n, count);
        case 2:  { if (count < 2) return 0; const int per = 2 * count - 2; const int m = md (n, per); return m < count ? m : per - m; }
        case 3:  return (int) (hash32 (seed + (uint32_t) n * 0x9E3779B1u) % (uint32_t) count);
        default: return md (n, count);
    }
}

struct LfoCfg  { int wave = 0; float rateHz = 2.f; bool sync = false; float beats = 1.f; float phase = 0.f, fade = 0.f, depth = 1.f; bool note = false, bipolar = true; };
struct EnvCfg  { float a = 0.01f, h = 0.f, d = 0.3f, s = 0.7f, r = 0.3f, curve = 0.f, velSens = 0.f; bool loop = false, bipolar = false, fromCurrent = false; };
struct StepCfg
{
    float v[kSteps] {}; int count = 8; float rateHz = 4.f; bool sync = true; float beats = 0.25f; int dir = 0; float smooth = 0.f; bool note = false, bipolar = true;
};
inline float stepValue (const StepCfg& c, double ph, uint32_t seed) noexcept
{
    const double nf = std::floor (ph); const int64_t n = (int64_t) nf; const float f = (float) (ph - nf);
    const float cur = c.v[stepIndex (c.dir, c.count, n, seed)];
    const float prev = n > 0 ? c.v[stepIndex (c.dir, c.count, n - 1, seed)] : cur;
    float t = 1.f;
    if (c.smooth > 0.001f) { t = std::min (1.f, f / c.smooth); t = t * t * (3.f - 2.f * t); }
    const float v = prev + (cur - prev) * t;
    return c.bipolar ? v : v * 0.5f + 0.5f;
}
// curve shaping of a 0..1 ramp; c in -1..1 (0 = linear, >0 slow start, <0 fast start)
inline float envShape (float x, float c) noexcept
{
    if (std::fabs (c) < 0.002f) return x;
    const float k = c * 5.f; return (std::exp (k * x) - 1.f) / (std::exp (k) - 1.f);
}

struct Slot { int src = 0, dst = 0; float amt = 0.f; };

struct ModContext
{
    bool active = false; int numSlots = 0; Slot slots[kSlots]; uint64_t usedMask = 0;
    LfoCfg lfo[kLfos]; EnvCfg env[kEnvs]; StepCfg step; float macro[kMacros] {};
    float modWheel = 0.f, pitchBend = 0.f, aftertouch = 0.f, expression = 0.f; float polyAT[128] {};
    double lfoPhase[kLfos] {}, stepPhase = 0.0, lfoInc[kLfos] {}, stepInc = 0.0;    // free-running clock: phase (cycles) at segment start / increment per sample

    void clearSlots() noexcept { numSlots = 0; usedMask = 0; active = false; }
    void addSlot (int src, int dst, float amt) noexcept
    {
        if (src <= 0 || src >= kNumSrc || dst <= 0 || dst >= kNumDst || amt == 0.f || numSlots >= kSlots) return;
        slots[numSlots++] = { src, dst, std::max (-1.f, std::min (1.f, amt)) };
        usedMask |= (uint64_t) 1 << src; active = true;
    }
    void setRates (double sr, double bpm) noexcept
    {
        const double bps = std::max (1.0, bpm) / 60.0;
        for (int k = 0; k < kLfos; ++k) lfoInc[k] = (lfo[k].sync ? bps / std::max (0.01f, lfo[k].beats) : (double) lfo[k].rateHz) / sr;
        stepInc = (step.sync ? bps / std::max (0.01f, step.beats) : (double) step.rateHz) / sr;
    }
    void advance (int n) noexcept
    {
        for (int k = 0; k < kLfos; ++k) { lfoPhase[k] += lfoInc[k] * n; if (lfoPhase[k] > 1.0e9) lfoPhase[k] -= 1.0e9; }
        stepPhase += stepInc * n; if (stepPhase > 1.0e9) stepPhase -= 1.0e9;
    }
    bool used (int s) const noexcept { return (usedMask >> s) & 1u; }
};

struct ModOut { float d[kNumDst] {}; };

struct VoiceMod
{
    bool pendingOn = false, gateState = false, first = true;
    uint32_t seed = 1; float rnd = 0.f, tNote = 0.f; double lfoPh[kLfos] {}, stepPh = 0.0;
    struct Env { int stage = 0; float level = 0.f, x = 0.f, from = 0.f, relFrom = 0.f; } e[kEnvs];
    float sm[kNumDst] {};

    void reset() noexcept { *this = VoiceMod(); }
    void noteOn (uint32_t s) noexcept { pendingOn = true; seed = s ? s : 1u; }

    void stepEnv (Env& en, const EnvCfg& c, float dt) noexcept
    {
        switch (en.stage)
        {
            case 1: en.x += dt / std::max (1e-4f, c.a); en.level = en.from + (1.f - en.from) * envShape (std::min (1.f, en.x), c.curve);
                    if (en.x >= 1.f) { en.level = 1.f; en.x = 0.f; en.stage = c.h > 1e-4f ? 2 : 3; } break;
            case 2: en.x += dt / std::max (1e-4f, c.h); en.level = 1.f; if (en.x >= 1.f) { en.x = 0.f; en.stage = 3; } break;
            case 3: en.x += dt / std::max (1e-4f, c.d); en.level = 1.f - (1.f - c.s) * envShape (std::min (1.f, en.x), c.curve);
                    if (en.x >= 1.f) { en.level = c.s; en.x = 0.f; if (c.loop && gateState) { en.stage = 1; en.from = en.level; } else en.stage = 4; } break;
            case 4: en.level = c.s; break;
            case 5: en.x += dt / std::max (1e-4f, c.r); en.level = en.relFrom * (1.f - envShape (std::min (1.f, en.x), c.curve));
                    if (en.x >= 1.f) { en.level = 0.f; en.stage = 0; } break;
            default: en.level = 0.f; break;
        }
    }

    // One control-rate step: `offset` = samples since the start of the current render segment (for the free-running clock).
    void update (const ModContext& mc, int chunk, int offset, double sr, int note, float vel, bool gate, ModOut& out) noexcept
    {
        const float dt = (float) ((double) chunk / sr);
        if (pendingOn)
        {
            pendingOn = false; gateState = true; tNote = 0.f; stepPh = 0.0; rnd = hashf (seed ^ 0xA5A5A5A5u);
            for (int k = 0; k < kLfos; ++k) lfoPh[k] = 0.0;
            for (int k = 0; k < kEnvs; ++k) { auto& en = e[k]; en.from = mc.env[k].fromCurrent ? en.level : 0.f; en.x = 0.f; en.stage = 1; }
        }
        if (gateState && ! gate)
        {
            gateState = false;
            for (auto& en : e) if (en.stage >= 1 && en.stage <= 4) { en.relFrom = en.level; en.x = 0.f; en.stage = 5; }
        }
        for (int k = 0; k < kEnvs; ++k) stepEnv (e[k], mc.env[k], dt);

        float src[kNumSrc] {};
        for (int k = 0; k < kLfos; ++k)
        {
            if (mc.used (SrcLfo1 + k))
            {
                const LfoCfg& c = mc.lfo[k];
                const double ph = (c.note ? lfoPh[k] : mc.lfoPhase[k] + mc.lfoInc[k] * offset) + (double) c.phase;
                float v = lfoShape (c.wave, ph, c.note ? seed + (uint32_t) k * 7919u : 0x1234u + (uint32_t) k * 7919u);
                if (! c.bipolar) v = v * 0.5f + 0.5f;
                const float fade = c.fade <= 1e-4f ? 1.f : std::min (1.f, tNote / c.fade);
                src[SrcLfo1 + k] = v * c.depth * fade;
            }
            lfoPh[k] += mc.lfoInc[k] * chunk;
        }
        for (int k = 0; k < kEnvs; ++k)
        {
            const EnvCfg& c = mc.env[k]; float v = e[k].level * (1.f - c.velSens + c.velSens * vel);
            src[SrcEnv1 + k] = c.bipolar ? v * 2.f - 1.f : v;
        }
        if (mc.used (SrcStep))
        {
            const StepCfg& c = mc.step; src[SrcStep] = stepValue (c, c.note ? stepPh : mc.stepPhase + mc.stepInc * offset, c.note ? seed : 0x777u);
        }
        stepPh += mc.stepInc * chunk; tNote += dt;
        src[SrcVelocity] = vel; src[SrcModWheel] = mc.modWheel; src[SrcPitchBend] = mc.pitchBend;
        src[SrcAftertouch] = std::max (mc.aftertouch, mc.polyAT[note & 127]);
        src[SrcKeyTrack] = std::max (-1.f, std::min (1.f, ((float) note - 60.f) / 48.f));
        src[SrcExpression] = mc.expression; src[SrcRandom] = rnd;
        for (int k = 0; k < kMacros; ++k) src[SrcMacro1 + k] = mc.macro[k];

        float acc[kNumDst] {};
        for (int i = 0; i < mc.numSlots; ++i) acc[mc.slots[i].dst] += src[mc.slots[i].src] * mc.slots[i].amt;
        const float k = first ? 1.f : 1.f - std::exp (-dt / 0.0015f);       // ~1.5 ms smoothing: removes clicks from steps / S&H / square edges
        first = false;
        for (int d = 0; d < kNumDst; ++d) { sm[d] += (acc[d] - sm[d]) * k; out.d[d] = sm[d]; }
    }
};
}} // namespace vanta::mod
