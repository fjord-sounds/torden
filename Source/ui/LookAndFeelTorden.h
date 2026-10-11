#pragma once
#include "Theme.h"

namespace torden::ui
{
    class LookAndFeelTorden : public juce::LookAndFeel_V4
    {
    public:
        LookAndFeelTorden() { applyPalette(); }

        void applyPalette()
        {
            const auto& p = Theme::get().palette();
            setColour (juce::ComboBox::backgroundColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::textColourId, p.text);
            setColour (juce::ComboBox::outlineColourId, p.line);
            setColour (juce::ComboBox::arrowColourId, p.textDim);
            setColour (juce::PopupMenu::backgroundColourId, p.panel);
            setColour (juce::PopupMenu::textColourId, p.text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, p.accentSoft);
            setColour (juce::PopupMenu::highlightedTextColourId, p.text);
            setColour (juce::TooltipWindow::backgroundColourId, p.panel);
            setColour (juce::TooltipWindow::textColourId, p.text);
            setColour (juce::TooltipWindow::outlineColourId, p.line);
        }

        // All standard components (popup menus, dialogs, text fields) use Manrope as well.
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font& f) override
        {
            return typeface (f.isBold() ? Weight::SemiBold : Weight::Regular);
        }

        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos,
                               float startAngle, float endAngle, juce::Slider& slider) override
        {
            const auto& p = Theme::get().palette();
            const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
            const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
            const auto c = bounds.getCentre();
            const float stroke = std::max (2.0f, radius * 0.11f);
            const float arcR = radius - stroke * 0.5f;
            const float angle = startAngle + pos * (endAngle - startAngle);

            // Track
            juce::Path track;
            track.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, startAngle, endAngle, true);
            g.setColour (p.track);
            g.strokePath (track, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            // Value arc – bipolar parameters start in the centre
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            const float from = bipolar ? (startAngle + endAngle) * 0.5f : startAngle;
            if (std::abs (angle - from) > 0.001f)
            {
                juce::Path value;
                value.addCentredArc (c.x, c.y, arcR, arcR, 0.0f, std::min (from, angle), std::max (from, angle), true);
                g.setColour (slider.isEnabled() ? p.accent : p.textDim);
                g.strokePath (value, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            // Knob: calm disc with a fine pointer
            const float bodyR = arcR - stroke * 1.6f;
            g.setColour (p.panel);
            g.fillEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f);
            g.setColour (p.line);
            g.drawEllipse (c.x - bodyR, c.y - bodyR, bodyR * 2.0f, bodyR * 2.0f, 1.0f);

            const auto tip = c.getPointOnCircumference (bodyR * 0.82f, angle);
            const auto base = c.getPointOnCircumference (bodyR * 0.35f, angle);
            g.setColour (p.text);
            g.drawLine ({ base, tip }, std::max (1.5f, stroke * 0.7f));
        }

        // Horizontal bars (mod amounts): a hairline track, the value filled from the centre for bipolar ranges
        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float,
                               juce::Slider::SliderStyle style, juce::Slider& slider) override
        {
            if (style != juce::Slider::LinearHorizontal && style != juce::Slider::LinearBar)
                return LookAndFeel_V4::drawLinearSlider (g, x, y, w, h, pos, 0, 0, style, slider);
            const auto& p = Theme::get().palette();
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h);
            const float cy = r.getCentreY();
            g.setColour (p.track);
            g.fillRoundedRectangle (r.getX(), cy - 2.0f, r.getWidth(), 4.0f, 2.0f);
            const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
            const float from = bipolar ? r.getCentreX() : r.getX();
            g.setColour (slider.isEnabled() ? p.accent : p.textDim);
            g.fillRoundedRectangle (std::min (from, pos), cy - 2.0f, std::abs (pos - from), 4.0f, 2.0f);
            if (bipolar)
            {
                g.setColour (p.textDim);
                g.fillRect (r.getCentreX() - 0.5f, cy - 5.0f, 1.0f, 10.0f);
            }
            g.setColour (p.panel);
            g.fillEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy }));
            g.setColour (p.accent);
            g.drawEllipse (juce::Rectangle<float> (12.0f, 12.0f).withCentre ({ pos, cy }), 1.6f);
        }

        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            const auto& p = Theme::get().palette();
            auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f);
            g.setColour (box.isMouseOver() ? p.textDim : p.line);
            g.drawRoundedRectangle (r, 3.0f, 1.0f);

            const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
            juce::Path arrow;
            arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
            arrow.lineTo (ax, ay + 2.0f);
            arrow.lineTo (ax + 3.5f, ay - 1.5f);
            g.setColour (p.textDim);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }

        void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
        {
            label.setBounds (6, 0, box.getWidth() - 22, box.getHeight());
            label.setFont (font (12.5f));
        }

        juce::Font getComboBoxFont (juce::ComboBox&) override { return font (12.5f); }
        juce::Font getPopupMenuFont() override { return font (13.0f); }

        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical,
                            int thumbStart, int thumbSize, bool isMouseOver, bool isMouseDown) override
        {
            const auto& p = Theme::get().palette();
            auto thumb = vertical ? juce::Rectangle<int> (x, thumbStart, w, thumbSize)
                                  : juce::Rectangle<int> (thumbStart, y, thumbSize, h);
            g.setColour (isMouseOver || isMouseDown ? p.textDim : p.line);
            g.fillRoundedRectangle (thumb.toFloat().reduced (1.0f), 3.0f);
            juce::ignoreUnused (vertical);
        }

        void fillTextEditorBackground (juce::Graphics& g, int w, int h, juce::TextEditor& ed) override
        {
            g.setColour (ed.findColour (juce::TextEditor::backgroundColourId));
            g.fillRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), (float) h * 0.5f);
        }

        void drawTextEditorOutline (juce::Graphics& g, int w, int h, juce::TextEditor& ed) override
        {
            const bool focused = ed.hasKeyboardFocus (true) && ! ed.isReadOnly();
            g.setColour (ed.findColour (focused ? juce::TextEditor::focusedOutlineColourId : juce::TextEditor::outlineColourId));
            g.drawRoundedRectangle (juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (0.5f), (float) h * 0.5f, 1.0f);
        }

        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override
        {
            const auto& p = Theme::get().palette();
            g.fillAll (p.panel);
            g.setColour (p.line);
            g.drawRect (0, 0, w, h);
        }
    };
}
