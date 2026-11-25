# Stage 4 (modulation): see STAGE4_REPORT.md (core tests 76/76; plugin compile unverified locally)

# Stage 3 completion: see STAGE3_REPORT.md (Granular + Additive pages, fixes, tests; plugin compile still unverified locally)

# VANTA X - Progress report (Stage 2: baseline build)

## Stage 2 results (honest status)
| Item | Status |
|---|---|
| DSP unit tests (`Tests/dsp_test.cpp`) | **PASS, 17/17 checks** on g++ 13.3 (Linux), `-Wall -Wextra -Wpedantic` with zero warnings, and also clean under AddressSanitizer + UBSan. (Stage 1 docs said 16; the actual count is 17.) **Not yet run on MSVC/Windows.** |
| JUCE 8.0.6 plugin compilation | **NOT VERIFIED.** The Stage 2 sandbox has no network and no JUCE/CMake, so the plugin could not be compiled. No compiler error has been observed or fixed from a real log. The only evidence is a manual line-by-line review against the JUCE 8 API (see below). The first GitHub Actions run is the real test. |
| `VANTA X.vst3` artifact | **Not produced yet.** Will be produced by the workflow; artifact name `VANTA-X-Windows-VST3` (logs: `VANTA-X-build-logs`). |
| Loaded in FL Studio / audio tested in a DAW | **No.** |

### Changes made in Stage 2
1. `Source/UI/PluginEditor.cpp` - amp-envelope display: replaced a range-for over a mixed braced list (`{ Point<float>(..), {xa,yt}, ... }`, fragile template deduction) with an explicit `Point<float>[5]`. Preventive compile-safety fix; not confirmed as an actual compiler error.
2. `Source/UI/PluginEditor.cpp` - preset popup menu: sub-menu item IDs used a running counter but the click handler maps `ID-100` to an index into the full list, so choosing a preset inside a category loaded the wrong preset. IDs are now `100 + index` everywhere. Real logic bug found by review.
3. `.github/workflows/build-windows.yml` - the DSP test is now built and run **before** the plugin (a plugin compile error can no longer hide test results); each step fails on a non-zero exit code and writes a log; the VST3 verification now checks the bundle's `Contents/x86_64-win/VANTA X.vst3` binary (exists, size, PE `MZ` header, exports `GetPluginFactory`); logs are uploaded as a separate artifact even on failure (`VANTA-X-build-logs`, `if: always()`).
4. Not changed: `PluginProcessor.h/.cpp`, `ParamList.h`, `CMakeLists.txt`, parameter IDs, UI, DSP. No review finding required touching them.

### What the manual JUCE review checked (and found no problem with)
Parameter list vs. layout vs. editor IDs (36/36 consistent, scripted check); `ParameterID`/`AudioParameter*` constructors; `FontOptions` (JUCE 8); `AlertWindow`/`MessageBoxIconType`; `ModalCallbackFunction`; `PopupMenu::Options`; `ParameterAttachment`; `LookAndFeel_V4` override signatures; member init order in processor and editor; all `AudioProcessor` pure virtuals implemented; include paths vs `target_include_directories`; CMake plugin code / manufacturer code format.
Known residual risks (unverified): MSVC `/W4` warnings (non-fatal), `AudioProcessorEditor::resized()` not calling the base (resize corner placement), anything only a real compiler reveals.

### If the first Actions run fails
Download artifact `VANTA-X-build-logs`, open `build-plugin.log`, and send the first `error C####` lines; fix those before anything else.

---

# (Previous) Stage 1 report

## Build status (honest)
| Item | Status |
|---|---|
| DSP core (Voice, VoiceManager, oscillators, ladder filter) | Compiled with g++ -Wall -Wextra, **unit checks pass (17 in Stage 2 count)** (pitch accuracy, finite output at high resonance, unison stereo, release, poly limit + stealing, mono/legato note stack, portamento) |
| JUCE plugin (processor, presets, editor) | **WRITTEN BUT NOT YET COMPILED.** The authoring sandbox has no network, so JUCE 8.0.6 could not be fetched. First step of Stage 2 is to push to GitHub, read the Actions log and fix any compiler errors. |
| VST3 binary | **Not produced yet** (comes from the GitHub Actions run) |
| Loaded in FL Studio / audio tested in a DAW | **No** |
| GUI resizing, automation, state restore | Implemented in code, **untested** |

## Done in Stage 1
- Project skeleton, CMake (JUCE 8.0.6 via FetchContent), GitHub Actions workflow producing artifact `VANTA-X-Windows-VST3`.
- Virtual Analog engine: 3 oscillators (Saw/Sine/Square/Triangle/Pulse, polyBLEP), per-osc tuning, hard sync (naive reset), unison (1-8, detune, stereo spread), sub osc, white/pink noise + colour, analog drift, portamento (time + curve), amp ADSR, 4-mode ladder filter (LP24/LP12/BP/HP) with resonance and drive.
- Voice engine: configurable polyphony (1-32), poly/mono/legato, note stack, voice stealing, sustain pedal, pitch bend, sample-accurate MIDI, no allocation in processBlock, denormal protection, parameter smoothing for master gain/pan/cutoff.
- Preset infrastructure: save/load/import/export/delete, categories (sub-folders), search, favourites, A/B, undo/redo (UndoManager), empty-library state, user folders for samples and wavetables. **No factory content.**
- UI: custom look-and-feel, knobs, faders, combos, stepper, meters, nav tabs, engine selector, preset bar, header; Virtual Analog page positioned from reference 03 with live drawings (waveform, filter response, pulse, noise, portamento, envelope).

## Not done (do not consider these delivered)
- Wavetable, FM, Granular, Additive engines + their pages (references 01, 02, 04, 05)
- Modulation system (8 LFOs, 8 envelopes, step/random, matrix), MIDI learn, macros, mod wheel
- Filters 2-4, serial/parallel routing, other filter types (comb, formant, phaser, spectral)
- Effects rack (13 effects), oscilloscope, spectrum analyzers
- MAIN / MOD / FILTER / FX / MATRIX / BROWSER pages (tabs exist, pages show a "scheduled" notice), sample/wavetable import UI
- Pixel-accurate polish against the references (fonts are Segoe UI, not the reference typeface; no logo artwork)
- Randomizer, oversampling, CPU optimisation pass

## Suggested next stages
1. Compile on GitHub Actions, fix errors, load in FL Studio, listen/test VA engine.
2. Modulation system + matrix + MOD/MATRIX pages. 3. Filters + FX rack. 4. Additive + Wavetable. 5. FM + Granular (+ sample import). 6. MAIN/BROWSER pages, polish vs references.

## Note on reference images
The preset names shown in the references (e.g. "808 Phantom") are mockup text; the plugin shows an empty-library state instead.
