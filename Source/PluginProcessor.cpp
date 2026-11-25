#include "PluginProcessor.h"
#include "UI/PluginEditor.h"

using namespace juce;

VantaProcessor::VantaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "VANTA_X", createLayout()),
      presets (apvts)
{
#define X(n) P.n = apvts.getRawParameterValue (#n);
    VANTA_PARAM_LIST(X)
#undef X
    for (int i = 0; i < vanta::kAddHarm; ++i)
    {
        P.addAmp[i]   = apvts.getRawParameterValue (vanta::addHarmId ('h', i));
        P.addPhase[i] = apvts.getRawParameterValue (vanta::addHarmId ('p', i));
    }
    {
        using namespace vanta::mod;
        auto get = [this] (const std::string& id) { return apvts.getRawParameterValue (id); };
        for (int k = 0; k < kLfos; ++k)  for (int f = 0; f < 9; ++f)  M.lfo[k][f] = get (lfoId (k + 1, kLfoFields[f]));
        for (int k = 0; k < kEnvs; ++k)  for (int f = 0; f < 10; ++f) M.env[k][f] = get (envId (k + 1, kEnvFields[f]));
        for (int i = 0; i < kSteps; ++i) M.stepV[i] = get (stepValId (i));
        for (int f = 0; f < 8; ++f)      M.step[f] = get (stepId (kStepFields[f]));
        for (int k = 0; k < kMacros; ++k) M.macro[k] = get (macroId (k + 1));
        for (int i = 0; i < kSlots; ++i) for (int f = 0; f < 4; ++f) M.slot[i][f] = get (slotId (i + 1, kSlotFields[f]));
    }
    for (auto* prm : getParameters()) if (auto* rp = dynamic_cast<RangedAudioParameter*> (prm)) allParams.push_back (rp);
    for (auto& a : ccMap) a.store (-1);
    sp.mod = &modctx;
    apvts.state.addListener (this);
    syncSampleFromState();
    syncMidiMapFromState();
}

VantaProcessor::~VantaProcessor() { cancelPendingUpdate(); apvts.state.removeListener (this); }

AudioProcessorValueTreeState::ParameterLayout VantaProcessor::createLayout()
{
    AudioProcessorValueTreeState::ParameterLayout L;
    auto F = [&] (const char* id, const char* name, float lo, float hi, float def, float centre = 0.f)
    {
        NormalisableRange<float> r (lo, hi);
        if (centre > 0.f) r.setSkewForCentre (centre);
        L.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, r, def));
    };
    auto I = [&] (const char* id, const char* name, int lo, int hi, int def)
    { L.add (std::make_unique<AudioParameterInt> (ParameterID { id, 1 }, name, lo, hi, def)); };
    auto C = [&] (const char* id, const char* name, StringArray ch, int def = 0)
    { L.add (std::make_unique<AudioParameterChoice> (ParameterID { id, 1 }, name, ch, def)); };

    F ("master_vol", "Master Volume", -60.f, 6.f, -0.8f);
    F ("master_pan", "Master Pan", -1.f, 1.f, 0.f);
    I ("poly", "Polyphony", 1, 32, 16);
    C ("voice_mode", "Voice Mode", { "Poly", "Mono", "Legato" });
    I ("bend_range", "Bend Range", 0, 24, 2);
    I ("uni_voices", "Unison Voices", 1, 8, 1);
    F ("uni_detune", "Unison Detune", 0.f, 1.f, 0.25f);
    F ("uni_spread", "Unison Spread", 0.f, 1.f, 0.72f);

    C ("va_wave", "VA Waveform", { "Saw", "Sine", "Square", "Triangle", "Pulse" });
    I ("va_oct", "Octave", -3, 3, 0);
    I ("va_semi", "Semitone", -12, 12, 0);
    F ("va_fine", "Fine", -1.f, 1.f, 0.f);
    F ("va_pw", "Pulse Width", 0.05f, 0.95f, 0.5f);
    C ("va_sync", "Hard Sync", { "Off", "On" });
    F ("va_drift", "Drift", 0.f, 1.f, 0.1f);
    F ("va_mix1", "Osc 1", 0.f, 1.f, 0.8f);
    F ("va_mix2", "Osc 2", 0.f, 1.f, 0.f);
    F ("va_mix3", "Osc 3", 0.f, 1.f, 0.f);
    F ("va_tune2", "Osc 2 Tune (ct)", -100.f, 100.f, 7.f);
    F ("va_tune3", "Osc 3 Tune (ct)", -100.f, 100.f, -7.f);
    F ("sub_level", "Sub Level", 0.f, 1.f, 0.f);
    I ("sub_oct", "Sub Octave", -2, -1, -1);
    C ("sub_wave", "Sub Wave", { "Sine", "Square", "Triangle" });
    F ("noise_level", "Noise Level", 0.f, 1.f, 0.f);
    C ("noise_type", "Noise Type", { "White", "Pink" });
    F ("noise_color", "Noise Color", 0.f, 1.f, 0.5f);
    F ("porta_time", "Portamento Time", 0.f, 5.f, 0.f, 0.5f);
    F ("porta_curve", "Portamento Curve", 0.f, 1.f, 0.5f);
    F ("amp_a", "Amp Attack", 0.0005f, 10.f, 0.005f, 0.2f);
    F ("amp_d", "Amp Decay", 0.001f, 10.f, 0.3f, 0.5f);
    F ("amp_s", "Amp Sustain", 0.f, 1.f, 0.8f);
    F ("amp_r", "Amp Release", 0.001f, 10.f, 0.3f, 0.5f);
    C ("flt_type", "Filter Type", { "LP 24dB", "LP 12dB", "BP", "HP" });
    F ("flt_cutoff", "Cutoff", 20.f, 20000.f, 8000.f, 1000.f);
    F ("flt_res", "Resonance", 0.f, 1.f, 0.f);
    F ("flt_drive", "Drive", 0.f, 1.f, 0.f);

    // ---------------------------------------------------------------- Stage 3: engine select
    C ("engine", "Engine", { "Wavetable", "FM", "Virtual Analog", "Granular", "Additive" }, 2);

    // ---------------------------------------------------------------- Stage 3: granular
    F ("gr_size", "Grain Size (ms)", 1.f, 500.f, 120.f, 60.f);
    F ("gr_sizerand", "Grain Size Random", 0.f, 1.f, 0.1f);
    F ("gr_density", "Grain Density (Hz)", 0.5f, 200.f, 32.f, 20.f);
    F ("gr_pos", "Grain Position", 0.f, 1.f, 0.42f);
    F ("gr_spray", "Position Random", 0.f, 1.f, 0.15f);
    F ("gr_speed", "Playback Speed", -4.f, 4.f, 0.f);
    F ("gr_pitch", "Grain Pitch (st)", -24.f, 24.f, 0.f);
    F ("gr_pitchrand", "Pitch Random", 0.f, 1.f, 0.05f);
    F ("gr_spread", "Stereo Spread", 0.f, 1.f, 0.75f);
    F ("gr_texture", "Texture", 0.f, 1.f, 0.3f);
    F ("gr_start", "Sample Start", 0.f, 1.f, 0.f);
    F ("gr_end", "Sample End", 0.f, 1.f, 1.f);
    C ("gr_dir", "Grain Direction", { "Forward", "Reverse", "Alternate", "Random" });
    C ("gr_window", "Grain Window", { "Hann", "Gaussian", "Triangle", "Trapezoid", "Sine", "Blackman" }, 1);
    F ("gr_width", "Window Width", 0.05f, 1.f, 0.9f);
    F ("gr_shape", "Window Shape", 0.f, 1.f, 0.5f);
    I ("gr_root", "Sample Root Note", 12, 108, 60);

    // ---------------------------------------------------------------- Stage 3: additive
    for (int i = 0; i < vanta::kAddHarm; ++i)
    {
        const String n (i + 1);
        F (vanta::addHarmId ('h', i).c_str(), ("Harmonic " + n + " Level").toRawUTF8(), 0.f, 1.f, 1.f / (float) (i + 1));
    }
    for (int i = 0; i < vanta::kAddHarm; ++i)
    {
        const String n (i + 1);
        F (vanta::addHarmId ('p', i).c_str(), ("Harmonic " + n + " Phase").toRawUTF8(), 0.f, 1.f, 0.f);
    }
    I ("add_count", "Harmonics", 1, 32, 32);
    F ("add_level", "Harmonic Level", 0.f, 1.f, 0.8f);
    F ("add_shift", "Harmonic Shift", -1.f, 1.f, 0.f);
    F ("add_morph", "Spectral Morph", 0.f, 1.f, 0.f);
    C ("add_target", "Morph Target", { "Saw", "Square", "Triangle", "Organ", "Formant" });
    F ("add_tone", "Tone", 0.f, 1.f, 1.f);
    F ("add_inharm", "Inharmonicity", 0.f, 1.f, 0.f);
    F ("add_balance", "Odd/Even Balance", -1.f, 1.f, 0.f);
    F ("add_rolloff", "Harmonic Rolloff", 0.f, 1.f, 0.f);
    I ("add_semi", "Fundamental (st)", -36, 36, 0);
    F ("add_fine", "Fundamental Fine (ct)", -100.f, 100.f, 0.f);
    C ("add_sf_on", "Spectral Filter", { "Off", "On" });
    F ("add_sf_pos", "Spectral Filter Position", 0.f, 1.f, 0.5f);
    F ("add_sf_width", "Spectral Filter Width", 0.f, 1.f, 0.4f);
    F ("add_sf_gain", "Spectral Filter Gain", -1.f, 1.f, 0.f);
    C ("add_henv", "Harmonic Envelopes", { "Off", "On" });
    F ("add_hdecay", "Harmonic Decay (s)", 0.05f, 8.f, 1.f, 0.8f);
    C ("add_uni", "Additive Unison", { "Off", "On" });
    F ("add_spread", "Additive Spread", 0.f, 1.f, 0.5f);
    F ("add_detune", "Additive Detune", 0.f, 1.f, 0.25f);

    // ---------------------------------------------------------------- Stage 4: modulation (all IDs are new; no existing ID is touched)
    {
        using namespace vanta::mod;
        StringArray waves, divs, dirs, srcs, dsts;
        for (auto* w : kLfoWaves) waves.add (w);
        for (auto* d : kDivNames) divs.add (d);
        for (auto* d : kStepDirs) dirs.add (d);
        for (auto* n : kSrcNames) srcs.add (n);
        for (auto* n : kDstNames) dsts.add (n);
        const StringArray offOn { "Off", "On" }, freeNote { "Free", "Note" }, polarity { "Unipolar", "Bipolar" };
        for (int k = 1; k <= kLfos; ++k)
        {
            const String n (k);
            C (lfoId (k, "wave").c_str(), ("LFO " + n + " Wave").toRawUTF8(), waves, 0);
            F (lfoId (k, "rate").c_str(), ("LFO " + n + " Rate (Hz)").toRawUTF8(), 0.01f, 50.f, 2.f, 2.f);
            C (lfoId (k, "sync").c_str(), ("LFO " + n + " Tempo Sync").toRawUTF8(), offOn, 0);
            C (lfoId (k, "div").c_str(), ("LFO " + n + " Division").toRawUTF8(), divs, 8);
            F (lfoId (k, "phase").c_str(), ("LFO " + n + " Phase").toRawUTF8(), 0.f, 1.f, 0.f);
            C (lfoId (k, "retrig").c_str(), ("LFO " + n + " Retrigger").toRawUTF8(), freeNote, 0);
            F (lfoId (k, "fade").c_str(), ("LFO " + n + " Fade-in (s)").toRawUTF8(), 0.f, 10.f, 0.f, 1.f);
            C (lfoId (k, "bipolar").c_str(), ("LFO " + n + " Polarity").toRawUTF8(), polarity, 1);
            F (lfoId (k, "depth").c_str(), ("LFO " + n + " Depth").toRawUTF8(), 0.f, 1.f, 1.f);
        }
        for (int k = 1; k <= kEnvs; ++k)
        {
            const String n (k);
            F (envId (k, "a").c_str(), ("Env " + n + " Attack (s)").toRawUTF8(), 0.0005f, 10.f, 0.01f, 0.3f);
            F (envId (k, "h").c_str(), ("Env " + n + " Hold (s)").toRawUTF8(), 0.f, 5.f, 0.f, 0.5f);
            F (envId (k, "d").c_str(), ("Env " + n + " Decay (s)").toRawUTF8(), 0.001f, 10.f, 0.3f, 0.5f);
            F (envId (k, "s").c_str(), ("Env " + n + " Sustain").toRawUTF8(), 0.f, 1.f, 0.7f);
            F (envId (k, "r").c_str(), ("Env " + n + " Release (s)").toRawUTF8(), 0.001f, 10.f, 0.3f, 0.5f);
            F (envId (k, "curve").c_str(), ("Env " + n + " Curve").toRawUTF8(), -1.f, 1.f, 0.f);
            F (envId (k, "vel").c_str(), ("Env " + n + " Velocity Sens").toRawUTF8(), 0.f, 1.f, 0.f);
            C (envId (k, "loop").c_str(), ("Env " + n + " Loop").toRawUTF8(), offOn, 0);
            C (envId (k, "bipolar").c_str(), ("Env " + n + " Polarity").toRawUTF8(), polarity, 0);
            C (envId (k, "retrig").c_str(), ("Env " + n + " Retrigger").toRawUTF8(), { "Restart", "From Current" }, 0);
        }
        I (stepId ("count").c_str(), "Step Count", 1, kSteps, 8);
        F (stepId ("rate").c_str(), "Step Rate (Hz)", 0.05f, 40.f, 4.f, 4.f);
        C (stepId ("sync").c_str(), "Step Tempo Sync", offOn, 1);
        C (stepId ("div").c_str(), "Step Division", divs, 5);
        C (stepId ("dir").c_str(), "Step Direction", dirs, 0);
        F (stepId ("smooth").c_str(), "Step Smooth", 0.f, 1.f, 0.f);
        C (stepId ("retrig").c_str(), "Step Retrigger", freeNote, 0);
        C (stepId ("bipolar").c_str(), "Step Polarity", polarity, 1);
        for (int i = 0; i < kSteps; ++i) F (stepValId (i).c_str(), ("Step " + String (i + 1) + " Value").toRawUTF8(), -1.f, 1.f, 0.f);
        for (int k = 1; k <= kMacros; ++k) F (macroId (k).c_str(), ("Macro " + String (k)).toRawUTF8(), 0.f, 1.f, 0.f);
        for (int i = 1; i <= kSlots; ++i)
        {
            const String n (i);
            C (slotId (i, "src").c_str(), ("Mod " + n + " Source").toRawUTF8(), srcs, 0);
            C (slotId (i, "dst").c_str(), ("Mod " + n + " Destination").toRawUTF8(), dsts, 0);
            F (slotId (i, "amt").c_str(), ("Mod " + n + " Amount").toRawUTF8(), -1.f, 1.f, 0.f);
            C (slotId (i, "on").c_str(), ("Mod " + n + " Enabled").toRawUTF8(), offOn, 1);
        }
    }
    return L;
}

bool VantaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{ return l.getMainOutputChannelSet() == AudioChannelSet::stereo(); }

void VantaProcessor::prepareToPlay (double sampleRate, int)
{
    sr = sampleRate;
    audioSample = nullptr;                      // re-acquire (and acknowledge) the sample on the first block
    voices.prepare (sr);
    gainSm.reset (sr, 0.02); panSm.reset (sr, 0.02);
    gainSm.setCurrentAndTargetValue (Decibels::decibelsToGain (P.master_vol->load()));
    panSm.setCurrentAndTargetValue (P.master_pan->load());
}

void VantaProcessor::readParams()
{
    sp.engine = jlimit (0, 4, (int) P.engine->load());
    sp.mode = (int) P.voice_mode->load(); sp.polyLimit = (int) P.poly->load();
    sp.bendRange = P.bend_range->load();
    sp.uniVoices = (int) P.uni_voices->load(); sp.uniDetune = P.uni_detune->load(); sp.uniSpread = P.uni_spread->load();
    sp.wave = (int) P.va_wave->load(); sp.oct = (int) P.va_oct->load(); sp.semi = (int) P.va_semi->load();
    sp.fine = P.va_fine->load(); sp.pw = P.va_pw->load(); sp.drift = P.va_drift->load();
    sp.sync = P.va_sync->load() > 0.5f;
    sp.mix[0] = P.va_mix1->load(); sp.mix[1] = P.va_mix2->load(); sp.mix[2] = P.va_mix3->load();
    sp.tune2 = P.va_tune2->load(); sp.tune3 = P.va_tune3->load();
    sp.subLevel = P.sub_level->load(); sp.subOct = (int) P.sub_oct->load(); sp.subWave = (int) P.sub_wave->load();
    sp.noiseLevel = P.noise_level->load(); sp.noiseType = (int) P.noise_type->load(); sp.noiseColor = P.noise_color->load();
    sp.glide = P.porta_time->load(); sp.glideCurve = P.porta_curve->load();
    sp.a = P.amp_a->load(); sp.d = P.amp_d->load(); sp.s = P.amp_s->load(); sp.r = P.amp_r->load();
    sp.fltType = (int) P.flt_type->load(); sp.cutoff = P.flt_cutoff->load();
    sp.res = P.flt_res->load(); sp.drive = P.flt_drive->load();

    // ---- granular
    {
        auto& g = sp.gran;
        const auto* s = samples.audioPointer();
        if (s != audioSample) { audioSample = s; samples.acknowledge (s); }
        g.sample = audioSample; g.viz = &grainViz;
        g.sizeMs = P.gr_size->load(); g.sizeRand = P.gr_sizerand->load(); g.density = P.gr_density->load();
        g.pos = P.gr_pos->load(); g.spray = P.gr_spray->load(); g.speed = P.gr_speed->load();
        g.pitch = P.gr_pitch->load(); g.pitchRand = P.gr_pitchrand->load(); g.spread = P.gr_spread->load(); g.texture = P.gr_texture->load();
        g.start = P.gr_start->load(); g.end = P.gr_end->load(); g.dir = (int) P.gr_dir->load(); g.root = (int) P.gr_root->load();
        const int wt = (int) P.gr_window->load(); const float ww = P.gr_width->load(), ws = P.gr_shape->load();
        if (wt != winType || ww != winWidth || ws != winShape)
        { vanta::grainwin::buildTable (grainTable.data(), wt, ww, ws); winType = wt; winWidth = ww; winShape = ws; }
        g.window = grainTable.data();
    }
    // ---- additive
    loadAdditiveParams (sp.add);
}

void VantaProcessor::loadAdditiveParams (vanta::AdditiveParams& a) const noexcept
{
    for (int k = 0; k < vanta::kAddHarm; ++k) { a.amp[k] = P.addAmp[k]->load(); a.phase[k] = P.addPhase[k]->load(); }
    a.count = (int) P.add_count->load(); a.level = P.add_level->load(); a.shift = P.add_shift->load(); a.morph = P.add_morph->load();
    a.target = (int) P.add_target->load(); a.tone = P.add_tone->load(); a.inharm = P.add_inharm->load();
    a.balance = P.add_balance->load(); a.rolloff = P.add_rolloff->load();
    a.semi = P.add_semi->load(); a.fine = P.add_fine->load();
    a.sfOn = P.add_sf_on->load() > 0.5f; a.sfPos = P.add_sf_pos->load(); a.sfWidth = P.add_sf_width->load(); a.sfGain = P.add_sf_gain->load();
    a.henv = P.add_henv->load() > 0.5f; a.hdecay = P.add_hdecay->load();
    a.uni = P.add_uni->load() > 0.5f; a.spread = P.add_spread->load(); a.detune = P.add_detune->load();
    vanta::additive::shape (a);
}

void VantaProcessor::readModParams (double bpm)
{
    using namespace vanta::mod; auto& mc = modctx;
    for (int k = 0; k < kLfos; ++k)
    {
        auto* q = M.lfo[k]; auto& c = mc.lfo[k];
        c.wave = jlimit (0, 6, (int) q[0]->load()); c.rateHz = q[1]->load(); c.sync = q[2]->load() > 0.5f; c.beats = kDivBeats[jlimit (0, kNumDivs - 1, (int) q[3]->load())];
        c.phase = q[4]->load(); c.note = q[5]->load() > 0.5f; c.fade = q[6]->load(); c.bipolar = q[7]->load() > 0.5f; c.depth = q[8]->load();
    }
    for (int k = 0; k < kEnvs; ++k)
    {
        auto* q = M.env[k]; auto& c = mc.env[k];
        c.a = q[0]->load(); c.h = q[1]->load(); c.d = q[2]->load(); c.s = q[3]->load(); c.r = q[4]->load(); c.curve = q[5]->load(); c.velSens = q[6]->load();
        c.loop = q[7]->load() > 0.5f; c.bipolar = q[8]->load() > 0.5f; c.fromCurrent = q[9]->load() > 0.5f;
    }
    auto& st = mc.step;
    for (int i = 0; i < kSteps; ++i) st.v[i] = M.stepV[i]->load();
    st.count = (int) M.step[0]->load(); st.rateHz = M.step[1]->load(); st.sync = M.step[2]->load() > 0.5f; st.beats = kDivBeats[jlimit (0, kNumDivs - 1, (int) M.step[3]->load())];
    st.dir = jlimit (0, 3, (int) M.step[4]->load()); st.smooth = M.step[5]->load(); st.note = M.step[6]->load() > 0.5f; st.bipolar = M.step[7]->load() > 0.5f;
    for (int k = 0; k < kMacros; ++k) mc.macro[k] = M.macro[k]->load();
    mc.clearSlots();
    for (int i = 0; i < kSlots; ++i)
        if (M.slot[i][3]->load() > 0.5f) mc.addSlot ((int) M.slot[i][0]->load(), (int) M.slot[i][1]->load(), M.slot[i][2]->load());
    mc.setRates (sr, bpm);
}

void VantaProcessor::handleMidi (const MidiMessage& m)
{
    if (m.isNoteOn())            { modctx.polyAT[m.getNoteNumber() & 127] = 0.f; voices.noteOn (m.getNoteNumber(), m.getFloatVelocity(), sp); }
    else if (m.isNoteOff())      { voices.noteOff (m.getNoteNumber(), sp); }
    else if (m.isAllNotesOff() || m.isAllSoundOff()) { voices.allNotesOff(); modctx.aftertouch = 0.f; std::fill (std::begin (modctx.polyAT), std::end (modctx.polyAT), 0.f); }   // CC123 / CC120 BEFORE the generic controller branch
    else if (m.isPitchWheel())   { const float b = (m.getPitchWheelValue() - 8192) / 8192.f; voices.setPitchBend (b); modctx.pitchBend = jlimit (-1.f, 1.f, b); }
    else if (m.isChannelPressure()) modctx.aftertouch = m.getChannelPressureValue() / 127.f;
    else if (m.isAftertouch())   modctx.polyAT[m.getNoteNumber() & 127] = m.getAfterTouchValue() / 127.f;
    else if (m.isController())
    {
        const int cc = m.getControllerNumber(); const float v = m.getControllerValue() / 127.f;
        if (cc == 64) voices.sustainPedal (m.getControllerValue() >= 64);
        else if (cc == 1)  { voices.setModWheel (v); modctx.modWheel = v; }
        else if (cc == 11) modctx.expression = v;
        lastCC.store (cc, std::memory_order_relaxed);
        const int lp = learnParam.load (std::memory_order_relaxed);
        if (lp >= 0 && lp < (int) allParams.size())                  // MIDI learn: the next controller that moves is assigned to the waiting parameter
        {
            ccMap[cc].store (lp); learnParam.store (-1); midiMapDirty.store (true); triggerAsyncUpdate();
        }
        const int idx = ccMap[cc].load (std::memory_order_relaxed);
        if (idx >= 0 && idx < (int) allParams.size()) allParams[(size_t) idx]->setValueNotifyingHost (v);
    }
}

void VantaProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const auto t0 = Time::getHighResolutionTicks();
    const int n = buffer.getNumSamples();
    buffer.clear();
    if (buffer.getNumChannels() < 2) return;

    keyboardState.processNextMidiBuffer (midi, 0, n, true);
    if (panicRequested.exchange (false)) voices.allNotesOff();
    readParams();
    double bpm = 120.0;
    if (auto* ph = getPlayHead()) if (auto pos = ph->getPosition()) if (auto b = pos->getBpm()) bpm = *b;
    readModParams (bpm);
    { const float b = uiBend.load(); if (b != lastUiBend) { voices.setPitchBend (b); modctx.pitchBend = jlimit (-1.f, 1.f, b); lastUiBend = b; } }

    float* L = buffer.getWritePointer (0);
    float* R = buffer.getWritePointer (1);

    int pos = 0;
    for (const auto meta : midi)
    {
        const int evPos = jlimit (pos, n, meta.samplePosition);
        if (evPos > pos) { voices.render (L + pos, R + pos, evPos - pos, sp); modctx.advance (evPos - pos); pos = evPos; }
        handleMidi (meta.getMessage());
    }
    if (pos < n) { voices.render (L + pos, R + pos, n - pos, sp); modctx.advance (n - pos); }

    gainSm.setTargetValue (Decibels::decibelsToGain (P.master_vol->load(), -60.f));
    panSm.setTargetValue (P.master_pan->load());
    float pkL = 0.f, pkR = 0.f;
    for (int i = 0; i < n; ++i)
    {
        const float g = gainSm.getNextValue();
        const float pan = panSm.getNextValue();
        const float ang = (pan + 1.f) * 0.78539816f;
        float l = L[i] * g * std::cos (ang) * 1.41421356f;
        float r = R[i] * g * std::sin (ang) * 1.41421356f;
        auto soft = [] (float x) { const float a = std::fabs (x); return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x); };
        l = soft (l); r = soft (r);
        L[i] = l; R[i] = r;
        pkL = jmax (pkL, std::fabs (l)); pkR = jmax (pkR, std::fabs (r));
    }
    levelL.store (jmax (pkL, levelL.load() * 0.9f)); levelR.store (jmax (pkR, levelR.load() * 0.9f));
    voiceCount.store (voices.activeCount()); grainCount.store (voices.grainCount());

    const double elapsed = Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
    const double load = elapsed / ((double) n / sr);
    cpuLoad.store (cpuLoad.load() * 0.9f + (float) load * 0.1f);
}

// ------------------------------------------------------------------ granular sample management (message thread)
void VantaProcessor::syncSampleFromState()
{
    const String path = apvts.state.getProperty ("gr_sample").toString();
    if (path.isEmpty())
    {
        if (samples.currentPath().isNotEmpty()) samples.useBuiltIn();      // only republish when something else was loaded
        sampleMissingName = {}; missingPath = {};
        return;
    }
    if (path == samples.currentPath()) return;                              // already loaded (this also runs on every parameter change)
    if (path == missingPath) return;                                        // already failed once: do NOT regenerate the built-in texture again

    File f (path);
    if (! f.existsAsFile())                                                 // moved machine / folder: look for the same file name in the user Samples folder
    {
        const auto hits = presets.getSamplesFolder().findChildFiles (File::findFiles, true, f.getFileName());
        if (! hits.isEmpty()) f = hits.getFirst();
    }
    String err;
    if (samples.loadFile (f, err))
    {
        sampleMissingName = {}; missingPath = {};
        if (f.getFullPathName() != path) apvts.state.setProperty ("gr_sample", f.getFullPathName(), nullptr);   // remember the relocated path
        return;
    }
    if (samples.currentPath().isNotEmpty()) samples.useBuiltIn();           // keep sounding with the built-in texture, never crash
    sampleMissingName = File (path).getFileName(); missingPath = path;
}

void VantaProcessor::handleAsyncUpdate()
{
    if (midiMapDirty.exchange (false)) persistMidiMap();             // a controller was learned on the audio thread: store it in the state
    if (needMapSync.exchange (false)) syncMidiMapFromState();         // the state was replaced (preset / host restore): rebuild the CC table
    syncSampleFromState();
}

bool VantaProcessor::loadSample (const File& f, String& error)
{
    if (! samples.loadFile (f, error)) return false;
    sampleMissingName = {}; missingPath = {};
    apvts.state.setProperty ("gr_sample", f.getFullPathName(), nullptr);
    return true;
}

void VantaProcessor::useBuiltInSample()
{
    samples.useBuiltIn(); sampleMissingName = {}; missingPath = {};
    apvts.state.setProperty ("gr_sample", String(), nullptr);
}

// ------------------------------------------------------------------ Stage 4: macro names + MIDI mappings (message thread)
String VantaProcessor::getMacroName (int i) const
{
    const String n = apvts.state.getProperty ("macro_name_" + String (i + 1)).toString();
    return n.isNotEmpty() ? n : "Macro " + String (i + 1);
}
void VantaProcessor::setMacroName (int i, const String& name)
{
    apvts.state.setProperty ("macro_name_" + String (i + 1), name.trim() == "Macro " + String (i + 1) ? String() : name.trim().substring (0, 24), nullptr);
}
std::vector<VantaProcessor::MidiMapping> VantaProcessor::getMidiMappings() const
{
    std::vector<MidiMapping> v;
    for (int cc = 0; cc < 128; ++cc) { const int idx = ccMap[cc].load(); if (idx >= 0) v.push_back ({ cc, idx }); }
    return v;
}
void VantaProcessor::removeMidiMapping (int cc) { if (cc >= 0 && cc < 128) { ccMap[cc].store (-1); persistMidiMap(); } }
void VantaProcessor::clearMidiMappings() { for (auto& a : ccMap) a.store (-1); persistMidiMap(); }
void VantaProcessor::persistMidiMap()
{
    auto t = apvts.state.getOrCreateChildWithName ("MIDIMAP", nullptr);
    t.removeAllChildren (nullptr);
    for (const auto& mm : getMidiMappings())
    {
        ValueTree c ("MAP"); c.setProperty ("cc", mm.cc, nullptr); c.setProperty ("param", allParams[(size_t) mm.paramIndex]->paramID, nullptr);
        t.appendChild (c, nullptr);
    }
}
void VantaProcessor::syncMidiMapFromState()
{
    for (auto& a : ccMap) a.store (-1);
    auto t = apvts.state.getChildWithName ("MIDIMAP");
    for (int i = 0; i < t.getNumChildren(); ++i)
    {
        const int cc = (int) t.getChild (i).getProperty ("cc", -1); const String id = t.getChild (i).getProperty ("param").toString();
        for (size_t k = 0; k < allParams.size(); ++k)
            if (cc >= 0 && cc < 128 && allParams[k]->paramID == id) { ccMap[cc].store ((int) k); break; }
    }
}

AudioProcessorEditor* VantaProcessor::createEditor() { return new VantaEditor (*this); }

void VantaProcessor::getStateInformation (MemoryBlock& dest)
{
    auto state = apvts.copyState();
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, dest);
}

void VantaProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = ValueTree::fromXml (*xml);
            presets.fillMissingParameters (tree);          // states saved by older versions get the defaults of the new parameters
            apvts.replaceState (tree);
        }
}

AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new VantaProcessor(); }
