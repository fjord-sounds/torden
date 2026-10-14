#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace P = torden::params;

// Torden inside the family design: thunderstorm amber, the storm gradient (amber -> lightning white -> storm blue).
const fjord::base::Brand& fjord::base::brand()
{
    static const Brand b = []
    {
        Brand x;
        x.name = "Torden";
        x.logo = "torden";
        x.tagline = "KICK DESIGNER";
        x.githubUrl = "https://github.com/fjord-sounds/torden";
        x.settingsFolder = "Fjord/Torden";
        x.userPresetFolder = "Fjord/Torden/Presets";
        x.light = familyLight (juce::Colour (0xffa8650a));   // deep amber
        x.dark = familyDark (juce::Colour (0xfff2b544));     // lightning amber
        x.gradientDark = { juce::Colour (0xfff2b544), juce::Colour (0xfff6e7c1), juce::Colour (0xff5b8fe6) };
        x.gradientLight = { juce::Colour (0xffb8700c), juce::Colour (0xffc9933a), juce::Colour (0xff2f68b0) };
        x.glow = { juce::Colour (0xfff2b544), juce::Colour (0xffe8a04a), juce::Colour (0xff5b8fe6) };
        x.glowDark = 0.14f;
        x.glowLight = 0.09f;
        x.flash = juce::Colour (0xfff6e7c1);
        x.categories = { "Techno", "House", "Hardstyle", "Trap", "Hip-Hop", "DnB", "Industrial", "Minimal", "Acoustic",
                         "Cinematic", "Pop", "Tropical" };
        x.tags = { "Punchy", "Deep", "Long", "Short", "Tonal", "Clicky", "Distorted", "Clean", "Roomy", "Noisy",
                   "Metallic", "Pitched", "Sub", "Analog", "Lo-Fi", "Wide", "Pattern" };
        return x;
    }();
    return b;
}

TordenProcessor::TordenProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "TordenState", P::createLayout())
{
    P::modTargets();   // build the static tables here, never on the audio thread
    torden::SoundState::ensureChildren (apvts.state);
    sound.syncAll();
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted (fjord-base/source/fjord_base/presets/Migration.h helps), and convert it in setStateInformation.
    constexpr int kStateVersion = 1;
}

TordenProcessor::~TordenProcessor() = default;

void TordenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    engine.prepare (sampleRate);
    fxChain.prepare (sampleRate, maxBlockSize);
    splitter.prepare (maxBlockSize);
    sequencer.reset();
    scope.sampleRate.store (sampleRate);
    masterGain.reset (sampleRate, 0.02);
    masterGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::MasterGain))->load()));
    presetLevel.reset (sampleRate, 0.02);
    presetLevel.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (apvts.getRawParameterValue (P::paramId (P::PresetLevel))->load()));
    dcCoef = (float) std::exp (-juce::MathConstants<double>::twoPi * 5.0 / sampleRate);
    dcX.fill (0.0f);
    dcY.fill (0.0f);
}

bool TordenProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo();
}

void TordenProcessor::triggerHit (float velocity, int note)
{
    pendingVelocity.store (velocity);
    pendingNote.store (note);
    pendingHits.fetch_add (1);
}

void TordenProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    keyboardState.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);

    if (panicRequested.exchange (false))
    {
        midi.clear();
        engine.stopAll();
        fxChain.reset();
    }

    const auto transport = fjord::base::plugin::Transport::read (*this, apvts.getRawParameterValue (P::paramId (P::SeqBpm))->load());
    hostPlaying.store (transport.playing);
    bpm.store (transport.bpm);

    splitter.process (buffer, midi, [&] (juce::AudioBuffer<float>& part, juce::MidiBuffer& partMidi, int offset)
    {
        processChunk (part, partMidi, transport.ppq + transport.bpm / 60.0 * offset / currentSampleRate, transport.playing);
    });
}

void TordenProcessor::processChunk (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi, double ppq, bool playing)
{
    const int n = buffer.getNumSamples();
    reader.read (values);

    // Layer solo: everything not soloed is muted (not saved, only for listening)
    if (const int solo = soloMask.load (std::memory_order_relaxed); solo != 0)
    {
        const P::Id onIds[] = { P::BodyOn, P::ClickOn, P::SubOn, P::NoiseOn };
        for (int l = 0; l < 4; ++l)
            if ((solo & (1 << l)) == 0) values[onIds[l]] = 0.0f;
    }

    // Collect hits: MIDI notes, the pad in the UI, the step sequencer
    int numEvents = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numEvents < kMaxEvents)
        {
            auto& e = events[(size_t) numEvents++];
            e.sample = meta.samplePosition;
            e.info = {};
            e.info.note = (float) m.getNoteNumber();
            e.info.velocity = m.getFloatVelocity();
        }
        else if (m.isAllNotesOff() || m.isAllSoundOff())
            engine.stopAll();
    }
    midi.clear();

    if (pendingHits.exchange (0) > 0 && numEvents < kMaxEvents)
    {
        auto& e = events[(size_t) numEvents++];
        e.sample = 0;
        e.info = {};
        const int note = pendingNote.load();
        e.info.note = note >= 0 ? (float) note : (float) values.i (P::RootNote);
        e.info.velocity = pendingVelocity.load();
    }

    torden::seq::StepSequencer::Clock clock;
    clock.beatsPerSample = bpm.load() / 60.0 / currentSampleRate;
    if (playing)
    {
        clock.ppq = ppq;
        clock.running = true;
        freePpq = ppq;
    }
    else
    {
        clock.ppq = freePpq;
        clock.running = seqRunning.load();
        freePpq = clock.running ? freePpq + clock.beatsPerSample * n : 0.0;
    }
    numEvents = sequencer.process (clock, n, values, sound.pattern(), events.data(), numEvents, kMaxEvents);
    std::stable_sort (events.begin(), events.begin() + numEvents,
                      [] (const auto& a, const auto& b) { return a.sample < b.sample; });

    auto* l = buffer.getWritePointer (0);
    auto* r = buffer.getWritePointer (1);
    engine.process (l, r, n, values, sound.envelopes(), events.data(), numEvents);

    presetLevel.setTargetValue (juce::Decibels::decibelsToGain (values[P::PresetLevel]));
    presetLevel.applyGain (buffer, n);

    std::array<int, kMaxEvents> hitPositions {};
    for (int i = 0; i < numEvents; ++i) hitPositions[(size_t) i] = events[(size_t) i].sample;
    fjord::base::fx::Context ctx;
    ctx.bpm = bpm.load();
    ctx.ppq = ppq;
    ctx.playing = playing;
    ctx.triggers = hitPositions.data();
    ctx.numTriggers = numEvents;
    fxChain.process ({ l, r, n }, P::fxValues (engine.liveValues()), fxOrder.get(), ctx, &fxMeters);

    masterGain.setTargetValue (juce::Decibels::decibelsToGain (values[P::MasterGain]));
    masterGain.applyGain (buffer, n);

    // DC blocker (5 Hz) and a last safety stage above -0.3 dBFS (the clipper normally handles the level)
    for (int ch = 0; ch < 2; ++ch)
    {
        auto* d = buffer.getWritePointer (ch);
        for (int i = 0; i < n; ++i)
        {
            const float in = d[i];
            dcY[(size_t) ch] = in - dcX[(size_t) ch] + dcCoef * dcY[(size_t) ch];
            dcX[(size_t) ch] = in;
            const float x = dcY[(size_t) ch];
            d[i] = std::abs (x) > 0.97f ? std::copysign (0.97f + 0.02f * torden::dsp::softClip ((std::abs (x) - 0.97f) / 0.02f), x) : x;
        }
    }

    scope.push (l, r, n);

    outputPeaks.push (buffer, 0, n);
}

torden::params::Values TordenProcessor::currentValues() const
{
    P::Values v;
    for (const auto& s : P::specs())
        v.v[(size_t) s.id] = apvts.getRawParameterValue (s.paramId)->load();
    return v;
}

void TordenProcessor::storeSnapshot (int slot)
{
    snapshots[(size_t) (slot & 1)] = presetManager.createPresetXml (presetManager.getCurrentName(), presetManager.getCurrentCategory());
}

void TordenProcessor::recallSnapshot (int slot)
{
    if (auto& xml = snapshots[(size_t) (slot & 1)])
    {
        const auto name = xml->getStringAttribute ("name");
        presetManager.applyXml (*xml, true);
        apvts.state.setProperty ("presetName", name, nullptr);
    }
}

int TordenProcessor::getNumPrograms()
{
    int n = 0;
    for (const auto& p : presetManager.getPresets())
        n += p.isFactory ? 1 : 0;
    return std::max (1, n);
}

int TordenProcessor::getCurrentProgram()
{
    return std::max (0, presetManager.getCurrentIndex());
}

void TordenProcessor::setCurrentProgram (int index)
{
    if (index != presetManager.getCurrentIndex() && index < getNumPrograms())
        presetManager.load (index);
}

const juce::String TordenProcessor::getProgramName (int index)
{
    const auto& list = presetManager.getPresets();
    return juce::isPositiveAndBelow (index, (int) list.size()) ? list[(size_t) index].name : juce::String ("Init");
}

juce::String TordenProcessor::getDebugReport() const
{
    fjord::base::plugin::DebugReport report (*this, const_cast<torden::PresetManager*> (&presetManager), maxBlockSize);
    report.add ("FX order", apvts.state.getProperty ("fxOrder").toString());
    juce::StringArray envelopes { "Envelopes:" };
    for (int i = 0; i < torden::dsp::kNumEnvs; ++i)
        envelopes.add (juce::String ("  ") + torden::dsp::envName ((torden::dsp::Env) i) + " "
                       + sound.getEnv ((torden::dsp::Env) i).toString());
    return report.finish (envelopes);
}

juce::AudioProcessorEditor* TordenProcessor::createEditor()
{
    return new TordenEditor (*this);
}

void TordenProcessor::getStateInformation (juce::MemoryBlock& dest)
{
    apvts.state.setProperty ("stateVersion", kStateVersion, nullptr);
    apvts.state.setProperty ("darkTheme", torden::ui::Theme::get().isDark(), nullptr);
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, dest);
}

void TordenProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            torden::SoundState::ensureChildren (tree);
            apvts.replaceState (tree);
            sound.syncAll();
            presetManager.restoreFromState();
            fxOrder.sync();
            undoManager.clearUndoHistory();

            if (apvts.state.hasProperty ("darkTheme"))
            {
                const bool dark = apvts.state.getProperty ("darkTheme");
                juce::MessageManager::callAsync ([dark] { torden::ui::Theme::get().setDark (dark); });
            }
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new TordenProcessor();
}
