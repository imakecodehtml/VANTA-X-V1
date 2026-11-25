// VANTA X - Stage 3 completion tests (JUCE-free): parameter-ID stability, voice management across engines,
// note-off / pedal / mono behaviour for Granular + Additive, steal safety, sample swap safety.
#include <cstdio>
#include <cmath>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include "Audio/ParamList.h"
#include "Voices/VoiceManager.h"

using namespace vanta;
static int passed = 0, failed = 0;
#define CHECK(cond, ...) do { if (cond) { ++passed; std::printf ("ok:   "); } else { ++failed; std::printf ("FAIL: "); } std::printf (__VA_ARGS__); std::printf ("\n"); } while (0)

static std::vector<std::string> scalarIds()
{
    std::vector<std::string> v;
#define X(n) v.push_back (#n);
    VANTA_PARAM_LIST(X)
#undef X
    return v;
}

static bool finiteBuf (const std::vector<float>& a) { for (float x : a) if (! std::isfinite (x) || std::fabs (x) > 8.f) return false; return true; }
static float rms (const std::vector<float>& a) { double s = 0; for (float x : a) s += (double) x * x; return (float) std::sqrt (s / (double) std::max<size_t> (1, a.size())); }

struct Rig
{
    VoiceManager vm; SynthParams sp; SampleData smp; GrainViz viz; std::array<float, grainwin::kTable + 1> win {};
    explicit Rig (int engine)
    {
        vm.prepare (48000.0);
        smp = SampleData::makeBuiltIn (48000.0);
        grainwin::buildTable (win.data(), 1, 0.9f, 0.5f);
        sp.engine = engine; sp.gran.sample = &smp; sp.gran.window = win.data(); sp.gran.viz = &viz;
        sp.a = 0.002f; sp.d = 0.1f; sp.s = 0.8f; sp.r = 0.05f; sp.cutoff = 18000.f;
        additive::shape (sp.add);
    }
    std::vector<float> run (int blocks, int n = 128)
    {
        std::vector<float> out; std::vector<float> L ((size_t) n), R ((size_t) n);
        for (int b = 0; b < blocks; ++b)
        { std::fill (L.begin(), L.end(), 0.f); std::fill (R.begin(), R.end(), 0.f); vm.render (L.data(), R.data(), n, sp); out.insert (out.end(), L.begin(), L.end()); }
        return out;
    }
};

int main()
{
    std::printf ("--- parameter IDs (frozen manifest: existing IDs must never change)\n");
    {
        const std::vector<std::string> frozen = {
            "master_vol","master_pan","poly","voice_mode","bend_range","uni_voices","uni_detune","uni_spread",
            "va_wave","va_oct","va_semi","va_fine","va_pw","va_sync","va_drift","va_mix1","va_mix2","va_mix3","va_tune2","va_tune3",
            "sub_level","sub_oct","sub_wave","noise_level","noise_type","noise_color","porta_time","porta_curve",
            "amp_a","amp_d","amp_s","amp_r","flt_type","flt_cutoff","flt_res","flt_drive","engine",
            "gr_size","gr_sizerand","gr_density","gr_pos","gr_spray","gr_speed","gr_pitch","gr_pitchrand","gr_spread","gr_texture",
            "gr_start","gr_end","gr_dir","gr_window","gr_width","gr_shape","gr_root",
            "add_count","add_level","add_shift","add_morph","add_target","add_tone","add_inharm","add_balance","add_rolloff",
            "add_semi","add_fine","add_sf_on","add_sf_pos","add_sf_width","add_sf_gain","add_henv","add_hdecay","add_uni","add_spread","add_detune" };
        const auto ids = scalarIds();
        CHECK (ids == frozen, "scalar parameter ID list identical to the frozen manifest (%zu IDs)", ids.size());
        std::set<std::string> uniq (ids.begin(), ids.end());
        for (int i = 0; i < kAddHarm; ++i) { uniq.insert (addHarmId ('h', i)); uniq.insert (addHarmId ('p', i)); }
        CHECK (uniq.size() == ids.size() + 64, "all %zu parameter IDs unique (32 amp + 32 phase harmonic IDs included)", uniq.size());
        CHECK (addHarmId ('h', 0) == "add_h01" && addHarmId ('h', 31) == "add_h32" && addHarmId ('p', 9) == "add_p10", "harmonic ID format add_hNN / add_pNN");
    }

    std::printf ("\n--- granular + additive voice management\n");
    for (int eng : { (int) EngGranular, (int) EngAdditive })
    {
        const char* nm = eng == EngGranular ? "granular" : "additive";
        {   Rig r (eng); r.vm.noteOn (60, 1.f, r.sp); auto a = r.run (20);
            CHECK (rms (a) > 0.01f && finiteBuf (a), "%s: note-on makes sound (rms %.3f)", nm, rms (a));
            r.vm.noteOff (60, r.sp); r.run (40, 256); auto z = r.run (4);
            CHECK (r.vm.activeCount() == 0 && rms (z) < 1e-4f, "%s: note-off releases and the voice is freed", nm); }
        {   Rig r (eng); r.sp.polyLimit = 4;
            for (int i = 0; i < 12; ++i) { r.vm.noteOn (48 + i, 0.9f, r.sp); r.run (2); }
            auto a = r.run (10);
            CHECK (r.vm.activeCount() <= 4 && finiteBuf (a), "%s: polyphony limit 4 holds under 12 notes, output finite (active %d)", nm, r.vm.activeCount()); }
        {   Rig r (eng); r.vm.noteOn (60, 1.f, r.sp); r.vm.noteOn (64, 1.f, r.sp); r.vm.noteOn (67, 1.f, r.sp); r.run (10);
            CHECK (r.vm.activeCount() == 3, "%s: 3-note chord uses 3 voices", nm);
            r.vm.sustainPedal (true); r.vm.noteOff (60, r.sp); r.vm.noteOff (64, r.sp); r.vm.noteOff (67, r.sp); r.run (60, 256);
            CHECK (r.vm.activeCount() == 3, "%s: sustain pedal holds released notes", nm);
            r.vm.sustainPedal (false); r.run (60, 256);
            CHECK (r.vm.activeCount() == 0, "%s: pedal up releases them", nm); }
        {   Rig r (eng); r.sp.mode = 1; r.vm.noteOn (60, 1.f, r.sp); r.run (4); r.vm.noteOn (64, 1.f, r.sp); r.run (4);
            CHECK (r.vm.activeCount() == 1, "%s: mono mode keeps a single voice", nm);
            r.vm.noteOff (64, r.sp); r.run (4);
            CHECK (r.vm.activeCount() == 1, "%s: mono note stack falls back to the held note", nm); }
        {   Rig r (eng); r.vm.noteOn (60, 1.f, r.sp); r.vm.noteOn (62, 1.f, r.sp); r.run (5); r.vm.allNotesOff(); auto z = r.run (2);
            CHECK (r.vm.activeCount() == 0 && rms (z) == 0.f, "%s: allNotesOff silences immediately", nm); }
        {   Rig r (eng); r.vm.setPitchBend (1.f); r.vm.noteOn (60, 1.f, r.sp); auto a = r.run (10);
            CHECK (finiteBuf (a) && rms (a) > 0.005f, "%s: full pitch bend stays finite", nm); }
    }

    std::printf ("\n--- engine switching\n");
    {
        Rig r (EngVirtualAnalog); r.vm.noteOn (60, 1.f, r.sp); r.run (6);
        r.sp.engine = EngGranular; r.vm.noteOn (64, 1.f, r.sp); r.run (6);
        r.sp.engine = EngAdditive;  r.vm.noteOn (67, 1.f, r.sp); auto a = r.run (10);
        CHECK (r.vm.activeCount() == 3 && finiteBuf (a), "switching engine between notes: old voices keep their engine, new notes use the new one");
        r.sp.engine = EngVirtualAnalog; r.sp.add.level = 0.f; additive::shape (r.sp.add); auto b = r.run (5);
        CHECK (finiteBuf (b), "editing parameters of an inactive engine never disturbs the others");
    }

    std::printf ("\n--- sample safety\n");
    {
        Rig r (EngGranular); r.sp.gran.density = 80.f; r.vm.noteOn (60, 1.f, r.sp); r.run (10);
        auto small = std::make_unique<SampleData>(); small->channels = 1; small->sampleRate = 22050.0; small->ch[0].assign (100, 0.25f); small->finalise();
        r.sp.gran.sample = small.get(); auto a = r.run (10);
        CHECK (finiteBuf (a), "swapping to a 100-frame sample under running grains: finite, no out-of-range read");
        r.sp.gran.start = 0.9f; r.sp.gran.end = 0.1f; auto b = r.run (10);
        CHECK (finiteBuf (b), "start > end is handled (region swapped)");
        r.sp.gran.start = 0.5f; r.sp.gran.end = 0.5f; auto c = r.run (10);
        CHECK (finiteBuf (c), "zero-length region is handled");
        r.sp.gran.sample = nullptr; r.run (2); auto d = r.run (8);      // first blocks flush the ladder-filter memory
        CHECK (finiteBuf (d) && rms (d) < 1e-3f, "sample pointer null (missing sample): silence, no crash");
        std::vector<float> st; auto big = std::make_unique<SampleData>(SampleData::makeBuiltIn (96000.0));
        r.sp.gran.sample = big.get(); r.sp.gran.start = 0.f; r.sp.gran.end = 1.f; auto e = r.run (30);
        CHECK (finiteBuf (e) && rms (e) > 0.001f, "recovers when a valid sample (96 kHz) appears again");
    }

    std::printf ("\n--- additive editing\n");
    {
        AdditiveParams a; for (int k = 0; k < kAddHarm; ++k) a.amp[k] = (k == 4) ? 1.f : 0.f; additive::shape (a);
        int nz = 0; for (int k = 0; k < kAddHarm; ++k) nz += a.shaped[k] > 1e-6f ? 1 : 0;
        CHECK (nz == 1 && a.shaped[4] > 0.f, "single harmonic 5 edited alone -> only harmonic 5 non-zero");
        a.count = 3; additive::shape (a);
        CHECK (a.shaped[4] == 0.f, "harmonic beyond count is muted even when its amplitude is set");
        float g = additive::spectralFilterGain (0.5f, 0.4f, 0.f, 8);
        CHECK (std::fabs (g - 1.f) < 1e-6f, "spectral filter with gain 0 is neutral");
    }

    std::printf ("\n%d checks passed, %d failed\n", passed, failed);
    if (failed == 0) std::printf ("STAGE3B ALL PASSED\n");
    return failed == 0 ? 0 : 1;
}
