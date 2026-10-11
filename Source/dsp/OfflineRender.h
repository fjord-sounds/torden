#pragma once
#include "KickEngine.h"
#include "../fx/FxChain.h"

namespace torden::dsp
{
    // Renders one hit away from the audio thread: for the kick display (per layer, before the effects),
    // the spectrum view and the WAV export (after the effects).
    class OfflineRenderer
    {
    public:
        struct Result
        {
            double sampleRate = 48000.0;
            std::array<std::vector<float>, kNumLayers> layers;   // before panning and effects
            std::vector<float> dryL, dryR;                        // all layers
            std::vector<float> outL, outR;                        // after the effect chain and clipper (if requested)
            int length() const { return (int) dryL.size(); }
        };

        Result render (const params::Values& values, const EnvelopeStore& envs, const fx::Order& order,
                       double sampleRate, float seconds, bool withFx, const HitInfo& hit = {}, double bpm = 120.0)
        {
            Result r;
            r.sampleRate = sampleRate;
            const int total = std::max (64, (int) (seconds * sampleRate));
            for (auto& l : r.layers) l.assign ((size_t) total, 0.0f);
            r.dryL.assign ((size_t) total, 0.0f);
            r.dryR.assign ((size_t) total, 0.0f);

            if (! juce::exactlyEqual (preparedRate, sampleRate))
            {
                engine.prepare (sampleRate);
                chain.prepare (sampleRate, blockSize);
                preparedRate = sampleRate;
            }
            engine.stopAll();
            engine.setSeed (1234);
            chain.reset();

            // Silence first so choked voices of the previous render are gone
            {
                std::array<float, blockSize> sl {}, sr {};
                engine.process (sl.data(), sr.data(), blockSize, values, envs, nullptr, 0);
            }

            const HitEvent event { 0, hit };
            const int hitPos = 0;
            for (int pos = 0; pos < total; pos += blockSize)
            {
                const int n = std::min (blockSize, total - pos);
                std::array<float*, kNumLayers> layerPtrs {};
                for (size_t k = 0; k < kNumLayers; ++k) layerPtrs[k] = r.layers[k].data() + pos;
                engine.process (r.dryL.data() + pos, r.dryR.data() + pos, n, values, envs,
                                pos == 0 ? &event : nullptr, pos == 0 ? 1 : 0, &layerPtrs);
            }

            if (withFx)
            {
                // Same order as the plugin: preset level -> effect chain (with clipper) -> master
                const auto fxValues = engine.liveValues();
                const float pre = juce::Decibels::decibelsToGain (values[params::PresetLevel]);
                const float post = juce::Decibels::decibelsToGain (values[params::MasterGain]);
                r.outL.resize ((size_t) total);
                r.outR.resize ((size_t) total);
                for (int i = 0; i < total; ++i) { r.outL[(size_t) i] = r.dryL[(size_t) i] * pre; r.outR[(size_t) i] = r.dryR[(size_t) i] * pre; }
                for (int pos = 0; pos < total; pos += blockSize)
                {
                    const int n = std::min (blockSize, total - pos);
                    fx::StereoBuffer b { r.outL.data() + pos, r.outR.data() + pos, n };
                    chain.process (b, fxValues, order, bpm, &hitPos, pos == 0 ? 1 : 0);
                }
                for (int i = 0; i < total; ++i) { r.outL[(size_t) i] *= post; r.outR[(size_t) i] *= post; }
            }
            return r;
        }

    private:
        static constexpr int blockSize = 256;
        double preparedRate = 0.0;
        KickEngine engine;
        fx::FxChain chain;
    };
}
