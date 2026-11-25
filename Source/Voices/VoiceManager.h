#pragma once
#include <array>
#include "Voice.h"

namespace vanta
{
class VoiceManager
{
public:
    static constexpr int kMax = 32;

    void prepare (double sr) { for (auto& v : voices) { v.prepare (sr); v.kill(); } stackCount = 0; lastNote = -1.f; }
    void allNotesOff() { for (auto& v : voices) v.kill(); stackCount = 0; pedal = false; }
    void setPitchBend (float norm) { bend = norm; }
    void setModWheel (float v) { modWheel = v; }
    float getModWheel() const { return modWheel; }

    void noteOn (int note, float vel, const SynthParams& p)
    {
        ++counter;
        if (p.mode == 0)
        {
            Voice* v = findVoice (std::max (1, std::min (kMax, p.polyLimit)));
            v->start (note, vel, p.glide > 0.0005f ? lastNote : -1.f, counter);
            v->engine = p.engine;
        }
        else
        {
            for (int i = 1; i < kMax; ++i) if (voices[i].active) voices[i].release();
            pushNote (note, vel);
            Voice& v = voices[0];
            const bool legato = (p.mode == 2) && v.active && v.gate;
            if (legato) { v.changeNote (note, counter); v.vel = vel; }
            else        { v.start (note, vel, p.glide > 0.0005f ? lastNote : -1.f, counter); v.engine = p.engine; }
        }
        lastNote = (float) note;
    }

    void noteOff (int note, const SynthParams& p)
    {
        if (p.mode == 0)
        {
            for (auto& v : voices)
                if (v.active && v.gate && v.note == note)
                { if (pedal) v.sustained = true; else v.release(); }
        }
        else
        {
            removeNote (note);
            Voice& v = voices[0];
            if (stackCount > 0) { const int n = stackN[stackCount - 1]; v.changeNote (n, ++counter); v.vel = stackV[stackCount - 1]; lastNote = (float) n; }
            else if (v.active && v.gate) { if (pedal) v.sustained = true; else v.release(); }
        }
    }

    void sustainPedal (bool down)
    {
        pedal = down;
        if (! down) for (auto& v : voices) if (v.sustained) v.release();
    }

    void render (float* L, float* R, int n, const SynthParams& p) noexcept
    {
        const float bendSemis = bend * p.bendRange;
        // CPU governor: total simultaneous grains stay roughly constant however many voices are playing
        const int act = std::max (1, activeCount());
        const int grainCap = std::max (8, std::min (GranularVoice::kMaxGrains, 384 / act));
        for (auto& v : voices) v.render (L, R, n, p, bendSemis, grainCap);
    }

    int grainCount() const noexcept { int c = 0; for (auto& v : voices) c += v.active && v.engine == EngGranular ? v.grainCount() : 0; return c; }

    int activeCount() const noexcept { int c = 0; for (auto& v : voices) c += v.active ? 1 : 0; return c; }

private:
    std::array<Voice, kMax> voices;
    uint32_t counter = 0; float lastNote = -1.f, bend = 0.f, modWheel = 0.f; bool pedal = false;
    int stackN[16] {}; float stackV[16] {}; int stackCount = 0;

    Voice* findVoice (int limit) noexcept
    {
        for (int i = 0; i < limit; ++i) if (! voices[i].active) return &voices[i];
        Voice* best = nullptr; float bestLevel = 2.f;
        for (int i = 0; i < limit; ++i)
            if (! voices[i].gate && voices[i].envLevel() < bestLevel) { best = &voices[i]; bestLevel = voices[i].envLevel(); }
        if (best) return best;
        best = &voices[0];
        for (int i = 1; i < limit; ++i) if (voices[i].order < best->order) best = &voices[i];
        return best;
    }
    void pushNote (int n, float v) { removeNote (n); if (stackCount == 16) { for (int i = 1; i < 16; ++i) { stackN[i-1] = stackN[i]; stackV[i-1] = stackV[i]; } --stackCount; } stackN[stackCount] = n; stackV[stackCount++] = v; }
    void removeNote (int n) { int j = 0; for (int i = 0; i < stackCount; ++i) if (stackN[i] != n) { stackN[j] = stackN[i]; stackV[j++] = stackV[i]; } stackCount = j; }
};
} // namespace vanta
