#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <algorithm>

namespace torden::dsp
{
    constexpr int kNumLayers = 4;        // body, click, sub, noise
    constexpr int kMaxVoices = 4;        // a kick is monophonic; extra voices only fade out or ring on (overlap mode)

    constexpr float kPi    = 3.14159265358979f;
    constexpr float kTwoPi = 6.28318530717959f;

    inline float noteToHz (float note) { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }
    inline float hzToNote (float hz)   { return 69.0f + 12.0f * std::log2 (std::max (1.0f, hz) / 440.0f); }

    // Soft saturation (Padé approximation of tanh), sounds round and is cheap.
    inline float softClip (float x)
    {
        x = std::clamp (x, -3.0f, 3.0f);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    // Small, fast random generator (xorshift32).
    struct Random
    {
        uint32_t state = 0x12345678u;

        void seed (uint32_t s) { state = s ? s : 0x9e3779b9u; }

        uint32_t nextInt()
        {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }

        float next01()      { return (float) (nextInt() >> 8) * (1.0f / 16777216.0f); }
        float nextBipolar() { return next01() * 2.0f - 1.0f; }
    };

    // One-pole smoother for parameters (prevents zipper noise).
    struct Smoother
    {
        float value = 0.0f, coef = 0.0f;

        void setTime (float seconds, float sampleRate)
        {
            coef = std::exp (-1.0f / std::max (1.0f, seconds * sampleRate));
        }

        void reset (float v) { value = v; }
        float process (float target) { return value = target + (value - target) * coef; }
    };

    // Topology-preserving state variable filter (Zavalishin): stable under fast modulation,
    // which matters for kicks where the cutoff often sweeps with the pitch.
    struct Svf
    {
        enum Type { Low, High, Band, Notch };
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() { ic1 = ic2 = 0.0f; }

        float process (float x, float cutoff, float reso, float sampleRate, int type)
        {
            const float fc = std::clamp (cutoff, 10.0f, sampleRate * 0.45f);
            const float g = std::tan (kPi * fc / sampleRate);
            const float k = 2.0f - 1.96f * std::clamp (reso, 0.0f, 1.0f);
            const float a1 = 1.0f / (1.0f + g * (g + k));
            const float a2 = g * a1, a3 = g * a2;
            const float v3 = x - ic2;
            const float v1 = a1 * ic1 + a2 * v3;
            const float v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;
            switch (type)
            {
                case High:  return x - k * v1 - v2;
                case Band:  return v1 * k;
                case Notch: return x - k * v1;
                default:    return v2;
            }
        }
    };

    // PolyBLEP residual for band-limited saw / square edges.
    inline float polyBlep (float t, float dt)
    {
        if (t < dt)        { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // Equal-power pan: -1 left, +1 right
    inline std::array<float, 2> panGains (float pan)
    {
        const float a = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * 0.25f * kPi;
        return { std::cos (a) * 1.41421356f, std::sin (a) * 1.41421356f };
    }
}
