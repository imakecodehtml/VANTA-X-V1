# VANTA X 1.0 PRO

JUCE 8.0.6 / C++17 / CMake VST3 synthesizer for Windows x64. Ships with **no** presets, samples, wavetables or one-shots:
the preset browser, sample and wavetable folders are infrastructure for *your* content (`%APPDATA%\VANTA X\`).

## Build
No local Visual Studio needed: push to GitHub. `.github/workflows/build-windows.yml` builds on `windows-latest`
(VS 2022, x64, Release), builds and runs the DSP unit test first, then the plugin, verifies the `VANTA X.vst3` binary and uploads the artifact `VANTA-X-Windows-VST3` (plus `VANTA-X-build-logs` with all logs, even on failure).

Local build: `cmake -S . -B build -G "Visual Studio 17 2022" -A x64 && cmake --build build --config Release`

Quick DSP check without JUCE: `g++ -std=c++17 -O2 -I Source Tests/dsp_test.cpp -o dsp_test && ./dsp_test`

## Layout
`Source/Voices` voice engine (poly/mono/legato, stealing, glide, ADSR, ladder filter) - `Source/Synthesis/VirtualAnalog` oscillators -
`Source/Presets` user preset infrastructure - `Source/UI` custom look-and-feel, widgets, editor - `docs/reference` the 5 design references.

See **PROGRESS.md** for what is done, what is not, and the verified build status.
