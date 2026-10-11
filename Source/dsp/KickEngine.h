#pragma once
#include "KickVoice.h"

namespace torden::dsp
{
    // Per-hit modulation sources. Velocity / note / random belong to the last hit; macros are knobs.
    struct ModSources
    {
        float velocity = 1.0f, note = 0.0f, random = 0.0f;
        std::array<float, P::kNumMacros> macros {};

        float get (P::ModSource s) const
        {
            switch (s)
            {
                case P::ModSource::Velocity: return velocity;
                case P::ModSource::Note:     return note;
                case P::ModSource::Random:   return random;
                case P::ModSource::Macro1:   return macros[0];
                case P::ModSource::Macro2:   return macros[1];
                case P::ModSource::Macro3:   return macros[2];
                case P::ModSource::Macro4:   return macros[3];
                case P::ModSource::None:
                default:                     return 0.0f;
            }
        }
    };

    // Applies the 8 matrix slots (in normalised parameter space, so every target has a sensible range).
    inline void applyModulation (const Values& base, Values& out, const ModSources& src)
    {
        out = base;
        const auto& targets = P::modTargets();
        for (int s = 0; s < P::kNumModSlots; ++s)
        {
            const auto source = (P::ModSource) base.i (P::modSrc (s));
            const int dst = base.i (P::modDst (s));
            const float amount = base[P::modAmt (s)];
            if (source == P::ModSource::None || dst <= 0 || dst > (int) targets.size() || std::abs (amount) < 1.0e-4f)
                continue;
            const auto id = targets[(size_t) dst - 1];
            const auto& spec = P::spec (id);
            const float n = P::toNormalised (spec, out[id]) + amount * src.get (source);
            out[id] = P::fromNormalised (spec, n);
        }
    }

    struct HitEvent
    {
        int sample = 0;
        HitInfo info;
    };

    // Plays the hits: voice allocation (choke or overlap), modulation per hit, visual feedback for the UI.
    class KickEngine
    {
    public:
        void prepare (double sampleRate)
        {
            sr = sampleRate;
            for (auto& v : voices) v.prepare (sampleRate);
        }

        void stopAll()
        {
            for (auto& v : voices) v.choke();
        }

        // Renders n samples (adds into outL/outR). Events must be sorted by sample position.
        void process (float* outL, float* outR, int n, const Values& base, const EnvelopeStore& envs,
                      const HitEvent* events, int numEvents, std::array<float*, kNumLayers>* layerOut = nullptr)
        {
            for (int m = 0; m < P::kNumMacros; ++m)
                sources.macros[(size_t) m] = base[P::macro (m)];
            applyModulation (base, live, sources);

            int pos = 0, e = 0;
            while (pos < n)
            {
                while (e < numEvents && events[e].sample <= pos)
                    trigger (base, envs, events[e++].info);

                const int end = e < numEvents ? std::min (n, events[e].sample) : n;
                if (end > pos)
                {
                    std::array<float*, kNumLayers> offsetLayers {};
                    if (layerOut != nullptr)
                        for (size_t k = 0; k < kNumLayers; ++k)
                            offsetLayers[k] = (*layerOut)[k] + pos;
                    for (auto& v : voices)
                        v.render (outL + pos, outR + pos, end - pos, live, layerOut != nullptr ? &offsetLayers : nullptr);
                    pos = end;
                }
            }
            samplesSinceHit += n;
        }

        // The parameters of the current block with modulation applied (for the effects).
        const Values& liveValues() const { return live; }

        // For the visuals
        uint32_t getHitCount() const { return hitCount.load (std::memory_order_relaxed); }
        float getLastVelocity() const { return lastVelocity.load (std::memory_order_relaxed); }
        float getLastNoteSource() const { return lastNoteSource.load (std::memory_order_relaxed); }
        float getLastRandomSource() const { return lastRandomSource.load (std::memory_order_relaxed); }
        int64_t getSamplesSinceHit() const { return samplesSinceHit.load (std::memory_order_relaxed); }
        bool isPlaying() const
        {
            for (const auto& v : voices) if (v.isActive()) return true;
            return false;
        }

        // Seed for the random source and the noise (fixed for offline renders, so the display is stable)
        void setSeed (uint32_t s) { rng.seed (s); }

    private:
        void trigger (const Values& base, const EnvelopeStore& envs, const HitInfo& hit)
        {
            sources.velocity = std::clamp (hit.velocity, 0.0f, 1.0f);
            sources.note = std::clamp ((hit.note - 60.0f) / 24.0f, -1.0f, 1.0f);
            sources.random = rng.nextBipolar();
            applyModulation (base, live, sources);

            const bool overlap = live.i (P::Retrigger) == 1;
            KickVoice* target = nullptr;
            for (auto& v : voices)
            {
                if (v.isActive())
                {
                    if (! overlap) v.choke();
                }
                else if (target == nullptr)
                    target = &v;
            }
            if (target == nullptr)
            {
                // All voices busy: steal the oldest one
                target = &voices[0];
                for (auto& v : voices)
                    if (v.age() > target->age()) target = &v;
                target->kill();
            }
            else if (overlap)
            {
                // Overlap keeps at most two ringing hits; older ones fade out
                int ringing = 0;
                for (auto& v : voices) ringing += v.isActive() ? 1 : 0;
                if (ringing >= 2)
                {
                    KickVoice* oldest = nullptr;
                    for (auto& v : voices)
                        if (v.isActive() && (oldest == nullptr || v.age() > oldest->age())) oldest = &v;
                    if (oldest != nullptr) oldest->choke();
                }
            }

            target->start (live, envs, hit, rng.nextInt());
            lastNoteSource.store (sources.note, std::memory_order_relaxed);
            lastRandomSource.store (sources.random, std::memory_order_relaxed);
            hitCount.fetch_add (1, std::memory_order_relaxed);
            lastVelocity.store (hit.velocity, std::memory_order_relaxed);
            samplesSinceHit.store (0, std::memory_order_relaxed);
        }

        double sr = 44100.0;
        std::array<KickVoice, kMaxVoices> voices;
        ModSources sources;
        Values live;
        Random rng;
        std::atomic<uint32_t> hitCount { 0 };
        std::atomic<float> lastVelocity { 1.0f }, lastNoteSource { 0.0f }, lastRandomSource { 0.0f };
        std::atomic<int64_t> samplesSinceHit { 1 << 30 };
    };
}
