#pragma once
#include "Theme.h"
#include "KickPreview.h"

namespace torden::ui
{
    // Which layer an envelope belongs to (for colours and the layer panel)
    inline int envLayer (dsp::Env e)
    {
        switch (e)
        {
            case dsp::Env::ClickAmp:                              return 1;
            case dsp::Env::SubAmp:                                return 2;
            case dsp::Env::NoiseAmp: case dsp::Env::NoiseFilter:  return 3;
            case dsp::Env::BodyAmp: case dsp::Env::Pitch: case dsp::Env::Drive: case dsp::Env::Count:
            default:                                              return 0;
        }
    }

    inline juce::Colour envColour (dsp::Env e, bool dark)
    {
        if (e == dsp::Env::Pitch) return Theme::get().palette().accent;
        if (e == dsp::Env::Drive) return dark ? juce::Colour (0xffff8a5c) : juce::Colour (0xffc2410c);
        return layerColour (envLayer (e), dark);
    }

    inline juce::String timeLabel (float seconds)
    {
        return seconds < 1.0f ? juce::String (juce::roundToInt (seconds * 1000.0f)) + " ms" : juce::String (seconds, 2) + " s";
    }

    // ---------------------------------------------------------------------------------------------
    // The anatomy of the kick: the rendered waveform with every layer as a coloured band, and the selected
    // envelope on top, editable point by point.
    //   drag a point            move it (shift = fine)
    //   drag a diamond          bend the segment
    //   double-click            add a point / delete a point
    //   right-click             point menu
    //   mouse wheel             zoom the time axis
    class KickTimeline : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        KickTimeline (TordenProcessor& p, KickPreview& kp) : proc (p), preview (kp)
        {
            preview.addChangeListener (this);
            changeListenerCallback (nullptr);   // the preview has already rendered: zoom to the kick
            startTimerHz (60);
            setMouseCursor (juce::MouseCursor::NormalCursor);
        }

        ~KickTimeline() override { preview.removeChangeListener (this); }

        std::function<void (dsp::Env)> onSelect;

        void selectEnv (dsp::Env e)
        {
            if (selected == e) return;
            selected = e;
            hoverPoint = hoverHandle = -1;
            repaint();
            if (onSelect) onSelect (e);
        }

        dsp::Env getSelected() const { return selected; }

        // For screenshots: advance the animations without a timer
        void tick() { timerCallback(); }

        void resized() override { columnsDirty = true; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();

            // Panel
            g.setColour (p.panel);
            g.fillRoundedRectangle (r.reduced (0.5f), 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);

            drawChips (g);
            const auto plot = plotArea();

            // Grid: time lines with labels
            const float step = niceStep (span);
            g.setFont (font (9.5f, Weight::Medium));
            for (float t = 0.0f; t <= span + 1.0e-4f; t += step)
            {
                const float x = timeToX (t);
                g.setColour (p.line.withAlpha (0.55f));
                g.fillRect (x, plot.getY(), 1.0f, plot.getHeight());
                g.setColour (p.textDim);
                g.drawText (timeLabel (t), juce::Rectangle<float> (x + 3.0f, plot.getBottom() + 2.0f, 60.0f, 14.0f), juce::Justification::centredLeft);
            }
            g.setColour (p.line);
            g.fillRect (plot.getX(), plot.getCentreY(), plot.getWidth(), 1.0f);

            drawWaveform (g, plot, dark);
            drawPlayhead (g, plot);
            drawEnvelope (g, plot, dark);
            drawAxisLabels (g, plot);

            // Hint line
            g.setColour (p.textDim.withAlpha (0.8f));
            g.setFont (font (9.5f));
            g.drawText (utf8 ("drag points \xc2\xb7 drag \xe2\x97\x87 to bend \xc2\xb7 double-click to add / remove \xc2\xb7 wheel to zoom"),
                        juce::Rectangle<float> (r.getX() + 14.0f, r.getBottom() - 17.0f, r.getWidth() - 28.0f, 14.0f),
                        juce::Justification::centredRight);
        }

        // ---- Mouse
        void mouseMove (const juce::MouseEvent& e) override
        {
            const int chip = chipAt (e.position);
            const int pt = pointAt (e.position);
            const int handle = pt < 0 ? handleAt (e.position) : -1;
            if (chip != hoverChip || pt != hoverPoint || handle != hoverHandle)
            {
                hoverChip = chip; hoverPoint = pt; hoverHandle = handle;
                setMouseCursor (chip >= 0 ? juce::MouseCursor::PointingHandCursor
                              : pt >= 0 ? juce::MouseCursor::DraggingHandCursor
                              : handle >= 0 ? juce::MouseCursor::UpDownResizeCursor : juce::MouseCursor::CrosshairCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hoverChip = hoverPoint = hoverHandle = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            if (const int chip = chipAt (e.position); chip >= 0)
            {
                if (chip == resetChip) return resetEnvelope();
                return selectEnv ((dsp::Env) chip);
            }

            dragPoint = pointAt (e.position);
            dragHandle = dragPoint < 0 ? handleAt (e.position) : -1;
            if (e.mods.isPopupMenu() && dragPoint >= 0)
                return showPointMenu (dragPoint);

            if (dragPoint >= 0 || dragHandle >= 0)
            {
                before = proc.sound.getEnv (selected);
                editing = before;
                dragStart = e.position;
                startCurve = dragHandle >= 0 ? before.points[(size_t) dragHandle].curve : 0.0f;
            }
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragPoint < 0 && dragHandle < 0) return;
            const auto plot = plotArea();
            auto& s = editing;
            if (dragPoint >= 0)
            {
                auto& pt = s.points[(size_t) dragPoint];
                const bool fine = e.mods.isShiftDown();
                const auto pos = fine ? dragStart + (e.position - dragStart) * 0.2f : e.position;
                const float lo = dragPoint == 0 ? 0.0f : s.points[(size_t) dragPoint - 1].time;
                const float hi = dragPoint + 1 < s.numPoints ? s.points[(size_t) dragPoint + 1].time : 8.0f;
                if (dragPoint > 0)
                    pt.time = juce::jlimit (lo, hi, xToTime (pos.x) / envScale());
                pt.level = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - pos.y) / plot.getHeight());
            }
            else
            {
                // Dragging up makes the segment bulge upwards
                const auto& a = s.points[(size_t) dragHandle - 1];
                const auto& b = s.points[(size_t) dragHandle];
                const float dir = b.level >= a.level ? 1.0f : -1.0f;
                const float dy = (dragStart.y - e.position.y) / 120.0f;
                s.points[(size_t) dragHandle].curve = juce::jlimit (-1.0f, 1.0f, startCurve + dy * dir);
            }
            s.sanitise();
            proc.sound.setEnv (selected, s, false);
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragPoint >= 0 || dragHandle >= 0)
            {
                // One undo step for the whole drag
                if (! (editing == before))
                {
                    proc.sound.setEnv (selected, before, false);
                    proc.undoManager.beginNewTransaction ("Edit envelope");
                    proc.sound.setEnv (selected, editing, true);
                }
            }
            dragPoint = dragHandle = -1;
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (! plotArea().contains (e.position)) return;
            auto s = proc.sound.getEnv (selected);
            proc.undoManager.beginNewTransaction ("Edit envelope");
            if (const int pt = pointAt (e.position); pt >= 0)
            {
                if (pt == 0 || s.numPoints <= 2) return;
                for (int i = pt; i < s.numPoints - 1; ++i) s.points[(size_t) i] = s.points[(size_t) i + 1];
                --s.numPoints;
            }
            else
            {
                if (s.numPoints >= dsp::kMaxEnvPoints) return;
                const float t = xToTime (e.position.x) / envScale();
                const auto plot = plotArea();
                const float level = juce::jlimit (0.0f, 1.0f, (plot.getBottom() - e.position.y) / plot.getHeight());
                int insert = s.numPoints;
                for (int i = 1; i < s.numPoints; ++i)
                    if (s.points[(size_t) i].time > t) { insert = i; break; }
                for (int i = s.numPoints; i > insert; --i) s.points[(size_t) i] = s.points[(size_t) i - 1];
                s.points[(size_t) insert] = { t, level, insert < s.numPoints ? s.points[(size_t) insert + 1].curve * 0.5f : 0.3f };
                ++s.numPoints;
            }
            s.sanitise();
            proc.sound.setEnv (selected, s, true);
            repaint();
        }

        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            const float delta = std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            span = juce::jlimit (0.03f, 3.0f, span * std::exp (-delta * 1.2f));
            autoZoom = false;
            columnsDirty = true;
            repaint();
        }

    private:
        static constexpr int resetChip = 100;

        juce::Rectangle<float> plotArea() const
        {
            return getLocalBounds().toFloat().withTrimmedTop (44.0f).withTrimmedBottom (38.0f).withTrimmedLeft (14.0f).withTrimmedRight (52.0f);
        }

        float timeToX (float t) const { const auto p = plotArea(); return p.getX() + t / span * p.getWidth(); }
        float xToTime (float x) const { const auto p = plotArea(); return std::max (0.0f, (x - p.getX()) / p.getWidth() * span); }
        float envScale() const { return std::max (0.01f, preview.envelopeScale (selected)); }

        juce::Point<float> pointPos (const dsp::EnvShape& s, int i) const
        {
            const auto plot = plotArea();
            const auto& pt = s.points[(size_t) i];
            return { timeToX (pt.time * envScale()), plot.getBottom() - pt.level * plot.getHeight() };
        }

        juce::Point<float> handlePos (const dsp::EnvShape& s, int i) const
        {
            const auto& a = s.points[(size_t) i - 1];
            const auto& b = s.points[(size_t) i];
            const float tm = 0.5f * (a.time + b.time);
            const float lv = a.level + (b.level - a.level) * dsp::EnvShape::shapeCurve (0.5f, b.curve);
            const auto plot = plotArea();
            return { timeToX (tm * envScale()), plot.getBottom() - lv * plot.getHeight() };
        }

        int pointAt (juce::Point<float> pos) const
        {
            const auto s = proc.sound.getEnv (selected);
            for (int i = s.numPoints - 1; i >= 0; --i)
                if (pointPos (s, i).getDistanceFrom (pos) < 8.0f) return i;
            return -1;
        }

        int handleAt (juce::Point<float> pos) const
        {
            const auto s = proc.sound.getEnv (selected);
            for (int i = 1; i < s.numPoints; ++i)
            {
                const auto a = pointPos (s, i - 1), b = pointPos (s, i);
                if (b.x - a.x < 18.0f) continue;   // too short to show a handle
                if (handlePos (s, i).getDistanceFrom (pos) < 7.0f) return i;
            }
            return -1;
        }

        // ---- Chips
        juce::Rectangle<float> chipBounds (int index) const
        {
            float x = 12.0f;
            for (int i = 0; i < dsp::kNumEnvs; ++i)
            {
                const float w = chipWidth (i);
                if (i == index) return { x, 10.0f, w, 24.0f };
                x += w + 4.0f;
            }
            return { (float) getWidth() - 64.0f, 10.0f, 52.0f, 24.0f };   // reset
        }

        static float chipWidth (int i)
        {
            return juce::GlyphArrangement::getStringWidth (font (10.0f, Weight::SemiBold).withExtraKerningFactor (0.14f),
                                                           juce::String (dsp::envLabel ((dsp::Env) i)).toUpperCase()) + 26.0f;
        }

        int chipAt (juce::Point<float> pos) const
        {
            for (int i = 0; i < dsp::kNumEnvs; ++i)
                if (chipBounds (i).contains (pos)) return i;
            if (chipBounds (resetChip).contains (pos)) return resetChip;
            return -1;
        }

        void drawChips (juce::Graphics& g)
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            for (int i = 0; i < dsp::kNumEnvs; ++i)
            {
                const auto r = chipBounds (i);
                const bool on = i == (int) selected;
                const auto c = envColour ((dsp::Env) i, dark);
                if (on)
                {
                    g.setColour (c.withAlpha (dark ? 0.2f : 0.14f));
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                }
                else if (i == hoverChip)
                {
                    g.setColour (p.line.withAlpha (0.5f));
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                }
                g.setColour (c);
                g.fillEllipse (r.getX() + 9.0f, r.getCentreY() - 2.5f, 5.0f, 5.0f);
                g.setColour (on ? p.text : p.textDim);
                drawSpacedText (g, juce::String (dsp::envLabel ((dsp::Env) i)).toUpperCase(), r.withTrimmedLeft (18.0f), 10.0f,
                                juce::Justification::centredLeft, 0.14f, on);
            }
            const auto rr = chipBounds (resetChip);
            g.setColour (hoverChip == resetChip ? p.text : p.textDim);
            drawSpacedText (g, "RESET", rr, 10.0f, juce::Justification::centred, 0.16f);
        }

        // ---- Waveform
        struct Columns
        {
            std::array<std::vector<float>, 4> layer;   // peak |x| per column
            std::vector<float> lo, hi;                  // summed waveform (min / max)
            void resize (size_t n)
            {
                for (auto& l : layer) l.assign (n, 0.0f);
                lo.assign (n, 0.0f);
                hi.assign (n, 0.0f);
            }
        };

        void buildColumns()
        {
            const auto plot = plotArea();
            const int n = std::max (1, (int) plot.getWidth());
            const auto& res = preview.result();
            const double sr = res.sampleRate;
            Columns c;
            c.resize ((size_t) n);
            float maxAbs = 0.05f;
            for (int x = 0; x < n; ++x)
            {
                const int s0 = (int) ((double) x / n * span * sr), s1 = std::max (s0 + 1, (int) ((double) (x + 1) / n * span * sr));
                float lo = 0.0f, hi = 0.0f;
                for (int s = s0; s < s1 && s < res.length(); ++s)
                {
                    const float v = 0.5f * (res.dryL[(size_t) s] + res.dryR[(size_t) s]);
                    lo = std::min (lo, v);
                    hi = std::max (hi, v);
                    for (size_t l = 0; l < 4; ++l)
                        c.layer[l][(size_t) x] = std::max (c.layer[l][(size_t) x], std::abs (res.layers[l][(size_t) s]));
                }
                c.lo[(size_t) x] = lo;
                c.hi[(size_t) x] = hi;
                maxAbs = std::max (maxAbs, std::max (-lo, hi));
            }
            // Normalise for the display (the level is shown by the meters)
            const float norm = 0.92f / maxAbs;
            for (auto& l : c.layer) for (auto& v : l) v *= norm;
            for (auto& v : c.lo) v *= norm;
            for (auto& v : c.hi) v *= norm;

            if (target.lo.size() == c.lo.size())
            {
                previous = shown();
                morph = 0.0f;
            }
            else
                morph = 1.0f;
            target = std::move (c);
            columnsDirty = false;
        }

        // The columns currently on screen (blending from the previous render while morphing)
        Columns shown() const
        {
            if (morph >= 1.0f || previous.lo.size() != target.lo.size()) return target;
            Columns c = target;
            const float t = morph * morph * (3.0f - 2.0f * morph);
            auto blend = [t] (std::vector<float>& out, const std::vector<float>& a) { for (size_t i = 0; i < out.size(); ++i) out[i] = a[i] + (out[i] - a[i]) * t; };
            for (size_t l = 0; l < 4; ++l) blend (c.layer[l], previous.layer[l]);
            blend (c.lo, previous.lo);
            blend (c.hi, previous.hi);
            return c;
        }

        void drawWaveform (juce::Graphics& g, juce::Rectangle<float> plot, bool dark)
        {
            if (columnsDirty) buildColumns();
            const auto c = shown();
            const float mid = plot.getCentreY(), half = plot.getHeight() * 0.5f;
            const int n = (int) c.lo.size();
            if (n < 2) return;

            // Layer bands: soft mirrored areas, drawn back to front (noise, sub, body, click)
            for (int l : { 3, 2, 0, 1 })
            {
                juce::Path band;
                band.startNewSubPath (plot.getX(), mid);
                for (int x = 0; x < n; ++x)
                    band.lineTo (plot.getX() + (float) x, mid - c.layer[(size_t) l][(size_t) x] * half);
                for (int x = n - 1; x >= 0; --x)
                    band.lineTo (plot.getX() + (float) x, mid + c.layer[(size_t) l][(size_t) x] * half);
                band.closeSubPath();
                const auto col = layerColour (l, dark);
                const bool focus = envLayer (selected) == l && selected != dsp::Env::Pitch && selected != dsp::Env::Drive;
                g.setGradientFill (juce::ColourGradient (col.withAlpha (focus ? 0.34f : 0.17f), plot.getX(), mid,
                                                         col.withAlpha (focus ? 0.12f : 0.05f), plot.getX(), plot.getY(), false));
                g.fillPath (band);
            }

            // The summed waveform as a fine line
            juce::Path wave;
            for (int x = 0; x < n; ++x)
            {
                const float px = plot.getX() + (float) x;
                const float y0 = mid - c.hi[(size_t) x] * half, y1 = mid - c.lo[(size_t) x] * half;
                if (x == 0) wave.startNewSubPath (px, y0);
                wave.lineTo (px, y0);
                wave.lineTo (px + 0.5f, y1);
            }
            const auto& p = Theme::get().palette();
            g.setColour (p.text.withAlpha (dark ? 0.55f : 0.45f));
            g.strokePath (wave, juce::PathStrokeType (0.8f));
        }

        void drawPlayhead (juce::Graphics& g, juce::Rectangle<float> plot)
        {
            const float t = (float) proc.getSecondsSinceHit();
            if (t > span || t > preview.kickDuration() + 0.25f) return;
            const float x = timeToX (t);
            const auto& p = Theme::get().palette();
            const float fade = 1.0f - juce::jlimit (0.0f, 1.0f, (t - preview.kickDuration()) / 0.25f);
            g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.0f), x - 40.0f, 0.0f, p.accent.withAlpha (0.22f * fade), x, 0.0f, false));
            g.fillRect (juce::Rectangle<float> (x - 40.0f, plot.getY(), 40.0f, plot.getHeight()).getIntersection (plot));
            g.setColour (p.accent.withAlpha (0.9f * fade));
            g.fillRect (x, plot.getY(), 1.5f, plot.getHeight());
        }

        void drawEnvelope (juce::Graphics& g, juce::Rectangle<float> plot, bool dark)
        {
            const auto s = dragPoint >= 0 || dragHandle >= 0 ? editing : proc.sound.getEnv (selected);
            const auto col = envColour (selected, dark);
            const float scale = envScale();

            juce::Path curve;
            const int steps = (int) plot.getWidth();
            const float last = s.duration() * scale;
            for (int i = 0; i <= steps; ++i)
            {
                const float t = span * (float) i / (float) steps;
                const float y = plot.getBottom() - s.valueAt (t / scale) * plot.getHeight();
                if (i == 0) curve.startNewSubPath (plot.getX(), y);
                else        curve.lineTo (plot.getX() + (float) i, y);
                if (t > last + span * 0.05f && i > 0) { curve.lineTo (plot.getRight(), y); break; }
            }

            juce::Path fill (curve);
            fill.lineTo (plot.getRight(), plot.getBottom());
            fill.lineTo (plot.getX(), plot.getBottom());
            fill.closeSubPath();
            g.setGradientFill (juce::ColourGradient (col.withAlpha (dark ? 0.16f : 0.10f), 0.0f, plot.getY(),
                                                     col.withAlpha (0.0f), 0.0f, plot.getBottom(), false));
            g.fillPath (fill);

            // Glow: wide soft strokes under the line
            for (auto [w, a] : { std::pair { 7.0f, 0.08f }, std::pair { 3.5f, 0.18f } })
            {
                g.setColour (col.withAlpha (a * (dark ? 1.4f : 1.0f)));
                g.strokePath (curve, juce::PathStrokeType (w, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            g.setColour (col);
            g.strokePath (curve, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            const auto& p = Theme::get().palette();
            for (int i = 1; i < s.numPoints; ++i)
            {
                const auto a = pointPos (s, i - 1), b = pointPos (s, i);
                if (b.x - a.x < 18.0f) continue;
                const auto h = handlePos (s, i);
                const bool hot = i == hoverHandle || i == dragHandle;
                juce::Path diamond;
                const float d = hot ? 4.5f : 3.2f;
                diamond.addQuadrilateral (h.x, h.y - d, h.x + d, h.y, h.x, h.y + d, h.x - d, h.y);
                g.setColour (p.panel);
                g.fillPath (diamond);
                g.setColour (col.withAlpha (hot ? 1.0f : 0.6f));
                g.strokePath (diamond, juce::PathStrokeType (1.2f));
            }
            for (int i = 0; i < s.numPoints; ++i)
            {
                const auto pos = pointPos (s, i);
                const bool hot = i == hoverPoint || i == dragPoint;
                const float rad = hot ? 6.0f : 4.5f;
                if (hot)
                {
                    g.setColour (col.withAlpha (0.25f));
                    g.fillEllipse (juce::Rectangle<float> (rad * 3.6f, rad * 3.6f).withCentre (pos));
                }
                g.setColour (p.panel);
                g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pos));
                g.setColour (col);
                g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (pos), 1.6f);
                if (hot)
                    drawPointLabel (g, s, i, pos);
            }
        }

        void drawPointLabel (juce::Graphics& g, const dsp::EnvShape& s, int i, juce::Point<float> pos)
        {
            const auto& p = Theme::get().palette();
            const auto& pt = s.points[(size_t) i];
            juce::String text = timeLabel (pt.time * envScale()) + utf8 ("  \xc2\xb7  ");
            if (selected == dsp::Env::Pitch)
            {
                const float hz = preview.getBaseHz() * std::exp2 (preview.values()[params::BodyPitch] * pt.level / 12.0f);
                text += juce::String (hz, hz < 100.0f ? 1 : 0) + " Hz " + params::noteName (juce::roundToInt (dsp::hzToNote (hz)));
            }
            else
                text += juce::String (juce::roundToInt (pt.level * 100.0f)) + " %";
            g.setFont (font (11.0f, Weight::Medium));
            const float w = juce::GlyphArrangement::getStringWidth (g.getCurrentFont(), text) + 16.0f;
            auto box = juce::Rectangle<float> (w, 20.0f).withCentre (pos.translated (0.0f, -20.0f));
            box = box.constrainedWithin (getLocalBounds().toFloat().reduced (4.0f));
            g.setColour (p.panel.withAlpha (0.95f));
            g.fillRoundedRectangle (box, 10.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (box, 10.0f, 1.0f);
            g.setColour (p.text);
            g.drawText (text, box, juce::Justification::centred);
        }

        // Right axis: Hz for the pitch envelope, % for the others
        void drawAxisLabels (juce::Graphics& g, juce::Rectangle<float> plot)
        {
            const auto& p = Theme::get().palette();
            g.setFont (font (9.5f, Weight::Medium));
            g.setColour (p.textDim);
            for (float lv : { 1.0f, 0.5f, 0.0f })
            {
                const float y = plot.getBottom() - lv * plot.getHeight();
                juce::String label;
                if (selected == dsp::Env::Pitch)
                {
                    const float hz = preview.getBaseHz() * std::exp2 (preview.values()[params::BodyPitch] * lv / 12.0f);
                    label = hz >= 1000.0f ? juce::String (hz / 1000.0f, 1) + "k" : juce::String (juce::roundToInt (hz));
                }
                else
                    label = juce::String (juce::roundToInt (lv * 100.0f));
                g.drawText (label, juce::Rectangle<float> (plot.getRight() + 6.0f, y - 7.0f, 42.0f, 14.0f), juce::Justification::centredLeft);
            }
            drawSpacedText (g, selected == dsp::Env::Pitch ? "HZ" : "%", juce::Rectangle<float> (plot.getRight() + 6.0f, plot.getY() + 12.0f, 40.0f, 14.0f),
                            8.5f, juce::Justification::centredLeft, 0.2f);
        }

        static float niceStep (float span)
        {
            for (float s : { 0.005f, 0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.25f, 0.5f, 1.0f })
                if (span / s <= 8.0f) return s;
            return 1.0f;
        }

        void showPointMenu (int index)
        {
            juce::PopupMenu m;
            m.addItem (1, "Delete point", index > 0 && proc.sound.getEnv (selected).numPoints > 2);
            m.addItem (2, "Straight segment", index > 0);
            m.addItem (3, "Punchy segment", index > 0);
            m.addSeparator();
            m.addItem (4, "Reset envelope");
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, index] (int r)
            {
                auto s = proc.sound.getEnv (selected);
                proc.undoManager.beginNewTransaction ("Edit envelope");
                if (r == 1 && index > 0 && s.numPoints > 2)
                {
                    for (int i = index; i < s.numPoints - 1; ++i) s.points[(size_t) i] = s.points[(size_t) i + 1];
                    --s.numPoints;
                }
                else if (r == 2) s.points[(size_t) index].curve = 0.0f;
                else if (r == 3) s.points[(size_t) index].curve = 0.65f;
                else if (r == 4) s = dsp::defaultEnv (selected);
                else return;
                s.sanitise();
                proc.sound.setEnv (selected, s, true);
                repaint();
            });
        }

        void resetEnvelope()
        {
            proc.undoManager.beginNewTransaction ("Reset envelope");
            proc.sound.setEnv (selected, dsp::defaultEnv (selected), true);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (autoZoom)
                span = juce::jlimit (0.05f, 3.0f, std::max (preview.kickDuration() * 1.08f, 0.08f));
            columnsDirty = true;
            repaint();
        }

        void timerCallback() override
        {
            bool needsRepaint = false;
            if (morph < 1.0f)
            {
                morph = std::min (1.0f, morph + 1.0f / (60.0f * 0.15f));
                needsRepaint = true;
            }
            const float t = (float) proc.getSecondsSinceHit();
            const bool playing = t < preview.kickDuration() + 0.3f;
            if (playing || wasPlaying) needsRepaint = true;
            wasPlaying = playing;
            if (needsRepaint && isShowing()) repaint();
        }

        TordenProcessor& proc;
        KickPreview& preview;
        dsp::Env selected = dsp::Env::BodyAmp;
        float span = 0.5f;
        bool autoZoom = true, columnsDirty = true, wasPlaying = false;
        Columns target, previous;
        float morph = 1.0f;

        int hoverChip = -1, hoverPoint = -1, hoverHandle = -1;
        int dragPoint = -1, dragHandle = -1;
        dsp::EnvShape before, editing;
        juce::Point<float> dragStart;
        float startCurve = 0.0f;
    };
}
