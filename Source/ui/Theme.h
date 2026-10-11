#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "BinaryData.h"

namespace torden::ui
{
    // Scandinavian palette shared with Fjord (icy white / polar night); Torden's accent is thunderstorm amber.
    struct Palette
    {
        juce::Colour background, panel, line, text, textDim, accent, accentSoft, track, keyWhite, keyBlack;
    };

    // Arctic day with deep amber, polar night with glowing lightning amber.
    inline Palette lightPalette()
    {
        return { juce::Colour (0xffedf3f4),   // background: icy white
                 juce::Colour (0xfff8fbfc),   // panel
                 juce::Colour (0xffcfdce1),   // hairlines
                 juce::Colour (0xff15253a),   // text: deep night blue
                 juce::Colour (0xff6f8291),   // secondary text
                 juce::Colour (0xffa8650a),   // accent: deep amber
                 juce::Colour (0x29a8650a),
                 juce::Colour (0xffdde7eb),   // knob track
                 juce::Colour (0xfffafcfd),
                 juce::Colour (0xff1c2b3d) };
    }

    inline Palette darkPalette()
    {
        return { juce::Colour (0xff0d1520),   // background: polar night
                 juce::Colour (0xff131e2c),
                 juce::Colour (0xff22314a),
                 juce::Colour (0xffe2edf0),   // text: icy white
                 juce::Colour (0xff7d8fa3),
                 juce::Colour (0xfff2b544),   // accent: lightning amber
                 juce::Colour (0x33f2b544),
                 juce::Colour (0xff1f2d40),
                 juce::Colour (0xffc3d0d8),
                 juce::Colour (0xff0a1018) };
    }

    // Storm colours: amber -> lightning white -> storm blue (darker in the light theme).
    inline std::array<juce::Colour, 3> stormColours (bool dark)
    {
        if (dark) return { juce::Colour (0xfff2b544), juce::Colour (0xfff6e7c1), juce::Colour (0xff5b8fe6) };
        return { juce::Colour (0xffb8700c), juce::Colour (0xffc9933a), juce::Colour (0xff2f68b0) };
    }

    inline juce::Colour stormAt (bool dark, float x)
    {
        const auto c = stormColours (dark);
        x = juce::jlimit (0.0f, 1.0f, x);
        return x < 0.5f ? c[0].interpolatedWith (c[1], x * 2.0f) : c[1].interpolatedWith (c[2], x * 2.0f - 1.0f);
    }

    // Colour of each layer (body, click, sub, noise): shades of the storm, readable in both themes.
    inline juce::Colour layerColour (int layer, bool dark)
    {
        static const juce::uint32 darkC[]  = { 0xfff2b544, 0xfff6e7c1, 0xffe0703a, 0xff6f9be8 };
        static const juce::uint32 lightC[] = { 0xffb8700c, 0xff8a6a3a, 0xffc0501d, 0xff2f68b0 };
        return juce::Colour ((dark ? darkC : lightC)[std::clamp (layer, 0, 3)]);
    }

    // Soft storm glow (amber, warm white, storm blue) behind the header and in backdrops.
    inline void drawStormGlow (juce::Graphics& g, juce::Rectangle<float> area, bool dark, float strength = 1.0f)
    {
        const float a = (dark ? 0.14f : 0.09f) * strength;
        const juce::Colour colours[] = { juce::Colour (0xfff2b544), juce::Colour (0xffe8a04a), juce::Colour (0xff5b8fe6) };
        for (int i = 0; i < 3; ++i)
        {
            const float cx = area.getX() + area.getWidth() * (0.18f + 0.32f * (float) i);
            const float cy = area.getY() + area.getHeight() * (0.15f + 0.1f * (float) (i % 2));
            juce::ColourGradient grad (colours[i].withAlpha (a), cx, cy, colours[i].withAlpha (0.0f),
                                       cx + area.getWidth() * 0.32f, cy, true);
            g.setGradientFill (grad);
            g.fillEllipse (juce::Rectangle<float> (area.getWidth() * 0.64f, area.getHeight() * 1.4f).withCentre ({ cx, cy }));
        }
    }

    // Global theme, stored in the user folder and shared by all instances.
    class Theme : public juce::ChangeBroadcaster
    {
    public:
        static Theme& get()
        {
            static Theme instance;
            return instance;
        }

        const Palette& palette() const { return current; }

        // Tests switch saving off so they do not change the user's settings.
        void setPersistent (bool shouldPersist) { persistent = shouldPersist; }
        bool isDark() const { return dark; }

        // Generic per-user flags (reduce motion, last page ...)
        bool getFlag (const juce::String& key, bool def) const
        {
            auto* file = const_cast<juce::ApplicationProperties&> (settings).getUserSettings();
            return file != nullptr ? file->getBoolValue (key, def) : def;
        }

        void setFlag (const juce::String& key, bool v)
        {
            if (auto* file = persistent ? settings.getUserSettings() : nullptr)
            {
                file->setValue (key, v);
                file->saveIfNeeded();
            }
            sendChangeMessage();
        }

        bool reduceMotion() const { return getFlag ("reduceMotion", false); }

        // Window size (scale) is remembered globally
        float getScale() const
        {
            auto* file = const_cast<juce::ApplicationProperties&> (settings).getUserSettings();
            return file != nullptr ? (float) file->getDoubleValue ("uiScale", 1.0) : 1.0f;
        }

        juce::File getSettingsFile() const
        {
            auto* file = const_cast<juce::ApplicationProperties&> (settings).getUserSettings();
            return file != nullptr ? file->getFile() : juce::File();
        }

        void setScale (float s)
        {
            if (auto* file = persistent ? settings.getUserSettings() : nullptr)
            {
                file->setValue ("uiScale", s);
                file->saveIfNeeded();
            }
        }

        void setDark (bool shouldBeDark)
        {
            if (dark == shouldBeDark)
                return;
            dark = shouldBeDark;
            current = dark ? darkPalette() : lightPalette();
            if (auto* file = persistent ? settings.getUserSettings() : nullptr)
            {
                file->setValue ("darkTheme", dark);
                file->saveIfNeeded();   // write immediately – hosts may end the process without cleaning up
            }
            sendChangeMessage();
        }

    private:
        Theme()
        {
            juce::PropertiesFile::Options o;
            o.applicationName = "Torden";
            o.filenameSuffix = ".settings";
            o.folderName = "Fjord/Torden";
            o.osxLibrarySubFolder = "Application Support";
            o.millisecondsBeforeSaving = 0;
            settings.setStorageParameters (o);

            if (auto* file = settings.getUserSettings())
                dark = file->getBoolValue ("darkTheme", false);
            current = dark ? darkPalette() : lightPalette();
        }

        juce::ApplicationProperties settings;
        bool dark = false, persistent = true;
        Palette current;
    };

    // String literals with non-ASCII characters are UTF-8 – this displays them correctly.
    inline juce::String utf8 (const char* text) { return juce::String::fromUTF8 (text); }

    // Manrope (embedded): Regular for text, Medium for all-caps labels, SemiBold for emphasis.
    enum class Weight { Regular, Medium, SemiBold };

    inline juce::Typeface::Ptr typeface (Weight w)
    {
        static const juce::Typeface::Ptr faces[] = {
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeRegular_ttf, BinaryData::ManropeRegular_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeMedium_ttf, BinaryData::ManropeMedium_ttfSize),
            juce::Typeface::createSystemTypefaceFor (BinaryData::ManropeSemiBold_ttf, BinaryData::ManropeSemiBold_ttfSize) };
        return faces[(int) w];
    }

    inline juce::Font font (float height, Weight w)
    {
        return juce::Font (juce::FontOptions().withTypeface (typeface (w)).withHeight (height));
    }

    inline juce::Font font (float height, bool bold = false)
    {
        return font (height, bold ? Weight::SemiBold : Weight::Regular);
    }

    // Small all-caps headings with letter spacing – typical Scandinavian typography.
    inline void drawSpacedText (juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area,
                                float height, juce::Justification just, float kerning = 0.18f, bool bold = false)
    {
        g.setFont (font (height, bold ? Weight::SemiBold : Weight::Medium).withExtraKerningFactor (kerning));
        g.drawText (text, area, just, false);
    }
}
