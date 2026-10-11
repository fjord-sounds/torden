#pragma once
#include "FxGraphs.h"
#include "LayerControls.h"

namespace torden::ui
{
    // Controls of every effect module (in the order they appear in the editor)
    inline void buildFxControls (int type, APVTS& s, ControlRow& row)
    {
        auto knob = [&] (P::Id id, const char* label) { row.add (makeKnob (s, id, label)); };
        auto seg = [&] (P::Id id, const char* label, float units, juce::StringArray names = {})
        {
            row.add (labelled (std::make_unique<Segmented> (s, P::paramId (id), nullptr, names), label), units);
        };
        auto toggle = [&] (P::Id id, const char* label, const char* title)
        {
            row.add (labelled (std::make_unique<Toggle> (s, P::paramId (id), label), title), 1.2f);
        };

        switch ((FxType) type)
        {
            case FxType::Saturation: seg (P::SatMode, "Curve", 4.5f); knob (P::SatDrive, "Drive"); knob (P::SatBias, "Bias"); knob (P::SatTone, "Tone");
                                     seg (P::SatOs, "Oversampling", 2.0f); knob (P::SatOut, "Output"); knob (P::SatMix, "Mix"); break;
            case FxType::Crush:      knob (P::CrushBits, "Bits"); knob (P::CrushRate, "Downsample"); knob (P::CrushJitter, "Jitter"); knob (P::CrushMix, "Mix"); break;
            case FxType::Transient:  knob (P::TransAttack, "Attack"); knob (P::TransSustain, "Sustain"); knob (P::TransSpeed, "Speed"); knob (P::TransOut, "Output"); break;
            case FxType::Compressor: knob (P::CompThresh, "Threshold"); knob (P::CompRatio, "Ratio"); knob (P::CompAttack, "Attack"); knob (P::CompRelease, "Release");
                                     knob (P::CompKnee, "Knee"); knob (P::CompMakeup, "Makeup"); knob (P::CompHpf, "SC Filter"); knob (P::CompMix, "Mix"); break;
            case FxType::Eq:         knob (P::EqLowCut, "Low Cut"); knob (P::EqLowFreq, "Low Freq"); knob (P::EqLowGain, "Low");
                                     knob (P::EqMidFreq, "Mid Freq"); knob (P::EqMidGain, "Mid"); knob (P::EqMidQ, "Mid Q");
                                     knob (P::EqMid2Freq, "Mid 2 Freq"); knob (P::EqMid2Gain, "Mid 2"); knob (P::EqMid2Q, "Mid 2 Q");
                                     knob (P::EqHighFreq, "High Freq"); knob (P::EqHighGain, "High"); knob (P::EqHighCut, "High Cut"); break;
            case FxType::Filter:     seg (P::FiltType, "Type", 4.5f, { "Ladder", "LP", "BP", "HP", "Notch" }); knob (P::FiltCutoff, "Cutoff"); knob (P::FiltReso, "Resonance");
                                     knob (P::FiltEnv, "Env Amount"); knob (P::FiltDecay, "Env Decay"); knob (P::FiltDrive, "Drive"); knob (P::FiltMix, "Mix"); break;
            case FxType::Reverb:     seg (P::VerbMode, "Space", 3.0f); knob (P::VerbSize, "Size"); knob (P::VerbDecay, "Decay"); knob (P::VerbDamp, "Damping");
                                     knob (P::VerbPre, "Pre-Delay"); knob (P::VerbLowCut, "Low Cut"); knob (P::VerbWidth, "Width"); knob (P::VerbDuck, "Ducking"); knob (P::VerbMix, "Mix"); break;
            case FxType::Delay:      toggle (P::DlySync, "Sync", "Tempo"); seg (P::DlyBeats, "Note", 5.0f); knob (P::DlyTime, "Time"); knob (P::DlyFeedback, "Feedback");
                                     toggle (P::DlyPingPong, "Ping-Pong", "Stereo"); knob (P::DlyDamp, "Damping"); knob (P::DlyLowCut, "Low Cut"); knob (P::DlyDuck, "Ducking"); knob (P::DlyMix, "Mix"); break;
            case FxType::Motion:     seg (P::ChrMode, "Mode", 3.0f); knob (P::ChrRate, "Rate"); knob (P::ChrDepth, "Depth"); knob (P::ChrFeedback, "Feedback"); knob (P::ChrMix, "Mix"); break;
            case FxType::Width:      knob (P::WidthAmount, "Width"); knob (P::WidthMono, "Mono Below"); break;
            case FxType::Count:      seg (P::ClipMode, "Curve", 3.0f); knob (P::ClipDrive, "Drive"); knob (P::ClipCeiling, "Ceiling"); break;
            default: break;
        }
    }

    // ---------------------------------------------------------------------------------------------
    // The chain as tiles: drag to reorder, click to edit, dot = on / off. Every tile shows its effect live.
    class FxStrip : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        explicit FxStrip (TordenProcessor& p) : proc (p) { setTooltip ("Drag to change the order, click to edit, the dot switches an effect on / off"); }

        std::function<void (int)> onSelect;
        int selected = (int) FxType::Saturation;

        void tick (float dt, const P::Values& v)
        {
            values = v;
            for (int t = 0; t < numTiles; ++t) anims[(size_t) t].tick (proc, dt, t);
            repaint();
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto order = dragOrder.has_value() ? *dragOrder : proc.getFxOrder();
            // Signal flow line behind the tiles
            g.setColour (p.line);
            g.fillRect (0.0f, (float) getHeight() * 0.5f, (float) getWidth(), 1.0f);

            for (int slot = 0; slot < numTiles; ++slot)
            {
                const int type = typeAt (order, slot);
                auto r = tileBounds (slot);
                if (dragType == type && dragOrder.has_value())
                    r = r.withX (juce::jlimit (0.0f, (float) getWidth() - r.getWidth(), dragX - grabOffset));
                drawTile (g, type, slot, r);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int s = slotAt (e.position.x);
            if (s != hover) { hover = s; repaint(); }
            setMouseCursor (s >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int s = slotAt (e.position.x);
            if (s < 0) return;
            const auto order = proc.getFxOrder();
            const int type = typeAt (order, s);
            if (powerBounds (tileBounds (s)).expanded (4.0f).contains (e.position))
            {
                auto* param = proc.apvts.getParameter (P::paramId (fx::fxOnParam (type)));
                param->beginChangeGesture();
                param->setValueNotifyingHost (param->getValue() > 0.5f ? 0.0f : 1.0f);
                param->endChangeGesture();
                return;
            }
            dragType = type == fx::kClipperType ? -1 : type;
            grabOffset = e.position.x - tileBounds (s).getX();
            selected = type;
            if (onSelect) onSelect (type);
            repaint();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragType < 0 || e.getDistanceFromDragStart() < 4) return;
            dragX = e.position.x;
            auto order = proc.getFxOrder();
            if (dragOrder.has_value()) order = *dragOrder;
            // Move the dragged module to the slot under the mouse
            const int target = juce::jlimit (0, fx::kNumFx - 1, slotAt (e.position.x - grabOffset + tileWidth() * 0.5f));
            auto it = std::find (order.begin(), order.end(), dragType);
            const int from = (int) std::distance (order.begin(), it);
            if (from != target)
            {
                const int t = order[(size_t) from];
                if (from < target) for (int i = from; i < target; ++i) order[(size_t) i] = order[(size_t) i + 1];
                else               for (int i = from; i > target; --i) order[(size_t) i] = order[(size_t) i - 1];
                order[(size_t) target] = t;
            }
            dragOrder = order;
            repaint();
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragOrder.has_value() && *dragOrder != proc.getFxOrder())
            {
                proc.undoManager.beginNewTransaction ("Effect order");
                proc.setFxOrder (*dragOrder);
            }
            dragOrder.reset();
            dragType = -1;
            repaint();
        }

    private:
        static constexpr int numTiles = fx::kNumFx + 1;
        static int typeAt (const fx::Order& order, int slot) { return slot < fx::kNumFx ? order[(size_t) slot] : fx::kClipperType; }
        float tileWidth() const { return ((float) getWidth() - (float) (numTiles - 1) * gap) / (float) numTiles; }
        juce::Rectangle<float> tileBounds (int slot) const { return { (float) slot * (tileWidth() + gap), 0.0f, tileWidth(), (float) getHeight() }; }
        static juce::Rectangle<float> powerBounds (juce::Rectangle<float> tile) { return { tile.getRight() - 22.0f, tile.getY() + 8.0f, 14.0f, 14.0f }; }
        int slotAt (float x) const
        {
            if (x < 0.0f || x > (float) getWidth()) return -1;
            return juce::jlimit (0, numTiles - 1, (int) (x / (tileWidth() + gap)));
        }

        void drawTile (juce::Graphics& g, int type, int slot, juce::Rectangle<float> r)
        {
            const auto& p = Theme::get().palette();
            const bool on = values.b (fx::fxOnParam (type));
            const bool sel = type == selected;
            r = r.reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (sel ? p.accent : (slotAt (r.getCentreX()) == hover ? p.textDim : p.line));
            g.drawRoundedRectangle (r, 6.0f, sel ? 1.4f : 1.0f);

            g.setColour (on ? p.text : p.textDim);
            drawSpacedText (g, (type == (int) FxType::Compressor ? juce::String ("Comp") : fx::moduleName (type)).toUpperCase(), juce::Rectangle<float> (r.getX() + 10.0f, r.getY() + 6.0f, r.getWidth() - 34.0f, 18.0f),
                            9.0f, juce::Justification::centredLeft, 0.14f, sel);
            g.setColour (p.textDim.withAlpha (0.7f));
            g.setFont (font (9.0f, Weight::Medium));
            g.drawText (type == fx::kClipperType ? juce::String ("OUT") : juce::String (slot + 1), juce::Rectangle<float> (r.getX() + 10.0f, r.getBottom() - 18.0f, 20.0f, 14.0f), juce::Justification::centredLeft);

            const auto dot = powerBounds (r).reduced (2.0f);
            if (on) { g.setColour (p.accent); g.fillEllipse (dot); }
            else    { g.setColour (p.textDim); g.drawEllipse (dot.reduced (0.5f), 1.2f); }

            auto graph = r.reduced (10.0f, 0.0f).withTrimmedTop (28.0f).withTrimmedBottom (20.0f);
            juce::Graphics::ScopedSaveState s (g);
            g.reduceClipRegion (graph.toNearestInt());
            if (! on) g.setOpacity (0.5f);
            drawFxGraph (g, type, graph, values, proc, anims[(size_t) type], true);
        }

        static constexpr float gap = 8.0f;
        TordenProcessor& proc;
        P::Values values;
        std::array<FxAnim, fx::kNumFx + 1> anims;
        std::optional<fx::Order> dragOrder;
        int dragType = -1, hover = -1;
        float dragX = 0.0f, grabOffset = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Big graph of the selected effect. The EQ graph is interactive: drag the band points.
    class FxGraphPanel : public juce::Component
    {
    public:
        explicit FxGraphPanel (TordenProcessor& p) : proc (p) {}

        int type = 0;

        void tick (float dt, const P::Values& v)
        {
            values = v;
            anim.tick (proc, dt, type);
            repaint();
        }

        void setType (int t) { type = t; anim = {}; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setGradientFill (juce::ColourGradient (dark ? juce::Colour (0xff0e1724) : p.panel, 0.0f, r.getY(),
                                                     dark ? p.panel : p.background, 0.0f, r.getBottom(), false));
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            const auto area = graphArea();
            {
                juce::Graphics::ScopedSaveState s (g);
                g.reduceClipRegion (area.expanded (2.0f).toNearestInt());
                drawFxGraph (g, type, area, values, proc, anim, false);
            }
            if (type == (int) FxType::Eq) drawEqHandles (g, area);
            if (type == (int) FxType::Eq || type == (int) FxType::Filter)
            {
                g.setColour (p.textDim);
                g.setFont (font (9.0f, Weight::Medium));
                for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
                    g.drawText (hz >= 1000.0f ? juce::String ((int) (hz / 1000.0f)) + "k" : juce::String ((int) hz),
                                juce::Rectangle<float> (freqToX (hz, area) - 20.0f, area.getBottom() + 4.0f, 40.0f, 12.0f), juce::Justification::centred);
            }
        }

        // ---- EQ handles
        void mouseDown (const juce::MouseEvent& e) override
        {
            dragBand = type == (int) FxType::Eq ? bandAt (e.position) : -1;
            if (dragBand >= 0)
                for (auto id : bandParams (dragBand)) if (id != P::Count) proc.apvts.getParameter (P::paramId (id))->beginChangeGesture();
        }

        void mouseDrag (const juce::MouseEvent& e) override
        {
            if (dragBand < 0) return;
            const auto area = graphArea();
            const auto ids = bandParams (dragBand);
            setPlain (ids[0], juce::jlimit (20.0f, 20000.0f, xToFreq (e.position.x, area)));
            if (ids[1] != P::Count)
                setPlain (ids[1], juce::jlimit (-18.0f, 18.0f, (area.getCentreY() - e.position.y) / (area.getHeight() * 0.5f) * 20.0f));
        }

        void mouseUp (const juce::MouseEvent&) override
        {
            if (dragBand >= 0)
                for (auto id : bandParams (dragBand)) if (id != P::Count) proc.apvts.getParameter (P::paramId (id))->endChangeGesture();
            dragBand = -1;
        }

        void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
        {
            // Wheel over a peak band changes its Q
            const int b = type == (int) FxType::Eq ? bandAt (e.position) : -1;
            if (b == 2 || b == 3)
            {
                const auto id = b == 2 ? P::EqMidQ : P::EqMid2Q;
                auto* param = proc.apvts.getParameter (P::paramId (id));
                param->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, param->getValue() + w.deltaY * 0.15f));
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int b = type == (int) FxType::Eq ? bandAt (e.position) : -1;
            if (b != hoverBand) { hoverBand = b; repaint(); }
            setMouseCursor (b >= 0 ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::NormalCursor);
        }

    private:
        juce::Rectangle<float> graphArea() const { return getLocalBounds().toFloat().reduced (20.0f, 18.0f).withTrimmedBottom (14.0f); }

        // Bands: 0 low cut, 1 low shelf, 2 mid, 3 mid 2, 4 high shelf, 5 high cut  -> { freq, gain }
        static std::array<P::Id, 2> bandParams (int b)
        {
            switch (b)
            {
                case 0:  return { P::EqLowCut, P::Count };
                case 1:  return { P::EqLowFreq, P::EqLowGain };
                case 2:  return { P::EqMidFreq, P::EqMidGain };
                case 3:  return { P::EqMid2Freq, P::EqMid2Gain };
                case 4:  return { P::EqHighFreq, P::EqHighGain };
                default: return { P::EqHighCut, P::Count };
            }
        }

        juce::Point<float> bandPos (int b, juce::Rectangle<float> area) const
        {
            const auto ids = bandParams (b);
            const float x = freqToX (values[ids[0]], area);
            const float db = ids[1] != P::Count ? values[ids[1]] : 0.0f;
            return { x, area.getCentreY() - db / 20.0f * area.getHeight() * 0.5f };
        }

        int bandAt (juce::Point<float> pos) const
        {
            const auto area = graphArea();
            for (int b = 0; b < 6; ++b)
                if (bandPos (b, area).getDistanceFrom (pos) < 10.0f) return b;
            return -1;
        }

        void drawEqHandles (juce::Graphics& g, juce::Rectangle<float> area)
        {
            const auto& p = Theme::get().palette();
            static const char* names[] = { "LC", "L", "M", "M2", "H", "HC" };
            for (int b = 0; b < 6; ++b)
            {
                const auto pos = bandPos (b, area);
                const bool hot = b == hoverBand || b == dragBand;
                if (hot)
                {
                    g.setColour (p.accent.withAlpha (0.2f));
                    g.fillEllipse (juce::Rectangle<float> (26.0f, 26.0f).withCentre (pos));
                }
                g.setColour (p.panel);
                g.fillEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (pos));
                g.setColour (p.accent);
                g.drawEllipse (juce::Rectangle<float> (16.0f, 16.0f).withCentre (pos), 1.4f);
                g.setColour (p.text);
                g.setFont (font (8.0f, Weight::SemiBold));
                g.drawText (names[b], juce::Rectangle<float> (16.0f, 16.0f).withCentre (pos), juce::Justification::centred);
            }
        }

        void setPlain (P::Id id, float v)
        {
            auto* param = proc.apvts.getParameter (P::paramId (id));
            param->setValueNotifyingHost (param->convertTo0to1 (v));
        }

        TordenProcessor& proc;
        P::Values values;
        FxAnim anim;
        int dragBand = -1, hoverBand = -1;
    };

    // ---------------------------------------------------------------------------------------------
    class FxPage : public juce::Component, private juce::Timer
    {
    public:
        explicit FxPage (TordenProcessor& p) : proc (p), strip (p), graph (p)
        {
            addAndMakeVisible (strip);
            addAndMakeVisible (graph);
            for (int t = 0; t <= fx::kNumFx; ++t)
            {
                buildFxControls (t, p.apvts, controls[(size_t) t]);
                controls[(size_t) t].addTo (*this);
                power[(size_t) t] = std::make_unique<Toggle> (p.apvts, P::paramId (fx::fxOnParam (t)), "On");
                addChildComponent (*power[(size_t) t]);
            }
            strip.onSelect = [this] (int t) { selectEffect (t); };
            selectEffect ((int) FxType::Saturation);
            startTimerHz (30);
        }

        void selectEffect (int type)
        {
            selected = juce::jlimit (0, fx::kNumFx, type);
            strip.selected = selected;
            graph.setType (selected);
            for (int t = 0; t <= fx::kNumFx; ++t)
            {
                controls[(size_t) t].setVisible (t == selected);
                power[(size_t) t]->setVisible (t == selected);
            }
            resized();
            repaint();
        }

        void tick()
        {
            const auto v = proc.currentValues();
            strip.tick (1.0f / 30.0f, v);
            graph.tick (1.0f / 30.0f, v);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = editorArea().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            g.setColour (p.text);
            drawSpacedText (g, fx::moduleName (selected).toUpperCase(), r.reduced (16.0f, 0.0f).removeFromTop (40.0f), 12.0f,
                            juce::Justification::centredLeft, 0.25f, true);
            g.setColour (p.textDim);
            drawSpacedText (g, fxDescription (selected), r.reduced (16.0f, 0.0f).withTrimmedTop (30.0f).removeFromTop (20.0f), 9.5f,
                            juce::Justification::centredLeft, 0.06f);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            strip.setBounds (r.removeFromTop (118));
            r.removeFromTop (12);
            graph.setBounds (r.removeFromLeft (470));
            r.removeFromLeft (12);
            auto e = editorArea().reduced (16, 0);
            auto head = e.removeFromTop (40);
            power[(size_t) selected]->setBounds (head.removeFromRight (60).withSizeKeepingCentre (60, 20));
            e.removeFromTop (16);

            // Controls in rows of 4 units
            auto& row = controls[(size_t) selected];
            const int unitW = e.getWidth() / 4;
            int used = 0;
            std::vector<std::pair<juce::Component*, int>> line;
            auto place = [&]
            {
                if (line.empty()) return;
                bool anyKnob = false;
                for (auto& [c, u] : line) anyKnob |= dynamic_cast<Knob*> (c) != nullptr;
                auto rowArea = e.removeFromTop (anyKnob ? 84 : 48);
                for (auto& [c, u] : line)
                {
                    auto cell = rowArea.removeFromLeft (unitW * u);
                    if (dynamic_cast<Knob*> (c) == nullptr) cell = cell.withSizeKeepingCentre (cell.getWidth() - 8, 40);
                    c->setBounds (cell);
                }
                e.removeFromTop (4);
                line.clear();
                used = 0;
            };
            for (auto& [c, units] : row.items)
            {
                const int u = juce::jlimit (1, 4, (int) std::ceil (units - 0.25f));
                if (used + u > 4) place();
                line.emplace_back (c.get(), u);
                used += u;
            }
            place();
        }

    private:
        juce::Rectangle<int> editorArea() const { return getLocalBounds().withTrimmedTop (130).withTrimmedLeft (482); }

        static juce::String fxDescription (int t)
        {
            static const char* d[] = { "Six transfer curves with bias, tilt tone and oversampling",
                                       "Bit depth and sample rate reduction",
                                       "More or less attack and sustain, independent of level",
                                       "Soft-knee compressor with parallel mix and sidechain filter",
                                       "Low cut, shelves, two bells, high cut  -  drag the points, wheel = Q",
                                       "Ladder or state-variable filter with an envelope on every hit",
                                       "Room, plate or hall with low cut and ducking",
                                       "Tempo-synced echo with damping, low cut and ducking",
                                       "Chorus, phaser or flanger for tails and noise",
                                       "Stereo width, mono below the crossover",
                                       "Always last: soft, hard or tape clipping up to the ceiling" };
            return d[t];
        }

        void timerCallback() override { if (isShowing()) tick(); }

        TordenProcessor& proc;
        FxStrip strip;
        FxGraphPanel graph;
        std::array<ControlRow, fx::kNumFx + 1> controls;
        std::array<std::unique_ptr<Toggle>, fx::kNumFx + 1> power;
        int selected = 0;
    };
}
