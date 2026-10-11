#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../dsp/Envelope.h"
#include "../seq/StepSequencer.h"

namespace torden
{
    // The parts of a sound that are not host parameters: the envelope shapes and the step pattern.
    // They live as children of the plugin state (so projects, presets and undo include them) and are
    // mirrored into lock-free stores for the audio thread.
    //
    //   <TordenState ...>
    //     <Envelopes><Env id="body_amp" points="0:1:0 0.07:0.72:0.35 ..."/> ...</Envelopes>
    //     <Pattern steps="1:1:0:1:1:1 0:0.8:0:1:1:1 ..."/>
    class SoundState : private juce::ValueTree::Listener
    {
    public:
        static inline const juce::Identifier envelopesId { "Envelopes" }, envId { "Env" }, patternId { "Pattern" },
                                             idProp { "id" }, pointsProp { "points" }, stepsProp { "steps" };

        SoundState (juce::AudioProcessorValueTreeState& s, juce::UndoManager* um) : apvts (s), undo (um)
        {
            ensureChildren (apvts.state);
            syncAll();
            apvts.state.addListener (this);
        }

        ~SoundState() override { apvts.state.removeListener (this); }

        const dsp::EnvelopeStore& envelopes() const { return envStore; }
        const seq::PatternStore& pattern() const { return patternStore; }

        dsp::EnvShape getEnv (dsp::Env e) const
        {
            const auto node = envNode (e);
            return dsp::EnvShape::fromString (node.getProperty (pointsProp).toString(), dsp::defaultEnv (e));
        }

        // undoable = false while dragging (one undo step is made at the end of the gesture)
        void setEnv (dsp::Env e, const dsp::EnvShape& shape, bool undoable = true)
        {
            auto node = envNode (e);
            node.setProperty (pointsProp, shape.toString(), undoable ? undo : nullptr);
        }

        seq::Pattern getPattern() const
        {
            return seq::Pattern::fromString (apvts.state.getChildWithName (patternId).getProperty (stepsProp).toString());
        }

        void setPattern (const seq::Pattern& p, bool undoable = true)
        {
            auto node = apvts.state.getOrCreateChildWithName (patternId, nullptr);
            node.setProperty (stepsProp, p.toString(), undoable ? undo : nullptr);
        }

        juce::UndoManager* getUndoManager() const { return undo; }

        // Adds missing children with default values (old states, presets, a fresh instance).
        static void ensureChildren (juce::ValueTree state)
        {
            auto envs = state.getOrCreateChildWithName (envelopesId, nullptr);
            for (int i = 0; i < dsp::kNumEnvs; ++i)
            {
                const auto e = (dsp::Env) i;
                if (! envs.getChildWithProperty (idProp, dsp::envName (e)).isValid())
                {
                    juce::ValueTree node (envId);
                    node.setProperty (idProp, dsp::envName (e), nullptr);
                    node.setProperty (pointsProp, dsp::defaultEnv (e).toString(), nullptr);
                    envs.appendChild (node, nullptr);
                }
            }
            auto pattern = state.getOrCreateChildWithName (patternId, nullptr);
            if (! pattern.hasProperty (stepsProp))
                pattern.setProperty (stepsProp, seq::Pattern::fourOnTheFloor().toString(), nullptr);
        }

        // Default envelopes and pattern (Init)
        static void resetToDefaults (juce::ValueTree state, bool includePattern, juce::UndoManager* um)
        {
            auto envs = state.getOrCreateChildWithName (envelopesId, nullptr);
            for (int i = 0; i < dsp::kNumEnvs; ++i)
            {
                const auto e = (dsp::Env) i;
                auto node = envs.getChildWithProperty (idProp, dsp::envName (e));
                if (node.isValid())
                    node.setProperty (pointsProp, dsp::defaultEnv (e).toString(), um);
            }
            if (includePattern)
                state.getOrCreateChildWithName (patternId, nullptr).setProperty (stepsProp, seq::Pattern::fourOnTheFloor().toString(), um);
        }

        void syncAll()
        {
            for (int i = 0; i < dsp::kNumEnvs; ++i)
                envStore.write ((dsp::Env) i, getEnv ((dsp::Env) i));
            patternStore.write (getPattern());
        }

    private:
        juce::ValueTree envNode (dsp::Env e) const
        {
            return apvts.state.getChildWithName (envelopesId).getChildWithProperty (idProp, dsp::envName (e));
        }

        void changed (juce::ValueTree& tree)
        {
            if (tree.hasType (envId) || tree.hasType (envelopesId) || tree.hasType (patternId))
                syncAll();
        }

        void valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&) override { changed (tree); }
        void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree& child) override { changed (child); }
        void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree& child, int) override { changed (child); }
        void valueTreeRedirected (juce::ValueTree& tree) override
        {
            ensureChildren (tree);
            syncAll();
        }

        juce::AudioProcessorValueTreeState& apvts;
        juce::UndoManager* undo;
        dsp::EnvelopeStore envStore;
        seq::PatternStore patternStore;
    };
}
