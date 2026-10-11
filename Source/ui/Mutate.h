#pragma once
#include "../PluginProcessor.h"

namespace torden::ui
{
    // Random variation of the current sound. Parts can be excluded (stored per user in the settings).
    struct Mutate
    {
        enum Part { Body, Click, Sub, Noise, Envelopes, Effects, NumParts };

        static const char* partName (int p)
        {
            static const char* names[] = { "Body", "Click", "Sub", "Noise", "Envelopes", "Effects" };
            return names[p];
        }

        static int partOf (params::Id id)
        {
            const juce::String pid = params::paramId (id);
            if (pid.startsWith ("body_")) return Body;
            if (pid.startsWith ("click_")) return Click;
            if (pid.startsWith ("sub_")) return Sub;
            if (pid.startsWith ("noise_")) return Noise;
            for (const char* fx : { "sat_", "crush_", "trans_", "comp_", "eq_", "filt_", "verb_", "dly_", "chr_", "width_" })
                if (pid.startsWith (fx)) return Effects;
            return -1;
        }

        static void apply (TordenProcessor& proc, float amount, const std::array<bool, NumParts>& enabled, juce::int64 seed)
        {
            juce::Random rnd (seed);
            proc.undoManager.beginNewTransaction ("Mutate");
            for (const auto& s : params::specs())
            {
                const int part = partOf (s.id);
                if (part < 0 || ! enabled[(size_t) part]) continue;
                const juce::String pid = s.paramId;
                // Switches and levels stay: mutate the character, not the mix decisions
                if (s.kind == params::Kind::Bool || pid.endsWith ("_level") || pid.endsWith ("_mix") || pid.endsWith ("_out")
                    || pid.endsWith ("_on") || pid == "sat_os") continue;
                auto* param = proc.apvts.getParameter (pid);
                if (param == nullptr) continue;
                float n = param->getValue();
                if (s.kind == params::Kind::Float)
                    n = juce::jlimit (0.0f, 1.0f, n + (rnd.nextFloat() * 2.0f - 1.0f) * amount * 0.35f);
                else if (rnd.nextFloat() < amount * 0.25f)
                    n = rnd.nextFloat();
                else
                    continue;
                param->beginChangeGesture();
                param->setValueNotifyingHost (n);
                param->endChangeGesture();
            }

            if (enabled[Envelopes])
                for (int e = 0; e < dsp::kNumEnvs; ++e)
                {
                    auto shape = proc.sound.getEnv ((dsp::Env) e);
                    for (int i = 1; i < shape.numPoints; ++i)
                    {
                        auto& pt = shape.points[(size_t) i];
                        pt.time *= 1.0f + (rnd.nextFloat() * 2.0f - 1.0f) * amount * 0.4f;
                        if (pt.level > 0.001f && i < shape.numPoints - 1)
                            pt.level = juce::jlimit (0.0f, 1.0f, pt.level + (rnd.nextFloat() * 2.0f - 1.0f) * amount * 0.25f);
                        pt.curve = juce::jlimit (-1.0f, 1.0f, pt.curve + (rnd.nextFloat() * 2.0f - 1.0f) * amount * 0.5f);
                    }
                    // keep the order of the points
                    for (int i = 1; i < shape.numPoints; ++i)
                        shape.points[(size_t) i].time = std::max (shape.points[(size_t) i].time, shape.points[(size_t) i - 1].time + 0.0005f);
                    shape.sanitise();
                    proc.sound.setEnv ((dsp::Env) e, shape, true);
                }
        }
    };
}
