#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include "../dsp/KickEngine.h"

namespace torden::seq
{
    constexpr int kMaxSteps = 32;

    struct Step
    {
        bool on = false;
        float velocity = 0.8f;     // 0..1 (accent = high velocity)
        float pitch = 0.0f;        // semitones -24..24
        int ratchet = 1;           // 1..4 hits inside the step
        float chance = 1.0f;       // probability 0..1
        float decay = 1.0f;        // envelope stretch 0.25..2

        bool operator== (const Step& o) const
        {
            return on == o.on && juce::exactlyEqual (velocity, o.velocity) && juce::exactlyEqual (pitch, o.pitch) && ratchet == o.ratchet
                && juce::exactlyEqual (chance, o.chance) && juce::exactlyEqual (decay, o.decay);
        }
    };

    struct Pattern
    {
        std::array<Step, kMaxSteps> steps {};

        bool operator== (const Pattern& o) const { return steps == o.steps; }

        static Pattern fourOnTheFloor()
        {
            Pattern p;
            for (int i = 0; i < kMaxSteps; i += 4)
                p.steps[(size_t) i].on = true;
            for (int i = 0; i < kMaxSteps; i += 16)
                p.steps[(size_t) i].velocity = 1.0f;
            return p;
        }

        juce::String toString() const
        {
            juce::StringArray parts;
            for (const auto& s : steps)
                parts.add (juce::String (s.on ? 1 : 0) + ":" + juce::String (s.velocity, 3) + ":" + juce::String (s.pitch, 2) + ":"
                           + juce::String (s.ratchet) + ":" + juce::String (s.chance, 3) + ":" + juce::String (s.decay, 3));
            return parts.joinIntoString (" ");
        }

        static Pattern fromString (const juce::String& text)
        {
            Pattern p;
            int i = 0;
            for (const auto& token : juce::StringArray::fromTokens (text, " ", ""))
            {
                if (i >= kMaxSteps) break;
                const auto v = juce::StringArray::fromTokens (token, ":", "");
                if (v.size() < 6) continue;
                auto& s = p.steps[(size_t) i++];
                s.on = v[0].getIntValue() != 0;
                s.velocity = std::clamp (v[1].getFloatValue(), 0.0f, 1.0f);
                s.pitch = std::clamp (v[2].getFloatValue(), -24.0f, 24.0f);
                s.ratchet = std::clamp (v[3].getIntValue(), 1, 4);
                s.chance = std::clamp (v[4].getFloatValue(), 0.0f, 1.0f);
                s.decay = std::clamp (v[5].getFloatValue(), 0.25f, 2.0f);
            }
            return p;
        }
    };

    // Lock-free copy of the pattern for the audio thread (a torn read only mixes two valid steps).
    class PatternStore
    {
    public:
        PatternStore() { write (Pattern::fourOnTheFloor()); }

        void write (const Pattern& p)
        {
            for (size_t i = 0; i < (size_t) kMaxSteps; ++i)
            {
                const auto& s = p.steps[i];
                on[i].store (s.on, std::memory_order_relaxed);
                velocity[i].store (s.velocity, std::memory_order_relaxed);
                pitch[i].store (s.pitch, std::memory_order_relaxed);
                ratchet[i].store (s.ratchet, std::memory_order_relaxed);
                chance[i].store (s.chance, std::memory_order_relaxed);
                decay[i].store (s.decay, std::memory_order_relaxed);
            }
            versionCounter.fetch_add (1, std::memory_order_release);
        }

        Step read (int index) const
        {
            const auto i = (size_t) std::clamp (index, 0, kMaxSteps - 1);
            return { on[i].load (std::memory_order_relaxed), velocity[i].load (std::memory_order_relaxed),
                     pitch[i].load (std::memory_order_relaxed), ratchet[i].load (std::memory_order_relaxed),
                     chance[i].load (std::memory_order_relaxed), decay[i].load (std::memory_order_relaxed) };
        }

        Pattern readAll() const
        {
            Pattern p;
            for (int i = 0; i < kMaxSteps; ++i) p.steps[(size_t) i] = read (i);
            return p;
        }

        uint32_t version() const { return versionCounter.load (std::memory_order_acquire); }

    private:
        std::array<std::atomic<bool>, kMaxSteps> on {};
        std::array<std::atomic<float>, kMaxSteps> velocity {}, pitch {}, chance {}, decay {};
        std::array<std::atomic<int>, kMaxSteps> ratchet {};
        std::atomic<uint32_t> versionCounter { 0 };
    };

    inline double stepBeats (int rate)
    {
        static const double beats[] = { 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125 };
        return beats[std::clamp (rate, 0, 4)];
    }

    // Turns the song position into kick hits. Follows the host transport; when the host is stopped,
    // an internal clock (Internal Tempo) runs while "Play" is on in the STEP page.
    class StepSequencer
    {
    public:
        struct Clock
        {
            double ppq = 0.0;            // position at the start of the block
            double beatsPerSample = 0.0;
            bool running = false;
        };

        void reset() { rng.seed (4242); currentStep.store (-1); }

        // Appends hits for this block to 'events' (returns the new count).
        int process (const Clock& clock, int numSamples, const params::Values& v, const PatternStore& pattern,
                     dsp::HitEvent* events, int numEvents, int maxEvents)
        {
            if (! v.b (params::SeqOn) || ! clock.running || clock.beatsPerSample <= 0.0)
            {
                currentStep.store (-1, std::memory_order_relaxed);
                return numEvents;
            }

            const int length = std::clamp (v.i (params::SeqLength), 1, kMaxSteps);
            const double len = stepBeats (v.i (params::SeqRate));
            const double swing = v[params::SeqSwing] * len * 0.66;
            const double start = clock.ppq, end = clock.ppq + clock.beatsPerSample * numSamples;
            const int root = v.i (params::RootNote);

            const auto first = (int64_t) std::floor (start / len) - 1;
            const auto last = (int64_t) std::floor (end / len);
            for (int64_t k = first; k <= last; ++k)
            {
                if (k < 0) continue;
                const int index = (int) (k % length);
                const auto s = pattern.read (index);
                const double stepStart = (double) k * len + ((k & 1) ? swing : 0.0);
                if (! s.on) continue;
                const int n = std::clamp (s.ratchet, 1, 4);
                const double sub = (len - ((k & 1) ? swing : 0.0)) / n;
                for (int r = 0; r < n; ++r)
                {
                    const double t = stepStart + sub * r;
                    if (t < start || t >= end) continue;
                    if (s.chance < 1.0f && rng.next01() >= s.chance) continue;
                    if (numEvents >= maxEvents) return numEvents;
                    dsp::HitEvent e;
                    e.sample = std::clamp ((int) ((t - start) / clock.beatsPerSample), 0, numSamples - 1);
                    e.info.note = (float) root;
                    e.info.velocity = s.velocity * (r == 0 ? 1.0f : 0.85f);
                    e.info.pitchOffset = s.pitch;
                    e.info.decayScale = s.decay;
                    events[numEvents++] = e;
                }
            }

            const auto now = (int64_t) std::floor (end / len);
            currentStep.store ((int) (std::max<int64_t> (0, now) % length), std::memory_order_relaxed);
            return numEvents;
        }

        int getCurrentStep() const { return currentStep.load (std::memory_order_relaxed); }

    private:
        dsp::Random rng;
        std::atomic<int> currentStep { -1 };
    };
}
