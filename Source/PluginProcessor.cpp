#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "ui/Theme.h"

namespace P = torden::params;

TordenProcessor::TordenProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, &undoManager, "TordenState", P::createLayout())
{
    P::modTargets();   // build the static tables here, never on the audio thread
    torden::SoundState::ensureChildren (apvts.state);
    sound.syncAll();
    if (! apvts.state.hasProperty ("fxOrder"))
        apvts.state.setProperty ("fxOrder", torden::fx::orderToString (torden::fx::defaultOrder()), nullptr);
    apvts.state.addListener (this);
    syncFxOrderFromState();
}

TordenProcessor::~TordenProcessor()
{
    apvts.state.removeListener (this);
}

void TordenProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    maxBlockSize = std::max (16, samplesPerBlock);
    engine.prepare (sampleRate);
    fxChain.prepare (sampleRate, maxBlockSize);
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

    double ppq = 0.0, tempo = 120.0;
    bool playing = false;
    if (auto* head = getPlayHead())
        if (auto pos = head->getPosition())
        {
            playing = pos->getIsPlaying();
            tempo = pos->getBpm().orFallback (0.0);
            ppq = pos->getPpqPosition().orFallback (0.0);
        }
    hostPlaying.store (playing);
    if (! (tempo > 1.0 && tempo < 2000.0))
        tempo = apvts.getRawParameterValue (P::paramId (P::SeqBpm))->load();
    bpm.store (tempo);

    const int total = buffer.getNumSamples();
    if (total <= maxBlockSize)
        return processChunk (buffer, midi, ppq, playing);

    for (int start = 0; start < total; start += maxBlockSize)
    {
        const int n = std::min (maxBlockSize, total - start);
        juce::AudioBuffer<float> part (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), start, n);
        juce::MidiBuffer partMidi;
        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + n)
                partMidi.addEvent (meta.getMessage(), meta.samplePosition - start);
        processChunk (part, partMidi, ppq + tempo / 60.0 * start / currentSampleRate, playing);
    }
    midi.clear();
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
    torden::fx::Order order;
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = fxOrder[i].load (std::memory_order_relaxed);
    fxChain.process ({ l, r, n }, engine.liveValues(), order, bpm.load(), hitPositions.data(), numEvents, &fxMeters);

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

    for (int ch = 0; ch < 2; ++ch)
    {
        const float peak = buffer.getMagnitude (ch, 0, n);
        auto& slot = outputPeak[(size_t) ch];
        float prev = slot.load (std::memory_order_relaxed);
        while (peak > prev && ! slot.compare_exchange_weak (prev, peak, std::memory_order_relaxed)) {}
    }
}

torden::params::Values TordenProcessor::currentValues() const
{
    P::Values v;
    for (const auto& s : P::specs())
        v[s.id] = apvts.getRawParameterValue (s.paramId)->load();
    return v;
}

torden::fx::Order TordenProcessor::getFxOrder() const
{
    torden::fx::Order o;
    for (size_t i = 0; i < o.size(); ++i)
        o[i] = fxOrder[i].load();
    return o;
}

void TordenProcessor::setFxOrder (const torden::fx::Order& o)
{
    apvts.state.setProperty ("fxOrder", torden::fx::orderToString (o), &undoManager);
}

void TordenProcessor::syncFxOrderFromState()
{
    const auto o = torden::fx::orderFromString (apvts.state.getProperty ("fxOrder").toString());
    for (size_t i = 0; i < o.size(); ++i)
        fxOrder[i].store (o[i]);
}

void TordenProcessor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& id)
{
    if (tree == apvts.state && id == juce::Identifier ("fxOrder"))
        syncFxOrderFromState();
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
   #ifdef JucePlugin_VersionString
    juce::String version = JucePlugin_VersionString;
   #else
    juce::String version = "dev";
   #endif
   #if JUCE_ARM
    const juce::String arch = "arm64";
   #elif JUCE_64BIT
    const juce::String arch = "x86_64";
   #else
    const juce::String arch = "32-bit";
   #endif

    juce::StringArray lines;
    const auto home = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getFullPathName();
    auto add = [&] (const juce::String& key, const juce::String& value)
    {
        lines.add (key.paddedRight (' ', 16) + (home.length() > 1 ? value.replace (home, "~") : value));
    };

    lines.add ("Torden debug info");
    lines.add ("-----------------");
   #ifdef TORDEN_GIT_HASH
    const juce::String build = juce::String (TORDEN_GIT_HASH) + ", ";
   #else
    const juce::String build;
   #endif
    add ("Version", version + " (" + build + arch + ", built " + __DATE__ + ")");
    add ("Format", juce::AudioProcessor::getWrapperTypeDescription (wrapperType));
    add ("Host", juce::PluginHostType().getHostDescription());
    add ("Plugin", juce::File::getSpecialLocation (juce::File::currentApplicationFile).getFullPathName());
    add ("User presets", torden::PresetManager::getUserPresetFolder().getFullPathName());
    add ("Settings", torden::ui::Theme::get().getSettingsFile().getFullPathName());
    add ("Sample rate", juce::String (getSampleRate(), 0) + " Hz, block " + juce::String (getBlockSize())
                        + " (prepared " + juce::String (maxBlockSize) + ")");
    add ("OS", juce::SystemStats::getOperatingSystemName() + (juce::SystemStats::isOperatingSystem64Bit() ? " 64-bit" : ""));
    add ("CPU", juce::SystemStats::getCpuModel().trim() + ", " + juce::String (juce::SystemStats::getNumCpus()) + " cores");
    add ("JUCE", juce::SystemStats::getJUCEVersion());
    add ("Theme / scale", juce::String (torden::ui::Theme::get().isDark() ? "dark" : "light") + ", "
                          + juce::String (juce::roundToInt (torden::ui::Theme::get().getScale() * 100.0f)) + " %");

    int factory = 0, user = 0;
    for (const auto& p : presetManager.getPresets())
        (p.isFactory ? factory : user)++;
    add ("Presets", juce::String (factory) + " factory, " + juce::String (user) + " user");
    add ("Preset", presetManager.getCurrentName() + " (" + presetManager.getCurrentCategory() + ")");
    add ("FX order", apvts.state.getProperty ("fxOrder").toString());

    juce::StringArray changed;
    for (auto* p : getParameters())
        if (auto* rp = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (std::abs (rp->getValue() - rp->getDefaultValue()) > 1.0e-5f)
                changed.add (rp->getParameterID() + "=" + juce::String (rp->convertFrom0to1 (rp->getValue()), 4));
    lines.add ("");
    lines.add ("Changed parameters (" + juce::String (changed.size()) + "):");
    lines.add (changed.joinIntoString (" "));
    lines.add ("Envelopes:");
    for (int i = 0; i < torden::dsp::kNumEnvs; ++i)
        lines.add (juce::String ("  ") + torden::dsp::envName ((torden::dsp::Env) i) + " "
                   + sound.getEnv ((torden::dsp::Env) i).toString());
    return lines.joinIntoString ("\n");
}

juce::AudioProcessorEditor* TordenProcessor::createEditor()
{
    return new TordenEditor (*this);
}

namespace
{
    // Version of the saved project state (not the plugin version). Raise it when a change needs the old state
    // to be converted, and add the conversion to migrateState().
    constexpr int kStateVersion = 1;

    void migrateState (juce::ValueTree& state)
    {
        const int saved = state.getProperty ("stateVersion", 0);
        juce::ignoreUnused (saved);
        state.setProperty ("stateVersion", kStateVersion, nullptr);
    }
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
            migrateState (apvts.state);
            sound.syncAll();
            presetManager.restoreFromState();
            syncFxOrderFromState();
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
