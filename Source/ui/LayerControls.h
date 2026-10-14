#pragma once
#include "../presets/Locks.h"
#include "Theme.h"
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
    class LayerChip : public juce::Component, public juce::SettableTooltipClient, private juce::Value::Listener
    {
    public:
        LayerChip (TordenProcessor& p, int layerIndex) : proc (p), layer (layerIndex)
        {
            const int lockGroups[] = { locks::ids().body, locks::ids().click, locks::ids().sub, locks::ids().noise };
            lockValue.referTo (p.apvts.state.getPropertyAsValue (locks::set()[lockGroups[layer]].property, nullptr));
            lockValue.addListener (this);
            static const params::Id ons[] = { params::BodyOn, params::ClickOn, params::SubOn, params::NoiseOn };
            auto* param = p.apvts.getParameter (params::paramId (ons[layer]));
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { on = v > 0.5f; repaint(); }, nullptr);
            attachment->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        ~LayerChip() override { lockValue.removeListener (this); }

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
            drawSpacedText (g, layerNames()[layer].toUpperCase(), r.withTrimmedLeft (26.0f).withTrimmedRight (50.0f), 10.5f,
                            juce::Justification::centredLeft, 0.18f, selected);

            const bool locked = isLocked();
            drawPadlock (g, lockArea().toFloat(), locked, locked ? p.accent : (hoverLock ? p.text : p.textDim));

            const bool solo = (proc.getSoloMask() & (1 << layer)) != 0;
            const auto s = soloArea().toFloat().withSizeKeepingCentre (17.0f, 17.0f);
            if (solo) { g.setColour (p.accent); g.fillRoundedRectangle (s, 4.0f); }
            g.setColour (solo ? p.panel : p.textDim);
            g.setFont (font (10.0f, Weight::SemiBold));
            g.drawText ("S", s, juce::Justification::centred);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; hoverLock = false; repaint(); }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const bool overLock = lockArea().contains (e.getPosition());
            if (overLock != hoverLock) { hoverLock = overLock; repaint(); }
            setTooltip (powerArea().contains (e.getPosition()) ? "Layer on / off"
                      : overLock ? "Lock: keep this layer when switching sound presets"
                      : soloArea().contains (e.getPosition()) ? "Solo (only for listening, not saved)" : "Show this layer");
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (powerArea().contains (e.getPosition()))
                attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f);
            else if (lockArea().contains (e.getPosition()))
                lockValue = ! isLocked();
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
        juce::Rectangle<int> lockArea() const  { return getLocalBounds().withTrimmedRight (26).removeFromRight (24); }
        bool isLocked() const { return (bool) lockValue.getValue(); }
        void valueChanged (juce::Value&) override { repaint(); }

        TordenProcessor& proc;
        int layer;
        bool on = false, hover = false, hoverLock = false;
        juce::Value lockValue;
        std::unique_ptr<juce::ParameterAttachment> attachment;
    };
}
