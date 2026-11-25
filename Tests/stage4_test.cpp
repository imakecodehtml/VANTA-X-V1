// VANTA X - Stage 4 modulation tests (JUCE-free).
#include <cstdio>
#include <cmath>
#include <set>
#include <vector>
#include <string>
#include <memory>
#include "Audio/ParamList.h"
#include "Voices/VoiceManager.h"

using namespace vanta; using namespace vanta::mod;
static int passed = 0, failed = 0;
#define CHECK(cond, ...) do { if (cond) { ++passed; std::printf ("ok:   "); } else { ++failed; std::printf ("FAIL: "); } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static bool near (float a, float b, float t = 1e-3f) { return std::fabs (a - b) <= t; }
static float rmsOf (const std::vector<float>& a, size_t from = 0) { double s = 0; for (size_t i = from; i < a.size(); ++i) s += (double) a[i] * a[i]; return (float) std::sqrt (s / (double) std::max<size_t> (1, a.size() - from)); }
static bool finiteLoose (const std::vector<float>& a) { for (float x : a) if (! std::isfinite (x) || std::fabs (x) > 64.f) return false; return true; }
static bool finite (const std::vector<float>& a) { for (float x : a) if (! std::isfinite (x) || std::fabs (x) > 8.f) return false; return true; }
static int crossings (const std::vector<float>& a) { int c = 0; for (size_t i = 1; i < a.size(); ++i) if ((a[i - 1] < 0) != (a[i] < 0)) ++c; return c; }

// single-voice evaluation helper
struct Eval
{
    ModContext mc; VoiceMod vm; double sr = 48000.0; int note = 60; float vel = 1.f; bool gate = true;
    void on() { vm.noteOn (12345u); }
    ModOut step (int chunk = kChunk, int offset = 0) { ModOut o; vm.update (mc, chunk, offset, sr, note, vel, gate, o); return o; }
    float run (int dst, float seconds) { const int n = (int) (seconds * sr / kChunk); ModOut o; for (int i = 0; i < n; ++i) { vm.update (mc, kChunk, i * kChunk, sr, note, vel, gate, o); mc.advance (kChunk); } return o.d[dst]; }
};

struct Rig
{
    VoiceManager vm; SynthParams sp; SampleData smp; GrainViz viz; std::array<float, grainwin::kTable + 1> win {}; ModContext mc;
    explicit Rig (int engine, bool useMod)
    {
        vm.prepare (48000.0); smp = SampleData::makeBuiltIn (48000.0); grainwin::buildTable (win.data(), 1, 0.9f, 0.5f);
        sp.engine = engine; sp.gran.sample = &smp; sp.gran.window = win.data(); sp.gran.viz = &viz;
        sp.a = 0.002f; sp.d = 0.2f; sp.s = 1.f; sp.r = 0.05f; sp.cutoff = 20000.f; sp.res = 0.f; sp.wave = 1; sp.mix[0] = 1.f; sp.drift = 0.f;
        additive::shape (sp.add); sp.mod = useMod ? &mc : nullptr;
    }
    std::vector<float> run (int blocks, int n = 128, std::vector<float>* R = nullptr)
    {
        std::vector<float> out, L ((size_t) n), Rr ((size_t) n);
        for (int b = 0; b < blocks; ++b)
        { std::fill (L.begin(), L.end(), 0.f); std::fill (Rr.begin(), Rr.end(), 0.f); vm.render (L.data(), Rr.data(), n, sp); mc.advance (n); out.insert (out.end(), L.begin(), L.end()); if (R) R->insert (R->end(), Rr.begin(), Rr.end()); }
        return out;
    }
};

int main()
{
    std::printf ("--- LFO waveforms\n");
    {
        CHECK (near (lfoShape (0, 0.0, 1), 0.f) && near (lfoShape (0, 0.25, 1), 1.f) && near (lfoShape (0, 0.75, 1), -1.f), "sine: 0 / +1 / -1 at 0, 1/4, 3/4");
        CHECK (near (lfoShape (0, 0.25 + 3.0, 1), 1.f), "sine is periodic across cycles");
        CHECK (near (lfoShape (1, 0.25, 1), 1.f) && near (lfoShape (1, 0.75, 1), -1.f) && near (lfoShape (1, 0.5, 1), 0.f), "triangle: peaks and zero crossings");
        CHECK (lfoShape (2, 0.1, 1) < lfoShape (2, 0.9, 1) && near (lfoShape (2, 0.0, 1), -1.f), "saw up rises -1 -> +1");
        CHECK (lfoShape (3, 0.1, 1) > lfoShape (3, 0.9, 1) && near (lfoShape (3, 0.0, 1), 1.f), "saw down falls +1 -> -1");
        CHECK (lfoShape (4, 0.2, 1) == 1.f && lfoShape (4, 0.7, 1) == -1.f, "square: +1 first half, -1 second half");
        bool constant = true; for (int i = 1; i < 50; ++i) constant &= lfoShape (5, 3.0 + i * 0.0199, 9) == lfoShape (5, 3.0, 9);
        CHECK (constant && lfoShape (5, 3.5, 9) != lfoShape (5, 4.5, 9), "sample&hold: constant inside a cycle, new value next cycle");
        float maxJump = 0.f; for (int i = 1; i < 4000; ++i) maxJump = std::max (maxJump, std::fabs (lfoShape (6, i * 0.001, 9) - lfoShape (6, (i - 1) * 0.001, 9)));
        CHECK (maxJump < 0.05f, "random (smooth) is continuous (max step %.4f)", maxJump);
        bool inRange = true; for (int w = 0; w < 7; ++w) for (int i = 0; i < 2000; ++i) { float v = lfoShape (w, i * 0.0137 - 5.0, 77); inRange &= v >= -1.0001f && v <= 1.0001f && std::isfinite (v); }
        CHECK (inRange, "all 7 waveforms stay within [-1,1], also for negative phase");
    }

    std::printf ("\n--- LFO rate, sync, phase, retrigger, fade, polarity\n");
    {
        ModContext mc; mc.lfo[0].rateHz = 3.f; mc.lfo[1].sync = true; mc.lfo[1].beats = kDivBeats[8]; mc.lfo[2].sync = true; mc.lfo[2].beats = kDivBeats[5];
        mc.lfo[3].sync = true; mc.lfo[3].beats = kDivBeats[13]; mc.lfo[4].sync = true; mc.lfo[4].beats = kDivBeats[7]; mc.setRates (48000.0, 120.0);
        CHECK (near ((float) (mc.lfoInc[0] * 48000.0), 3.f, 1e-3f), "free rate 3 Hz");
        CHECK (near ((float) (mc.lfoInc[1] * 48000.0), 2.f, 1e-3f), "sync 1/4 @120 BPM = 2 Hz");
        CHECK (near ((float) (mc.lfoInc[2] * 48000.0), 4.f, 1e-3f), "sync 1/8 @120 BPM = 4 Hz");
        CHECK (near ((float) (mc.lfoInc[3] * 48000.0), 0.5f, 1e-3f), "sync 1 bar @120 BPM = 0.5 Hz");
        CHECK (near ((float) (mc.lfoInc[4] * 48000.0), 3.f, 1e-3f), "sync 1/4T @120 BPM = 3 Hz");
        mc.setRates (48000.0, 60.0); CHECK (near ((float) (mc.lfoInc[1] * 48000.0), 1.f, 1e-3f), "tempo change re-scales sync rate");
        CHECK (kNumDivs == 16 && near (kDivBeats[0], 0.125f), "16 musical divisions available (1/32 .. 4 bars)");
    }
    {   // phase offset, depth, unipolar
        Eval e; e.mc.lfo[0].wave = 0; e.mc.lfo[0].rateHz = 0.f; e.mc.lfo[0].phase = 0.25f; e.mc.lfo[0].depth = 0.5f; e.mc.setRates (48000.0, 120.0);
        e.mc.addSlot (SrcLfo1, DstCutoff, 1.f); e.on(); auto o = e.step();
        CHECK (near (o.d[DstCutoff], 0.5f, 0.01f), "phase 0.25 sine at depth 0.5 -> 0.5 (got %.3f)", o.d[DstCutoff]);
        Eval u; u.mc.lfo[0].wave = 0; u.mc.lfo[0].rateHz = 0.f; u.mc.lfo[0].phase = 0.75f; u.mc.lfo[0].bipolar = false; u.mc.setRates (48000.0, 120.0);
        u.mc.addSlot (SrcLfo1, DstCutoff, 1.f); u.on(); auto o2 = u.step();
        CHECK (o2.d[DstCutoff] >= -0.001f && o2.d[DstCutoff] < 0.01f, "unipolar mode maps -1 to 0 (got %.3f)", o2.d[DstCutoff]);
    }
    {   // free-running vs note retrigger
        Eval a, b;
        for (Eval* e : { &a, &b }) { e->mc.lfo[0].rateHz = 1.f; e->mc.lfo[0].wave = 2; e->mc.setRates (48000.0, 120.0); e->mc.addSlot (SrcLfo1, DstCutoff, 1.f); }
        a.mc.lfo[0].note = false; b.mc.lfo[0].note = true;
        a.mc.lfoPhase[0] = b.mc.lfoPhase[0] = 0.6;                       // clock is at 0.6 cycles when the note starts
        a.on(); b.on(); auto fa = a.step(), fb = b.step();
        CHECK (near (fa.d[DstCutoff], lfoShape (2, 0.6, 0), 0.02f), "free-running LFO follows the global clock (%.3f)", fa.d[DstCutoff]);
        CHECK (near (fb.d[DstCutoff], -1.f, 0.02f), "note-retrigger LFO restarts at its phase offset (%.3f)", fb.d[DstCutoff]);
    }
    {   // fade-in
        Eval e; e.mc.lfo[0].wave = 4; e.mc.lfo[0].rateHz = 0.f; e.mc.lfo[0].fade = 1.f; e.mc.setRates (48000.0, 120.0); e.mc.addSlot (SrcLfo1, DstCutoff, 1.f); e.on();
        const float early = e.run (DstCutoff, 0.1f), late = e.run (DstCutoff, 1.5f);
        CHECK (early > 0.05f && early < 0.25f && near (late, 1.f, 0.02f), "fade-in ramps the LFO depth from 0 to 1 (%.2f -> %.2f)", early, late);
    }

    std::printf ("\n--- envelopes\n");
    {
        auto mk = [] { auto e = std::make_unique<Eval>(); e->mc.env[0].a = 0.1f; e->mc.env[0].h = 0.1f; e->mc.env[0].d = 0.2f; e->mc.env[0].s = 0.5f; e->mc.env[0].r = 0.2f; e->mc.addSlot (SrcEnv1, DstCutoff, 1.f); e->mc.setRates (48000.0, 120.0); e->on(); return e; };
        auto e = mk(); const float atMid = e->run (DstCutoff, 0.05f), atPeak = e->run (DstCutoff, 0.06f), inHold = e->run (DstCutoff, 0.08f);
        CHECK (near (atMid, 0.5f, 0.06f) && near (atPeak, 1.f, 0.05f) && near (inHold, 1.f, 0.02f), "attack ramps up, hold stays at 1 (%.2f %.2f %.2f)", atMid, atPeak, inHold);
        const float dec = e->run (DstCutoff, 0.5f); CHECK (near (dec, 0.5f, 0.02f), "decay settles on sustain 0.5 (%.3f)", dec);
        e->gate = false; const float rel = e->run (DstCutoff, 0.1f), end = e->run (DstCutoff, 0.4f);
        CHECK (rel > 0.05f && rel < 0.45f && near (end, 0.f, 0.01f), "release falls to 0 (%.2f -> %.3f)", rel, end);
        auto c1 = mk(), c2 = mk(); c1->mc.env[0].curve = 0.9f; c2->mc.env[0].curve = -0.9f;
        const float v1 = c1->run (DstCutoff, 0.05f), v2 = c2->run (DstCutoff, 0.05f);
        CHECK (v1 < 0.35f && v2 > 0.65f, "curve control bends the attack (+: slow start %.2f, -: fast start %.2f)", v1, v2);
        auto vs = mk(); vs->mc.env[0].velSens = 1.f; vs->vel = 0.5f; vs->run (DstCutoff, 0.1f); const float vp = vs->run (DstCutoff, 0.1f);
        CHECK (near (vp, 0.5f, 0.03f), "velocity sensitivity scales the output (%.2f at vel 0.5)", vp);
        auto bp = mk(); bp->mc.env[0].bipolar = true; const float b0 = bp->run (DstCutoff, 0.001f); bp->run (DstCutoff, 0.14f);
        CHECK (b0 < -0.7f && near (bp->run (DstCutoff, 0.01f), 1.f, 0.05f), "bipolar mode maps 0..1 to -1..+1 (start %.2f)", b0);
        auto lp = mk(); lp->mc.env[0].loop = true; lp->mc.env[0].a = 0.02f; lp->mc.env[0].h = 0.f; lp->mc.env[0].d = 0.02f; lp->mc.env[0].s = 0.f;
        float mn = 1.f, mx = 0.f; for (int i = 0; i < 600; ++i) { ModOut o; lp->vm.update (lp->mc, kChunk, 0, 48000.0, 60, 1.f, true, o); mn = std::min (mn, o.d[DstCutoff]); mx = std::max (mx, o.d[DstCutoff]); if (i > 100) { } }
        CHECK (mx > 0.9f && mn < 0.1f, "loop mode cycles A->D repeatedly while the key is held (%.2f..%.2f)", mn, mx);
        auto rs = mk(), fc = mk(); rs->run (DstCutoff, 0.5f); fc->run (DstCutoff, 0.5f); rs->gate = fc->gate = false; rs->run (DstCutoff, 0.1f); fc->run (DstCutoff, 0.1f);
        const float lvlBefore = rs->vm.e[0].level; fc->mc.env[0].fromCurrent = true; rs->vm.noteOn (5); fc->vm.noteOn (5); rs->gate = fc->gate = true;
        rs->step(); fc->step();
        CHECK (fc->vm.e[0].level > rs->vm.e[0].level + 0.01f && lvlBefore >= 0.f, "'From Current' retrigger continues from the present level, 'Restart' from 0");
    }

    std::printf ("\n--- step modulator\n");
    {
        StepCfg c; for (int i = 0; i < kSteps; ++i) c.v[i] = (float) i / 15.f * 2.f - 1.f; c.count = 4;
        auto seq = [&] (int dir) { std::vector<int> v; for (int n = 0; n < 8; ++n) v.push_back (stepIndex (dir, c.count, n, 5)); return v; };
        CHECK (seq (0) == std::vector<int> ({ 0, 1, 2, 3, 0, 1, 2, 3 }), "forward");
        CHECK (seq (1) == std::vector<int> ({ 3, 2, 1, 0, 3, 2, 1, 0 }), "backward");
        CHECK (seq (2) == std::vector<int> ({ 0, 1, 2, 3, 2, 1, 0, 1 }), "ping-pong");
        bool rr = true, varied = false; auto r = seq (3); for (int i : r) { rr &= i >= 0 && i < 4; varied |= i != r[0]; } CHECK (rr && varied && seq (3) == r, "random: in range, varied, repeatable");
        c.count = 16; c.dir = 0; c.smooth = 0.f; c.bipolar = true;
        CHECK (near (stepValue (c, 5.5, 1), c.v[5]) && near (stepValue (c, 15.9, 1), c.v[15]) && near (stepValue (c, 16.1, 1), c.v[0]), "16 steps, stepped output, wraps");
        c.smooth = 1.f; CHECK (stepValue (c, 6.5, 1) > c.v[5] && stepValue (c, 6.5, 1) < c.v[6], "smooth mode glides between steps");
        c.smooth = 0.f; c.bipolar = false; CHECK (near (stepValue (c, 0.5, 1), 0.f) && near (stepValue (c, 15.5, 1), 1.f), "unipolar output 0..1");
        Eval e; e.mc.step.sync = false; e.mc.step.rateHz = 10.f; e.mc.step.count = 4; for (int i = 0; i < 4; ++i) e.mc.step.v[i] = i * 0.25f; e.mc.step.note = true; e.mc.setRates (48000.0, 120.0);
        e.mc.addSlot (SrcStep, DstCutoff, 1.f); e.on(); const float s0 = e.step().d[DstCutoff]; const float s2 = e.run (DstCutoff, 0.25f);
        CHECK (near (s0, 0.f, 0.02f) && near (s2, 0.5f, 0.03f), "step modulator drives the matrix and restarts on note (%.2f, %.2f)", s0, s2);
    }

    std::printf ("\n--- matrix routing, macros\n");
    {
        Eval e; e.mc.modWheel = 0.5f; e.mc.pitchBend = -1.f; e.mc.expression = 1.f; e.mc.aftertouch = 0.25f; e.mc.polyAT[60] = 0.8f; e.vel = 0.5f; e.note = 72;
        e.mc.addSlot (SrcModWheel, DstCutoff, 1.f); e.mc.addSlot (SrcPitchBend, DstRes, 0.5f); e.mc.addSlot (SrcVelocity, DstDrive, -1.f);
        e.mc.addSlot (SrcKeyTrack, DstPW, 1.f); e.mc.addSlot (SrcExpression, DstPan, 0.3f); e.mc.addSlot (SrcAftertouch, DstWidth, 1.f); e.on(); auto o = e.step();
        CHECK (near (o.d[DstCutoff], 0.5f) && near (o.d[DstRes], -0.5f) && near (o.d[DstDrive], -0.5f), "positive and negative amounts (%.2f %.2f %.2f)", o.d[DstCutoff], o.d[DstRes], o.d[DstDrive]);
        CHECK (near (o.d[DstPW], 0.25f) && near (o.d[DstPan], 0.3f) && near (o.d[DstWidth], 0.25f), "key track (+1 octave = 0.25), expression, channel aftertouch");
        e.note = 60; const float pat = e.run (DstWidth, 0.05f); CHECK (near (pat, 0.8f, 0.02f), "polyphonic aftertouch is per note (%.2f)", pat);
        Eval s; s.mc.modWheel = 1.f; s.mc.addSlot (SrcModWheel, DstCutoff, 0.5f); s.mc.addSlot (SrcModWheel, DstCutoff, 0.25f); s.mc.addSlot (SrcMacro1, DstCutoff, -1.f); s.mc.macro[0] = 0.5f; s.on();
        CHECK (near (s.step().d[DstCutoff], 0.25f), "several connections to one destination add up (0.5 + 0.25 - 0.5)");
        Eval z; z.mc.modWheel = 1.f; z.mc.addSlot (SrcModWheel, DstCutoff, 0.f); z.mc.addSlot (SrcNone, DstCutoff, 1.f); z.mc.addSlot (SrcModWheel, DstNone, 1.f);
        CHECK (z.mc.numSlots == 0 && ! z.mc.active, "zero amount / no source / no destination slots are not routed");
        Eval m; m.mc.macro[0] = 1.f; m.mc.macro[1] = 0.5f; m.mc.addSlot (SrcMacro1, DstCutoff, 0.8f); m.mc.addSlot (SrcMacro1, DstRes, -0.4f); m.mc.addSlot (SrcMacro1, DstGrPos, 1.f); m.mc.addSlot (SrcMacro1 + 1, DstDrive, 1.f); m.on(); auto mo = m.step();
        CHECK (near (mo.d[DstCutoff], 0.8f) && near (mo.d[DstRes], -0.4f) && near (mo.d[DstGrPos], 1.f) && near (mo.d[DstDrive], 0.5f), "a macro drives several destinations with + / - ranges");
        Eval q; q.mc.addSlot (SrcLfo1, DstCutoff, 3.f); CHECK (q.mc.slots[0].amt == 1.f, "amount is clamped to +-1");
    }

    std::printf ("\n--- modulation applied to the real engines\n");
    {
        // 1. mod context without any routing == legacy path
        for (int eng : { (int) EngVirtualAnalog, (int) EngGranular, (int) EngAdditive })
        {
            Rig a (eng, false); a.vm.noteOn (57, 1.f, a.sp); auto x = a.run (30);
            Rig b (eng, true);  b.mc.addSlot (SrcModWheel, DstCutoff, 1.f);   // active, but mod wheel = 0 -> all destinations 0
            b.vm.noteOn (57, 1.f, b.sp); auto y = b.run (30);
            float diff = 0.f; for (size_t i = 0; i < x.size(); ++i) diff = std::max (diff, std::fabs (x[i] - y[i]));
            CHECK (finite (y) && (eng == EngVirtualAnalog ? diff < 1e-5f : rmsOf (y) > 0.3f * rmsOf (x)), "engine %d: zero modulation keeps the sound (max diff %.2g)", eng, diff);
        }
        // 2. pitch (VA sine, +12 st by mod wheel)
        { Rig a (EngVirtualAnalog, true); a.vm.noteOn (57, 1.f, a.sp); a.run (4); auto base = a.run (375);
          Rig b (EngVirtualAnalog, true); b.mc.modWheel = 1.f; b.mc.addSlot (SrcModWheel, DstPitch, 0.5f); b.vm.noteOn (57, 1.f, b.sp); b.run (4); auto up = b.run (375);
          const float ratio = (float) crossings (up) / (float) crossings (base);
          CHECK (near (ratio, 2.f, 0.05f), "VA: mod wheel -> pitch +0.5 = one octave up (freq ratio %.3f)", ratio);
          Rig c (EngVirtualAnalog, true); c.mc.modWheel = 1.f; c.mc.addSlot (SrcModWheel, DstPitch, -0.5f); c.vm.noteOn (57, 1.f, c.sp); c.run (4); auto dn = c.run (375);
          CHECK (near ((float) crossings (dn) / (float) crossings (base), 0.5f, 0.03f), "VA: negative amount -> one octave down"); }
        // 3. amp, pan, width
        { Rig a (EngVirtualAnalog, true); a.vm.noteOn (57, 1.f, a.sp); a.run (4); const float r0 = rmsOf (a.run (50));
          Rig b (EngVirtualAnalog, true); b.mc.modWheel = 1.f; b.mc.addSlot (SrcModWheel, DstAmp, -0.5f); b.vm.noteOn (57, 1.f, b.sp); b.run (30); const float r1 = rmsOf (b.run (50));
          CHECK (near (r1 / r0, 0.5f, 0.03f), "amp level: -0.5 halves the output (ratio %.3f)", r1 / r0);
          Rig c (EngVirtualAnalog, true); c.mc.modWheel = 1.f; c.mc.addSlot (SrcModWheel, DstPan, 1.f); c.vm.noteOn (57, 1.f, c.sp); c.run (4);
          std::vector<float> R; auto L = c.run (50, 128, &R);
          CHECK (rmsOf (R) > 20.f * rmsOf (L), "pan +1 moves the voice to the right channel (L %.4f R %.4f)", rmsOf (L), rmsOf (R)); }
        // 4. filter cutoff: darker sound with negative amount (VA saw)
        { auto bright = [] (std::vector<float> v) { double s = 0; for (size_t i = 1; i < v.size(); ++i) { const double d = v[i] - v[i - 1]; s += d * d; } return (float) std::sqrt (s / v.size()); };
          Rig a (EngVirtualAnalog, true); a.sp.wave = 0; a.sp.cutoff = 8000.f; a.vm.noteOn (57, 1.f, a.sp); a.run (20); const float b0 = bright (a.run (50));
          Rig b (EngVirtualAnalog, true); b.sp.wave = 0; b.sp.cutoff = 8000.f; b.mc.macro[0] = 1.f; b.mc.addSlot (SrcMacro1, DstCutoff, -0.6f); b.vm.noteOn (57, 1.f, b.sp); b.run (40); const float b1 = bright (b.run (50));
          Rig c (EngVirtualAnalog, true); c.sp.wave = 0; c.sp.cutoff = 800.f; c.mc.macro[0] = 1.f; c.mc.addSlot (SrcMacro1, DstCutoff, 0.6f); c.vm.noteOn (57, 1.f, c.sp); c.run (40); const float b2 = bright (c.run (50));
          Rig d (EngVirtualAnalog, true); d.sp.wave = 0; d.sp.cutoff = 800.f; d.vm.noteOn (57, 1.f, d.sp); d.run (40); const float b3 = bright (d.run (50));
          CHECK (b1 < 0.5f * b0 && b2 > 1.5f * b3, "filter cutoff: -0.6 darkens (%.4f < %.4f), +0.6 brightens (%.4f > %.4f)", b1, b0, b2, b3); }
        // 5. LFO vibrato through the full path (free-running, tempo sync) stays stable and audible
        { Rig a (EngVirtualAnalog, true); a.mc.lfo[0].sync = true; a.mc.lfo[0].beats = kDivBeats[5]; a.mc.setRates (48000.0, 140.0); a.mc.addSlot (SrcLfo1, DstPitch, 0.1f);
          a.vm.noteOn (57, 1.f, a.sp); auto y = a.run (200); CHECK (finite (y) && rmsOf (y) > 0.1f, "tempo-synced LFO vibrato is finite and audible"); }
        // 6. granular + additive destinations change the sound
        { auto diffOf = [] (const std::vector<float>& a, const std::vector<float>& b) { double s = 0; for (size_t i = 0; i < a.size(); ++i) s += std::fabs (a[i] - b[i]); return (float) (s / a.size()); };
          for (int dst : { (int) DstGrPos, (int) DstGrSize, (int) DstGrDensity, (int) DstGrPitch, (int) DstGrSpray })
          {
              Rig a (EngGranular, true); a.sp.gran.sizeRand = 0.f; a.sp.gran.density = 40.f; a.vm.noteOn (60, 1.f, a.sp); a.run (6); auto x = a.run (60);
              Rig b (EngGranular, true); b.sp.gran.sizeRand = 0.f; b.sp.gran.density = 40.f; b.mc.macro[0] = 1.f; b.mc.addSlot (SrcMacro1, dst, dst == DstGrPos ? 0.5f : 0.6f); b.vm.noteOn (60, 1.f, b.sp); b.run (6); auto y = b.run (60);
              CHECK (finite (y) && diffOf (x, y) > 1e-3f, "granular destination '%s' changes the audio (%.4f)", kDstNames[dst], diffOf (x, y));
          }
          for (int dst : { (int) DstAddBalance, (int) DstAddMorph, (int) DstAddTone, (int) DstAddShift, (int) DstAddInharm, (int) DstAddRolloff })
          {
              Rig a (EngAdditive, true); a.sp.add.tone = 1.f; a.sp.add.target = 1; additive::shape (a.sp.add); a.vm.noteOn (60, 1.f, a.sp); a.run (6); auto x = a.run (40);
              Rig b (EngAdditive, true); b.sp.add.tone = 1.f; b.sp.add.target = 1; additive::shape (b.sp.add); b.mc.macro[0] = 1.f; b.mc.addSlot (SrcMacro1, dst, dst == DstAddTone ? -0.7f : 0.7f); b.vm.noteOn (60, 1.f, b.sp); b.run (6); auto y = b.run (40);
              CHECK (finite (y) && diffOf (x, y) > 1e-3f, "additive destination '%s' changes the audio (%.4f)", kDstNames[dst], diffOf (x, y));
          }
          for (int dst : { (int) DstPW, (int) DstOsc2, (int) DstUniDetune, (int) DstRes, (int) DstDrive })
          {
              Rig a (EngVirtualAnalog, true); a.sp.wave = 4; a.sp.uniVoices = 4; a.sp.uniDetune = 0.3f; a.sp.res = 0.3f; a.vm.noteOn (60, 1.f, a.sp); a.run (6); auto x = a.run (40);
              Rig b (EngVirtualAnalog, true); b.sp.wave = 4; b.sp.uniVoices = 4; b.sp.uniDetune = 0.3f; b.sp.res = 0.3f; b.mc.macro[0] = 1.f; b.mc.addSlot (SrcMacro1, dst, 0.5f); b.vm.noteOn (60, 1.f, b.sp); b.run (6); auto y = b.run (40);
              CHECK (finite (y) && diffOf (x, y) > 1e-3f, "VA destination '%s' changes the audio (%.4f)", kDstNames[dst], diffOf (x, y));
          }
        }
        // 7. robustness: every source into every destination, extreme amounts, voice stealing, all engines
        { for (int eng : { (int) EngVirtualAnalog, (int) EngGranular, (int) EngAdditive })
          { Rig r (eng, true); r.sp.polyLimit = 4; r.mc.modWheel = 1.f; r.mc.pitchBend = 1.f; r.mc.aftertouch = 1.f; r.mc.expression = 1.f; for (int k = 0; k < kMacros; ++k) r.mc.macro[k] = 1.f;
            for (int k = 0; k < kLfos; ++k) { r.mc.lfo[k].wave = k % 7; r.mc.lfo[k].rateHz = 30.f; }
            for (int i = 0; i < kSlots; ++i) r.mc.addSlot (1 + (i * 7) % (kNumSrc - 1), 1 + (i * 5) % (kNumDst - 1), (i & 1) ? 1.f : -1.f);
            r.mc.setRates (48000.0, 200.0);
            for (int i = 0; i < 12; ++i) { r.vm.noteOn (36 + i * 4, 1.f, r.sp); r.run (3, 100); }
            auto y = r.run (30, 77); r.vm.allNotesOff();
            CHECK (finiteLoose (y) && r.vm.activeCount() == 0, "engine %d: all sources x all destinations at full depth, 12 notes on 4 voices: stable and finite", eng); } }
        // 8. a voice that is stolen keeps modulating without clicks to NaN, note-off releases mod envelopes
        { Rig r (EngVirtualAnalog, true); r.mc.env[0].r = 0.05f; r.mc.addSlot (SrcEnv1, DstCutoff, 1.f); r.vm.noteOn (60, 1.f, r.sp); r.run (10); r.vm.noteOff (60, r.sp); r.run (200);
          CHECK (r.vm.activeCount() == 0, "voice frees itself after release with modulation active"); }
    }

    std::printf ("\n--- parameter IDs\n");
    {
        std::set<std::string> ids;
#define X(n) ids.insert (#n);
        VANTA_PARAM_LIST(X)
#undef X
        const size_t before = ids.size() + 64; size_t added = 0;
        for (int i = 0; i < kSlots + 8; ++i) {}
        for (int i = 0; i < kLfos; ++i) for (auto f : { "wave", "rate", "sync", "div", "phase", "retrig", "fade", "bipolar", "depth" }) { ids.insert (lfoId (i + 1, f)); ++added; }
        for (int i = 0; i < kEnvs; ++i) for (auto f : { "a", "h", "d", "s", "r", "curve", "vel", "loop", "bipolar", "retrig" }) { ids.insert (envId (i + 1, f)); ++added; }
        for (int i = 0; i < kSteps; ++i) { ids.insert (stepValId (i)); ++added; }
        for (auto f : { "count", "rate", "sync", "div", "dir", "smooth", "retrig", "bipolar" }) { ids.insert (stepId (f)); ++added; }
        for (int i = 0; i < kMacros; ++i) { ids.insert (macroId (i + 1)); ++added; }
        for (int i = 0; i < kSlots; ++i) for (auto f : { "src", "dst", "amt", "on" }) { ids.insert (slotId (i + 1, f)); ++added; }
        CHECK (ids.size() == before - 64 + added, "all %zu new modulation parameter IDs are unique and do not collide with existing IDs", added);
        CHECK (lfoId (3, "rate") == "lfo3_rate" && envId (8, "curve") == "env8_curve" && stepValId (0) == "stp_v01" && slotId (32, "amt") == "mm32_amt" && macroId (5) == "mac5", "ID format");
        CHECK (kLfos >= 8 && kEnvs >= 8 && kSteps >= 16 && kMacros >= 8, "required counts: >=8 LFOs, >=8 envelopes, >=16 steps, >=8 macros");
    }

    std::printf ("\n%d checks passed, %d failed\n", passed, failed);
    if (failed == 0) std::printf ("STAGE4 ALL PASSED\n");
    return failed == 0 ? 0 : 1;
}
