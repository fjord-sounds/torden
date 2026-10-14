#pragma once
#include <fjord_base/presets/PresetManagerBase.h>

namespace torden
{
    // Presets are XML files (.torden) with plain values, the envelopes and the step pattern:
    //   <TordenPreset name="Deep Stone" category="Techno" author="Fjord" tags="Deep,Sub" macros="..." fxOrder="...">
    //     <Param id="body_level" value="0.8"/> … <Envelopes/> <Pattern/>
    // Missing parameters get their default value – this keeps old presets compatible with new versions.
    class PresetManager : public fjord::base::presets::PresetManagerBase
    {
    public:
        explicit PresetManager (juce::AudioProcessorValueTreeState&, juce::UndoManager* = nullptr);

        // Cleans up an effect order from a preset or project (unknown / missing effects)
        static juce::String normaliseOrder (const juce::String&);

    protected:
        void applyExtras (const juce::XmlElement&, bool isInit) override;
        void writeExtras (juce::XmlElement&) const override;
        juce::String normaliseFxOrder (const juce::String& order) const override { return normaliseOrder (order); }
    };
}
