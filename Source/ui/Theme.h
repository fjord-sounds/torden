#pragma once
#include <fjord_base/ui/Theme.h>
#include <fjord_base/ui/Controls.h>
#include <fjord_base/ui/Keyboard.h>
#include <fjord_base/ui/PluginView.h>

// Torden's UI builds on the shared family UI of fjord-base (palette, Manrope, controls, header, presets).
// Only Torden's own colour helpers live here: the storm (amber -> lightning white -> storm blue) is the brand gradient.
namespace torden::ui
{
    using namespace fjord::base::ui;

    inline const std::array<juce::Colour, 3>& stormColours (bool dark) { return gradientColours (dark); }
    inline juce::Colour stormAt (bool dark, float x) { return gradientAt (dark, x); }
    inline void drawStormGlow (juce::Graphics& g, juce::Rectangle<float> area, bool dark, float strength = 1.0f) { drawGlow (g, area, dark, strength); }

    // Colour of each layer (body, click, sub, noise): shades of the storm, readable in both themes.
    inline juce::Colour layerColour (int layer, bool dark)
    {
        static const juce::uint32 darkC[]  = { 0xfff2b544, 0xfff6e7c1, 0xffe0703a, 0xff6f9be8 };
        static const juce::uint32 lightC[] = { 0xffb8700c, 0xff8a6a3a, 0xffc0501d, 0xff2f68b0 };
        return juce::Colour ((dark ? darkC : lightC)[std::clamp (layer, 0, 3)]);
    }
}
