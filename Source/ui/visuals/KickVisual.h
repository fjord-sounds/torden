#pragma once
#include <juce_dsp/juce_dsp.h>
#include "../Controls.h"
#include "../KickPreview.h"

namespace torden::ui
{
    // ---------------------------------------------------------------------------------------------
    // The hero display next to the kick: three views, switched with the small tabs in its corner.
    //   STORM     thunderclouds over a fjord – every hit is a lightning strike, brightness by velocity
    //   SPECTRUM  3D waterfall of the rendered kick: how the fundamental falls with the pitch envelope
    //   SCOPE     the live output since the last hit, with phosphor afterglow of the previous hits
    class KickVisual : public juce::Component, private juce::Timer, private juce::ChangeListener
    {
    public:
        KickVisual (TordenProcessor& p, KickPreview& kp) : proc (p), preview (kp)
        {
            juce::Random r (11);
            for (size_t k = 0; k < ridges.size(); ++k)
            {
                float ph[4];
                for (auto& v : ph) v = r.nextFloat() * 6.28f;
                for (size_t i = 0; i < ridges[k].size(); ++i)
                {
                    const float x = (float) i / (float) (ridges[k].size() - 1);
                    const float wall = std::pow (std::abs (x - 0.42f) * 1.8f, k == 0 ? 1.0f : 1.7f);
                    const float detail = 0.25f * std::sin (x * 9.0f + ph[0]) + 0.15f * std::sin (x * 23.0f + ph[1])
                                       + 0.07f * std::sin (x * 51.0f + ph[2]) + 0.03f * std::sin (x * 97.0f + ph[3]);
                    ridges[k][i] = std::clamp ((k == 0 ? 0.42f : 0.1f) + wall * 0.7f + detail * 0.45f, 0.04f, 1.1f);
                }
            }
            for (auto& c : clouds)
                c = { r.nextFloat(), 0.05f + r.nextFloat() * 0.38f, 0.18f + r.nextFloat() * 0.28f, 0.07f + r.nextFloat() * 0.09f,
                      0.004f + r.nextFloat() * 0.01f, r.nextFloat() };
            for (auto& d : drops)
                d = { r.nextFloat(), r.nextFloat(), 0.6f + r.nextFloat() * 0.8f };

            preview.addChangeListener (this);
            buildWaterfall();
            startTimerHz (60);
        }

        ~KickVisual() override { preview.removeChangeListener (this); }

        // For screenshots
        void tick() { timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto r = getLocalBounds().toFloat();

            juce::Path clip;
            clip.addRoundedRectangle (r.reduced (0.5f), 6.0f);
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (clip);
                if (mode == 0)      paintStorm (g, r, dark);
                else if (mode == 1) paintWaterfall (g, r, p, dark);
                else                paintScope (g, r, p, dark);
            }
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            // Mode tabs (top right)
            const bool onDark = mode == 0 && dark;
            for (int i = 0; i < 3; ++i)
            {
                const auto t = tabBounds (i);
                const bool on = i == mode;
                const auto ink = mode == 0 ? (dark ? juce::Colour (0xffe2edf0) : p.text) : p.text;
                g.setColour (on ? (mode == 0 ? p.accent : p.accent) : ink.withAlpha (i == hoverTab ? 0.85f : (onDark ? 0.45f : 0.5f)));
                drawSpacedText (g, modeNames[i], t, 9.5f, juce::Justification::centred, 0.24f, on);
                if (on)
                    g.fillRoundedRectangle (t.getCentreX() - 7.0f, t.getBottom() - 3.0f, 14.0f, 1.5f, 0.75f);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            int t = -1;
            for (int i = 0; i < 3; ++i) if (tabBounds (i).contains (e.position)) t = i;
            if (t != hoverTab) { hoverTab = t; setMouseCursor (t >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor); repaint(); }
        }

        void mouseExit (const juce::MouseEvent&) override { hoverTab = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            for (int i = 0; i < 3; ++i)
                if (tabBounds (i).contains (e.position)) { mode = i; repaint(); return; }
            // Click anywhere else: audition
            proc.triggerHit (juce::jlimit (0.2f, 1.0f, 1.0f - e.position.y / (float) getHeight() * 0.5f));
        }

        void setMode (int m) { mode = juce::jlimit (0, 2, m); repaint(); }

    private:
        static inline const char* modeNames[] = { "STORM", "SPECTRUM", "SCOPE" };

        juce::Rectangle<float> tabBounds (int i) const
        {
            const float w = i == 1 ? 74.0f : 58.0f;
            float x = (float) getWidth() - 12.0f - 58.0f - 74.0f - 58.0f;
            for (int k = 0; k < i; ++k) x += k == 1 ? 74.0f : 58.0f;
            return { x, 8.0f, w, 22.0f };
        }

        // ---- Storm ---------------------------------------------------------------------------
        struct Cloud { float x, y, w, h, speed, phase; };
        struct Drop { float x, y, len; };
        struct Bolt
        {
            std::vector<std::pair<juce::Path, float>> segments;   // path + thickness (main channel thicker)
            float life = 0.0f, strength = 0.0f, strikeX = 0.5f;
        };

        void spawnBolt (float velocity)
        {
            const auto r = getLocalBounds().toFloat();
            juce::Random rnd ((juce::int64) (proc.getHitCount() * 7919u + 13u));
            Bolt b;
            b.strength = 0.35f + 0.65f * velocity;
            b.life = 1.0f;
            const float startX = r.getWidth() * (0.25f + rnd.nextFloat() * 0.5f);
            b.strikeX = juce::jlimit (0.1f, 0.9f, startX / r.getWidth() + (rnd.nextFloat() - 0.5f) * 0.25f);
            const float groundY = r.getHeight() * groundLevel (b.strikeX);

            // Main channel by midpoint displacement, branches split off at random
            std::function<void (juce::Point<float>, juce::Point<float>, float, int)> build;
            build = [&] (juce::Point<float> a, juce::Point<float> z, float thickness, int depth)
            {
                std::vector<juce::Point<float>> pts { a, z };
                float disp = a.getDistanceFrom (z) * 0.22f;
                for (int pass = 0; pass < 6; ++pass)
                {
                    std::vector<juce::Point<float>> next;
                    for (size_t i = 0; i + 1 < pts.size(); ++i)
                    {
                        next.push_back (pts[i]);
                        const auto mid = (pts[i] + pts[i + 1]) * 0.5f;
                        next.push_back (mid.translated ((rnd.nextFloat() - 0.5f) * disp * 2.0f, (rnd.nextFloat() - 0.5f) * disp * 0.4f));
                    }
                    next.push_back (pts.back());
                    pts = std::move (next);
                    disp *= 0.55f;
                }
                juce::Path path;
                path.startNewSubPath (pts[0]);
                for (size_t i = 1; i < pts.size(); ++i) path.lineTo (pts[i]);
                b.segments.push_back ({ path, thickness });

                if (depth < 2)
                    for (size_t i = 6; i + 8 < pts.size(); i += 6)
                        if (rnd.nextFloat() < (depth == 0 ? 0.28f : 0.14f))
                        {
                            const auto from = pts[i];
                            const float len = (z.y - from.y) * (0.25f + rnd.nextFloat() * 0.35f);
                            const auto to = from.translated ((rnd.nextFloat() - 0.5f) * len * 1.4f, len);
                            build (from, to, thickness * 0.45f, depth + 1);
                        }
            };
            build ({ startX, r.getHeight() * 0.08f }, { b.strikeX * r.getWidth(), groundY }, 2.4f, 0);
            bolts.push_back (std::move (b));
            if (bolts.size() > 3) bolts.erase (bolts.begin());
            flash = std::max (flash, 0.35f + 0.65f * velocity);
        }

        float groundLevel (float x) const
        {
            const auto& ridge = ridges[1];
            const float pos = juce::jlimit (0.0f, 1.0f, x) * (float) (ridge.size() - 1);
            const auto i = (size_t) pos;
            const float f = pos - (float) i;
            const float h = ridge[i] + (ridge[std::min (i + 1, ridge.size() - 1)] - ridge[i]) * f;
            const float horizon = 0.74f;
            return horizon - h * 0.26f * 1.0f;
        }

        void paintStorm (juce::Graphics& g, juce::Rectangle<float> r, bool dark)
        {
            const float lit = flash;
            // Sky
            const auto skyTop = dark ? juce::Colour (0xff070b13) : juce::Colour (0xffcfd8de);
            const auto skyBottom = dark ? juce::Colour (0xff18233a) : juce::Colour (0xffe9eef0);
            g.setGradientFill (juce::ColourGradient (skyTop.interpolatedWith (juce::Colour (0xff3a3550), lit * (dark ? 0.6f : 0.0f)), 0.0f, r.getY(),
                                                     skyBottom.interpolatedWith (juce::Colour (0xfff6e7c1), lit * 0.35f), 0.0f, r.getBottom(), false));
            g.fillRect (r);

            const float horizon = r.getY() + r.getHeight() * 0.74f;
            const float breathe = level;

            // Clouds: soft blobs drifting, lit from below by the lightning
            for (size_t i = 0; i < clouds.size(); ++i)
            {
                const auto& c = clouds[i];
                float cx = std::fmod (c.x + time * c.speed, 1.3f) - 0.15f;
                const float cy = c.y + 0.012f * std::sin (time * 0.4f + c.phase * 6.0f);
                const auto centre = juce::Point<float> (r.getX() + cx * r.getWidth(), r.getY() + cy * r.getHeight());
                const float w = c.w * r.getWidth() * (1.0f + 0.08f * breathe), h = c.h * r.getHeight() * (1.0f + 0.25f * breathe);
                float near = 0.0f;
                for (const auto& b : bolts)
                    near = std::max (near, b.life * b.strength * std::max (0.0f, 1.0f - std::abs (cx - b.strikeX) * 2.2f));
                const auto base = dark ? juce::Colour (0xff1e2a40) : juce::Colour (0xffa9b6c0);
                const auto glow = dark ? juce::Colour (0xfff6e7c1) : juce::Colour (0xfffff4dc);
                const auto colour = base.interpolatedWith (glow, juce::jlimit (0.0f, 0.8f, near * 0.9f + lit * 0.25f));
                const float alpha = dark ? 0.55f : 0.5f;
                g.setGradientFill (juce::ColourGradient (colour.withAlpha (alpha), centre, colour.withAlpha (0.0f),
                                                         centre.translated (w * 0.5f, 0.0f), true));
                g.fillEllipse (juce::Rectangle<float> (w, h * 1.6f).withCentre (centre));
            }

            // Rain
            g.setColour ((dark ? juce::Colour (0xff9fb4cc) : juce::Colour (0xff5e7383)).withAlpha (dark ? 0.12f : 0.14f));
            for (const auto& d : drops)
            {
                const float y = std::fmod (d.y + time * 0.9f * d.len, 1.0f);
                const float x = std::fmod (d.x + time * 0.05f + y * 0.08f, 1.0f);
                const auto a = juce::Point<float> (r.getX() + x * r.getWidth(), r.getY() + y * r.getHeight() * 0.8f);
                g.drawLine ({ a, a.translated (-3.0f * d.len, 11.0f * d.len) }, 0.8f);
            }

            // Bolts (behind the mountains)
            for (const auto& b : bolts)
            {
                if (b.life <= 0.0f) continue;
                const float flicker = b.life > 0.6f ? 1.0f : (0.55f + 0.45f * std::sin (b.life * 60.0f));
                const float a = juce::jlimit (0.0f, 1.0f, b.life * b.strength * flicker * 1.4f);
                const auto core = juce::Colour (0xfffff8e6), halo = dark ? juce::Colour (0xfff2b544) : juce::Colour (0xffe8a04a);
                for (const auto& [path, w] : b.segments)
                {
                    g.setColour (halo.withAlpha (a * 0.12f));
                    g.strokePath (path, juce::PathStrokeType (w * 9.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (halo.withAlpha (a * 0.35f));
                    g.strokePath (path, juce::PathStrokeType (w * 3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                    g.setColour (core.withAlpha (a));
                    g.strokePath (path, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
                }
                // Impact glow on the ridge
                const auto hit = juce::Point<float> (b.strikeX * r.getWidth(), r.getHeight() * groundLevel (b.strikeX));
                g.setGradientFill (juce::ColourGradient (halo.withAlpha (a * 0.5f), hit, halo.withAlpha (0.0f), hit.translated (60.0f, 0.0f), true));
                g.fillEllipse (juce::Rectangle<float> (120.0f, 120.0f).withCentre (hit));
            }

            // Mountains: far and near, lit by the flash
            const auto mountainFar = dark ? juce::Colour (0xff0b1220) : juce::Colour (0xff8a9aa6);
            const auto mountainNear = dark ? juce::Colour (0xff05080e) : juce::Colour (0xff5f7280);
            for (size_t k = 0; k < ridges.size(); ++k)
            {
                juce::Path m;
                const float height = r.getHeight() * (k == 0 ? 0.34f : 0.26f);
                m.startNewSubPath (r.getX(), horizon);
                for (size_t i = 0; i < ridges[k].size(); ++i)
                    m.lineTo (r.getX() + r.getWidth() * (float) i / (float) (ridges[k].size() - 1), horizon - ridges[k][i] * height);
                m.lineTo (r.getRight(), horizon);
                m.closeSubPath();
                const auto c = (k == 0 ? mountainFar : mountainNear).interpolatedWith (juce::Colour (0xfff6e7c1), lit * (k == 0 ? 0.22f : 0.1f));
                g.setColour (c);
                g.fillPath (m);
            }

            // Water: dark band with the reflected flash
            const auto water = dark ? juce::Colour (0xff070c15) : juce::Colour (0xffb7c4cc);
            g.setGradientFill (juce::ColourGradient (water.interpolatedWith (juce::Colour (0xfff2b544), lit * 0.18f), 0.0f, horizon,
                                                     water, 0.0f, r.getBottom(), false));
            g.fillRect (juce::Rectangle<float> (r.getX(), horizon, r.getWidth(), r.getBottom() - horizon));
            for (const auto& b : bolts)
            {
                if (b.life <= 0.0f) continue;
                const float x = b.strikeX * r.getWidth();
                for (int i = 0; i < 7; ++i)
                {
                    const float y = horizon + 3.0f + (float) i * (r.getBottom() - horizon) / 8.0f;
                    const float w = 30.0f * (1.0f - (float) i / 9.0f) * (1.0f + 0.3f * std::sin (time * 9.0f + (float) i));
                    g.setColour (juce::Colour (0xfff6e7c1).withAlpha (b.life * b.strength * 0.4f * (1.0f - (float) i / 7.0f)));
                    g.fillRoundedRectangle (x - w * 0.5f, y, w, 1.5f, 0.75f);
                }
            }
            // Ripples in the water move with the level
            g.setColour ((dark ? juce::Colours::white : juce::Colour (0xff42566a)).withAlpha (0.06f + 0.1f * breathe));
            for (int i = 0; i < 5; ++i)
            {
                const float y = horizon + 6.0f + (float) i * 7.0f;
                const float x0 = r.getX() + std::fmod ((float) i * 0.37f + time * 0.02f, 1.0f) * r.getWidth();
                g.fillRoundedRectangle (x0, y, 40.0f + 30.0f * breathe, 1.0f, 0.5f);
            }

            // Flash over everything
            if (lit > 0.01f)
            {
                g.setColour (juce::Colour (0xfffff8e6).withAlpha (lit * (dark ? 0.16f : 0.22f)));
                g.fillRect (r);
            }
        }

        // ---- Spectrum waterfall -----------------------------------------------------------------
        void buildWaterfall()
        {
            const auto& res = preview.result();
            frames.clear();
            if (res.outL.empty()) return;
            constexpr int order = 11, size = 1 << order;
            juce::dsp::FFT fft (order);
            std::vector<float> buffer ((size_t) size * 2), window ((size_t) size);
            for (int i = 0; i < size; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (dsp::kTwoPi * (float) i / (float) (size - 1));

            const float seconds = std::min ((float) res.length() / (float) res.sampleRate, std::max (0.15f, preview.kickDuration() * 1.05f));
            const int hop = std::max (1, (int) (seconds * res.sampleRate / numFrames));
            for (int f = 0; f < numFrames; ++f)
            {
                std::fill (buffer.begin(), buffer.end(), 0.0f);
                const int start = f * hop - size / 4;
                for (int i = 0; i < size; ++i)
                {
                    const int s = start + i;
                    if (s >= 0 && s < res.length())
                        buffer[(size_t) i] = 0.5f * (res.outL[(size_t) s] + res.outR[(size_t) s]) * window[(size_t) i];
                }
                fft.performFrequencyOnlyForwardTransform (buffer.data());
                std::array<float, numBins> bins {};
                for (int b = 0; b < numBins; ++b)
                {
                    const float f0 = 20.0f * std::pow (800.0f, (float) b / numBins), f1 = 20.0f * std::pow (800.0f, (float) (b + 1) / numBins);
                    const int k0 = std::max (1, (int) (f0 / res.sampleRate * size)), k1 = std::max (k0 + 1, (int) (f1 / res.sampleRate * size));
                    float m = 0.0f;
                    for (int k = k0; k < k1 && k < size / 2; ++k) m = std::max (m, buffer[(size_t) k]);
                    bins[(size_t) b] = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (m / (size * 0.25f), -90.0f) + 72.0f) / 72.0f);
                }
                frames.push_back (bins);
            }
        }

        void paintWaterfall (juce::Graphics& g, juce::Rectangle<float> r, const Palette& p, bool dark)
        {
            g.setGradientFill (juce::ColourGradient (dark ? juce::Colour (0xff0a111c) : p.panel, 0.0f, r.getY(),
                                                     dark ? juce::Colour (0xff121c2c) : p.background, 0.0f, r.getBottom(), false));
            g.fillRect (r);
            if (frames.empty()) return;

            const auto area = r.reduced (18.0f, 0.0f).withTrimmedTop (44.0f).withTrimmedBottom (26.0f);
            const float depthX = area.getWidth() * 0.22f, depthY = area.getHeight() * 0.42f;
            const float w = area.getWidth() - depthX, h = area.getHeight() - depthY;
            const int n = (int) frames.size();

            // Back (late) to front (early)
            for (int f = n - 1; f >= 0; --f)
            {
                const float z = (float) f / (float) (n - 1);
                const float ox = area.getX() + depthX * z, oy = area.getBottom() - depthY * z;
                juce::Path line;
                line.startNewSubPath (ox, oy);
                for (int b = 0; b < numBins; ++b)
                    line.lineTo (ox + w * (float) b / (float) (numBins - 1), oy - frames[(size_t) f][(size_t) b] * h);
                juce::Path body (line);
                body.lineTo (ox + w, oy);
                body.closeSubPath();
                const auto col = stormAt (dark, 0.15f + z * 0.85f);
                const float reveal = juce::jlimit (0.0f, 1.0f, (waterfallReveal * (float) n - (float) f) * 0.5f);
                g.setColour ((dark ? juce::Colour (0xff0e1724) : p.panel).withAlpha (0.92f));
                g.fillPath (body);
                g.setColour (col.withAlpha ((0.25f + 0.75f * (1.0f - z)) * reveal));
                g.strokePath (line, juce::PathStrokeType (f == 0 ? 1.6f : 1.0f));
            }

            // Frequency labels on the front edge
            g.setFont (font (9.0f, Weight::Medium));
            g.setColour (dark ? p.text.withAlpha (0.55f) : p.textDim);
            for (float hz : { 30.0f, 60.0f, 120.0f, 250.0f, 500.0f, 1000.0f, 4000.0f, 10000.0f })
            {
                const float x = area.getX() + w * std::log (hz / 20.0f) / std::log (800.0f);
                g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                            juce::Rectangle<float> (x - 20.0f, area.getBottom() + 4.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
            drawSpacedText (g, "TIME", juce::Rectangle<float> (area.getRight() - 60.0f, area.getBottom() - depthY - 16.0f, 60.0f, 12.0f),
                            8.5f, juce::Justification::centredRight, 0.3f);
        }

        // ---- Scope -------------------------------------------------------------------------------
        void paintScope (juce::Graphics& g, juce::Rectangle<float> r, const Palette& p, bool dark)
        {
            g.setGradientFill (juce::ColourGradient (dark ? juce::Colour (0xff0a111c) : p.panel, 0.0f, r.getY(),
                                                     dark ? juce::Colour (0xff121c2c) : p.background, 0.0f, r.getBottom(), false));
            g.fillRect (r);
            const auto area = r.reduced (14.0f, 0.0f).withTrimmedTop (40.0f).withTrimmedBottom (16.0f);
            g.setColour (p.line.withAlpha (0.6f));
            g.fillRect (area.getX(), area.getCentreY(), area.getWidth(), 1.0f);

            // Older captures fade like phosphor
            for (size_t i = 0; i < traces.size(); ++i)
            {
                const float age = (float) (traces.size() - i);
                const auto col = i + 1 == traces.size() ? p.accent : stormAt (dark, 0.9f);
                g.setColour (col.withAlpha (i + 1 == traces.size() ? 1.0f : 0.35f / age));
                g.strokePath (traces[i].getPath (area), juce::PathStrokeType (i + 1 == traces.size() ? 1.5f : 1.0f));
            }
            g.setColour (dark ? p.text.withAlpha (0.5f) : p.textDim);
            g.setFont (font (9.0f, Weight::Medium));
            g.drawText ("0", area.withHeight (12.0f).translated (0.0f, area.getHeight() + 2.0f), juce::Justification::centredLeft);
            g.drawText (juce::String (juce::roundToInt (scopeWindow * 1000.0f)) + " ms", area.withHeight (12.0f).translated (0.0f, area.getHeight() + 2.0f),
                        juce::Justification::centredRight);
        }

        struct Trace
        {
            std::vector<float> lo, hi;
            juce::Path getPath (juce::Rectangle<float> a) const
            {
                juce::Path path;
                const int n = (int) lo.size();
                for (int i = 0; i < n; ++i)
                {
                    const float x = a.getX() + a.getWidth() * (float) i / (float) std::max (1, n - 1);
                    const float y0 = a.getCentreY() - hi[(size_t) i] * a.getHeight() * 0.48f;
                    const float y1 = a.getCentreY() - lo[(size_t) i] * a.getHeight() * 0.48f;
                    if (i == 0) path.startNewSubPath (x, y0);
                    path.lineTo (x, y0);
                    path.lineTo (x + 0.5f, y1);
                }
                return path;
            }
        };

        void captureScope()
        {
            const double since = proc.getSecondsSinceHit();
            const double sr = proc.scope.sampleRate.load();
            if (since > scopeWindow + 0.05 || sr <= 0.0) return;
            const int n = juce::jlimit (16, dsp::ScopeBuffer::size - 1, (int) (std::min (since, (double) scopeWindow) * sr));
            std::vector<float> data ((size_t) n);
            proc.scope.copyLatest (data.data(), nullptr, n);
            const int columns = 300;
            const int total = (int) (scopeWindow * sr);
            Trace t;
            t.lo.assign ((size_t) columns, 0.0f);
            t.hi.assign ((size_t) columns, 0.0f);
            const int filled = std::min (columns, (int) ((double) n / total * columns));
            for (int c = 0; c < filled; ++c)
            {
                const int s0 = c * total / columns, s1 = std::min (n, (c + 1) * total / columns);
                float lo = 0.0f, hi = 0.0f;
                for (int s = s0; s < s1; ++s) { lo = std::min (lo, data[(size_t) s]); hi = std::max (hi, data[(size_t) s]); }
                t.lo[(size_t) c] = lo;
                t.hi[(size_t) c] = hi;
            }
            t.lo.resize ((size_t) std::max (1, filled));
            t.hi.resize ((size_t) std::max (1, filled));
            // Scale: the trace keeps its own x-scale (filled portion of the window)
            Trace scaled;
            scaled.lo.assign ((size_t) columns, 0.0f);
            scaled.hi.assign ((size_t) columns, 0.0f);
            for (int c = 0; c < filled; ++c) { scaled.lo[(size_t) c] = t.lo[(size_t) c]; scaled.hi[(size_t) c] = t.hi[(size_t) c]; }

            if (proc.getHitCount() != scopeHit || traces.empty())
            {
                scopeHit = proc.getHitCount();
                traces.push_back (scaled);
                if (traces.size() > 5) traces.erase (traces.begin());
            }
            else
                traces.back() = scaled;
        }

        // ---- Animation ---------------------------------------------------------------------------
        void timerCallback() override
        {
            const float dt = 1.0f / 60.0f;
            const bool slow = Theme::get().reduceMotion();
            time += slow ? dt * 0.3f : dt;

            const auto hits = proc.getHitCount();
            if (hits != lastHits)
            {
                lastHits = hits;
                if (proc.getSecondsSinceHit() < 0.25)
                    spawnBolt (proc.getLastVelocity());
            }
            for (auto& b : bolts) b.life = std::max (0.0f, b.life - dt / (slow ? 0.25f : 0.45f));
            bolts.erase (std::remove_if (bolts.begin(), bolts.end(), [] (const Bolt& b) { return b.life <= 0.0f; }), bolts.end());
            flash = std::max (0.0f, flash - dt / 0.18f);

            // Output level from the scope buffer (for the clouds and water)
            std::array<float, 512> recent {};
            proc.scope.copyLatest (recent.data(), nullptr, (int) recent.size());
            float sum = 0.0f;
            for (float v : recent) sum += v * v;
            const float rms = std::sqrt (sum / (float) recent.size());
            level += (std::min (1.0f, rms * 3.0f) - level) * 0.15f;

            if (waterfallReveal < 1.0f) waterfallReveal = std::min (1.0f, waterfallReveal + dt / 0.5f);
            if (mode == 2) captureScope();
            if (isShowing()) repaint();
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            buildWaterfall();
            waterfallReveal = 0.0f;
            if (mode == 1) repaint();
        }

        TordenProcessor& proc;
        KickPreview& preview;
        int mode = 0, hoverTab = -1;
        float time = 0.0f, flash = 0.0f, level = 0.0f, waterfallReveal = 1.0f;
        uint32_t lastHits = 0, scopeHit = 0;
        std::array<std::array<float, 64>, 2> ridges {};
        std::array<Cloud, 14> clouds {};
        std::array<Drop, 70> drops {};
        std::vector<Bolt> bolts;

        static constexpr int numFrames = 34, numBins = 120;
        std::vector<std::array<float, numBins>> frames;
        std::vector<Trace> traces;
        static constexpr float scopeWindow = 0.4f;
    };
}
