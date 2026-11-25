# VANTA X - Stage 4: Advanced Modulation System

## Verification status (honest)
| Item | Status |
|---|---|
| `dsp_test.cpp` (17), `stage3_test.cpp` (54), `stage3b_test.cpp` (33) | pass, unchanged (g++ 13, Linux) |
| `stage4_test.cpp` (new) | **76/76 pass**, also clean under ASan + UBSan |
| JUCE 8.0.6 / MSVC compilation | **NOT VERIFIED.** The authoring sandbox has no network, CMake or JUCE. All JUCE-dependent code (processor wiring, APVTS parameters, MIDI learn, UI) was reviewed by hand but never compiled. |
| `VANTA X.vst3` | **Not produced here.** Produced by the GitHub Actions workflow (artifact `VANTA-X-Windows-VST3`). |
| FL Studio | **Not tested.** |
| Preset save/restore of modulation | Implemented through the APVTS state (every modulation setting is a parameter or a state property), but **not run** (needs JUCE). Only parameter-ID uniqueness is unit-tested. |

## Premises of the brief that do not match the project
- **Wavetable and FM engines do not exist** in this project (the DSP falls back to Virtual Analog for those engine ids since Stage 2/3). They were not invented here (that would be a different stage). Modulation is integrated with the three engines that exist: **Virtual Analog, Granular, Additive**.
- **There is no effects engine**, so "effect parameters" cannot be a real destination yet; no fake destination was added.

## Architecture
- `Source/Modulation/ModSystem.h` (JUCE-free, allocation-free, lock-free): sources, destinations, stateless LFO/step functions, `ModContext` (block configuration, global controller values, free-running clock), `VoiceMod` (per-voice phases, 8 envelopes, smoothing).
- Every modulation setting is an APVTS parameter (host-automatable, saved in presets/state): 8 LFOs x 9, 8 envelopes x 10, step modulator (8 + 16 step values), 8 macros, 32 matrix slots x 4 (src, dst, amount, enable). All IDs are new; no existing ID changed (frozen-manifest test from Stage 3b still passes).
- Audio thread: once per block the processor reads the parameters into `ModContext` (no allocation); each voice evaluates sources at control rate (32-sample chunks), sums the active matrix slots per destination, smooths them (~1.5 ms) and applies them to a per-chunk copy of its parameters. With no active slot the voice takes the exact pre-Stage-4 code path (bit-identical; existing presets sound the same).
- Global sources (mod wheel, pitch bend, channel + polyphonic aftertouch, expression CC11, macros, free-running LFO/step clock) live in `ModContext`; voice sources (envelopes, velocity, key tracking, per-note random, note-retriggered LFO/step) live in `VoiceMod`.

## Features
- **8 LFOs**: Sine, Triangle, Saw Up, Saw Down, Square, Sample & Hold, Random (smooth); rate (Hz) or tempo sync (16 divisions 1/32 .. 4 bars, host BPM); phase; retrigger Free (global clock) / Note (restart on note-on); fade-in; bipolar/unipolar; depth.
- **8 envelopes**: Attack, Hold, Decay, Sustain, Release, curve, velocity sensitivity, loop, bipolar, retrigger (Restart / From Current). Graphical editor: drag A, H, D/S and R handles, drag the line to bend the curve.
- **Step modulator**: 16 steps, step count 1-16, rate / tempo sync, Forward / Backward / Ping-Pong / Random, smooth amount, Free / Note retrigger, bipolar/unipolar; bar editor with patterns.
- **Matrix**: 32 slots; sources LFO 1-8, Env 1-8, Velocity, Mod Wheel, Pitch Bend, Aftertouch (channel + poly), Key Track, Expression, Random, Step Mod, Macro 1-8; amount -1..+1; per-slot enable; live routing diagram.
- **Destinations** (all real DSP): Pitch (+-24 st at 1.0), Amp Level, Pan, Stereo Width, Filter Cutoff (+-5 oct), Resonance, Drive, VA Pulse Width, VA Osc 1/2/3 level, Unison Detune, Grain Position / Size / Density / Spray / Pitch / Texture / Spread, Additive Odd-Even Balance / Spectral Morph / Tone / Harmonic Shift / Inharmonicity / Rolloff.
- **8 macros**: host-automatable knobs, renamable (double-click the name; stored in the state), each can drive any number of destinations with + / - amounts via matrix slots.
- **MIDI**: Mod Wheel, Pitch Bend, Velocity, Channel Aftertouch, Polyphonic Aftertouch, Expression (CC11), MIDI Learn (choose a parameter, then move a controller; CC -> any parameter, mappings listed with remove / clear), mappings stored in the plugin state (`MIDIMAP`); presets without a MIDIMAP keep the current mappings. CC120/123 handled before generic CCs.
- **UI**: MOD tab (LFO editor, envelope editor, step modulator, macros) and MATRIX tab (matrix, routing view, MIDI learn). Existing pages untouched.

## Tests (`Tests/stage4_test.cpp`)
Waveforms (range, shape, periodicity, S&H, smooth random), sync maths at several BPM, phase / depth / unipolar, free vs note retrigger, fade-in, envelope stages / curve / velocity / bipolar / loop / retrigger modes, step directions + smoothing + polarity, matrix routing with positive / negative / summed amounts and invalid slots, macros driving several destinations, polyphonic aftertouch, and engine integration: zero modulation keeps the sound, pitch +-1 octave measured by frequency, amp, pan, cutoff darker / brighter, and every Granular / Additive / VA destination measurably changes the audio; stress (all sources x all destinations x 3 engines x voice stealing) finite; ID uniqueness.

## Modified / created files
Modified: `Source/Voices/Voice.h`, `Source/Voices/SynthParams.h`, `Source/PluginProcessor.{h,cpp}`, `Source/Presets/PresetManager.cpp`, `Source/UI/PluginEditor.{h,cpp}`, `CMakeLists.txt`, `.github/workflows/build-windows.yml`, `PROGRESS.md`.
Created: `Source/Modulation/ModSystem.h`, `Source/UI/ModPages.h`, `Tests/stage4_test.cpp`, `STAGE4_REPORT.md`.

## Known limitations
- Not compiled against JUCE / MSVC here; not run in a DAW; UI not visually checked (no modulation reference image was supplied).
- Tempo sync follows the host BPM but is not locked to the song position (phase restarts with the clock, not with bar lines).
- MIDI learn picks the target parameter from a menu instead of by clicking a knob. The learned CC is applied from the audio thread with `setValueNotifyingHost` (no gesture begin/end).
- Expression (CC11) defaults to 0 as a source.
- Modulation of Wavetable / FM / effects is not possible (those do not exist yet).
- If a matrix slot is first created while a note is already sounding, that voice's modulation envelopes start from that moment.
- Granular / additive voices re-evaluate their per-block setup every 32 samples while modulation is active (extra CPU only in that case).
