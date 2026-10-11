#pragma once
#include "Effects.h"

namespace torden::fx
{
    // Reorderable effect modules. Indices are stored in "fxOrder" – append only.
    enum class FxType { Saturation, Crush, Transient, Compressor, Eq, Filter, Reverb, Delay, Motion, Width, Count };
    constexpr int kNumFx = (int) FxType::Count;
    using Order = std::array<int, kNumFx>;

    inline const juce::StringArray& fxNames()
    {
        static const juce::StringArray names { "Saturation", "Crush", "Transient", "Compressor", "EQ", "Filter",
                                               "Reverb", "Delay", "Motion", "Width" };
        return names;
    }

    // The clipper sits after the chain (not reorderable) but is shown as an 11th module in the UI.
    constexpr int kClipperType = kNumFx;

    inline P::Id fxOnParam (int type)
    {
        static const P::Id ids[] = { P::SatOn, P::CrushOn, P::TransOn, P::CompOn, P::EqOn, P::FiltOn,
                                     P::VerbOn, P::DlyOn, P::ChrOn, P::WidthOn, P::ClipOn };
        return ids[type];
    }

    inline juce::String moduleName (int type) { return type == kClipperType ? juce::String ("Clipper") : fxNames()[type]; }

    // A kick-friendly default: shape the hit, then colour, then space.
    inline Order defaultOrder()
    {
        return { (int) FxType::Transient, (int) FxType::Saturation, (int) FxType::Filter, (int) FxType::Eq,
                 (int) FxType::Compressor, (int) FxType::Crush, (int) FxType::Motion, (int) FxType::Delay,
                 (int) FxType::Reverb, (int) FxType::Width };
    }

    inline juce::String orderToString (const Order& o)
    {
        juce::StringArray s;
        for (int t : o) s.add (juce::String (t));
        return s.joinIntoString (",");
    }

    // Accepts partial / broken strings: missing modules are appended in their default position.
    inline Order orderFromString (const juce::String& text)
    {
        Order o {};
        int n = 0;
        std::array<bool, kNumFx> used {};
        for (const auto& t : juce::StringArray::fromTokens (text, ",", ""))
        {
            const int v = t.getIntValue();
            if (t.trim().containsOnly ("0123456789") && v >= 0 && v < kNumFx && ! used[(size_t) v] && n < kNumFx)
            {
                o[(size_t) n++] = v;
                used[(size_t) v] = true;
            }
        }
        for (int t : defaultOrder())
            if (! used[(size_t) t] && n < kNumFx)
                o[(size_t) n++] = t;
        return o;
    }

    // Lock-free values for the live visuals on the effect tiles.
    struct FxMeters
    {
        std::array<std::atomic<float>, kNumFx> in {}, out {};
        std::atomic<float> compReduction { 0.0f }, transientGain { 0.0f }, filterEnv { 0.0f }, reverbTail { 0.0f },
                           motionPhase { 0.0f }, clipOver { 0.0f };
    };

    class FxChain
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sampleRate = sr;
            sat.prepare (sr, maxBlock);
            crush.prepare (sr);
            trans.prepare (sr);
            comp.prepare (sr);
            eq.prepare (sr);
            filter.prepare (sr);
            reverb.prepare (sr);
            delay.prepare (sr);
            motion.prepare (sr);
            width.prepare (sr);
            wasOn.fill (false);
        }

        void reset()
        {
            sat.reset(); crush.reset(); trans.reset(); comp.reset(); eq.reset();
            filter.reset(); reverb.reset(); delay.reset(); motion.reset(); width.reset();
        }

        // hits: sample positions of new hits in this block (restart the filter envelope)
        void process (StereoBuffer b, const Values& v, const Order& order, double bpm, const int* hits, int numHits,
                      FxMeters* meters = nullptr)
        {
            for (int type : order)
            {
                const bool on = v.b (fxOnParam (type));
                if (on && ! wasOn[(size_t) type]) resetModule (type);   // no stale tails when switched back on
                wasOn[(size_t) type] = on;
                if (! on) continue;

                float inPeak = 0.0f;
                if (meters != nullptr)
                    for (int i = 0; i < b.n; ++i) inPeak = std::max (inPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));

                switch ((FxType) type)
                {
                    case FxType::Saturation: sat.process (b, v); break;
                    case FxType::Crush:      crush.process (b, v); break;
                    case FxType::Transient:  trans.process (b, v); break;
                    case FxType::Compressor: comp.process (b, v); break;
                    case FxType::Eq:         eq.process (b, v); break;
                    case FxType::Filter:     filter.process (b, v, hits, numHits); break;
                    case FxType::Reverb:     reverb.process (b, v); break;
                    case FxType::Delay:      delay.process (b, v, bpm); break;
                    case FxType::Motion:     motion.process (b, v); break;
                    case FxType::Width:      width.process (b, v); break;
                    case FxType::Count:
                    default: break;
                }

                if (meters != nullptr)
                {
                    float outPeak = 0.0f;
                    for (int i = 0; i < b.n; ++i) outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
                    meters->in[(size_t) type].store (inPeak, std::memory_order_relaxed);
                    meters->out[(size_t) type].store (outPeak, std::memory_order_relaxed);
                }
            }

            if (v.b (P::ClipOn))
                clipper.process (b, v);
            else
                clipper.lastOver = 0.0f;

            if (meters != nullptr)
            {
                meters->compReduction.store (v.b (P::CompOn) ? comp.lastReduction : 0.0f, std::memory_order_relaxed);
                meters->transientGain.store (v.b (P::TransOn) ? trans.lastGainDb : 0.0f, std::memory_order_relaxed);
                meters->filterEnv.store (filter.currentEnv(), std::memory_order_relaxed);
                meters->reverbTail.store (reverb.tailLevel, std::memory_order_relaxed);
                meters->motionPhase.store (motion.getPhase(), std::memory_order_relaxed);
                meters->clipOver.store (clipper.lastOver, std::memory_order_relaxed);
            }
        }

    private:
        void resetModule (int type)
        {
            switch ((FxType) type)
            {
                case FxType::Saturation: sat.reset(); break;
                case FxType::Crush:      crush.reset(); break;
                case FxType::Transient:  trans.reset(); break;
                case FxType::Compressor: comp.reset(); break;
                case FxType::Eq:         eq.reset(); break;
                case FxType::Filter:     filter.reset(); break;
                case FxType::Reverb:     reverb.reset(); break;
                case FxType::Delay:      delay.reset(); break;
                case FxType::Motion:     motion.reset(); break;
                case FxType::Width:      width.reset(); break;
                case FxType::Count:
                default: break;
            }
        }

        double sampleRate = 44100.0;
        Saturation sat;
        Crush crush;
        TransientShaper trans;
        Compressor comp;
        Eq eq;
        Filter filter;
        Reverb reverb;
        Delay delay;
        Motion motion;
        Width width;
        Clipper clipper;
        std::array<bool, kNumFx> wasOn {};
    };
}
