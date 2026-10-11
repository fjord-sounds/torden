#pragma once
#include "LayerControls.h"

namespace torden::ui
{
    namespace P = params;

    // ---------------------------------------------------------------------------------------------
    // The step grid: the main row switches steps on / off and shows them as lightning cells (accent glow =
    // velocity, little bolts = ratchets, transparency = chance). The lanes below are drawn with the mouse.
    class StepGrid : public juce::Component, private juce::Timer
    {
    public:
        explicit StepGrid (TordenProcessor& p) : proc (p) { startTimerHz (60); }

        enum Lane { Steps, Velocity, Pitch, Ratchet, Chance, Decay, NumLanes };

        void tick() { timerCallback(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const auto pattern = dragging ? editing : proc.sound.getPattern();
            const int length = proc.apvts.getRawParameterValue (P::paramId (P::SeqLength))->load() > 0
                             ? juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::SeqLength))->load()) : 16;
            const bool running = proc.apvts.getRawParameterValue (P::paramId (P::SeqOn))->load() > 0.5f;
            const int playing = running ? proc.getCurrentStep() : -1;

            for (int l = 0; l < NumLanes; ++l)
            {
                const auto row = laneBounds (l);
                g.setColour (l == Steps ? p.text : p.textDim);
                drawSpacedText (g, laneName (l), row.withWidth (labelWidth - 8.0f), l == Steps ? 10.5f : 9.5f,
                                juce::Justification::centredLeft, 0.18f, l == Steps);
                if (l > Steps)
                {
                    g.setColour (p.line.withAlpha (0.5f));
                    g.fillRect (row.getX() + labelWidth, row.getBottom() + 1.0f, row.getWidth() - labelWidth, 1.0f);
                }
            }

            for (int i = 0; i < seq::kMaxSteps; ++i)
            {
                const auto& s = pattern.steps[(size_t) i];
                const bool inRange = i < length;
                const float dim = inRange ? 1.0f : 0.3f;
                const auto cell = cellBounds (Steps, i).reduced (2.0f, 0.0f);
                const float pulse = i == playing ? 1.0f : stepGlow[(size_t) i];

                // Main cell
                const bool beat = i % 4 == 0;
                g.setColour ((beat ? p.track.brighter (dark ? 0.08f : 0.0f).darker (dark ? 0.0f : 0.04f) : p.track).withMultipliedAlpha (dim));
                g.fillRoundedRectangle (cell, 4.0f);
                if (s.on)
                {
                    const float a = (0.35f + 0.65f * s.chance) * dim;
                    if (pulse > 0.0f)
                    {
                        g.setGradientFill (juce::ColourGradient (p.accent.withAlpha (0.5f * pulse * a), cell.getCentre(), p.accent.withAlpha (0.0f),
                                                                 cell.getCentre().translated (cell.getWidth() * 1.6f, 0.0f), true));
                        g.fillRect (cell.expanded (cell.getWidth(), 10.0f));
                    }
                    const auto fill = p.accent.interpolatedWith (dark ? juce::Colour (0xfff6e7c1) : p.accent.brighter (0.4f), pulse * 0.6f);
                    g.setColour (fill.withAlpha (a * (0.45f + 0.55f * s.velocity)));
                    g.fillRoundedRectangle (cell.withTrimmedTop (cell.getHeight() * (1.0f - (0.35f + 0.65f * s.velocity))), 4.0f);
                    g.setColour (p.accent.withAlpha (a));
                    g.drawRoundedRectangle (cell, 4.0f, 1.2f);
                    // Ratchets as small bolts
                    for (int k = 0; k < s.ratchet && s.ratchet > 1; ++k)
                    {
                        const float x = cell.getX() + cell.getWidth() * ((float) k + 0.5f) / (float) s.ratchet;
                        juce::Path bolt;
                        bolt.startNewSubPath (x + 2.0f, cell.getY() + 5.0f);
                        bolt.lineTo (x - 1.5f, cell.getY() + 11.0f);
                        bolt.lineTo (x + 1.5f, cell.getY() + 11.0f);
                        bolt.lineTo (x - 2.0f, cell.getY() + 17.0f);
                        g.setColour (p.panel.withAlpha (0.9f));
                        g.strokePath (bolt, juce::PathStrokeType (1.3f));
                    }
                }
                if (i == playing)
                {
                    g.setColour (p.text.withAlpha (0.8f));
                    g.fillRoundedRectangle (cell.getX() + 3.0f, cell.getBottom() + 3.0f, cell.getWidth() - 6.0f, 2.0f, 1.0f);
                }
                g.setColour (p.textDim.withAlpha (dim));
                g.setFont (font (8.5f, Weight::Medium));
                g.drawText (juce::String (i + 1), cell.withY (cell.getBottom() + 6.0f).withHeight (11.0f), juce::Justification::centred);

                // Lanes
                const auto c = layerColour (0, dark).withMultipliedAlpha (s.on ? dim : dim * 0.35f);
                auto bar = [&] (int laneIndex, float v01)
                {
                    const auto lc = cellBounds (laneIndex, i).reduced (3.0f, 2.0f);
                    g.setColour (p.track.withMultipliedAlpha (dim));
                    g.fillRoundedRectangle (lc, 2.0f);
                    g.setColour (c);
                    g.fillRoundedRectangle (lc.withTrimmedTop (lc.getHeight() * (1.0f - juce::jlimit (0.0f, 1.0f, v01))), 2.0f);
                };
                bar (Velocity, s.velocity);
                bar (Chance, s.chance);
                bar (Decay, (s.decay - 0.25f) / 1.75f);
                {
                    const auto lc = cellBounds (Pitch, i).reduced (3.0f, 2.0f);
                    g.setColour (p.track.withMultipliedAlpha (dim));
                    g.fillRoundedRectangle (lc, 2.0f);
                    const float cy = lc.getCentreY(), y = cy - s.pitch / 24.0f * lc.getHeight() * 0.5f;
                    g.setColour (c);
                    g.fillRoundedRectangle (lc.getX(), std::min (cy, y), lc.getWidth(), std::max (1.5f, std::abs (y - cy)), 1.5f);
                    if (std::abs (s.pitch) > 0.01f && inRange)
                    {
                        g.setColour (p.text.withAlpha (0.7f));
                        g.setFont (font (8.0f, Weight::Medium));
                        g.drawText ((s.pitch > 0 ? "+" : "") + juce::String (juce::roundToInt (s.pitch)), lc.withHeight (10.0f).translated (0.0f, s.pitch > 0 ? lc.getHeight() - 10.0f : 0.0f),
                                    juce::Justification::centred);
                    }
                }
                {
                    const auto lc = cellBounds (Ratchet, i).reduced (3.0f, 2.0f);
                    for (int k = 0; k < 4; ++k)
                    {
                        const auto blk = juce::Rectangle<float> (lc.getX(), lc.getBottom() - (float) (k + 1) * lc.getHeight() / 4.0f + 1.0f, lc.getWidth(), lc.getHeight() / 4.0f - 2.0f);
                        g.setColour (k < s.ratchet ? c : p.track.withMultipliedAlpha (dim));
                        g.fillRoundedRectangle (blk, 1.5f);
                    }
                }
            }
        }

        // ---- Editing: click toggles a step (drag paints), lanes are drawn
        void mouseDown (const juce::MouseEvent& e) override
        {
            editing = proc.sound.getPattern();
            before = editing;
            dragging = true;
            lane = laneAt (e.position.y);
            const int i = stepAt (e.position.x);
            if (lane == Steps && i >= 0)
                paintValue = ! editing.steps[(size_t) i].on;
            apply (e.position);
        }

        void mouseDrag (const juce::MouseEvent& e) override { apply (e.position); }

        void mouseUp (const juce::MouseEvent&) override
        {
            dragging = false;
            if (! (editing == before))
            {
                proc.sound.setPattern (before, false);
                proc.undoManager.beginNewTransaction ("Edit pattern");
                proc.sound.setPattern (editing, true);
            }
            repaint();
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            // Double-click on a lane resets it for that step
            const int i = stepAt (e.position.x);
            const int l = laneAt (e.position.y);
            if (i < 0 || l <= Steps) return;
            auto pat = proc.sound.getPattern();
            auto& s = pat.steps[(size_t) i];
            switch (l)
            {
                case Velocity: s.velocity = 0.8f; break;
                case Pitch:    s.pitch = 0.0f; break;
                case Ratchet:  s.ratchet = 1; break;
                case Chance:   s.chance = 1.0f; break;
                default:       s.decay = 1.0f; break;
            }
            proc.undoManager.beginNewTransaction ("Edit pattern");
            proc.sound.setPattern (pat, true);
            repaint();
        }

    private:
        static constexpr float labelWidth = 82.0f;

        static const char* laneName (int lane)
        {
            static const char* names[] = { "STEPS", "VELOCITY", "PITCH", "RATCHET", "CHANCE", "DECAY" };
            return names[lane];
        }

        juce::Rectangle<float> laneBounds (int l) const
        {
            const float mainH = 72.0f, gap = 14.0f;
            const float laneH = ((float) getHeight() - mainH - 22.0f - gap * 5.0f) / 5.0f;
            if (l == Steps) return { 0.0f, 0.0f, (float) getWidth(), mainH };
            return { 0.0f, mainH + 22.0f + (float) (l - 1) * (laneH + gap), (float) getWidth(), laneH };
        }

        juce::Rectangle<float> cellBounds (int l, int i) const
        {
            const auto row = laneBounds (l);
            const float w = (row.getWidth() - labelWidth) / (float) seq::kMaxSteps;
            return { row.getX() + labelWidth + (float) i * w, row.getY(), w, row.getHeight() };
        }

        int stepAt (float x) const
        {
            const float w = ((float) getWidth() - labelWidth) / (float) seq::kMaxSteps;
            const int i = (int) std::floor ((x - labelWidth) / w);
            return i >= 0 && i < seq::kMaxSteps ? i : -1;
        }

        int laneAt (float y) const
        {
            for (int l = 0; l < NumLanes; ++l)
            {
                const auto b = laneBounds (l);
                if (y >= b.getY() - 7.0f && y <= b.getBottom() + 7.0f) return l;
            }
            return -1;
        }

        void apply (juce::Point<float> pos)
        {
            const int i = stepAt (pos.x);
            if (i < 0 || lane < 0) return;
            auto& s = editing.steps[(size_t) i];
            const auto b = laneBounds (lane);
            const float v = juce::jlimit (0.0f, 1.0f, (b.getBottom() - pos.y) / b.getHeight());
            switch (lane)
            {
                case Steps:    s.on = paintValue; break;
                case Velocity: s.velocity = std::max (0.05f, v); break;
                case Pitch:    s.pitch = std::round ((v * 2.0f - 1.0f) * 24.0f); break;
                case Ratchet:  s.ratchet = juce::jlimit (1, 4, 1 + (int) (v * 4.0f)); break;
                case Chance:   s.chance = std::round (v * 20.0f) / 20.0f; break;
                default:       s.decay = 0.25f + v * 1.75f; break;
            }
            proc.sound.setPattern (editing, false);
            repaint();
        }

        void timerCallback() override
        {
            const int step = proc.getCurrentStep();
            if (step != lastStep && step >= 0) stepGlow[(size_t) step] = 1.0f;
            lastStep = step;
            bool any = false;
            for (auto& gl : stepGlow) { if (gl > 0.0f) { gl = std::max (0.0f, gl - 1.0f / 60.0f / 0.35f); any = true; } }
            if ((any || step >= 0) && isShowing()) repaint();
        }

        TordenProcessor& proc;
        seq::Pattern editing, before;
        bool dragging = false, paintValue = true;
        int lane = -1, lastStep = -1;
        std::array<float, seq::kMaxSteps> stepGlow {};
    };

    // ---------------------------------------------------------------------------------------------
    // Transport for the standalone / stopped host: play button with a running indicator
    class PlayButton : public juce::Component, public juce::SettableTooltipClient, private juce::Timer
    {
    public:
        explicit PlayButton (TordenProcessor& p) : proc (p)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
            setTooltip ("Play the pattern with the internal tempo (when the host is stopped). With the host playing, the pattern follows the host.");
            startTimerHz (15);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const bool host = proc.isHostPlaying();
            const bool on = proc.isSequencerRunning() || host;
            g.setColour (on ? p.accent : p.panel);
            g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
            g.setColour (on ? p.accent : (hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            auto icon = r.removeFromLeft (r.getHeight()).reduced (r.getHeight() * 0.32f);
            g.setColour (on ? p.panel : p.text);
            if (proc.isSequencerRunning() && ! host)
                g.fillRect (icon.reduced (1.0f));
            else
            {
                juce::Path tri;
                tri.addTriangle (icon.getX() + 1.0f, icon.getY(), icon.getRight(), icon.getCentreY(), icon.getX() + 1.0f, icon.getBottom());
                g.fillPath (tri);
            }
            drawSpacedText (g, host ? "HOST" : (proc.isSequencerRunning() ? "STOP" : "PLAY"), r.withTrimmedRight (10.0f), 10.5f,
                            juce::Justification::centredLeft, 0.2f, true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseDown (const juce::MouseEvent&) override
        {
            proc.setSequencerRunning (! proc.isSequencerRunning());
            if (proc.isSequencerRunning())
                if (auto* param = proc.apvts.getParameter (P::paramId (P::SeqOn)); param->getValue() < 0.5f)
                    param->setValueNotifyingHost (1.0f);
            repaint();
        }

    private:
        void timerCallback() override { repaint(); }
        TordenProcessor& proc;
        bool hover = false;
    };

    class StepPage : public juce::Component
    {
    public:
        explicit StepPage (TordenProcessor& p)
            : proc (p), grid (p), play (p),
              seqOn (p.apvts, P::paramId (P::SeqOn), "Step mode"),
              rate (p.apvts, P::paramId (P::SeqRate)),
              length (p.apvts, P::paramId (P::SeqLength), "Steps"),
              swing (p.apvts, P::paramId (P::SeqSwing), "Swing"),
              tempo (p.apvts, P::paramId (P::SeqBpm), "Tempo"),
              clear ("Clear"), four ("4 / 4"), offbeat ("Broken"), random ("Random"), left (utf8 ("\xe2\x86\x90")), right (utf8 ("\xe2\x86\x92"))
        {
            for (auto* c : std::initializer_list<juce::Component*> { &grid, &play, &seqOn, &rate, &length, &swing, &tempo,
                                                                      &clear, &four, &offbeat, &random, &left, &right })
                addAndMakeVisible (c);
            for (auto* k : { &length, &swing, &tempo }) k->setLabelHeight (14);

            clear.onClick = [this] { edit ([] (seq::Pattern& pt) { for (auto& s : pt.steps) s = {}; }); };
            four.onClick = [this] { edit ([] (seq::Pattern& pt) { pt = seq::Pattern::fourOnTheFloor(); }); };
            offbeat.onClick = [this] { edit ([] (seq::Pattern& pt)
            {
                for (auto& s : pt.steps) s = {};
                for (int i : { 0, 3, 6, 10, 11, 16, 19, 22, 26, 29 }) { pt.steps[(size_t) i].on = true; pt.steps[(size_t) i].velocity = i % 4 == 0 ? 1.0f : 0.7f; }
                pt.steps[11].ratchet = 2;
                pt.steps[29].chance = 0.5f;
            }); };
            random.onClick = [this] { edit ([] (seq::Pattern& pt)
            {
                juce::Random r;
                for (int i = 0; i < seq::kMaxSteps; ++i)
                {
                    auto& s = pt.steps[(size_t) i];
                    s.on = i % 4 == 0 || r.nextFloat() < 0.18f;
                    s.velocity = i % 4 == 0 ? 1.0f : 0.5f + r.nextFloat() * 0.4f;
                    s.ratchet = r.nextFloat() < 0.1f ? 2 + r.nextInt (2) : 1;
                    s.chance = i % 4 == 0 ? 1.0f : (r.nextFloat() < 0.3f ? 0.5f : 1.0f);
                    s.pitch = r.nextFloat() < 0.1f ? (float) (r.nextBool() ? 12 : -5) : 0.0f;
                    s.decay = 1.0f;
                }
            }); };
            left.onClick = [this] { edit ([] (seq::Pattern& pt) { std::rotate (pt.steps.begin(), pt.steps.begin() + 1, pt.steps.end()); }); };
            right.onClick = [this] { edit ([] (seq::Pattern& pt) { std::rotate (pt.steps.rbegin(), pt.steps.rbegin() + 1, pt.steps.rend()); }); };
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            for (auto area : { barArea(), gridArea() })
            {
                auto r = area.toFloat().reduced (0.5f);
                g.setColour (p.panel);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (p.line);
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            g.drawText (utf8 ("click / drag to set steps \xc2\xb7 draw the lanes \xc2\xb7 double-click a lane value to reset"),
                        gridArea().reduced (16, 8).removeFromBottom (14).toFloat(), juce::Justification::centredRight);
        }

        void resized() override
        {
            auto bar = barArea().reduced (16, 8);
            play.setBounds (bar.removeFromLeft (96).withSizeKeepingCentre (96, 30));
            bar.removeFromLeft (16);
            seqOn.setBounds (bar.removeFromLeft (112).withSizeKeepingCentre (112, 22));
            bar.removeFromLeft (8);
            rate.setBounds (bar.removeFromLeft (200).withSizeKeepingCentre (200, 24));
            bar.removeFromLeft (16);
            length.setBounds (bar.removeFromLeft (70));
            swing.setBounds (bar.removeFromLeft (70));
            tempo.setBounds (bar.removeFromLeft (70));
            bar.removeFromLeft (12);
            for (auto* b : { &right, &left, &random, &offbeat, &four, &clear })
            {
                const int w = b == &left || b == &right ? 32 : 62;
                b->setBounds (bar.removeFromRight (w).withSizeKeepingCentre (w, 26));
                bar.removeFromRight (6);
            }
            grid.setBounds (gridArea().reduced (16, 14).withTrimmedBottom (14));
        }

        void tick() { grid.tick(); }

    private:
        juce::Rectangle<int> barArea() const  { return getLocalBounds().removeFromTop (66); }
        juce::Rectangle<int> gridArea() const { return getLocalBounds().withTrimmedTop (78); }

        void edit (std::function<void (seq::Pattern&)> fn)
        {
            auto pat = proc.sound.getPattern();
            fn (pat);
            proc.undoManager.beginNewTransaction ("Edit pattern");
            proc.sound.setPattern (pat, true);
            grid.repaint();
        }

        // Small pill button (local copy, the footer's lives in Footer.h)
        class Button : public juce::Component
        {
        public:
            explicit Button (juce::String t) : text (std::move (t)) { setMouseCursor (juce::MouseCursor::PointingHandCursor); }
            std::function<void()> onClick;
            void paint (juce::Graphics& g) override
            {
                const auto& p = Theme::get().palette();
                auto r = getLocalBounds().toFloat().reduced (0.5f);
                g.setColour (hover ? p.textDim : p.line);
                g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                g.setColour (hover ? p.text : p.text.withAlpha (0.8f));
                drawSpacedText (g, text.toUpperCase(), r, 9.5f, juce::Justification::centred, 0.14f, true);
            }
            void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
            void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
            void mouseUp (const juce::MouseEvent& e) override { if (getLocalBounds().contains (e.getPosition()) && onClick) onClick(); }
        private:
            juce::String text;
            bool hover = false;
        };

        TordenProcessor& proc;
        StepGrid grid;
        PlayButton play;
        Toggle seqOn;
        Segmented rate;
        Knob length, swing, tempo;
        Button clear, four, offbeat, random, left, right;
    };
}
