#include "PresetManager.h"
#include "../state/SoundState.h"
#include "Locks.h"
#include <fjord_base/fx/FxParams.h>

namespace torden
{
    PresetManager::PresetManager (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um)
        : PresetManagerBase (s, um, { ".torden", "TordenPreset", true, "Load step patterns with presets", { "master_gain" } })
    {
        setLocks (locks::set());   // locked parts (Locks.h) keep their values when a preset is loaded
        initialise();
    }

    juce::String PresetManager::normaliseOrder (const juce::String& text)
    {
        namespace fx = fjord::base::fx;
        return fx::orderToString (fx::orderFromString (text, fx::defaultOrder()));
    }

    void PresetManager::applyExtras (const juce::XmlElement& xml, bool isInit)
    {
        // Envelopes: missing ones fall back to the default shape
        auto envs = state.state.getOrCreateChildWithName (SoundState::envelopesId, nullptr);
        const auto* xmlEnvs = xml.getChildByName ("Envelopes");
        for (int i = 0; i < dsp::kNumEnvs; ++i)
        {
            const auto e = (dsp::Env) i;
            juce::String points = dsp::defaultEnv (e).toString();
            if (xmlEnvs != nullptr)
                for (auto* env : xmlEnvs->getChildWithTagNameIterator ("Env"))
                    if (env->getStringAttribute ("id") == dsp::envName (e))
                        points = dsp::EnvShape::fromString (env->getStringAttribute ("points"), dsp::defaultEnv (e)).toString();
            auto node = envs.getChildWithProperty (SoundState::idProp, dsp::envName (e));
            if (node.isValid() && ! isLockedProperty (locks::envProperty (i)))
                node.setProperty (SoundState::pointsProp, points, undo);
        }

        if ((isInit || getLoadPatterns()) && ! isLockedProperty ("stepLock"))
        {
            const auto* pattern = xml.getChildByName ("Pattern");
            const auto steps = pattern != nullptr ? pattern->getStringAttribute ("steps")
                                                  : seq::Pattern::fourOnTheFloor().toString();
            state.state.getOrCreateChildWithName (SoundState::patternId, nullptr).setProperty (SoundState::stepsProp, steps, undo);
        }
    }

    void PresetManager::writeExtras (juce::XmlElement& xml) const
    {
        auto* envs = xml.createNewChildElement ("Envelopes");
        const auto envTree = state.state.getChildWithName (SoundState::envelopesId);
        for (int i = 0; i < dsp::kNumEnvs; ++i)
        {
            const auto e = (dsp::Env) i;
            auto* env = envs->createNewChildElement ("Env");
            env->setAttribute ("id", dsp::envName (e));
            env->setAttribute ("points", envTree.getChildWithProperty (SoundState::idProp, dsp::envName (e))
                                                .getProperty (SoundState::pointsProp, dsp::defaultEnv (e).toString()).toString());
        }
        auto* pattern = xml.createNewChildElement ("Pattern");
        pattern->setAttribute ("steps", state.state.getChildWithName (SoundState::patternId).getProperty (SoundState::stepsProp).toString());
    }
}
