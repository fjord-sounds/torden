#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"

namespace torden::ui
{
    using APVTS = juce::AudioProcessorValueTreeState;

    // Slider that leaves horizontal wheel/trackpad swipes to its parents, so scrollable strips keep scrolling over knobs.
    class KnobSlider : public juce::Slider
    {
    public:
        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            if (std::abs (w.deltaX) > std::abs (w.deltaY))
                juce::Component::mouseWheelMove (e, w);   // passes the event on to the parent
            else
                juce::Slider::mouseWheelMove (e, w);
        }
    };

    // Rotary knob with its name below; hovering or dragging shows the value.
    class Knob : public juce::Component
    {
    public:
        Knob (APVTS& state, const juce::String& paramId, const juce::String& labelText)
            : name (labelText)
        {
            slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            slider.setMouseDragSensitivity (220);
            slider.setVelocityBasedMode (false);
            addAndMakeVisible (slider);

            auto* param = state.getParameter (paramId);
            jassert (param != nullptr);
            attachment = std::make_unique<juce::SliderParameterAttachment> (*param, slider, nullptr);
            slider.setDoubleClickReturnValue (true, param->convertFrom0to1 (param->getDefaultValue()));
            slider.setTooltip (param->getName (64));

            slider.onValueChange = [this] { repaint(); };
            slider.onDragStart = [this] { dragging = true; repaint(); };
            slider.onDragEnd   = [this] { dragging = false; repaint(); };
            slider.addMouseListener (this, false);
        }

        void mouseEnter (const juce::MouseEvent&) override { hovering = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hovering = false; repaint(); }

        void setLabel (const juce::String& text)
        {
            if (text != name) { name = text; repaint(); }
        }

        void setLabelHeight (int h) { labelHeight = h; resized(); }

        void resized() override
        {
            auto r = getLocalBounds();
            r.removeFromBottom (labelHeight);
            const int size = std::min (r.getWidth(), r.getHeight());
            slider.setBounds (r.withSizeKeepingCentre (size, size));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool showValue = hovering || dragging;
            g.setColour (showValue ? p.text : p.textDim);
            const auto text = showValue ? slider.getTextFromValue (slider.getValue()) : name.toUpperCase();
            drawSpacedText (g, text, getLocalBounds().removeFromBottom (labelHeight).toFloat(),
                            labelHeight > 18 ? 12.0f : 10.5f, juce::Justification::centred, showValue ? 0.02f : 0.12f);
        }

        int labelHeight = 16;

    private:
        juce::String name;
        KnobSlider slider;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        bool hovering = false, dragging = false;
    };

    // Selector bar for choice parameters: text or small waveform glyphs.
    class Segmented : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        using GlyphFn = std::function<juce::Path (int index, juce::Rectangle<float> area)>;

        Segmented (APVTS& state, const juce::String& paramId, GlyphFn glyphs = nullptr, juce::StringArray labels = {})
            : glyph (std::move (glyphs))
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
            jassert (param != nullptr);
            items = labels.isEmpty() ? param->choices : labels;
            attachment = std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float v) { selected = juce::roundToInt (v); repaint(); }, nullptr);
            attachment->sendInitialUpdate();
            setTooltip (param->getName (64));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const float w = (float) getWidth() / (float) items.size();

            for (int i = 0; i < items.size(); ++i)
            {
                auto cell = juce::Rectangle<float> (w * (float) i, 0.0f, w, (float) getHeight()).reduced (2.0f, 1.0f);
                const bool on = i == selected;

                if (on)
                {
                    g.setColour (p.accentSoft);
                    g.fillRoundedRectangle (cell, 3.0f);
                }

                g.setColour (on ? p.accent : (i == hovered ? p.text : p.textDim));

                if (glyph != nullptr)
                    g.strokePath (glyph (i, cell.reduced (cell.getWidth() * 0.2f, cell.getHeight() * 0.28f)),
                                  juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                else
                    drawSpacedText (g, items[i].toUpperCase(), cell, 10.5f, juce::Justification::centred, 0.08f, on);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override { setHovered (indexAt (e.x)); }
        void mouseExit (const juce::MouseEvent&) override   { setHovered (-1); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            attachment->setValueAsCompleteGesture ((float) indexAt (e.x));
        }

    private:
        int indexAt (int x) const
        {
            return std::clamp (x * items.size() / std::max (1, getWidth()), 0, items.size() - 1);
        }

        void setHovered (int i)
        {
            if (hovered != i) { hovered = i; repaint(); }
        }

        GlyphFn glyph;
        juce::StringArray items;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int selected = 0, hovered = -1;
    };

    // Glyphs for oscillator waveforms: sine, triangle, saw, square, wavetable
    inline juce::Path waveGlyph (int index, juce::Rectangle<float> r)
    {
        juce::Path path;
        const float l = r.getX(), rt = r.getRight(), t = r.getY(), b = r.getBottom(), m = r.getCentreY();
        const float w = r.getWidth();

        auto sinePath = [&] (float x0, float y0, float width, float amp)
        {
            path.startNewSubPath (x0, y0);
            for (int i = 1; i <= 24; ++i)
            {
                const float x = (float) i / 24.0f;
                path.lineTo (x0 + x * width, y0 - std::sin (x * juce::MathConstants<float>::twoPi) * amp);
            }
        };

        switch (index)
        {
            case 0: sinePath (l, m, w, m - t); break;
            case 1:
                path.startNewSubPath (l, m);
                path.lineTo (l + w * 0.25f, t);
                path.lineTo (l + w * 0.75f, b);
                path.lineTo (rt, m);
                break;
            case 2:
                path.startNewSubPath (l, b);
                path.lineTo (l + w * 0.5f, t);
                path.lineTo (l + w * 0.5f, b);
                path.lineTo (rt, t);
                break;
            case 3:
                path.startNewSubPath (l, b);
                path.lineTo (l, t);
                path.lineTo (l + w * 0.5f, t);
                path.lineTo (l + w * 0.5f, b);
                path.lineTo (rt, b);
                path.lineTo (rt, t);
                break;
            default:   // Wavetable: staggered waves
            {
                const float h = r.getHeight();
                for (int k = 0; k < 3; ++k)
                    sinePath (l + (float) k * w * 0.12f, t + h * (0.3f + 0.25f * (float) k), w * 0.76f, h * 0.22f);
                break;
            }
        }
        return path;
    }

    // Glyphs for LFO shapes
    inline juce::Path lfoGlyph (int index, juce::Rectangle<float> r)
    {
        if (index < 2) return waveGlyph (index, r);
        juce::Path path;
        const float l = r.getX(), rt = r.getRight(), t = r.getY(), b = r.getBottom(), w = r.getWidth();
        switch (index)
        {
            case 2: path.startNewSubPath (l, b); path.lineTo (rt, t); path.lineTo (rt, b); break;   // Saw Up
            case 3: path.startNewSubPath (l, b); path.lineTo (l, t); path.lineTo (rt, b); break;    // Saw Down
            case 4: return waveGlyph (3, r);
            case 5:   // Sample & hold: steps
            {
                const float ys[] = { 0.6f, 0.15f, 0.85f, 0.35f };
                path.startNewSubPath (l, t + ys[0] * r.getHeight());
                for (int i = 0; i < 4; ++i)
                {
                    const float y = t + ys[i] * r.getHeight();
                    path.lineTo (l + w * (float) i / 4.0f, y);
                    path.lineTo (l + w * (float) (i + 1) / 4.0f, y);
                }
                break;
            }
            default:  // Smooth Random
            {
                path.startNewSubPath (l, r.getCentreY());
                path.cubicTo (l + w * 0.2f, t, l + w * 0.35f, t, l + w * 0.5f, r.getCentreY());
                path.cubicTo (l + w * 0.65f, b, l + w * 0.8f, b + 0.0f, rt, t + r.getHeight() * 0.3f);
                break;
            }
        }
        return path;
    }

    // Small round switch (on/off), e.g. for oscillators.
    class Toggle : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        Toggle (APVTS& state, const juce::String& paramId, juce::String labelText = {})
            : label (std::move (labelText))
        {
            auto* param = state.getParameter (paramId);
            jassert (param != nullptr);
            attachment = std::make_unique<juce::ParameterAttachment> (
                *param, [this] (float v) { on = v > 0.5f; repaint(); if (onChange) onChange (on); }, nullptr);
            attachment->sendInitialUpdate();
            setTooltip (param->getName (64));
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat();
            const float d = std::min (12.0f, r.getHeight() - 2.0f);
            auto dot = r.removeFromLeft (d + 2.0f).withSizeKeepingCentre (d, d);

            g.setColour (on ? p.accent : p.line);
            if (on) g.fillEllipse (dot);
            else    g.drawEllipse (dot.reduced (0.5f), 1.2f);

            if (label.isNotEmpty())
            {
                g.setColour (on ? p.text : p.textDim);
                drawSpacedText (g, label.toUpperCase(), r.withTrimmedLeft (5.0f), 10.5f, juce::Justification::centredLeft, 0.1f);
            }
        }

        void mouseDown (const juce::MouseEvent&) override { attachment->setValueAsCompleteGesture (on ? 0.0f : 1.0f); }

        bool isOn() const { return on; }
        std::function<void (bool)> onChange;

    private:
        juce::String label;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        bool on = false;
    };

    // Dropdown for longer choice lists.
    class Choice : public juce::Component
    {
    public:
        Choice (APVTS& state, const juce::String& paramId)
        {
            auto* param = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
            jassert (param != nullptr);
            box.addItemList (param->choices, 1);
            addAndMakeVisible (box);
            attachment = std::make_unique<juce::ComboBoxParameterAttachment> (*param, box);
        }

        void resized() override { box.setBounds (getLocalBounds()); }

    private:
        juce::ComboBox box;
        std::unique_ptr<juce::ComboBoxParameterAttachment> attachment;
    };

    // Calm panel with an all-caps title and a fine outline.
    class Section : public juce::Component
    {
    public:
        explicit Section (juce::String t) : title (std::move (t)) {}

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            g.setColour (p.text);
            drawSpacedText (g, title.toUpperCase(), r.reduced (16.0f, 0.0f).removeFromTop (34.0f),
                            11.0f, juce::Justification::centredLeft, 0.25f, true);
        }

        // Content area below the title
        juce::Rectangle<int> content() const { return getLocalBounds().reduced (12, 0).withTrimmedTop (34).withTrimmedBottom (10); }

        juce::Rectangle<int> titleArea() const { return getLocalBounds().reduced (16, 0).removeFromTop (34); }


    private:
        juce::String title;
    };

    // Padlock (closed / open), used for the lock switches.
    inline void drawPadlock (juce::Graphics& g, juce::Rectangle<float> area, bool closed, juce::Colour c)
    {
        const auto r = area.withSizeKeepingCentre (10.0f, 12.0f);
        const auto body = r.withTrimmedTop (5.0f);
        juce::Path shackle;
        const float sx = body.getX() + 2.0f, sw = body.getWidth() - 4.0f, top = r.getY();
        shackle.startNewSubPath (sx, body.getY());
        shackle.lineTo (sx, top + sw * 0.5f);
        shackle.addCentredArc (sx + sw * 0.5f, top + sw * 0.5f, sw * 0.5f, sw * 0.5f, 0.0f,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi);
        shackle.lineTo (sx + sw, closed ? body.getY() : top + sw * 0.5f + 1.5f);
        g.setColour (c);
        g.strokePath (shackle, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        if (closed) g.fillRoundedRectangle (body, 1.5f);
        else        g.drawRoundedRectangle (body.reduced (0.5f), 1.5f, 1.3f);
    }

    // Lays out components evenly in a row.
    inline void layoutRow (juce::Rectangle<int> area, std::initializer_list<juce::Component*> comps)
    {
        const int n = (int) comps.size();
        const int w = area.getWidth() / std::max (1, n);
        for (auto* c : comps)
            if (c != nullptr)
                c->setBounds (area.removeFromLeft (w));
            else
                area.removeFromLeft (w);
    }
}
