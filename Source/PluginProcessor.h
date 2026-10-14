#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "Parameters.h"
#include "dsp/KickEngine.h"
#include "dsp/Analysis.h"
#include <fjord_base/fx/Chain.h>
#include <fjord_base/fx/OrderState.h>
#include <fjord_base/fx/ChainPresets.h>
#include <fjord_base/plugin/PluginHelpers.h>
#include "seq/StepSequencer.h"
#include "state/SoundState.h"
#include "presets/PresetManager.h"

class TordenProcessor : public juce::AudioProcessor
{
public:
    TordenProcessor();
    ~TordenProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int) override;
    const juce::String getProgramName (int) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    double getTailLengthSeconds() const override { return 6.0; }

    juce::UndoManager undoManager;
    juce::AudioProcessorValueTreeState apvts;
    torden::SoundState sound { apvts, &undoManager };
    torden::PresetManager presetManager { apvts, &undoManager };
    juce::MidiKeyboardState keyboardState;
    torden::dsp::ScopeBuffer scope;
    fjord::base::fx::Meters fxMeters;
    fjord::base::fx::ChainPresets chainPresets { apvts };   // effect chains (shared by every Fjord plugin)
    fjord::base::fx::OrderState fxOrder { apvts, &undoManager, fjord::base::fx::defaultOrder(), torden::PresetManager::normaliseOrder };

    // Effect order (stored in the state and in presets as "fxOrder")
    fjord::base::fx::Order getFxOrder() const { return fxOrder.get(); }
    void setFxOrder (const fjord::base::fx::Order& o) { fxOrder.set (o); }

    // Hit from the UI (pad, timeline click). Thread-safe.
    void triggerHit (float velocity = 1.0f, int note = -1);
    void requestPanic() { panicRequested.store (true); }

    // Layer solo (not saved): bit mask over the four layers
    void setSoloMask (int mask) { soloMask.store (mask); }
    int getSoloMask() const { return soloMask.load(); }

    // Internal transport for the step sequencer when the host is stopped (standalone)
    void setSequencerRunning (bool run) { seqRunning.store (run); }
    bool isSequencerRunning() const { return seqRunning.load(); }
    bool isHostPlaying() const { return hostPlaying.load(); }
    int getCurrentStep() const { return sequencer.getCurrentStep(); }

    // Visual feedback
    uint32_t getHitCount() const { return engine.getHitCount(); }
    float getLastVelocity() const { return engine.getLastVelocity(); }
    float getLastNoteSource() const { return engine.getLastNoteSource(); }
    float getLastRandomSource() const { return engine.getLastRandomSource(); }
    double getSecondsSinceHit() const { return (double) engine.getSamplesSinceHit() / currentSampleRate; }
    double getCurrentSampleRate() const { return currentSampleRate; }
    double getBpm() const { return bpm.load(); }

    std::array<float, 2> takeOutputPeaks() { return outputPeaks.take(); }

    // Current parameter values (plain) for offline renders
    torden::params::Values currentValues() const;

    // A/B comparison: two snapshots of the whole sound
    void storeSnapshot (int slot);
    void recallSnapshot (int slot);
    bool hasSnapshot (int slot) const { return snapshots[(size_t) (slot & 1)] != nullptr; }
    int getActiveSnapshot() const { return activeSnapshot; }
    void setActiveSnapshot (int slot) { activeSnapshot = slot; }

    // Plain-text report for the About panel.
    juce::String getDebugReport() const;

private:
    void processChunk (juce::AudioBuffer<float>&, juce::MidiBuffer&, double ppq, bool playing);

    torden::params::Reader reader { apvts };
    torden::params::Values values;
    torden::dsp::KickEngine engine;
    fjord::base::fx::Chain fxChain;
    torden::seq::StepSequencer sequencer;
    fjord::base::plugin::BlockSplitter splitter;

    static constexpr int kMaxEvents = 128;
    std::array<torden::dsp::HitEvent, kMaxEvents> events {};
    std::atomic<int> pendingHits { 0 };
    std::atomic<float> pendingVelocity { 1.0f };
    std::atomic<int> pendingNote { -1 };

    double currentSampleRate = 44100.0, freePpq = 0.0;
    int maxBlockSize = 512;
    std::atomic<bool> panicRequested { false }, seqRunning { false }, hostPlaying { false };
    std::atomic<int> soloMask { 0 };
    std::atomic<double> bpm { 120.0 };
    fjord::base::plugin::PeakMeter outputPeaks;
    juce::SmoothedValue<float> masterGain, presetLevel;
    std::array<float, 2> dcX {}, dcY {};
    float dcCoef = 0.9995f;

    std::array<std::unique_ptr<juce::XmlElement>, 2> snapshots;
    int activeSnapshot = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TordenProcessor)
};
