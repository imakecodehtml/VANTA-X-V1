#pragma once
#include <cmath>
#include <algorithm>

namespace vanta
{
enum Wave { Saw = 0, Sine, Square, Triangle, Pulse };

inline float polyBlep (float t, float dt) noexcept
{
    if (dt <= 0.f) return 0.f;
    if (t < dt)         { t /= dt;               return t + t - t * t - 1.f; }
    if (t > 1.f - dt)   { t = (t - 1.f) / dt;    return t * t + t + t + 1.f; }
    return 0.f;
}

// ph in [0,1), dt = freq/sampleRate. dt==0 gives the ideal (non band-limited) shape, used for drawing.
inline float oscSample (int wave, float ph, float dt, float pw) noexcept
{
    switch (wave)
    {
        case Sine:     return std::sin (6.2831853f * ph);
        case Saw:      return 2.f * ph - 1.f - polyBlep (ph, dt);
        case Triangle: return 4.f * std::fabs (ph - 0.5f) - 1.f;
        case Square:
        case Pulse:
        default:
        {
            const float p = (wave == Square) ? 0.5f : pw;
            float v = ph < p ? 1.f : -1.f;
            v += polyBlep (ph, dt);
            float t2 = ph - p; if (t2 < 0.f) t2 += 1.f;
            v -= polyBlep (t2, dt);
            return v;
        }
    }
}
} // namespace vanta
