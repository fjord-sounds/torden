#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include "Theme.h"

namespace torden::ui
{
    // On-screen keyboard: soft keys with rounded fronts, hairline gaps and the accent for held notes.
    class Keyboard : public juce::MidiKeyboardComponent
    {
    public:
        explicit Keyboard (juce::MidiKeyboardState& s) : MidiKeyboardComponent (s, horizontalKeyboard)
        {
            setBlackNoteLengthProportion (0.62f);
            setBlackNoteWidthProportion (0.62f);
            // Background and separators are drawn per key (gaps instead of lines)
            setColour (whiteNoteColourId, juce::Colours::transparentBlack);
            setColour (keySeparatorLineColourId, juce::Colours::transparentBlack);
            setColour (shadowColourId, juce::Colours::transparentBlack);
        }

        void paint (juce::Graphics& g) override
        {
            const auto r = getLocalBounds().toFloat();
            juce::Path frame;
            frame.addRoundedRectangle (r, radius);
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (frame);
                g.setColour (Theme::get().palette().line);   // shows through the gaps
                g.fillPath (frame);
                MidiKeyboardComponent::paint (g);
            }
            g.setColour (Theme::get().palette().line);
            g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
        }

        void drawWhiteNote (int note, juce::Graphics& g, juce::Rectangle<float> area,
                            bool isDown, bool isOver, juce::Colour, juce::Colour) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto key = area.withTrimmedRight (1.0f);
            const auto shape = keyShape (key, 3.5f);

            const auto base = p.keyWhite;
            g.setGradientFill ({ base.darker (dark ? 0.0f : 0.035f).brighter (dark ? 0.05f : 0.0f), key.getX(), key.getY(),
                                 base, key.getX(), key.getBottom(), false });
            g.fillPath (shape);

            if (isDown)
            {
                g.setGradientFill ({ p.accent.withAlpha (dark ? 0.18f : 0.10f), key.getX(), key.getY(),
                                     p.accent.withAlpha (dark ? 0.55f : 0.42f), key.getX(), key.getBottom(), false });
                g.fillPath (shape);
            }
            else if (isOver)
            {
                g.setColour (p.accentSoft);
                g.fillPath (shape);
            }

            // Front edge: a thin accent line when held, a soft shade otherwise
            g.setColour (isDown ? p.accent : p.line.withAlpha (dark ? 0.8f : 0.6f));
            g.fillRoundedRectangle (key.getX() + 2.0f, key.getBottom() - (isDown ? 3.0f : 2.0f),
                                    key.getWidth() - 4.0f, isDown ? 2.0f : 1.0f, 1.0f);

            if (const auto text = getWhiteNoteText (note); text.isNotEmpty())
            {
                g.setColour (isDown ? p.text : p.textDim);
                g.setFont (font (9.0f, Weight::Medium).withExtraKerningFactor (0.06f));
                g.drawText (text, key.withTrimmedBottom (7.0f), juce::Justification::centredBottom, false);
            }
        }

        void drawBlackNote (int, juce::Graphics& g, juce::Rectangle<float> area,
                            bool isDown, bool isOver, juce::Colour) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto key = area.reduced (0.5f, 0.0f).withTrimmedTop (-1.0f);
            const auto shape = keyShape (key, 2.5f);

            // Soft shadow on the white keys
            g.setColour (juce::Colours::black.withAlpha (dark ? 0.25f : 0.10f));
            g.fillPath (keyShape (key.translated (0.0f, 1.5f).expanded (0.5f, 0.0f), 3.0f));

            const auto fill = isDown ? p.accent : (isOver ? p.keyBlack.interpolatedWith (p.accent, 0.35f) : p.keyBlack);
            g.setGradientFill ({ fill, key.getX(), key.getY(),
                                 fill.brighter (dark ? 0.35f : 0.22f), key.getX(), key.getBottom(), false });
            g.fillPath (shape);

            // Front face of the key
            auto front = key.removeFromBottom (juce::jmin (5.0f, key.getHeight() * 0.12f)).reduced (1.5f, 0.0f).withTrimmedBottom (1.5f);
            g.setColour (juce::Colours::white.withAlpha (isDown ? 0.25f : (dark ? 0.10f : 0.16f)));
            g.fillRoundedRectangle (front, 1.5f);
        }

        juce::String getWhiteNoteText (int note) override
        {
            return note % 12 == 0 ? "C" + juce::String (note / 12 - 2) : juce::String();
        }

    private:
        // Square top, rounded front corners
        static juce::Path keyShape (juce::Rectangle<float> r, float corner)
        {
            juce::Path s;
            s.addRoundedRectangle (r.getX(), r.getY(), r.getWidth(), r.getHeight(), corner, corner,
                                   false, false, true, true);
            return s;
        }

        static constexpr float radius = 6.0f;
    };
}
