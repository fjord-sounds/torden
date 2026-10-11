#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include "DspCommon.h"

namespace torden::dsp
{
    // The editable envelopes of a kick. Order is stored in presets (by name, see envName) – append only.
    enum class Env { BodyAmp, Pitch, ClickAmp, SubAmp, NoiseAmp, NoiseFilter, Drive, Count };
    constexpr int kNumEnvs = (int) Env::Count;
    constexpr int kMaxEnvPoints = 12;

    inline const char* envName (Env e)
    {
        static const char* names[] = { "body_amp", "pitch", "click_amp", "sub_amp", "noise_amp", "noise_filter", "drive" };
        return names[(int) e];
    }

    inline const char* envLabel (Env e)
    {
        static const char* names[] = { "Body Amp", "Pitch", "Click Amp", "Sub Amp", "Noise Amp", "Noise Filter", "Drive" };
        return names[(int) e];
    }

    // One breakpoint: time in seconds from the hit, level 0..1, and the curve of the segment that ends here
    // (-1 slow start .. 0 linear .. +1 fast start – positive curves give the typical punchy exponential fall).
    struct EnvPoint
    {
        float time = 0.0f, level = 0.0f, curve = 0.0f;
        bool operator== (const EnvPoint& o) const { return juce::exactlyEqual (time, o.time) && juce::exactlyEqual (level, o.level) && juce::exactlyEqual (curve, o.curve); }
    };

    struct EnvShape
    {
        std::array<EnvPoint, kMaxEnvPoints> points {};
        int numPoints = 0;

        bool operator== (const EnvShape& o) const
        {
            if (numPoints != o.numPoints) return false;
            for (int i = 0; i < numPoints; ++i)
                if (! (points[(size_t) i] == o.points[(size_t) i])) return false;
            return true;
        }

        float duration() const { return numPoints > 0 ? points[(size_t) numPoints - 1].time : 0.0f; }

        // Curve shaping of one segment, x in 0..1
        static float shapeCurve (float x, float curve)
        {
            if (std::abs (curve) < 0.001f) return x;
            const float k = curve * 9.0f;
            return (1.0f - std::exp (-k * x)) / (1.0f - std::exp (-k));
        }

        // Level at a time (seconds). Before the first point: its level; after the last: the last level.
        float valueAt (float t) const
        {
            if (numPoints == 0) return 0.0f;
            if (t <= points[0].time) return points[0].level;
            for (int i = 1; i < numPoints; ++i)
            {
                const auto& b = points[(size_t) i];
                if (t < b.time)
                {
                    const auto& a = points[(size_t) i - 1];
                    const float len = b.time - a.time;
                    const float x = len > 0.0f ? (t - a.time) / len : 1.0f;
                    return a.level + (b.level - a.level) * shapeCurve (x, b.curve);
                }
            }
            return points[(size_t) numPoints - 1].level;
        }

        // Keeps the invariants: first point at t = 0, times rising, values in range.
        void sanitise()
        {
            numPoints = std::clamp (numPoints, 1, kMaxEnvPoints);
            points[0].time = 0.0f;
            for (int i = 0; i < numPoints; ++i)
            {
                auto& p = points[(size_t) i];
                p.level = std::clamp (p.level, 0.0f, 1.0f);
                p.curve = std::clamp (p.curve, -1.0f, 1.0f);
                p.time = std::clamp (p.time, i == 0 ? 0.0f : points[(size_t) i - 1].time, 8.0f);
            }
        }

        juce::String toString() const
        {
            juce::StringArray parts;
            for (int i = 0; i < numPoints; ++i)
            {
                const auto& p = points[(size_t) i];
                parts.add (juce::String (p.time, 5) + ":" + juce::String (p.level, 4) + ":" + juce::String (p.curve, 3));
            }
            return parts.joinIntoString (" ");
        }

        static EnvShape fromString (const juce::String& text, const EnvShape& fallback)
        {
            EnvShape s;
            for (const auto& token : juce::StringArray::fromTokens (text, " ", ""))
            {
                const auto v = juce::StringArray::fromTokens (token, ":", "");
                if (v.size() < 2 || s.numPoints >= kMaxEnvPoints) continue;
                s.points[(size_t) s.numPoints++] = { v[0].getFloatValue(), v[1].getFloatValue(), v.size() > 2 ? v[2].getFloatValue() : 0.0f };
            }
            if (s.numPoints == 0) return fallback;
            s.sanitise();
            return s;
        }

        static EnvShape make (std::initializer_list<EnvPoint> pts)
        {
            EnvShape s;
            for (const auto& p : pts)
                if (s.numPoints < kMaxEnvPoints)
                    s.points[(size_t) s.numPoints++] = p;
            s.sanitise();
            return s;
        }
    };

    // Shapes of a fresh "Init" kick
    inline EnvShape defaultEnv (Env e)
    {
        switch (e)
        {
            case Env::BodyAmp:     return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.07f, 0.72f, 0.35f }, { 0.42f, 0.0f, 0.45f } });
            case Env::Pitch:       return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.11f, 0.0f, 0.7f } });
            case Env::ClickAmp:    return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.012f, 0.0f, 0.6f } });
            case Env::SubAmp:      return EnvShape::make ({ { 0.0f, 0.0f, 0.0f }, { 0.006f, 1.0f, 0.0f }, { 0.2f, 0.8f, 0.2f }, { 0.6f, 0.0f, 0.4f } });
            case Env::NoiseAmp:    return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.08f, 0.0f, 0.65f } });
            case Env::NoiseFilter: return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.06f, 0.0f, 0.5f } });
            case Env::Drive:       return EnvShape::make ({ { 0.0f, 1.0f, 0.0f }, { 0.12f, 0.3f, 0.3f }, { 0.5f, 0.0f, 0.0f } });
            case Env::Count:
            default:               return EnvShape::make ({ { 0.0f, 0.0f, 0.0f } });
        }
    }

    // Lock-free hand-over of the envelope shapes from the message thread to the audio thread.
    // Every field is an atomic; a sequence counter tells the reader when a write was in progress, so a voice
    // never starts with a half-written shape.
    class EnvelopeStore
    {
    public:
        EnvelopeStore()
        {
            for (int e = 0; e < kNumEnvs; ++e)
                write ((Env) e, defaultEnv ((Env) e));
        }

        void write (Env e, const EnvShape& s)
        {
            auto& slot = slots[(size_t) e];
            slot.seq.fetch_add (1, std::memory_order_acq_rel);   // odd: writing
            slot.count.store (s.numPoints, std::memory_order_relaxed);
            for (int i = 0; i < kMaxEnvPoints; ++i)
            {
                slot.t[(size_t) i].store (s.points[(size_t) i].time, std::memory_order_relaxed);
                slot.l[(size_t) i].store (s.points[(size_t) i].level, std::memory_order_relaxed);
                slot.c[(size_t) i].store (s.points[(size_t) i].curve, std::memory_order_relaxed);
            }
            slot.seq.fetch_add (1, std::memory_order_acq_rel);   // even: done
        }

        EnvShape read (Env e) const
        {
            const auto& slot = slots[(size_t) e];
            EnvShape s;
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto before = slot.seq.load (std::memory_order_acquire);
                s.numPoints = std::clamp (slot.count.load (std::memory_order_relaxed), 1, kMaxEnvPoints);
                for (int i = 0; i < kMaxEnvPoints; ++i)
                    s.points[(size_t) i] = { slot.t[(size_t) i].load (std::memory_order_relaxed),
                                             slot.l[(size_t) i].load (std::memory_order_relaxed),
                                             slot.c[(size_t) i].load (std::memory_order_relaxed) };
                if ((before & 1u) == 0 && slot.seq.load (std::memory_order_acquire) == before)
                    break;
            }
            return s;
        }

        // Changes with every write – lets displays know when to redraw.
        uint32_t version() const
        {
            uint32_t v = 0;
            for (const auto& s : slots) v += s.seq.load (std::memory_order_relaxed);
            return v;
        }

    private:
        struct Slot
        {
            std::atomic<uint32_t> seq { 0 };
            std::atomic<int> count { 0 };
            std::array<std::atomic<float>, kMaxEnvPoints> t {}, l {}, c {};
        };
        std::array<Slot, kNumEnvs> slots;
    };

    // Plays one shape at audio rate: advances sample by sample with the segment kept as state.
    class EnvPlayer
    {
    public:
        // timeScale stretches all times (decay knobs, length, step decay)
        void start (const EnvShape& s, float timeScale, float sampleRate)
        {
            shape = s;
            scale = std::max (0.01f, timeScale);
            dt = 1.0f / (sampleRate * scale);   // envelope time per sample, in unscaled seconds
            t = 0.0f;
            segment = 1;
            value = shape.numPoints > 0 ? shape.points[0].level : 0.0f;
        }

        float next()
        {
            const int n = shape.numPoints;
            while (segment < n && t >= shape.points[(size_t) segment].time)
                ++segment;
            if (segment >= n)
                value = n > 0 ? shape.points[(size_t) n - 1].level : 0.0f;
            else
            {
                const auto& a = shape.points[(size_t) segment - 1];
                const auto& b = shape.points[(size_t) segment];
                const float len = b.time - a.time;
                const float x = len > 0.0f ? (t - a.time) / len : 1.0f;
                value = a.level + (b.level - a.level) * EnvShape::shapeCurve (x, b.curve);
            }
            t += dt;
            return value;
        }

        bool finished() const { return segment >= shape.numPoints; }
        float current() const { return value; }
        float scaledDuration() const { return shape.duration() * scale; }

    private:
        EnvShape shape;
        float scale = 1.0f, dt = 0.0f, t = 0.0f, value = 0.0f;
        int segment = 1;
    };
}
