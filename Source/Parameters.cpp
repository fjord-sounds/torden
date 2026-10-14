#include "Parameters.h"

namespace torden::params
{
    namespace
    {
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
            auto table = fjord::base::params::finish (std::array<Spec, Count> { TORDEN_PARAMS (FJORD_PARAM_SPEC) });

            // Mod target lists are built from the table itself
            juce::StringArray targets { "None" };
            for (const auto& s : table)
                if (s.kind == Kind::Float && ! excludedFromModulation ((Id) s.id))
                    targets.add (s.name);
            for (auto& s : table)
                if (s.choices.size() == 1 && s.choices[0] == "@targets")
                {
                    s.choices = targets;
                    s.max = (float) (targets.size() - 1);
                }
            return table;
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
                if (s.kind == Kind::Float && ! excludedFromModulation ((Id) s.id))
                    ids.push_back ((Id) s.id);
            return ids;
        }();
        return list;
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;
        fjord::base::params::addToLayout (layout, specs());
        return layout;
    }

    Values defaults()
    {
        Values v;
        for (const auto& s : specs())
            v.v[(size_t) s.id] = s.def;
        return v;
    }
}
