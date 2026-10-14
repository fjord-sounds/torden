#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include "Theme.h"
#include "Theme.h"
#include "Mutate.h"
#include "KickPreview.h"

namespace torden::ui
{
    // ---------------------------------------------------------------------------------------------
    // Big pad: click to hit (higher = harder). Rings ripple out on every hit, from any source.
    class HitPad : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit HitPad (TordenProcessor& p) : proc (p)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Play the kick (click higher for a harder hit)");
        }

        void tick (float dt)
        {
            const auto hits = proc.getHitCount();
            if (hits != lastHits)
            {
                lastHits = hits;
                rings.push_back ({ 0.0f, proc.getLastVelocity() });
                if (rings.size() > 6) rings.erase (rings.begin());
                glow = 0.4f + 0.6f * proc.getLastVelocity();
            }
            for (auto& r : rings) r.age += dt / 0.7f;
            rings.erase (std::remove_if (rings.begin(), rings.end(), [] (const Ring& r) { return r.age >= 1.0f; }), rings.end());
            glow = std::max (0.0f, glow - dt / 0.25f);
            if (! rings.empty() || glow > 0.0f || wasActive) repaint();
            wasActive = ! rings.empty() || glow > 0.0f;
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            juce::Path clip;
            clip.addRoundedRectangle (r, 10.0f);
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (clip);
                g.setGradientFill (juce::ColourGradient (p.panel, 0.0f, r.getY(), p.background, 0.0f, r.getBottom(), false));
                g.fillRect (r);
                const auto c = r.getCentre();
                if (glow > 0.0f)
                {
                    g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.45f * glow), c, p.accent.withAlpha (0.0f),
                                                             c.translated (r.getWidth() * 0.6f, 0.0f), true));
                    g.fillRect (r);
                }
                for (const auto& ring : rings)
                {
                    const float rad = 10.0f + ring.age * r.getWidth() * 0.7f;
                    g.setColour (stormAt (dark, ring.age * 0.6f).withAlpha ((1.0f - ring.age) * (0.3f + 0.6f * ring.velocity)));
                    g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.5f * (1.0f - ring.age) + 0.5f);
                }
            }
            g.setColour (pressed ? p.accent : (hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, 10.0f, 1.0f);
            g.setColour (glow > 0.3f ? p.accent : p.text);
            drawSpacedText (g, "HIT", r, 13.0f, juce::Justification::centred, 0.4f, true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override
        {
            pressed = true;
            proc.triggerHit (juce::jlimit (0.15f, 1.0f, 1.0f - e.position.y / (float) getHeight() * 0.85f + 0.1f));
            repaint();
        }
        void mouseUp (const juce::MouseEvent&) override { pressed = false; repaint(); }

    private:
        struct Ring { float age, velocity; };
        TordenProcessor& proc;
        std::vector<Ring> rings;
        uint32_t lastHits = 0;
        float glow = 0.0f;
        bool hover = false, pressed = false, wasActive = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Small text button (pill)
    class SmallButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit SmallButton (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void()> onClick;
        std::function<void (const juce::MouseEvent&)> onRightClick;
        bool active = false;
        void setText (const juce::String& t) { text = t; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            if (active)
            {
                g.setColour (p.accentSoft);
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (active ? p.accent : (hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            g.setColour (active ? p.accent : (hover ? p.text : p.text.withAlpha (0.8f)));
            drawSpacedText (g, text, r, 10.0f, juce::Justification::centred, 0.18f, active);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! getLocalBounds().contains (e.getPosition())) return;
            if (e.mods.isPopupMenu()) { if (onRightClick) onRightClick (e); }
            else if (onClick) onClick();
        }

    private:
        juce::String text;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Mutate: a button with an amount you drag up / down, right-click chooses which parts change.
    class MutateButton : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit MutateButton (TordenProcessor& p) : proc (p)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Mutate: random variation of the sound. Drag up / down for the amount, right-click for the parts");
            for (int i = 0; i < Mutate::NumParts; ++i)
                enabled[(size_t) i] = Theme::get().getFlag (juce::String ("mutate_") + Mutate::partName (i), true);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover ? p.textDim : p.line);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            // Amount as a fill from the left
            juce::Path clip;
            clip.addRoundedRectangle (r, r.getHeight() * 0.5f);
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (clip);
                g.setColour (p.accentSoft);
                g.fillRect (r.withWidth (r.getWidth() * amount));
                if (flash > 0.0f)
                {
                    g.setColour (p.accent.withAlpha (flash * 0.35f));
                    g.fillRect (r);
                }
            }
            g.setColour (p.text);
            drawSpacedText (g, "MUTATE " + juce::String (juce::roundToInt (amount * 100.0f)) + "%", r, 10.0f,
                            juce::Justification::centred, 0.16f, true);
        }

        void tick (float dt) { if (flash > 0.0f) { flash = std::max (0.0f, flash - dt / 0.4f); repaint(); } }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override { startAmount = amount; if (e.mods.isPopupMenu()) showMenu(); }
        void mouseDrag (const juce::MouseEvent& e) override
        {
            amount = juce::jlimit (0.05f, 1.0f, startAmount - (float) e.getDistanceFromDragStartY() / 120.0f);
            repaint();
        }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu() || e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
            Mutate::apply (proc, amount, enabled, juce::Time::currentTimeMillis());
            proc.triggerHit (0.9f);
            flash = 1.0f;
            repaint();
        }

    private:
        void showMenu()
        {
            juce::PopupMenu m;
            m.addSectionHeader ("Parts that mutate");
            for (int i = 0; i < Mutate::NumParts; ++i)
                m.addItem (i + 1, Mutate::partName (i), true, enabled[(size_t) i]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r < 1) return;
                enabled[(size_t) r - 1] = ! enabled[(size_t) r - 1];
                Theme::get().setFlag (juce::String ("mutate_") + Mutate::partName (r - 1), enabled[(size_t) r - 1]);
            });
        }

        TordenProcessor& proc;
        std::array<bool, Mutate::NumParts> enabled {};
        float amount = 0.3f, startAmount = 0.3f, flash = 0.0f;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Export: drag the rendered kick (with effects) straight into the DAW, or click to save a WAV.
    class ExportTile : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        ExportTile (TordenProcessor& p, KickPreview& kp) : proc (p), preview (kp)
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            setTooltip ("Drag the kick into your DAW as a WAV file (click: save as ...)");
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover ? p.panel.brighter (dark ? 0.05f : 0.0f) : p.panel);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (hover ? p.accent : p.line);
            g.drawRoundedRectangle (r, 8.0f, 1.0f);

            // Mini waveform of the rendered kick
            auto wave = r.reduced (10.0f, 0.0f).withTrimmedTop (8.0f).withHeight (r.getHeight() * 0.5f);
            const auto& res = preview.result();
            if (! res.outL.empty())
            {
                juce::Path path;
                const int n = (int) wave.getWidth();
                const int len = std::min (res.length(), (int) (std::max (0.1f, preview.kickDuration()) * res.sampleRate));
                float maxV = 0.01f;
                std::vector<float> cols ((size_t) n);
                for (int x = 0; x < n; ++x)
                {
                    float m = 0.0f;
                    for (int s = x * len / n; s < (x + 1) * len / n; ++s) m = std::max (m, std::abs (res.outL[(size_t) s]));
                    cols[(size_t) x] = m;
                    maxV = std::max (maxV, m);
                }
                path.startNewSubPath (wave.getX(), wave.getCentreY());
                for (int x = 0; x < n; ++x) path.lineTo (wave.getX() + (float) x, wave.getCentreY() - cols[(size_t) x] / maxV * wave.getHeight() * 0.5f);
                for (int x = n - 1; x >= 0; --x) path.lineTo (wave.getX() + (float) x, wave.getCentreY() + cols[(size_t) x] / maxV * wave.getHeight() * 0.5f);
                path.closeSubPath();
                g.setColour (p.accent.withAlpha (hover ? 0.8f : 0.55f));
                g.fillPath (path);
            }
            g.setColour (hover ? p.text : p.textDim);
            drawSpacedText (g, utf8 ("DRAG WAV"), r.withTrimmedTop (r.getHeight() * 0.58f), 9.5f, juce::Justification::centred, 0.22f, hover);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragging || e.getDistanceFromDragStart() < 6) return;
            dragging = true;
            const auto file = writeTempFile();
            if (file.existsAsFile())
                juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this,
                                                                           [this] { dragging = false; });
            else
                dragging = false;
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (e.mouseWasDraggedSinceMouseDown() || ! getLocalBounds().contains (e.getPosition())) return;
            chooser = std::make_unique<juce::FileChooser> ("Save kick as WAV",
                juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile (fileName()), "*.wav");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      const auto f = fc.getResult();
                                      if (f != juce::File()) writeWav (f.withFileExtension ("wav"));
                                  });
        }

        juce::String fileName() const
        {
            auto name = proc.presetManager.getCurrentName();
            return juce::File::createLegalFileName ("Torden " + name) + ".wav";
        }

    private:
        juce::File writeTempFile()
        {
            auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Torden");
            folder.createDirectory();
            auto file = folder.getChildFile (fileName());
            writeWav (file);
            return file;
        }

        // Renders at the host sample rate (48 kHz without a host), 24 bit, the kick plus its effect tail
        void writeWav (const juce::File& file)
        {
            dsp::OfflineRenderer renderer;
            dsp::HitInfo hit;
            const auto values = proc.currentValues();
            hit.note = (float) values.i (params::RootNote);
            const double sr = proc.getCurrentSampleRate() > 1000.0 ? proc.getCurrentSampleRate() : 48000.0;
            const bool tails = values.b (params::FxVerbOn) || values.b (params::FxDlyOn) || values.b (params::FxHarmOn);
            const auto res = renderer.render (values, proc.sound.envelopes(), proc.getFxOrder(), sr,
                                              preview.kickDuration() + (tails ? 1.5f : 0.05f), true, hit, proc.getBpm());
            // Trim trailing silence and fade the last 5 ms
            int end = res.length();
            while (end > 1 && std::abs (res.outL[(size_t) end - 1]) < 1.0e-5f && std::abs (res.outR[(size_t) end - 1]) < 1.0e-5f) --end;
            juce::AudioBuffer<float> buf (2, std::max (1, end));
            buf.copyFrom (0, 0, res.outL.data(), buf.getNumSamples());
            buf.copyFrom (1, 0, res.outR.data(), buf.getNumSamples());
            const int fade = std::min (buf.getNumSamples(), (int) (0.005 * sr));
            buf.applyGainRamp (buf.getNumSamples() - fade, fade, 1.0f, 0.0f);

            file.deleteFile();
            juce::WavAudioFormat wav;
            if (auto stream = file.createOutputStream())
                if (auto writer = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), sr, 2, 24, {}, 0)))
                {
                    stream.release();
                    writer->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                }
        }

        TordenProcessor& proc;
        KickPreview& preview;
        std::unique_ptr<juce::FileChooser> chooser;
        bool hover = false, dragging = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Phase scope (Lissajous, mid up / side across) with phosphor afterglow and a correlation meter.
    class PhaseScope : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit PhaseScope (TordenProcessor& p) : proc (p) { setTooltip ("Stereo image (vertical = mono) and correlation"); }

        void tick()
        {
            constexpr int n = 1024;
            std::array<float, n> l {}, r {};
            proc.scope.copyLatest (l.data(), r.data(), n);
            float num = 0.0f, el = 0.0f, er = 0.0f, peak = 0.0f;
            for (int i = 0; i < n; ++i)
            {
                num += l[(size_t) i] * r[(size_t) i]; el += l[(size_t) i] * l[(size_t) i]; er += r[(size_t) i] * r[(size_t) i];
                peak = std::max (peak, std::max (std::abs (l[(size_t) i]), std::abs (r[(size_t) i])));
            }
            const float corr = el > 1.0e-8f && er > 1.0e-8f ? num / std::sqrt (el * er) : 1.0f;
            correlation += (corr - correlation) * 0.2f;
            gain += (0.9f / std::max (0.1f, peak) - gain) * 0.1f;

            juce::Path trace;
            for (int i = 0; i < n; i += 2)
            {
                const float mid = (l[(size_t) i] + r[(size_t) i]) * 0.5f * gain, side = (l[(size_t) i] - r[(size_t) i]) * 0.5f * gain;
                const juce::Point<float> pt (side, -mid);
                if (i == 0) trace.startNewSubPath (pt); else trace.lineTo (pt);
            }
            history.push_back (trace);
            if (history.size() > 6) history.erase (history.begin());
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 8.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 8.0f, 1.0f);

            auto meter = r.removeFromBottom (12.0f).reduced (10.0f, 3.0f);
            auto area = r.reduced (8.0f);
            const float size = std::min (area.getWidth(), area.getHeight());
            area = area.withSizeKeepingCentre (size, size);
            g.setColour (p.line.withAlpha (0.7f));
            g.drawLine (area.getCentreX(), area.getY(), area.getCentreX(), area.getBottom(), 0.8f);
            g.drawLine (area.getX(), area.getCentreY(), area.getRight(), area.getCentreY(), 0.8f);

            const auto t = juce::AffineTransform::scale (size * 0.5f).translated (area.getCentre());
            for (size_t i = 0; i < history.size(); ++i)
            {
                const float age = (float) (history.size() - 1 - i);
                g.setColour (p.accent.withAlpha (i + 1 == history.size() ? 0.85f : 0.3f / (1.0f + age)));
                g.strokePath (history[i], juce::PathStrokeType (i + 1 == history.size() ? 1.1f : 0.8f), t);
            }

            g.setColour (p.track);
            g.fillRoundedRectangle (meter, 2.0f);
            const float x = meter.getCentreX() + juce::jlimit (-1.0f, 1.0f, correlation) * meter.getWidth() * 0.5f;
            g.setColour (correlation < 0.0f ? juce::Colour (0xffe0703a) : p.accent);
            g.fillRoundedRectangle (juce::Rectangle<float> (std::min (x, meter.getCentreX()), meter.getY(),
                                                            std::abs (x - meter.getCentreX()) + 1.0f, meter.getHeight()), 2.0f);
        }

    private:
        TordenProcessor& proc;
        std::vector<juce::Path> history;
        float correlation = 1.0f, gain = 1.0f;
    };

    // ---------------------------------------------------------------------------------------------
    class Footer : public juce::Component, private juce::Timer
    {
    public:
        Footer (TordenProcessor& p, KickPreview& kp)
            : proc (p), pad (p), keyboard (p.keyboardState), mutate (p), exportTile (p, kp), scope (p),
              a ("A"), b ("B"), copy (utf8 ("A \xe2\x86\x92 B"))
        {
            addAndMakeVisible (pad);
            addAndMakeVisible (a);
            addAndMakeVisible (b);
            addAndMakeVisible (copy);
            addAndMakeVisible (mutate);
            keyboard.setAvailableRange (24, 71);
            keyboard.setLowestVisibleKey (24);
            keyboard.setScrollButtonsVisible (false);
            addAndMakeVisible (keyboard);
            addAndMakeVisible (exportTile);
            addAndMakeVisible (scope);

            a.setTooltip ("Sound A (compare two versions)");
            b.setTooltip ("Sound B");
            copy.setTooltip ("Copy the sound you hear to the other slot");
            a.onClick = [this] { switchTo (0); };
            b.onClick = [this] { switchTo (1); };
            copy.onClick = [this]
            {
                const int other = 1 - proc.getActiveSnapshot();
                proc.storeSnapshot (other);
                copy.setText (proc.getActiveSnapshot() == 0 ? utf8 ("A \xe2\x86\x92 B") : utf8 ("B \xe2\x86\x92 A"));
            };
            updateAB();
            startTimerHz (30);
        }

        void tick()
        {
            const float dt = 1.0f / 30.0f;
            pad.tick (dt);
            mutate.tick (dt);
            scope.tick();
        }

        void resized() override
        {
            auto r = getLocalBounds();
            pad.setBounds (r.removeFromLeft (128));
            r.removeFromLeft (12);
            auto col = r.removeFromLeft (150);
            auto top = col.removeFromTop (col.getHeight() / 2).reduced (0, 6);
            a.setBounds (top.removeFromLeft (36));
            top.removeFromLeft (4);
            b.setBounds (top.removeFromLeft (36));
            top.removeFromLeft (4);
            copy.setBounds (top);
            mutate.setBounds (col.reduced (0, 6));
            r.removeFromLeft (12);
            scope.setBounds (r.removeFromRight (84));
            r.removeFromRight (10);
            exportTile.setBounds (r.removeFromRight (112));
            r.removeFromRight (12);
            keyboard.setBounds (r);
            keyboard.setKeyWidth ((float) keyboard.getWidth() / 28.0f);   // C0 - B3
        }

    private:
        void switchTo (int slot)
        {
            if (slot == proc.getActiveSnapshot()) return;
            proc.storeSnapshot (proc.getActiveSnapshot());
            if (proc.hasSnapshot (slot)) proc.recallSnapshot (slot);
            else                         proc.storeSnapshot (slot);
            proc.setActiveSnapshot (slot);
            proc.triggerHit (0.9f);
            updateAB();
        }

        void updateAB()
        {
            a.active = proc.getActiveSnapshot() == 0;
            b.active = proc.getActiveSnapshot() == 1;
            copy.setText (a.active ? utf8 ("A \xe2\x86\x92 B") : utf8 ("B \xe2\x86\x92 A"));
            a.repaint();
            b.repaint();
        }

        void timerCallback() override { tick(); }

        TordenProcessor& proc;
        HitPad pad;
        Keyboard keyboard;
        MutateButton mutate;
        ExportTile exportTile;
        PhaseScope scope;
        SmallButton a, b, copy;
    };
}
