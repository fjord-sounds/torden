#pragma once
#include "Theme.h"
#include "PresetBrowser.h"

namespace torden::ui
{
    inline std::array<juce::Colour, 3> auroraColours (bool dark) { return stormColours (dark); }
    inline juce::Colour auroraAt (bool dark, float x) { return stormAt (dark, x); }

    // ---------------------------------------------------------------------------------------------
    // Output level as storm light over the water (a flash runs along the horizon on every hit): the left channel rises as rays above the horizon,
    // the right channel is their reflection below. Rays light up from left (-48 dB) to right (0 dB).
    class LightningMeter : public juce::Component, public juce::TooltipClient, private juce::Timer
    {
    public:
        LightningMeter (std::function<std::array<float, 2>()> peakSource, std::function<uint32_t()> hitSource)
            : takePeaks (std::move (peakSource)), hits (std::move (hitSource))
        {
            setInterceptsMouseClicks (true, false);
            startTimerHz (30);
        }

        // One animation frame (the timer calls it; the screenshot tool calls it directly).
        void tick()
        {
            const auto peaks = takePeaks();
            bool changed = flash > 0.0f;
            if (hits != nullptr)
            {
                const auto h = hits();
                if (h != lastHit) { lastHit = h; flash = 1.0f; changed = true; }
            }
            flash = std::max (0.0f, flash - 1.0f / 30.0f / (Theme::get().reduceMotion() ? 0.08f : 0.22f));
            for (size_t ch = 0; ch < 2; ++ch)
            {
                const float db = juce::Decibels::gainToDecibels (peaks[ch], floorDb);
                const float before = level[ch];
                level[ch] = db > level[ch] ? db : std::max (floorDb, level[ch] - 1.1f);   // fall ~33 dB/s
                if (db >= hold[ch]) { hold[ch] = db; holdFrames[ch] = 36; }
                else if (--holdFrames[ch] < 0) hold[ch] = std::max (floorDb, hold[ch] - 0.8f);
                changed |= std::abs (level[ch] - before) > 0.01f || level[ch] > floorDb;
            }
            if (changed || lit)
            {
                lit = level[0] > floorDb || level[1] > floorDb || hold[0] > floorDb || hold[1] > floorDb;
                time += 1.0f / 30.0f;
                repaint();
            }
        }

        juce::String getTooltip() override
        {
            auto fmt = [] (float db) { return db <= floorDb + 0.1f ? juce::String ("-inf") : juce::String (db, 1); };
            return "Output  L " + fmt (hold[0]) + " dB   R " + fmt (hold[1]) + " dB";
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat().reduced (2.0f, 1.0f);
            const float horizon = r.getY() + r.getHeight() * 0.62f;
            const float above = horizon - r.getY(), below = r.getBottom() - horizon;
            const float colW = r.getWidth() / (float) numRays;

            // Soft glow behind the lit part
            const float reach = juce::jmap (std::max (level[0], level[1]), floorDb, 0.0f, 0.0f, 1.0f);
            if (reach > 0.0f)
            {
                const auto glow = auroraAt (dark, reach * 0.8f);
                const juce::Point<float> c { r.getX() + r.getWidth() * reach * 0.5f, horizon };
                g.setGradientFill (juce::ColourGradient (glow.withAlpha ((dark ? 0.22f : 0.12f) * (0.4f + reach)), c,
                                                         glow.withAlpha (0.0f), c.translated (r.getWidth() * (0.15f + reach * 0.45f), 0.0f), true));
                g.fillRect (r);
            }

            // Curtain: a soft veil over the lit rays (left channel), waving slowly
            if (level[0] > floorDb)
            {
                const float lit0 = juce::jmap (level[0], floorDb, 0.0f, 0.0f, 1.0f);
                juce::Path veil;
                veil.startNewSubPath (r.getX(), horizon);
                const int n = 48;
                for (int i = 0; i <= n; ++i)
                {
                    const float x = lit0 * (float) i / (float) n;
                    const float fade = std::min (1.0f, (lit0 - x) * 8.0f + 0.15f);
                    const float wave = 0.5f + 0.5f * std::sin (x * 11.0f - time * 1.2f);
                    veil.lineTo (r.getX() + x * r.getWidth(), horizon - above * fade * (0.55f + 0.4f * wave));
                }
                veil.lineTo (r.getX() + lit0 * r.getWidth(), horizon);
                veil.closeSubPath();
                juce::ColourGradient grad (auroraAt (dark, 0.0f).withAlpha (dark ? 0.3f : 0.2f), r.getX(), 0.0f,
                                           auroraAt (dark, lit0).withAlpha (dark ? 0.3f : 0.2f), r.getX() + lit0 * r.getWidth(), 0.0f, false);
                g.setGradientFill (grad);
                g.fillPath (veil);
            }

            // Horizon
            g.setColour (p.line);
            g.fillRect (r.getX(), horizon, r.getWidth(), 1.0f);

            for (int i = 0; i < numRays; ++i)
            {
                const float x = ((float) i + 0.5f) / (float) numRays;
                const float rayDb = floorDb * (1.0f - x);
                const float cx = r.getX() + x * r.getWidth();
                const auto colour = auroraAt (dark, x);

                for (size_t ch = 0; ch < 2; ++ch)
                {
                    const bool up = ch == 0;
                    const float space = up ? above : below;
                    if (level[ch] < rayDb)
                    {
                        // Unlit: a small dot on the horizon as scale
                        if (up && i % 2 == 0)
                        {
                            g.setColour (p.textDim.withAlpha (0.35f));
                            g.fillEllipse (cx - 0.9f, horizon - 2.6f, 1.8f, 1.8f);
                        }
                        continue;
                    }

                    // Rays flicker slowly; the closer to the current level, the shorter (the curtain's edge)
                    const float edge = juce::jlimit (0.0f, 1.0f, (level[ch] - rayDb) / 9.0f);
                    const float flicker = 0.5f + 0.5f * std::sin (time * (1.3f + 0.17f * (float) (i % 5)) + (float) i * 1.7f + (float) ch);
                    const float h = space * (0.28f + 0.5f * edge + 0.22f * flicker) * (up ? 1.0f : 0.75f);
                    const float w = colW * 0.5f;
                    const float alpha = (up ? (dark ? 0.95f : 0.85f) : (dark ? 0.45f : 0.38f)) * (0.55f + 0.45f * edge);
                    const float ripple = up ? 0.0f : 0.6f * std::sin (time * 2.4f + (float) i * 0.9f);
                    const juce::Rectangle<float> ray (cx - w * 0.5f + ripple, up ? horizon - h : horizon + 1.0f, w, h);
                    g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), cx, horizon, colour.withAlpha (0.0f),
                                                             cx, up ? horizon - h : horizon + h, false));
                    g.fillRoundedRectangle (ray, w * 0.5f);
                }
            }

            // Lightning flash on every hit: a bright streak along the horizon
            if (flash > 0.0f)
            {
                const auto c = juce::Colour (0xfff6e7c1);
                g.setGradientFill (juce::ColourGradient (c.withAlpha (flash * (dark ? 0.55f : 0.45f)), r.getX(), horizon,
                                                         auroraAt (dark, 0.0f).withAlpha (0.0f), r.getRight(), horizon, false));
                g.fillRect (r.withY (horizon - 1.5f).withHeight (3.0f));
                g.setColour (p.accent.withAlpha (flash * 0.12f));
                g.fillRect (r);
            }

            // Peak hold: a small star above the rays
            const float peak = std::max (hold[0], hold[1]);
            if (peak > floorDb)
            {
                const float x = juce::jmap (peak, floorDb, 0.0f, 0.0f, 1.0f);
                const float cx = r.getX() + x * r.getWidth();
                const auto c = peak > -1.0f ? p.text : auroraAt (dark, x).brighter (dark ? 0.4f : 0.0f);
                g.setColour (c);
                g.fillEllipse (cx - 1.6f, r.getY() + 0.5f, 3.2f, 3.2f);
            }
        }

    private:
        void timerCallback() override { tick(); }

        static constexpr float floorDb = -48.0f;
        static constexpr int numRays = 40;
        std::function<std::array<float, 2>()> takePeaks;
        std::function<uint32_t()> hits;
        uint32_t lastHit = 0;
        float flash = 0.0f;
        std::array<float, 2> level { floorDb, floorDb }, hold { floorDb, floorDb };
        std::array<int, 2> holdFrames {};
        float time = 0.0f;
        bool lit = false;
    };

    // ---------------------------------------------------------------------------------------------
    // "Copy debug info": on click an aurora sweeps through the pill and the clipboard icon turns into a tick.
    class CopyButton : public juce::Component, private juce::Timer
    {
    public:
        CopyButton() { setMouseCursor (juce::MouseCursor::PointingHandCursor); }

        std::function<void()> onClick;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = r.getHeight() * 0.5f;

            // Base: aurora gradient pill
            juce::ColourGradient grad (auroraAt (dark, 0.0f), r.getX(), 0.0f, auroraAt (dark, 1.0f), r.getRight(), 0.0f, false);
            grad.addColour (0.5, auroraAt (dark, 0.5f));
            g.setGradientFill (grad);
            g.setOpacity (hover ? 1.0f : 0.92f);
            g.fillRoundedRectangle (r, rad);
            g.setOpacity (1.0f);

            // Sweep after copying: a bright band runs once through the pill
            if (phase > 0.0f && phase < 1.0f)
            {
                juce::Graphics::ScopedSaveState s (g);
                juce::Path clip;
                clip.addRoundedRectangle (r, rad);
                g.reduceClipRegion (clip);
                const float x = r.getX() - r.getWidth() * 0.3f + phase * r.getWidth() * 1.6f;
                juce::ColourGradient band (juce::Colours::white.withAlpha (0.0f), x - 40.0f, 0.0f,
                                           juce::Colours::white.withAlpha (0.0f), x + 40.0f, 0.0f, false);
                band.addColour (0.5, juce::Colours::white.withAlpha (0.45f));
                g.setGradientFill (band);
                g.fillRect (r);
            }

            const auto ink = dark ? p.background : juce::Colours::white;
            g.setColour (ink);
            const bool copied = phase > 0.0f;
            auto content = r.withSizeKeepingCentre (copied ? 96.0f : 150.0f, r.getHeight());
            auto icon = content.removeFromLeft (14.0f).withSizeKeepingCentre (12.0f, 13.0f);
            if (copied)
            {
                juce::Path tick;
                tick.startNewSubPath (icon.getX() + 1.0f, icon.getCentreY());
                tick.lineTo (icon.getX() + 4.5f, icon.getBottom() - 2.5f);
                tick.lineTo (icon.getRight(), icon.getY() + 2.0f);
                g.strokePath (tick, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            else
            {
                // Two overlapping sheets
                g.drawRoundedRectangle (icon.withTrimmedLeft (3.5f).withTrimmedBottom (3.5f), 1.8f, 1.3f);
                g.drawRoundedRectangle (icon.withTrimmedRight (3.5f).withTrimmedTop (3.5f), 1.8f, 1.3f);
            }
            content.removeFromLeft (8.0f);
            drawSpacedText (g, copied ? "COPIED" : "COPY DEBUG INFO", content, 10.5f, juce::Justification::centredLeft, 0.18f, true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (! getLocalBounds().contains (e.getPosition())) return;
            if (onClick) onClick();
            phase = 0.001f;
            startTimerHz (60);
        }

        void reset() { phase = 0.0f; stopTimer(); repaint(); }

    private:
        void timerCallback() override
        {
            phase += 1.0f / 60.0f / 0.7f;    // sweep 0.7 s, then the tick stays until 2 s
            if (phase > 2.0f / 0.7f) reset();
            repaint();
        }

        float phase = 0.0f;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // Opens on a click on the logo: version, format, host, paths and system, with a button that copies the
    // full debug report (including all changed parameters) to the clipboard.
    class AboutPanel : public juce::Component
    {
    public:
        AboutPanel (std::function<juce::String()> reportSource, std::function<void()> panic)
            : makeReport (std::move (reportSource)), reveal ("Show plugin", false), stop ("Stop notes", false)
        {
            addAndMakeVisible (copy);
            addAndMakeVisible (reveal);
            addAndMakeVisible (stop);
            copy.onClick = [this] { juce::SystemClipboard::copyTextToClipboard (makeReport()); };
            reveal.onClick = [] { juce::File::getSpecialLocation (juce::File::currentApplicationFile).revealToUser(); };
            stop.onClick = std::move (panic);
        }

        void open()
        {
            rows.clear();
            for (const auto& line : juce::StringArray::fromLines (makeReport()))
            {
                if (line.isEmpty()) break;                         // the parameter list is only in the copied text
                const auto key = line.substring (0, 16).trim(), value = line.substring (16).trim();
                if (key.isNotEmpty() && value.isNotEmpty() && ! key.startsWith ("Torden") && ! key.startsWith ("-")
                    && key != "Version" && key != "FX order")
                    rows.push_back ({ key, value });
                if (key == "Version") version = value;
            }
            copy.reset();
            setVisible (true);
            toFront (false);
        }

        void close() { setVisible (false); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            g.fillAll (p.background.withAlpha (0.82f));

            const auto c = card().toFloat();
            g.setColour (juce::Colours::black.withAlpha (dark ? 0.25f : 0.07f));
            g.fillRoundedRectangle (c.translated (0.0f, 8.0f).expanded (3.0f), 16.0f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (c, 14.0f);

            // Banner: aurora over a dark sky / pale day, the logo in the middle
            {
                juce::Graphics::ScopedSaveState s (g);
                juce::Path clip;
                clip.addRoundedRectangle (c.getX(), c.getY(), c.getWidth(), bannerHeight, 14.0f, 14.0f, true, true, false, false);
                g.reduceClipRegion (clip);
                const auto banner = c.withHeight (bannerHeight);
                g.setColour (dark ? p.background : p.background.darker (0.015f));
                g.fillRect (banner);
                const auto col = auroraColours (dark);
                for (int k = 0; k < 3; ++k)
                {
                    juce::Path curtain;
                    const float y0 = banner.getY() + banner.getHeight() * (0.22f + 0.1f * (float) k);
                    const int n = 48;
                    for (int i = 0; i <= n; ++i)
                    {
                        const float x = (float) i / (float) n;
                        const float y = y0 + banner.getHeight() * 0.09f * std::sin (x * (4.0f + (float) k) + (float) k * 1.9f);
                        if (i == 0) curtain.startNewSubPath (banner.getX(), y);
                        else        curtain.lineTo (banner.getX() + x * banner.getWidth(), y);
                    }
                    curtain.lineTo (banner.getRight(), banner.getBottom());
                    curtain.lineTo (banner.getX(), banner.getBottom());
                    curtain.closeSubPath();
                    g.setGradientFill (juce::ColourGradient (col[(size_t) k].withAlpha (dark ? 0.32f : 0.2f), 0.0f, y0,
                                                             col[(size_t) k].withAlpha (0.0f), 0.0f, y0 + banner.getHeight() * 0.55f, false));
                    g.fillPath (curtain);
                }
                g.setColour (p.line);
                g.fillRect (banner.getX(), banner.getBottom() - 1.0f, banner.getWidth(), 1.0f);

                const auto logoFont = font (44.0f).withExtraKerningFactor (0.04f);
                const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, "torden");
                const float lx = banner.getCentreX() - logoW * 0.5f;
                g.setColour (p.text);
                g.setFont (logoFont);
                g.drawText ("torden", juce::Rectangle<float> (lx, banner.getY() + 22.0f, logoW + 4.0f, 52.0f), juce::Justification::centredLeft);
                g.setColour (p.accent);
                g.fillEllipse (lx + logoW + 4.0f, banner.getY() + 22.0f + 35.0f, 7.0f, 7.0f);
                g.setColour (p.textDim);
                drawSpacedText (g, "KICK DESIGNER  " + utf8 ("\xc2\xb7") + "  VERSION " + version.upToFirstOccurrenceOf (" ", false, false),
                                banner.withTrimmedTop (80.0f).withHeight (20.0f), 10.0f, juce::Justification::centred, 0.25f);
                g.setFont (font (11.5f));
                g.drawText (version.fromFirstOccurrenceOf (" ", false, false).removeCharacters ("()"),
                            banner.withTrimmedTop (100.0f).withHeight (18.0f), juce::Justification::centred);
            }

            // Info rows
            auto area = card().reduced (36, 0).withTrimmedTop ((int) bannerHeight + 18);
            for (const auto& [key, value] : rows)
            {
                auto row = area.removeFromTop (rowHeight).toFloat();
                g.setColour (p.textDim);
                drawSpacedText (g, key.toUpperCase(), row.removeFromLeft (128.0f), 9.5f, juce::Justification::centredLeft, 0.2f);
                g.setColour (p.text);
                g.setFont (font (12.5f));
                g.drawFittedText (value, row.toNearestInt(), juce::Justification::centredLeft, 1, 0.8f);
                g.setColour (p.line.withAlpha (0.6f));
                g.fillRect (row.getX() - 128.0f, row.getBottom() - 0.5f, row.getWidth() + 128.0f, 0.5f);
            }
        }

        void resized() override
        {
            auto buttons = card().reduced (36, 0).withTrimmedBottom (24);
            buttons = buttons.removeFromBottom (34);
            copy.setBounds (buttons.removeFromRight (196));
            buttons.removeFromRight (10);
            reveal.setBounds (buttons.removeFromRight (124));
            buttons.removeFromRight (10);
            stop.setBounds (buttons.removeFromRight (118));
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (! card().contains (e.getPosition())) close();
        }

    private:
        static constexpr float bannerHeight = 128.0f;
        static constexpr int rowHeight = 25;

        juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (620, 524); }

        std::function<juce::String()> makeReport;
        juce::String version;
        std::vector<std::pair<juce::String, juce::String>> rows;
        CopyButton copy;
        PillButton reveal, stop;
    };
}
