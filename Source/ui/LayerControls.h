#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace torden::ui
{
    inline const juce::StringArray& layerNames()
    {
        static const juce::StringArray names { "Body", "Click", "Sub", "Noise" };
        return names;
    }

    inline std::unique_ptr<Knob> makeKnob (APVTS& s, params::Id id, const juce::String& label)
    {
        return std::make_unique<Knob> (s, params::paramId (id), label);
    }

    // Small label above a selector (segmented / dropdown / toggle)
    class Labelled : public juce::Component
    {
    public:
        Labelled (std::unique_ptr<juce::Component> c, juce::String t) : content (std::move (c)), title (std::move (t))
        {
            addAndMakeVisible (*content);
        }

        void paint (juce::Graphics& g) override
        {
            g.setColour (Theme::get().palette().textDim);
            drawSpacedText (g, title.toUpperCase(), getLocalBounds().removeFromTop (14).toFloat(), 9.5f,
                            juce::Justification::centredLeft, 0.16f);
        }

        void resized() override { content->setBounds (getLocalBounds().withTrimmedTop (16).withHeight (24)); }

        juce::Component& get() { return *content; }

    private:
        std::unique_ptr<juce::Component> content;
        juce::String title;
    };

    // A row of controls: each entry has a relative width (knobs 1, wide selectors 2-4).
    struct ControlRow
    {
        std::vector<std::pair<std::unique_ptr<juce::Component>, float>> items;

        void add (std::unique_ptr<juce::Component> c, float units = 1.0f) { items.emplace_back (std::move (c), units); }

        void addTo (juce::Component& parent)
        {
            for (auto& [c, u] : items) parent.addAndMakeVisible (*c);
        }

        void setVisible (bool v) { for (auto& [c, u] : items) c->setVisible (v); }

        // Selectors get a fixed height and sit centred in their cell
        void layout (juce::Rectangle<int> area, int gap = 4)
        {
            float total = 0.0f;
            for (auto& [c, u] : items) total += u;
            const float unit = (float) (area.getWidth() - gap * ((int) items.size() - 1)) / std::max (1.0f, total);
            float x = (float) area.getX();
            for (auto& [c, u] : items)
            {
                const int w = (int) (unit * u);
                juce::Rectangle<int> cell ((int) x, area.getY(), w, area.getHeight());
                if (dynamic_cast<Knob*> (c.get()) == nullptr)
                {
                    const int h = dynamic_cast<Labelled*> (c.get()) != nullptr ? 40 : 24;
                    cell = cell.withSizeKeepingCentre (w, h).translated (0, -6);
                }
                c->setBounds (cell);
                x += (float) w + (float) gap;
            }
        }
    };

    inline std::unique_ptr<juce::Component> labelled (std::unique_ptr<juce::Component> c, const juce::String& title)
    {
        return std::make_unique<Labelled> (std::move (c), title);
    }

    // Glyphs for the body shape and sub waves
    inline juce::Path filterGlyph (int index, juce::Rectangle<float> r)
    {
        juce::Path p;
        const float l = r.getX(), rt = r.getRight(), t = r.getY(), b = r.getBottom(), w = r.getWidth();
        switch (index)
        {
            case 0:  p.startNewSubPath (l, t + 2.0f); p.lineTo (l + w * 0.55f, t + 2.0f); p.quadraticTo (l + w * 0.75f, t, rt, b); break;   // low pass
            case 1:  p.startNewSubPath (l, b); p.quadraticTo (l + w * 0.25f, t, l + w * 0.45f, t + 2.0f); p.lineTo (rt, t + 2.0f); break;   // high pass
            case 2:  p.startNewSubPath (l, b); p.quadraticTo (l + w * 0.5f, t - 4.0f, rt, b); break;                                       // band pass
            default: p.startNewSubPath (l, t + 2.0f); p.lineTo (l + w * 0.35f, t + 2.0f); p.quadraticTo (l + w * 0.5f, b + 6.0f, l + w * 0.65f, t + 2.0f); p.lineTo (rt, t + 2.0f); break;   // notch
        }
        return p;
    }

    // The two rows of controls for one layer: its own character on top, the shared mixer / filter below.
    struct LayerRows
    {
        ControlRow character, common;
    };

    inline LayerRows buildLayerRows (int layer, APVTS& s)
    {
        namespace P = params;
        LayerRows rows;
        auto& a = rows.character;
        switch (layer)
        {
            case 0:
                a.add (makeKnob (s, P::BodyTune, "Tune"));
                a.add (makeKnob (s, P::BodyPitch, "Sweep"));
                a.add (makeKnob (s, P::BodyPTime, "Sweep Time"));
                a.add (makeKnob (s, P::BodyShape, "Shape"));
                a.add (makeKnob (s, P::BodyFold, "Fold"));
                a.add (makeKnob (s, P::BodyFm, "FM"));
                a.add (makeKnob (s, P::BodyHarm, "Harmonics"));
                a.add (makeKnob (s, P::BodyPhase, "Phase"));
                break;
            case 1:
                a.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::ClickMode), nullptr,
                                                              juce::StringArray { "Imp", "Beat", "Noise", "FM", "Snap", "Tick" }), "Mode"), 4.6f);
                a.add (makeKnob (s, P::ClickPitch, "Pitch"));
                a.add (makeKnob (s, P::ClickColor, "Color"));
                break;
            case 2:
                a.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::SubWave), waveGlyph), "Wave"), 1.8f);
                a.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::SubMode)), "Pitch"), 1.8f);
                a.add (makeKnob (s, P::SubOctave, "Octave"));
                a.add (makeKnob (s, P::SubHz, "Fixed Hz"));
                a.add (makeKnob (s, P::SubFollow, "Follow"));
                break;
            default:
                a.add (labelled (std::make_unique<Segmented> (s, P::paramId (P::NoiseType), nullptr,
                                                              juce::StringArray { "White", "Pink", "Crackle", "Metal", "Skin" }), "Source"), 4.2f);
                a.add (makeKnob (s, P::NoiseTune, "Tune"));
                a.add (makeKnob (s, P::NoiseRes, "Resonance"));
                a.add (makeKnob (s, P::NoiseFEnv, "Filter Env"));
                break;
        }

        static const P::Id decay[] = { P::BodyDecay, P::ClickDecay, P::SubDecay, P::NoiseDecay };
        static const P::Id drive[] = { P::BodyDrive, P::ClickDrive, P::SubDrive, P::NoiseDrive };
        a.add (makeKnob (s, drive[layer], "Drive"));
        a.add (makeKnob (s, decay[layer], "Decay"));

        static const P::Id ids[4][7] = {
            { P::BodyLevel, P::BodyPan, P::BodyDelay, P::BodyFType, P::BodyCutoff, P::BodyReso, P::BodyInvert },
            { P::ClickLevel, P::ClickPan, P::ClickDelay, P::ClickFType, P::ClickCutoff, P::ClickReso, P::ClickInvert },
            { P::SubLevel, P::SubPan, P::SubDelay, P::SubFType, P::SubCutoff, P::SubReso, P::SubInvert },
            { P::NoiseLevel, P::NoisePan, P::NoiseDelay, P::NoiseFType, P::NoiseCutoff, P::NoiseReso, P::NoiseInvert } };
        auto& b = rows.common;
        b.add (makeKnob (s, ids[layer][0], "Level"));
        b.add (makeKnob (s, ids[layer][1], "Pan"));
        b.add (makeKnob (s, ids[layer][2], "Delay"));
        b.add (labelled (std::make_unique<Segmented> (s, P::paramId (ids[layer][3]), filterGlyph), "Filter"), 2.4f);
        b.add (makeKnob (s, ids[layer][4], "Cutoff"));
        b.add (makeKnob (s, ids[layer][5], "Reso"));
        b.add (labelled (std::make_unique<Toggle> (s, P::paramId (ids[layer][6]), "Flip"), "Phase"), 1.2f);
        return rows;
    }

    // Chip for one layer: dot = on/off, name selects, "S" = solo (not saved)
    class LayerChip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        LayerChip (TordenProcessor& p, int layerIndex) : proc (p), layer (layerIndex)
        {
            static const params::Id ons[] = { params::BodyOn, params::ClickOn, params::SubOn, params::NoiseOn };
            auto* param = p.apvts.getParameter (params::paramId (ons[layer]));
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr);
            attachment->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        std::function<void (int)> onSelect;
        bool selected = false;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const auto col = layerColour (layer, dark);
            if (selected)
            {
                g.setColour (col.withAlpha (dark ? 0.18f : 0.12f));
                g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            }
            g.setColour (selected ? col.withAlpha (0.8f) : (hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);

            const auto dot = powerArea().toFloat().withSizeKeepingCentre (9.0f, 9.0f);
            if (on) { g.setColour (col); g.fillEllipse (dot); }
            else    { g.setColour (p.textDim); g.drawEllipse (dot.reduced (0.5f), 1.2f); }

            g.setColour (on ? (selected ? p.text : p.text.withAlpha (0.85f)) : p.textDim);
            drawSpacedText (g, layerNames()[layer].toUpperCase(), r.withTrimmedLeft (26.0f).withTrimmedRight (26.0f), 10.5f,
                            juce::Justification::centredLeft, 0.18f, selected);

            const bool solo = (proc.getSoloMask() & (1 << layer)) != 0;
            const auto s = soloArea().toFloat().withSizeKeepingCentre (17.0f, 17.0f);
            if (solo) { g.setColour (p.accent); g.fillRoundedRectangle (s, 4.0f); }
            g.setColour (solo ? p.panel : p.textDim);
            g.setFont (font (10.0f, Weight::SemiBold));
            g.drawText ("S", s, juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

        void mouseMove (const juce::MouseEvent& e) override
        {
            setTooltip (powerArea().contains (e.getPosition()) ? "Layer on / off"
                      : soloArea().contains (e.getPosition()) ? "Solo (only for listening, not saved)" : "Show this layer");
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (powerArea().contains (e.getPosition()))
                attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
            else if (soloArea().contains (e.getPosition()))
            {
                proc.setSoloMask (proc.getSoloMask() ^ (1 << layer));
                if (auto* parent = getParentComponent()) parent->repaint();
                repaint();
            }
            else if (onSelect)
                onSelect (layer);
        }

    private:
        juce::Rectangle<int> powerArea() const { return getLocalBounds().removeFromLeft (26); }
        juce::Rectangle<int> soloArea() const  { return getLocalBounds().removeFromRight (26); }

        TordenProcessor& proc;
        int layer;
        bool on = false, hover = false;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };
}
