// Offline test and tool: renders Torden without a DAW and checks the output (NaN, clipping, DC, silence,
// clicks on retrigger, the step sequencer, preset round trips). Also makes screenshots, loudness data and the icon.
#include <juce_audio_formats/juce_audio_formats.h>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "dsp/OfflineRender.h"
#include "ui/Theme.h"

namespace P = torden::params;

namespace
{
    double testSampleRate = 48000.0;
    int failures = 0;

    void set (juce::AudioProcessorValueTreeState& s, P::Id id, float value)
    {
        auto* p = s.getParameter (P::paramId (id));
        jassert (p != nullptr);
        p->setValueNotifyingHost (p->convertTo0to1 (value));
    }

    float get (juce::AudioProcessorValueTreeState& s, P::Id id)
    {
        auto* p = s.getParameter (P::paramId (id));
        return p->convertFrom0to1 (p->getValue());
    }

    void check (bool ok, const juce::String& what)
    {
        if (! ok) { ++failures; printf ("  FAIL: %s\n", what.toRawUTF8()); }
    }

    // Host transport for the step sequencer tests
    struct FakePlayHead : juce::AudioPlayHead
    {
        double ppq = 0.0, bpm = 120.0;
        bool playing = false;
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (playing);
            info.setBpm (bpm);
            info.setPpqPosition (ppq);
            return info;
        }
    };

    struct Stats
    {
        float peak = 0.0f, maxJump = 0.0f;
        double dc = 0.0, tailRms = 0.0, rms = 0.0;
        bool finite = true;
        double cpu = 0.0;
        juce::AudioBuffer<float> audio;
    };

    // Renders 'seconds' with hits at the given times (seconds); the last second is checked for silence.
    Stats renderHits (TordenProcessor& proc, const std::vector<double>& hits, double seconds, int block = 512,
                      FakePlayHead* head = nullptr, const std::vector<int>& notes = {})
    {
        const double sr = testSampleRate;
        proc.setPlayConfigDetails (0, 2, sr, block);
        proc.prepareToPlay (sr, block);
        if (head != nullptr) proc.setPlayHead (head);

        const int total = (int) (sr * seconds);
        Stats s;
        s.audio.setSize (2, total);
        juce::AudioBuffer<float> buf (2, block);
        double sum = 0.0, sumSq = 0.0, tailSq = 0.0, cpuSeconds = 0.0;
        long tailCount = 0;
        float prev[2] = { 0.0f, 0.0f };

        for (int pos = 0; pos < total; pos += block)
        {
            const int n = std::min (block, total - pos);
            juce::MidiBuffer midi;
            for (size_t h = 0; h < hits.size(); ++h)
            {
                const int at = (int) (hits[h] * sr);
                if (at >= pos && at < pos + n)
                    midi.addEvent (juce::MidiMessage::noteOn (1, notes.empty() ? 36 : notes[h % notes.size()], (juce::uint8) 110), at - pos);
            }
            buf.setSize (2, n, false, false, true);
            const auto t0 = juce::Time::getMillisecondCounterHiRes();
            proc.processBlock (buf, midi);
            cpuSeconds += (juce::Time::getMillisecondCounterHiRes() - t0) / 1000.0;
            if (head != nullptr) head->ppq += head->bpm / 60.0 * n / sr;

            for (int ch = 0; ch < 2; ++ch)
            {
                s.audio.copyFrom (ch, pos, buf, ch, 0, n);
                for (int i = 0; i < n; ++i)
                {
                    const float v = buf.getSample (ch, i);
                    s.finite &= std::isfinite (v);
                    s.peak = std::max (s.peak, std::abs (v));
                    s.maxJump = std::max (s.maxJump, std::abs (v - prev[ch]));
                    prev[ch] = v;
                    sum += v;
                    sumSq += (double) v * v;
                    if (pos + i > total - (int) sr) { tailSq += (double) v * v; ++tailCount; }
                }
            }
        }
        s.dc = sum / (2.0 * total);
        s.rms = std::sqrt (sumSq / (2.0 * total));
        s.tailRms = tailCount > 0 ? std::sqrt (tailSq / (double) tailCount) : 0.0;
        s.cpu = 100.0 * cpuSeconds / (total / sr);
        proc.setPlayHead (nullptr);
        return s;
    }

    void writeWav (const juce::AudioBuffer<float>& audio, const juce::File& file, double sr)
    {
        juce::WavAudioFormat wav;
        file.getParentDirectory().createDirectory();
        file.deleteFile();
        if (auto stream = file.createOutputStream())
            if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
            {
                stream.release();
                writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
            }
    }

    struct Case
    {
        juce::String name;
        std::function<void (TordenProcessor&)> setup;
        bool tail = false;   // reverb / delay: the end does not have to be silent
    };

    std::vector<Case> soundCases()
    {
        std::vector<Case> cases;
        cases.push_back ({ "init", [] (TordenProcessor&) {} });
        for (int m = 0; m < 6; ++m)
            cases.push_back ({ "click mode " + juce::String (m), [m] (TordenProcessor& p) { set (p.apvts, P::ClickMode, (float) m); set (p.apvts, P::ClickLevel, 1.0f); } });
        for (int t = 0; t < 5; ++t)
            cases.push_back ({ "noise type " + juce::String (t), [t] (TordenProcessor& p)
                { set (p.apvts, P::NoiseOn, 1); set (p.apvts, P::NoiseType, (float) t); set (p.apvts, P::NoiseRes, 1.0f); set (p.apvts, P::NoiseLevel, 1.0f); } });
        for (int w = 0; w < 3; ++w)
            cases.push_back ({ "sub wave " + juce::String (w), [w] (TordenProcessor& p) { set (p.apvts, P::SubOn, 1); set (p.apvts, P::SubWave, (float) w); } });
        cases.push_back ({ "body extreme", [] (TordenProcessor& p)
            { set (p.apvts, P::BodyShape, 3.0f); set (p.apvts, P::BodyFold, 1.0f); set (p.apvts, P::BodyFm, 1.0f); set (p.apvts, P::BodyHarm, 1.0f);
              set (p.apvts, P::BodyDrive, 1.0f); set (p.apvts, P::BodyPitch, 72.0f); set (p.apvts, P::BodyReso, 1.0f); set (p.apvts, P::BodyCutoff, 300.0f); } });
        cases.push_back ({ "all layers loud", [] (TordenProcessor& p)
            { for (auto id : { P::BodyOn, P::ClickOn, P::SubOn, P::NoiseOn }) set (p.apvts, id, 1);
              for (auto id : { P::BodyLevel, P::ClickLevel, P::SubLevel, P::NoiseLevel }) set (p.apvts, id, 1.0f);
              for (auto id : { P::BodyDrive, P::SubDrive, P::ClickDrive, P::NoiseDrive }) set (p.apvts, id, 1.0f); } });
        cases.push_back ({ "tracked pitch", [] (TordenProcessor& p) { set (p.apvts, P::PitchMode, 1); set (p.apvts, P::SubOn, 1); } });
        cases.push_back ({ "long decay", [] (TordenProcessor& p) { set (p.apvts, P::Length, 2.0f); set (p.apvts, P::BodyDecay, 2.5f); } });

        const P::Id fxOn[] = { P::SatOn, P::CrushOn, P::TransOn, P::CompOn, P::EqOn, P::FiltOn, P::VerbOn, P::DlyOn, P::ChrOn, P::WidthOn };
        for (int f = 0; f < 10; ++f)
            cases.push_back ({ "fx " + torden::fx::fxNames()[f], [f, fxOn] (TordenProcessor& p) { set (p.apvts, fxOn[f], 1); }, f == 6 || f == 7 });
        for (int m = 0; m < 6; ++m)
            cases.push_back ({ "saturation mode " + juce::String (m), [m] (TordenProcessor& p)
                { set (p.apvts, P::SatOn, 1); set (p.apvts, P::SatMode, (float) m); set (p.apvts, P::SatDrive, 1.0f); set (p.apvts, P::SatOs, 2); } });
        for (int t = 0; t < 5; ++t)
            cases.push_back ({ "filter type " + juce::String (t), [t] (TordenProcessor& p)
                { set (p.apvts, P::FiltOn, 1); set (p.apvts, P::FiltType, (float) t); set (p.apvts, P::FiltReso, 1.0f); set (p.apvts, P::FiltEnv, 6.0f); } });
        cases.push_back ({ "all fx", [fxOn] (TordenProcessor& p)
            { for (auto id : fxOn) set (p.apvts, id, 1); set (p.apvts, P::SatDrive, 1.0f); set (p.apvts, P::NoiseOn, 1); }, true });
        cases.push_back ({ "modulation", [] (TordenProcessor& p)
            { set (p.apvts, P::Mod1Src, 1); set (p.apvts, P::Mod1Dst, 5); set (p.apvts, P::Mod1Amt, 1.0f);
              set (p.apvts, P::Mod2Src, 3); set (p.apvts, P::Mod2Dst, 20); set (p.apvts, P::Mod2Amt, -1.0f);
              set (p.apvts, P::Macro1, 1.0f); set (p.apvts, P::Mod3Src, 4); set (p.apvts, P::Mod3Dst, 3); set (p.apvts, P::Mod3Amt, 0.5f); } });
        return cases;
    }

    bool soundTests (const juce::File& outDir, bool writeFiles)
    {
        printf ("Sounds (hits at 0, 0.6, 1.2 s; last second must be silent unless reverb/delay)\n");
        const int before = failures;
        for (const auto& c : soundCases())
        {
            TordenProcessor proc;
            c.setup (proc);
            const auto s = renderHits (proc, { 0.0, 0.6, 1.2 }, 4.5);
            const bool ok = s.finite && s.peak <= 0.995f && s.peak > 0.02f && std::abs (s.dc) < 0.01 && (c.tail || s.tailRms < 1.0e-4);
            printf ("  %-24s peak %.3f  rms %.3f  dc %+.4f  tail %.1e  cpu %5.2f%%  %s\n", c.name.toRawUTF8(), s.peak, s.rms, s.dc,
                    s.tailRms, s.cpu, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
            if (writeFiles) writeWav (s.audio, outDir.getChildFile (c.name + ".wav"), testSampleRate);
        }
        return failures == before;
    }

    // Choke must not click: the largest sample step with fast retriggers stays close to that of a single hit.
    void retriggerTest()
    {
        printf ("Retrigger\n");
        for (int mode = 0; mode < 2; ++mode)
        {
            TordenProcessor single, fast;
            for (auto* p : { &single, &fast })
            {
                set (p->apvts, P::Retrigger, (float) mode);
                set (p->apvts, P::ClickOn, 0);   // the click itself has steep edges; this test is about the body
                set (p->apvts, P::SubOn, 1);
                set (p->apvts, P::Length, 3.0f);
            }
            const auto a = renderHits (single, { 0.0 }, 1.5);
            std::vector<double> hits;
            for (int i = 0; i < 12; ++i) hits.push_back (0.0 + i * 0.073);
            const auto b = renderHits (fast, hits, 2.0);
            const bool ok = b.maxJump < a.maxJump * 1.6f + 0.02f && b.finite && b.peak <= 0.995f;
            printf ("  %-10s single jump %.3f  retrigger jump %.3f  peak %.3f  %s\n", mode == 0 ? "choke" : "overlap", a.maxJump, b.maxJump, b.peak, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
        }
    }

    // Step mode: four on the floor, 2 bars at 120 BPM = 8 hits; ratchet 3 on step 1 adds 2 per bar;
    // swing and chance stay in range.
    void sequencerTest()
    {
        printf ("Step sequencer\n");
        auto countHits = [] (std::function<void (TordenProcessor&)> setup)
        {
            TordenProcessor proc;
            set (proc.apvts, P::SeqOn, 1);
            set (proc.apvts, P::SeqLength, 16);
            set (proc.apvts, P::SeqRate, 2);
            setup (proc);
            FakePlayHead head;
            head.playing = true;
            head.bpm = 120.0;
            head.ppq = 0.0;
            const auto before = proc.getHitCount();
            renderHits (proc, {}, 4.0 - 0.001, 333, &head);   // 2 bars at 120 BPM
            return (int) (proc.getHitCount() - before);
        };

        const int plain = countHits ([] (TordenProcessor&) {});
        const int ratchet = countHits ([] (TordenProcessor& p) { auto pat = p.sound.getPattern(); pat.steps[0].ratchet = 3; p.sound.setPattern (pat); });
        const int swing = countHits ([] (TordenProcessor& p) { set (p.apvts, P::SeqSwing, 0.75f);
                                                               auto pat = p.sound.getPattern(); pat.steps[1].on = true; p.sound.setPattern (pat); });
        const int none = countHits ([] (TordenProcessor& p) { auto pat = p.sound.getPattern(); for (auto& s : pat.steps) s.chance = 0.0f; p.sound.setPattern (pat); });
        printf ("  four on the floor %d (8)  ratchet %d (12)  swing + step 2 %d (10)  chance 0 %d (0)\n", plain, ratchet, swing, none);
        check (plain == 8, "four on the floor");
        check (ratchet == 12, "ratchet");
        check (swing == 10, "swing");
        check (none == 0, "chance");

        // Stopped host + internal transport
        TordenProcessor proc;
        set (proc.apvts, P::SeqOn, 1);
        set (proc.apvts, P::SeqBpm, 120.0f);
        proc.setSequencerRunning (true);
        FakePlayHead head;
        head.playing = false;
        head.bpm = 0.0;
        const auto before = proc.getHitCount();
        renderHits (proc, {}, 2.0 - 0.001, 512, &head);
        const int internal = (int) (proc.getHitCount() - before);
        printf ("  internal clock %d (4)\n", internal);
        check (internal == 4, "internal clock");
    }

    // Odd block sizes and blocks larger than announced.
    void blockSizeTest()
    {
        printf ("Block sizes\n");
        for (int block : { 1, 7, 64, 333, 2048 })
        {
            TordenProcessor proc;
            set (proc.apvts, P::VerbOn, 1);
            set (proc.apvts, P::SatOn, 1);
            const double sr = testSampleRate;
            proc.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> buf (2, block);
            float peak = 0.0f;
            bool finite = true;
            for (int pos = 0; pos < (int) sr; pos += block)
            {
                juce::MidiBuffer midi;
                if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
                proc.processBlock (buf, midi);
                for (int ch = 0; ch < 2; ++ch)
                    for (int i = 0; i < block; ++i) { finite &= std::isfinite (buf.getSample (ch, i)); peak = std::max (peak, std::abs (buf.getSample (ch, i))); }
            }
            const bool ok = finite && peak > 0.05f && peak < 1.0f;
            printf ("  block %4d  peak %.3f  %s\n", block, peak, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
        }
    }

    // Preset XML round trip: parameters, envelopes, pattern and effect order survive save -> init -> load.
    void presetRoundTrip()
    {
        printf ("Preset round trip\n");
        const int before = failures;
        TordenProcessor proc;
        set (proc.apvts, P::BodyTune, 63.0f);
        set (proc.apvts, P::SatOn, 1);
        set (proc.apvts, P::ClickMode, 4);
        auto env = proc.sound.getEnv (torden::dsp::Env::Pitch);
        env.points[1] = { 0.23f, 0.1f, -0.4f };
        proc.sound.setEnv (torden::dsp::Env::Pitch, env);
        auto pat = proc.sound.getPattern();
        pat.steps[3] = { true, 0.33f, 5.0f, 2, 0.5f, 1.5f };
        proc.sound.setPattern (pat);
        auto order = proc.getFxOrder();
        std::swap (order[0], order[9]);
        proc.setFxOrder (order);

        const auto xml = proc.presetManager.createPresetXml ("Test", "Techno");
        proc.presetManager.loadInit();
        check (std::abs (get (proc.apvts, P::BodyTune) - 49.0f) < 0.01f, "init resets tune");
        proc.presetManager.applyXml (*xml, true);

        check (std::abs (get (proc.apvts, P::BodyTune) - 63.0f) < 0.01f, "tune");
        check (get (proc.apvts, P::SatOn) > 0.5f, "saturation on");
        check ((int) get (proc.apvts, P::ClickMode) == 4, "click mode");
        const auto env2 = proc.sound.getEnv (torden::dsp::Env::Pitch);
        check (std::abs (env2.points[1].time - 0.23f) < 1.0e-4f && std::abs (env2.points[1].curve + 0.4f) < 1.0e-3f, "envelope");
        const auto read = proc.sound.envelopes().read (torden::dsp::Env::Pitch);
        check (std::abs (read.points[1].time - 0.23f) < 1.0e-4f, "envelope reaches the audio store");
        const auto pat2 = proc.sound.getPattern();
        check (pat2.steps[3].on && pat2.steps[3].ratchet == 2 && std::abs (pat2.steps[3].pitch - 5.0f) < 0.01f, "pattern");
        check (proc.getFxOrder() == order, "fx order");

        // Host state round trip (with undo history cleared)
        juce::MemoryBlock block;
        proc.getStateInformation (block);
        TordenProcessor other;
        other.setStateInformation (block.getData(), (int) block.getSize());
        check (std::abs (get (other.apvts, P::BodyTune) - 63.0f) < 0.01f, "state tune");
        check (other.sound.getEnv (torden::dsp::Env::Pitch) == proc.sound.getEnv (torden::dsp::Env::Pitch), "state envelope");
        check (other.getFxOrder() == order, "state fx order");

        // Undo restores the envelope edit
        TordenProcessor u;
        auto e = u.sound.getEnv (torden::dsp::Env::BodyAmp);
        const auto original = e;
        e.points[1].level = 0.2f;
        u.undoManager.beginNewTransaction();
        u.sound.setEnv (torden::dsp::Env::BodyAmp, e);
        u.undoManager.undo();
        check (u.sound.getEnv (torden::dsp::Env::BodyAmp) == original, "undo envelope");
        printf ("  %s\n", failures == before ? "OK" : "see failures above");
    }

    // Every factory preset: renders cleanly, is not silent, does not clip.
    void factoryPresetTests (const juce::File& outDir, bool writeFiles)
    {
        TordenProcessor probe;
        const auto& list = probe.presetManager.getPresets();
        int factory = 0;
        for (const auto& p : list) factory += p.isFactory ? 1 : 0;
        printf ("Factory presets (%d)\n", factory);
        for (int i = 0; i < (int) list.size(); ++i)
        {
            if (! list[(size_t) i].isFactory) continue;
            TordenProcessor proc;
            proc.presetManager.load (i);
            const bool tail = proc.apvts.getRawParameterValue ("verb_on")->load() > 0.5f || proc.apvts.getRawParameterValue ("dly_on")->load() > 0.5f;
            const auto s = renderHits (proc, { 0.0, 1.0 }, 5.0);
            const bool ok = s.finite && s.peak <= 0.995f && s.peak > 0.05f && std::abs (s.dc) < 0.01 && (tail || s.tailRms < 3.0e-4);
            printf ("  %-12s %-22s peak %.3f  rms %.3f  tail %.1e  %s\n", list[(size_t) i].category.toRawUTF8(), list[(size_t) i].name.toRawUTF8(),
                    s.peak, s.rms, s.tailRms, ok ? "OK" : "FAIL");
            if (! ok) ++failures;
            if (writeFiles) writeWav (s.audio, outDir.getChildFile ("presets/" + list[(size_t) i].name + ".wav"), testSampleRate);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Loudness of every factory preset (RMS of the first 400 ms of a hit, master 0 dB, preset level 0 dB) as JSON.
static int loudness (const juce::File& out)
{
    TordenProcessor probe;
    const auto& list = probe.presetManager.getPresets();
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].isFactory) continue;
        TordenProcessor proc;
        proc.presetManager.load (i);
        set (proc.apvts, P::PresetLevel, 0.0f);
        set (proc.apvts, P::MasterGain, 0.0f);
        set (proc.apvts, P::ClipOn, 0);
        const auto s = renderHits (proc, { 0.0 }, 0.4);
        root->setProperty (list[(size_t) i].name, juce::Decibels::gainToDecibels (s.rms, -120.0));
    }
    out.replaceWithText (juce::JSON::toString (juce::var (root.get())));
    printf ("Wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

// Parameter table for the preset generator: id, kind, min, max, default
static int paramInfo (const juce::File& out)
{
    juce::StringArray lines;
    const auto& targets = P::modTargets();
    for (const auto& s : P::specs())
    {
        const auto it = std::find (targets.begin(), targets.end(), s.id);
        const int target = it == targets.end() ? -1 : (int) std::distance (targets.begin(), it) + 1;
        const char* kind = s.kind == P::Kind::Float ? "float" : s.kind == P::Kind::Int ? "int" : s.kind == P::Kind::Choice ? "choice" : "bool";
        lines.add (juce::String (s.paramId) + "\t" + kind + "\t" + juce::String (s.min) + "\t" + juce::String (s.max) + "\t"
                   + juce::String (s.def) + "\t" + juce::String (target) + "\t" + s.choices.joinIntoString ("|"));
    }
    out.replaceWithText (lines.joinIntoString ("\n") + "\n");
    return 0;
}

static int findPreset (TordenProcessor& proc, const juce::String& name)
{
    const auto& list = proc.presetManager.getPresets();
    for (int i = 0; i < (int) list.size(); ++i)
        if (list[(size_t) i].name == name) return i;
    return -1;
}

// One hit of a preset (or "init") as a 24-bit WAV
static int single (const juce::String& presetName, const juce::File& out, int note)
{
    torden::PresetManager::ignoreUserPresets = true;
    TordenProcessor proc;
    if (presetName != "init")
    {
        const int index = findPreset (proc, presetName);
        if (index < 0) { printf ("Preset not found: %s\n", presetName.toRawUTF8()); return 1; }
        proc.presetManager.load (index);
    }
    const auto s = renderHits (proc, { 0.0 }, 2.0, 512, nullptr, { note });
    writeWav (s.audio, out, testSampleRate);
    printf ("peak %.3f rms %.3f\n", s.peak, s.rms);
    return 0;
}

// One UI screenshot: page = kick|layers|fx|mod|step|browser|about|save, theme = light|dark
static int takeScreenshot (const juce::String& pageName, const juce::String& presetName, const juce::String& theme,
                           const juce::File& outFile, int fxIndex)
{
    static const juce::StringArray pages { "kick", "layers", "fx", "mod", "step", "browser", "about", "save" };
    const int pageIndex = pages.indexOf (pageName);
    if (pageIndex < 0) { printf ("Unknown page: %s\n", pageName.toRawUTF8()); return 1; }

    torden::PresetManager::ignoreUserPresets = true;
    TordenProcessor proc;
    if (presetName != "init")
    {
        const int index = findPreset (proc, presetName);
        if (index < 0) { printf ("Preset not found: %s\n", presetName.toRawUTF8()); return 1; }
        proc.presetManager.load (index);
    }
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);

    torden::ui::Theme::get().setDark (theme == "dark");
    std::unique_ptr<juce::AudioProcessorEditor> editorWindow (proc.createEditor());
    editorWindow->setSize (TordenView::baseWidth, TordenView::baseHeight);
    auto* editor = dynamic_cast<TordenEditor*> (editorWindow.get());
    if (editor == nullptr) return 1;
    auto& view = editor->getView();

    if (pageIndex == 5)      { view.selectPage (0); view.showBrowser (true); }
    else if (pageIndex == 6) { proc.setRateAndBufferSizeDetails (48000.0, 512); view.selectPage (0); view.showAbout(); }
    else if (pageIndex == 7) { view.selectPage (0); view.showSaveDialog(); }
    else                     view.selectPage (pageIndex);
    if (pageIndex == 2 && fxIndex >= 0)
        view.getFxPage().selectEffect (fxIndex);

    // Play a few hits while the visuals animate, so they show the sound
    juce::AudioBuffer<float> buffer (2, 512);
    for (int k = 0; k < 90; ++k)
    {
        juce::MidiBuffer midi;
        if (k == 0 || k == 40 || k == 72) midi.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 120), 0);
        buffer.clear();
        proc.processBlock (buffer, midi);
        view.tickAnimations();
    }

    const auto image = view.createComponentSnapshot (view.getLocalBounds(), true, 2.0f);
    outFile.getParentDirectory().createDirectory();
    outFile.deleteFile();
    if (auto stream = outFile.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (image, *stream);
    editorWindow.reset();
    torden::ui::Theme::get().setDark (false);
    return outFile.existsAsFile() ? 0 : 1;
}

// App icon (1024 x 1024): the Fjord family style – a scene in a squircle. Storm clouds, an amber lightning bolt
// striking behind the mountains, the "t." mark and its reflection in the water.
static int makeIcon (const juce::File& out)
{
    constexpr int size = 1024;
    juce::Image img (juce::Image::ARGB, size, size, true);
    juce::Graphics g (img);
    const auto card = juce::Rectangle<float> (100.0f, 100.0f, 824.0f, 824.0f);
    juce::Path shape;
    shape.addRoundedRectangle (card, 210.0f);

    // Soft shadow
    for (int i = 12; i > 0; --i)
    {
        g.setColour (juce::Colours::black.withAlpha (0.012f));
        g.fillPath (shape, juce::AffineTransform::translation (0.0f, 10.0f).scaled (1.0f + (float) i * 0.004f, 1.0f + (float) i * 0.004f, 512.0f, 540.0f));
    }

    g.saveState();
    g.reduceClipRegion (shape);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff0d1520), 0.0f, 100.0f, juce::Colour (0xff0a0f18), 0.0f, 924.0f, false));
    g.fillRect (card);

    // Lightning glow and bolt
    const juce::Point<float> strike (612.0f, 560.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xfff2b544).withAlpha (0.45f), strike.translated (-40.0f, -150.0f),
                                             juce::Colour (0xfff2b544).withAlpha (0.0f), strike.translated (300.0f, -150.0f), true));
    g.fillRect (card);
    juce::Path bolt;
    bolt.startNewSubPath (690.0f, 180.0f);
    for (auto pt : { juce::Point<float> (640.0f, 300.0f), { 676.0f, 312.0f }, { 615.0f, 430.0f }, { 650.0f, 440.0f }, strike })
        bolt.lineTo (pt);
    juce::Path branch;
    branch.startNewSubPath (640.0f, 300.0f);
    branch.lineTo (585.0f, 360.0f);
    branch.lineTo (598.0f, 372.0f);
    branch.lineTo (560.0f, 420.0f);
    for (auto [w, a] : { std::pair { 46.0f, 0.10f }, std::pair { 22.0f, 0.25f }, std::pair { 8.0f, 1.0f } })
    {
        g.setColour ((a < 1.0f ? juce::Colour (0xfff2b544) : juce::Colour (0xfffff3d6)).withAlpha (a));
        g.strokePath (bolt, juce::PathStrokeType (w, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
        g.strokePath (branch, juce::PathStrokeType (w * 0.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    }

    // Cloud bands: storm blue, slate, amber-lit underside
    const juce::Colour bands[] = { juce::Colour (0xff2c3a58), juce::Colour (0xff3a4660), juce::Colour (0xff8a6a3a) };
    for (int k = 0; k < 3; ++k)
    {
        juce::Path wave;
        const float y0 = 200.0f + 55.0f * (float) k;
        wave.startNewSubPath (100.0f, 100.0f);
        for (int i = 0; i <= 64; ++i)
        {
            const float x = (float) i / 64.0f;
            wave.lineTo (100.0f + x * 824.0f, y0 + 34.0f * std::sin (x * (5.0f + (float) k) + (float) k * 1.3f));
        }
        wave.lineTo (924.0f, 100.0f);
        wave.closeSubPath();
        g.setGradientFill (juce::ColourGradient (bands[k].withAlpha (0.0f), 0.0f, y0 - 120.0f, bands[k].withAlpha (k == 2 ? 0.55f : 0.9f), 0.0f, y0 + 30.0f, false));
        g.fillPath (wave);
    }

    // Mountains
    juce::Path far, near;
    far.startNewSubPath (100.0f, 660.0f);
    for (auto pt : { juce::Point<float> (100.0f, 470.0f), { 230.0f, 560.0f }, { 340.0f, 470.0f }, { 470.0f, 620.0f }, { 640.0f, 470.0f },
                     { 760.0f, 540.0f }, { 840.0f, 450.0f }, { 924.0f, 500.0f }, { 924.0f, 660.0f } })
        far.lineTo (pt);
    far.closeSubPath();
    g.setColour (juce::Colour (0xff1e2a40));
    g.fillPath (far);
    near.startNewSubPath (100.0f, 660.0f);
    for (auto pt : { juce::Point<float> (100.0f, 430.0f), { 250.0f, 520.0f }, { 400.0f, 660.0f }, { 640.0f, 660.0f }, { 780.0f, 470.0f },
                     { 924.0f, 400.0f }, { 924.0f, 660.0f } })
        near.lineTo (pt);
    near.closeSubPath();
    g.setColour (juce::Colour (0xff0b111b));
    g.fillPath (near);

    // Water
    g.setColour (juce::Colour (0xff080c14));
    g.fillRect (100.0f, 660.0f, 824.0f, 264.0f);

    // The mark: "t" in Manrope and the amber dot, reflected
    const auto markFont = torden::ui::font (360.0f, torden::ui::Weight::Regular);
    g.setFont (markFont);
    g.setColour (juce::Colour (0xffe2edf0));
    g.drawText ("t", juce::Rectangle<float> (330.0f, 330.0f, 260.0f, 340.0f), juce::Justification::centredBottom, false);
    g.setColour (juce::Colour (0xfff2b544));
    g.fillEllipse (juce::Rectangle<float> (46.0f, 46.0f).withCentre ({ 560.0f, 640.0f }));
    {
        juce::Graphics::ScopedSaveState s (g);
        g.reduceClipRegion (juce::Rectangle<int> (100, 664, 824, 200));
        g.addTransform (juce::AffineTransform::verticalFlip (1328.0f));
        g.setColour (juce::Colour (0xffe2edf0).withAlpha (0.14f));
        g.drawText ("t", juce::Rectangle<float> (330.0f, 330.0f, 260.0f, 340.0f), juce::Justification::centredBottom, false);
        g.setColour (juce::Colour (0xfff2b544).withAlpha (0.22f));
        g.fillEllipse (juce::Rectangle<float> (46.0f, 46.0f).withCentre ({ 560.0f, 640.0f }));
    }

    // Reflection lines
    const juce::Colour lines[] = { juce::Colour (0xfff2b544), juce::Colour (0xffe8a04a), juce::Colour (0xff5b8fe6), juce::Colour (0xfff2b544) };
    const float widths[] = { 492.0f, 330.0f, 196.0f, 82.0f };
    for (int i = 0; i < 4; ++i)
    {
        g.setColour (lines[i].withAlpha (0.85f));
        g.fillRoundedRectangle (juce::Rectangle<float> (widths[i], 7.0f).withCentre ({ 512.0f, 714.0f + 41.0f * (float) i }), 3.5f);
    }
    g.restoreState();

    g.setColour (juce::Colour (0x40ffffff));
    g.strokePath (shape, juce::PathStrokeType (2.0f));

    out.deleteFile();
    if (auto stream = out.createOutputStream())
        juce::PNGImageFormat().writeImageToStream (img, *stream);
    printf ("Wrote %s\n", out.getFullPathName().toRawUTF8());
    return 0;
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    torden::ui::Theme::get().setPersistent (false);

    juce::StringArray args;
    for (int i = 1; i < argc; ++i) args.add (argv[i]);
    if (const int i = args.indexOf ("--sr"); i >= 0 && i + 1 < args.size())
    {
        testSampleRate = args[i + 1].getDoubleValue();
        args.removeRange (i, 2);
    }

    if (args.size() > 1 && args[0] == "--icon") return makeIcon (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (args.size() > 1 && args[0] == "--param-info") return paramInfo (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (args.size() > 1 && args[0] == "--loudness")   return loudness (juce::File::getCurrentWorkingDirectory().getChildFile (args[1]));
    if (args.size() > 2 && args[0] == "--single")
        return single (args[1], juce::File::getCurrentWorkingDirectory().getChildFile (args[2]), args.size() > 3 ? args[3].getIntValue() : 36);
    if (args.size() > 4 && args[0] == "--screenshot")
        return takeScreenshot (args[1], args[2], args[3], juce::File::getCurrentWorkingDirectory().getChildFile (args[4]),
                               args.size() > 5 ? args[5].getIntValue() : -1);

    const auto outDir = juce::File::getCurrentWorkingDirectory().getChildFile (args.isEmpty() ? "build/renders" : args[0]);
    const bool writeFiles = juce::SystemStats::getEnvironmentVariable ("TORDEN_WAV", {}).getIntValue() != 0;
    printf ("Torden render test at %.0f Hz\n", testSampleRate);

    soundTests (outDir, writeFiles);
    retriggerTest();
    sequencerTest();
    blockSizeTest();
    presetRoundTrip();
    factoryPresetTests (outDir, writeFiles);

    printf (failures == 0 ? "ALL PASSED\n" : "%d FAILURES\n", failures);
    return failures == 0 ? 0 : 1;
}
