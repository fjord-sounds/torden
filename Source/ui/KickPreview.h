#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "../dsp/OfflineRender.h"

namespace torden::ui
{
    // Renders the current kick in the background of the UI whenever a parameter, an envelope or the effect
    // order changes. The kick display, the layer previews and the spectrum all draw from this one render.
    class KickPreview : public juce::ChangeBroadcaster, private juce::Timer
    {
    public:
        static constexpr double renderRate = 32000.0;

        explicit KickPreview (TordenProcessor& p) : proc (p)
        {
            refresh (true);
            startTimerHz (30);
        }

        const dsp::OfflineRenderer::Result& result() const { return current; }
        const params::Values& values() const { return lastValues; }
        float renderedSeconds() const { return (float) current.length() / (float) current.sampleRate; }

        // How long the kick lasts (the longest amplitude envelope of an active layer, stretched like the voice does)
        float kickDuration() const { return duration; }

        // Time scale the voice applies to an envelope (for the display, so curve and waveform line up)
        float envelopeScale (dsp::Env e) const
        {
            const auto& v = lastValues;
            const float len = v[params::Length];
            switch (e)
            {
                case dsp::Env::BodyAmp:     return len * v[params::BodyDecay];
                case dsp::Env::ClickAmp:    return len * v[params::ClickDecay];
                case dsp::Env::SubAmp:      return len * v[params::SubDecay];
                case dsp::Env::NoiseAmp:
                case dsp::Env::NoiseFilter: return len * v[params::NoiseDecay];
                case dsp::Env::Pitch:       return v[params::BodyPTime];
                case dsp::Env::Drive:
                case dsp::Env::Count:
                default:                    return len;
            }
        }

        // Body pitch in Hz at a time after the hit (same maths as the voice)
        float bodyHzAt (float seconds) const
        {
            const auto shape = proc.sound.getEnv (dsp::Env::Pitch);
            const float scale = std::max (0.01f, envelopeScale (dsp::Env::Pitch));
            const float level = shape.valueAt (seconds / scale);
            return baseHz * std::exp2 (lastValues[params::BodyPitch] * level / 12.0f);
        }

        float getBaseHz() const { return baseHz; }

        // Forces a new render on the next tick (e.g. after a theme change nothing changes – only for edits)
        void refresh (bool now = false)
        {
            dirty = true;
            if (now) timerCallback();
        }

        uint32_t generation() const { return gen; }

    private:
        void timerCallback() override
        {
            const auto values = proc.currentValues();
            const auto order = proc.getFxOrder();
            const auto envVersion = proc.sound.envelopes().version();

            uint64_t hash = 1469598103934665603ull;
            auto mix = [&hash] (const void* data, size_t size)
            {
                auto* bytes = static_cast<const uint8_t*> (data);
                for (size_t i = 0; i < size; ++i) { hash ^= bytes[i]; hash *= 1099511628211ull; }
            };
            mix (values.v.data(), sizeof (float) * values.v.size());
            mix (order.data(), sizeof (int) * order.size());
            mix (&envVersion, sizeof (envVersion));

            if (hash == lastHash && ! dirty) return;
            // Long kicks with reverb take a few ms to render: while a knob moves, render at most every 2x that time
            const double now = juce::Time::getMillisecondCounterHiRes();
            if (! dirty && now - lastRender < 2.0 * lastCost) return;
            lastHash = hash;
            dirty = false;
            lastValues = values;

            // Length of the kick from the envelopes
            duration = 0.05f;
            const dsp::Env amps[] = { dsp::Env::BodyAmp, dsp::Env::ClickAmp, dsp::Env::SubAmp, dsp::Env::NoiseAmp };
            const params::Id ons[] = { params::BodyOn, params::ClickOn, params::SubOn, params::NoiseOn };
            const params::Id delays[] = { params::BodyDelay, params::ClickDelay, params::SubDelay, params::NoiseDelay };
            for (int l = 0; l < 4; ++l)
                if (values.b (ons[l]))
                    duration = std::max (duration, proc.sound.getEnv (amps[l]).duration() * envelopeScale (amps[l]) + values[delays[l]] * 0.001f);

            dsp::HitInfo hit;
            hit.note = (float) values.i (params::RootNote);
            baseHz = dsp::bodyBaseHz (values, hit);
            const bool tails = values.b (params::VerbOn) || values.b (params::DlyOn);
            const float seconds = juce::jlimit (0.2f, 3.5f, duration * 1.1f + (tails ? 0.6f : 0.05f));
            current = renderer.render (values, proc.sound.envelopes(), order, renderRate, seconds, true, hit, proc.getBpm());
            lastRender = juce::Time::getMillisecondCounterHiRes();
            lastCost = lastRender - now;
            ++gen;
            sendSynchronousChangeMessage();
        }

        TordenProcessor& proc;
        dsp::OfflineRenderer renderer;
        dsp::OfflineRenderer::Result current;
        params::Values lastValues;
        uint64_t lastHash = 0;
        double lastRender = 0.0, lastCost = 0.0;
        float duration = 0.5f, baseHz = 50.0f;
        bool dirty = true;
        uint32_t gen = 0;
    };
}
