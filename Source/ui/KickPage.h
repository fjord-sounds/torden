#pragma once
#include "LayerControls.h"
#include "KickTimeline.h"
#include "visuals/KickVisual.h"

namespace torden::ui
{
    // Tuning readout: the note the kick ends on (where the ear hears its pitch) and the sweep.
    class TuningDisplay : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit TuningDisplay (KickPreview& kp) : preview (kp) { preview.addChangeListener (this); }
        ~TuningDisplay() override { preview.removeChangeListener (this); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const float hz = preview.getBaseHz();
            const float note = dsp::hzToNote (hz);
            const int nearest = juce::roundToInt (note);
            const int cents = juce::roundToInt ((note - (float) nearest) * 100.0f);
            auto r = getLocalBounds().toFloat();

            g.setColour (p.text);
            g.setFont (font (26.0f).withExtraKerningFactor (0.02f));
            const auto name = params::noteName (nearest);
            const float nameW = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), name);
            g.drawText (name, r.withWidth (nameW + 4.0f), juce::Justification::centredLeft);

            auto info = r.withTrimmedLeft (nameW + 14.0f);
            g.setColour (p.textDim);
            g.setFont (font (12.0f, Weight::Medium));
            const float startHz = hz * std::exp2 (preview.values()[params::BodyPitch] / 12.0f);
            g.drawText (juce::String (hz, 1) + " Hz   " + (cents >= 0 ? "+" : "") + juce::String (cents) + " ct",
                        info.removeFromTop (r.getHeight() * 0.5f), juce::Justification::bottomLeft);
            g.drawText (utf8 ("sweep ") + (startHz >= 1000.0f ? juce::String (startHz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (startHz)))
                        + utf8 (" \xe2\x86\x92 ") + juce::String (juce::roundToInt (hz)) + " Hz",
                        info, juce::Justification::topLeft);

            // Cents meter: a small bar, centre = in tune
            auto meter = r.removeFromRight (64.0f).withSizeKeepingCentre (64.0f, 6.0f);
            g.setColour (p.track);
            g.fillRoundedRectangle (meter, 3.0f);
            const float x = meter.getCentreX() + (float) cents / 50.0f * meter.getWidth() * 0.5f;
            g.setColour (std::abs (cents) <= 5 ? p.accent : p.textDim);
            g.fillEllipse (juce::Rectangle<float> (8.0f, 8.0f).withCentre ({ x, meter.getCentreY() }));
            g.setColour (p.line);
            g.fillRect (meter.getCentreX() - 0.5f, meter.getY() - 4.0f, 1.0f, meter.getHeight() + 8.0f);
        }

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        KickPreview& preview;
    };

    // ---------------------------------------------------------------------------------------------
    // Selected layer: chips for all four layers on top, the controls of the selected one below.
    class LayerPanel : public juce::Component
    {
    public:
        explicit LayerPanel (TordenProcessor& p)
        {
            for (int l = 0; l < 4; ++l)
            {
                chips[(size_t) l] = std::make_unique<LayerChip> (p, l);
                chips[(size_t) l]->onSelect = [this] (int layer) { select (layer); if (onSelect) onSelect (layer); };
                addAndMakeVisible (*chips[(size_t) l]);
                rows[(size_t) l] = buildLayerRows (l, p.apvts);
                rows[(size_t) l].character.addTo (*this);
                rows[(size_t) l].common.addTo (*this);
            }
            select (0);
        }

        std::function<void (int)> onSelect;

        void select (int layer)
        {
            current = layer;
            for (int l = 0; l < 4; ++l)
            {
                chips[(size_t) l]->selected = l == layer;
                chips[(size_t) l]->repaint();
                rows[(size_t) l].character.setVisible (l == layer);
                rows[(size_t) l].common.setVisible (l == layer);
            }
            resized();
            repaint();
        }

        int getSelected() const { return current; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            // Divider between character and mixer rows
            g.setColour (p.line.withAlpha (0.6f));
            g.fillRect (16.0f, dividerY, r.getWidth() - 32.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (12, 10);
            auto chipRow = r.removeFromTop (28);
            for (auto& c : chips)
            {
                c->setBounds (chipRow.removeFromLeft (128));
                chipRow.removeFromLeft (8);
            }
            r.removeFromTop (8);
            const int rowH = (r.getHeight() - 8) / 2;
            auto& rr = rows[(size_t) current];
            rr.character.layout (r.removeFromTop (rowH));
            dividerY = (float) r.getY() + 3.0f;
            r.removeFromTop (8);
            rr.common.layout (r.removeFromTop (rowH));
        }

    private:
        std::array<std::unique_ptr<LayerChip>, 4> chips;
        std::array<LayerRows, 4> rows;
        int current = 0;
        float dividerY = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Global: pitch mode, tuning, length, retrigger and velocity response.
    class GlobalPanel : public Section
    {
    public:
        GlobalPanel (TordenProcessor& p, KickPreview& kp)
            : Section ("Kick"), tuning (kp),
              pitchMode (p.apvts, params::paramId (params::PitchMode)),
              retrigger (p.apvts, params::paramId (params::Retrigger)),
              root (p.apvts, params::paramId (params::RootNote), "Root"),
              tune (p.apvts, params::paramId (params::Tune), "Tune"),
              fine (p.apvts, params::paramId (params::Fine), "Fine"),
              length (p.apvts, params::paramId (params::Length), "Length"),
              velLevel (p.apvts, params::paramId (params::VelLevel), "Vel Level"),
              velPitch (p.apvts, params::paramId (params::VelPitch), "Vel Pitch"),
              velClick (p.apvts, params::paramId (params::VelClick), "Vel Click"),
              driveEnv (p.apvts, params::paramId (params::DriveEnv), "Drive Env")
        {
            for (auto* c : std::initializer_list<juce::Component*> { &tuning, &pitchMode, &retrigger, &root, &tune, &fine, &length,
                                                                      &velLevel, &velPitch, &velClick, &driveEnv })
                addAndMakeVisible (c);
            for (auto* k : { &root, &tune, &fine, &length, &velLevel, &velPitch, &velClick, &driveEnv })
                k->setLabelHeight (14);
        }

        void resized() override
        {
            auto r = content();
            tuning.setBounds (r.removeFromTop (40).reduced (4, 0));
            r.removeFromTop (6);
            auto seg = r.removeFromTop (24);
            pitchMode.setBounds (seg.removeFromLeft (seg.getWidth() / 2 - 4));
            seg.removeFromLeft (8);
            retrigger.setBounds (seg);
            r.removeFromTop (6);
            const int h = r.getHeight() / 2;
            layoutRow (r.removeFromTop (h), { &root, &tune, &fine, &length });
            layoutRow (r, { &velLevel, &velPitch, &velClick, &driveEnv });
        }

    private:
        TuningDisplay tuning;
        Segmented pitchMode, retrigger;
        Knob root, tune, fine, length, velLevel, velPitch, velClick, driveEnv;
    };

    // ---------------------------------------------------------------------------------------------
    class KickPage : public juce::Component
    {
    public:
        KickPage (TordenProcessor& p, KickPreview& kp)
            : timeline (p, kp), visual (p, kp), layers (p), global (p, kp)
        {
            addAndMakeVisible (timeline);
            addAndMakeVisible (visual);
            addAndMakeVisible (layers);
            addAndMakeVisible (global);

            // Timeline and layer panel follow each other
            timeline.onSelect = [this] (dsp::Env e)
            {
                if (e != dsp::Env::Pitch && e != dsp::Env::Drive) layers.select (envLayer (e));
            };
            layers.onSelect = [this] (int layer)
            {
                static const dsp::Env envs[] = { dsp::Env::BodyAmp, dsp::Env::ClickAmp, dsp::Env::SubAmp, dsp::Env::NoiseAmp };
                if (envLayer (timeline.getSelected()) != layer || timeline.getSelected() == dsp::Env::Pitch || timeline.getSelected() == dsp::Env::Drive)
                    timeline.selectEnv (envs[layer]);
            };
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto top = r.removeFromTop (296);
            visual.setBounds (top.removeFromRight (362));
            top.removeFromRight (12);
            timeline.setBounds (top);
            r.removeFromTop (12);
            global.setBounds (r.removeFromRight (362));
            r.removeFromRight (12);
            layers.setBounds (r);
        }

        void tick() { timeline.tick(); visual.tick(); }
        KickVisual& getVisual() { return visual; }

    private:
        KickTimeline timeline;
        KickVisual visual;
        LayerPanel layers;
        GlobalPanel global;
    };
}
