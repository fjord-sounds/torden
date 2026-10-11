#include "Parameters.h"

namespace torden::params
{
    namespace
    {
        constexpr auto F = Kind::Float;
        constexpr auto I = Kind::Int;
        constexpr auto C = Kind::Choice;
        constexpr auto B = Kind::Bool;

        using enum Format;

        bool excludedFromModulation (Id id)
        {
            if (id == MasterGain || id == PresetLevel || id == SeqBpm || id == SeqSwing
                || id == Macro1 || id == Macro2 || id == Macro3 || id == Macro4)
                return true;
            for (int s = 0; s < kNumModSlots; ++s)
                if (id == modAmt (s))
                    return true;
            return false;
        }

        std::array<Spec, Count> buildSpecs()
        {
            std::array<Spec, Count> table;
            size_t n = 0;
            #define TORDEN_SPEC_ENTRY(e, pid, nm, kind, lo, hi, def, curve, fmt, ch) \
                table[n++] = Spec { e, pid, nm, kind, (float) (lo), (float) (hi), (float) (def), (float) (curve), fmt, \
                                    juce::StringArray::fromTokens (ch, "|", "") };
            TORDEN_PARAMS (TORDEN_SPEC_ENTRY)
            #undef TORDEN_SPEC_ENTRY
            jassert (n == (size_t) Count);

            for (auto& s : table)
                s.choices.removeEmptyStrings();

            // Mod target lists are built from the table itself
            juce::StringArray targets { "None" };
            for (const auto& s : table)
                if (s.kind == Kind::Float && ! excludedFromModulation (s.id))
                    targets.add (s.name);
            for (auto& s : table)
                if (s.choices.size() == 1 && s.choices[0] == "@targets")
                {
                    s.choices = targets;
                    s.max = (float) (targets.size() - 1);
                }
            return table;
        }

        juce::String trimNumber (float v, int decimals)
        {
            auto s = juce::String (v, decimals);
            if (s.containsChar ('.'))
                s = s.trimCharactersAtEnd ("0").trimCharactersAtEnd (".");
            return s == "-0" ? juce::String ("0") : s;
        }
    }

    const std::array<Spec, Count>& specs()
    {
        static const auto table = buildSpecs();
        return table;
    }

    const std::vector<Id>& modTargets()
    {
        static const auto list = []
        {
            std::vector<Id> ids;
            for (const auto& s : specs())
                if (s.kind == Kind::Float && ! excludedFromModulation (s.id))
                    ids.push_back (s.id);
            return ids;
        }();
        return list;
    }

    float toNormalised (const Spec& s, float v)
    {
        v = std::clamp (v, s.min, s.max);
        if (s.max <= s.min) return 0.0f;
        if (s.curve < 0.0f)
            return std::log (v / s.min) / std::log (s.max / s.min);
        const float lin = (v - s.min) / (s.max - s.min);
        if (s.curve > 0.0f && s.kind == Kind::Float)
        {
            const float skew = std::log (0.5f) / std::log ((s.curve - s.min) / (s.max - s.min));
            return std::pow (lin, skew);
        }
        return lin;
    }

    float fromNormalised (const Spec& s, float n)
    {
        n = std::clamp (n, 0.0f, 1.0f);
        if (s.curve < 0.0f)
            return s.min * std::pow (s.max / s.min, n);
        if (s.curve > 0.0f && s.kind == Kind::Float)
        {
            const float skew = std::log (0.5f) / std::log ((s.curve - s.min) / (s.max - s.min));
            return s.min + (s.max - s.min) * std::pow (n, 1.0f / skew);
        }
        const float v = s.min + (s.max - s.min) * n;
        return s.kind == Kind::Float ? v : std::round (v);
    }

    juce::String noteName (int note)
    {
        static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((note % 12) + 12) % 12]) + juce::String (note / 12 - 1);
    }

    juce::String formatValue (const Spec& s, float v)
    {
        switch (s.format)
        {
            case Pct:   return juce::String (juce::roundToInt (v * 100.0f)) + " %";
            case Bip:   return (v > 0.005f ? "+" : "") + juce::String (juce::roundToInt (v * 100.0f)) + " %";
            case Hz:    return v < 1000.0f ? trimNumber (v, v < 100.0f ? 1 : 0) + " Hz" : trimNumber (v / 1000.0f, 2) + " kHz";
            case Time:  return v < 1.0f ? trimNumber (v * 1000.0f, v < 0.1f ? 1 : 0) + " ms" : trimNumber (v, 2) + " s";
            case Ms:    return v < 10.0f ? trimNumber (v, 2) + " ms" : (v < 1000.0f ? trimNumber (v, 0) + " ms" : trimNumber (v / 1000.0f, 2) + " s");
            case Db:    return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB";
            case Semi:  return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st";
            case Cents: return (v > 0.5f ? "+" : "") + juce::String (juce::roundToInt (v)) + " ct";
            case Mult:  return trimNumber (v, 2) + juce::String::fromUTF8 (" \xc3\x97");
            case Deg:   return juce::String (juce::roundToInt (v)) + juce::String::fromUTF8 ("\xc2\xb0");
            case Oct:   return (v > 0.005f ? "+" : "") + trimNumber (v, s.kind == Kind::Int ? 0 : 1) + " oct";
            case Bits:  return trimNumber (v, 1) + " bit";
            case Ratio: return trimNumber (v, 1) + " : 1";
            case Bpm:   return juce::String (v, 1) + " BPM";
            case Steps: return juce::String (juce::roundToInt (v)) + " steps";
            case Note:  return noteName (juce::roundToInt (v));
            case Pan:
            {
                const int p = juce::roundToInt (v * 100.0f);
                return p == 0 ? juce::String ("C") : (p < 0 ? "L" + juce::String (-p) : "R" + juce::String (p));
            }
            case Shape:
            {
                static const char* names[] = { "Sine", "Triangle", "Saw", "Square" };
                const int a = std::clamp ((int) std::floor (v), 0, 3);
                const float f = v - (float) a;
                if (f < 0.02f || a == 3) return names[a];
                if (f > 0.98f) return names[a + 1];
                return juce::String (names[a]) + " > " + names[a + 1] + " " + juce::String (juce::roundToInt (f * 100.0f)) + "%";
            }
            case Plain: return trimNumber (v, 2);
            case None:
            default:    return trimNumber (v, 2);
        }
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        for (const auto& s : specs())
        {
            const juce::ParameterID pid { s.paramId, 1 };
            switch (s.kind)
            {
                case Kind::Float:
                {
                    const auto copy = s;
                    juce::NormalisableRange<float> range (
                        s.min, s.max,
                        [copy] (float, float, float n) { return fromNormalised (copy, n); },
                        [copy] (float, float, float v) { return toNormalised (copy, v); });
                    layout.add (std::make_unique<juce::AudioParameterFloat> (
                        pid, s.name, range, s.def,
                        juce::AudioParameterFloatAttributes().withStringFromValueFunction (
                            [copy] (float v, int) { return formatValue (copy, v); })));
                    break;
                }
                case Kind::Int:
                {
                    const auto copy = s;
                    layout.add (std::make_unique<juce::AudioParameterInt> (
                        pid, s.name, (int) s.min, (int) s.max, (int) s.def,
                        juce::AudioParameterIntAttributes().withStringFromValueFunction (
                            [copy] (int v, int) { return formatValue (copy, (float) v); })));
                    break;
                }
                case Kind::Choice:
                    layout.add (std::make_unique<juce::AudioParameterChoice> (pid, s.name, s.choices, (int) s.def));
                    break;
                case Kind::Bool:
                    layout.add (std::make_unique<juce::AudioParameterBool> (pid, s.name, s.def > 0.5f));
                    break;
            }
        }
        return layout;
    }

    Values defaults()
    {
        Values v;
        for (const auto& s : specs())
            v[s.id] = s.def;
        return v;
    }
}
