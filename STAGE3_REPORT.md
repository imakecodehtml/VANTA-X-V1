# VANTA X - Stage 3 completion report

## Verification status (honest)
| Item | Status |
|---|---|
| DSP tests `dsp_test.cpp` | **17/17 pass** (g++ 13, Linux) |
| `stage3_test.cpp` (Granular + Additive DSP) | **54/54 pass** (g++ 13, Linux) |
| `stage3b_test.cpp` (new: param-ID manifest, voices, sample safety) | **33/33 pass**, also clean under ASan + UBSan |
| JUCE 8.0.6 / MSVC compilation of the plugin | **NOT VERIFIED.** The authoring sandbox has no network, CMake or JUCE. The new UI/processor code was reviewed by hand against the JUCE 8 API but never compiled. |
| `VANTA X.vst3` artifact | **Not produced here.** It is produced by the GitHub Actions workflow (artifact `VANTA-X-Windows-VST3`). |
| FL Studio | **Not tested.** |

If the Actions run fails, download artifact `VANTA-X-build-logs`, open `build-plugin.log` and send the first `error C####` lines.

## Implemented in this completion pass
- **Granular page** (OSC tab, Granular engine): real sample view (peak overview of the loaded file, draggable start/end/position handles, spray band, live grain markers), drag-and-drop + file chooser + user-Samples-folder menu, live grain cloud, Grain Size/Density/Position/Pos Random/Pitch/Stereo Spread, Start/End/Scan Speed/Direction, Size Random/Pitch Random/Texture, grain window type/width/shape with live window plot, amp ADSR with plot, root note, polyphony/mode/bend/glide, filter. All attached to the existing parameters.
- **Additive page**: 32-harmonic spectrum editor (click/drag bars, double-click reset, right-click patterns: saw/square/triangle/full/clear/random; phase view and read-only output view), 8-slider harmonic bank with 4 pages, harmonic count, level/shift/morph (+target)/tone/inharmonicity, odd-even balance, rolloff, fundamental semitone/fine, spectral filter (switch + curve + knobs), output spectrum (dB, real shaped levels), harmonic envelopes, unison (spread/detune), amp ADSR, voice controls, filter.
- Engine selector is now bound to the `engine` parameter (presets / automation switch the page).
- Bug fixes found by review: CC120/CC123 (All Sound/Notes Off) never reached the voice manager; a missing sample file regenerated the built-in texture on every parameter change (grain reset + glitches); older presets did not get explicit defaults for new parameters (`fillMissingParameters`); samples are also searched by file name in the user Samples folder when a stored path is gone.
- New tests + CMake `add_test` entries + workflow step.

## Modified files
`Source/PluginProcessor.{h,cpp}`, `Source/Presets/PresetManager.{h,cpp}`, `Source/Audio/SampleBank.h`, `Source/Synthesis/Additive/AdditiveEngine.h` (spectralFilterGain extracted, same math), `Source/UI/PluginEditor.{h,cpp}`, `CMakeLists.txt`, `.github/workflows/build-windows.yml`, docs.
## Created files
`Source/UI/EnginePages.h`, `Tests/stage3b_test.cpp`, `STAGE3_REPORT.md`.

## Known limitations
- Not compiled against JUCE/MSVC in this environment (see above); not run in a DAW.
- Reference mockups also show Modulation, Effects Sends, Macros, Spectrum analyser and Formant/Fade-In controls. Those systems do not exist yet (later stages), so they were NOT drawn as dead controls; the panels hold real controls instead.
- Sample decoding runs on the message thread (UI may pause briefly on very long files; max 120 s per file). Wavetable / FM engines remain unimplemented (DSP falls back to Virtual Analog).
- Voice stealing restarts the stolen voice's engine state (possible small click on stolen voices only).
- Sample paths are stored in presets as absolute paths (fallback: same file name in the user Samples folder).
