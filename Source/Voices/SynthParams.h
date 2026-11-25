#pragma once
#include "Synthesis/Granular/GranularEngine.h"
#include "Synthesis/Additive/AdditiveEngine.h"
#include "Modulation/ModSystem.h"

namespace vanta
{
enum Engine { EngWavetable = 0, EngFM, EngVirtualAnalog, EngGranular, EngAdditive };

// Snapshot of parameters read once per audio block (plain data, no allocation).
struct SynthParams
{
    int   engine = EngVirtualAnalog;   // latched per voice at note-on (Wavetable/FM are not implemented yet -> VA)
    int   mode = 0;          // 0 poly, 1 mono, 2 legato
    int   polyLimit = 16;
    float bendRange = 2.f;
    int   uniVoices = 1;
    float uniDetune = 0.f;   // 0..1 -> 0..50 cents
    float uniSpread = 0.f;

    int   wave = 0, oct = 0, semi = 0;
    float fine = 0.f, pw = 0.5f, drift = 0.f;
    bool  sync = false;
    float mix[3] { 0.8f, 0.f, 0.f };
    float tune2 = 7.f, tune3 = -7.f;      // cents

    float subLevel = 0.f; int subOct = -1, subWave = 0;
    float noiseLevel = 0.f; int noiseType = 0; float noiseColor = 0.5f;
    float glide = 0.f, glideCurve = 0.5f;
    float a = 0.005f, d = 0.3f, s = 0.8f, r = 0.3f;
    int   fltType = 0; float cutoff = 8000.f, res = 0.f, drive = 0.f;

    GranularParams gran;     // Stage 3
    AdditiveParams add;      // Stage 3
    const mod::ModContext* mod = nullptr;   // Stage 4: null / inactive -> exactly the pre-Stage-4 signal path
};
}
