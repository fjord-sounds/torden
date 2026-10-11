#pragma once
#include "LayerControls.h"
#include "KickPreview.h"

namespace torden::ui
{
    // Waveform of one layer from the preview render, with its amplitude envelope drawn softly behind.
    class LayerWave : public juce::Component, private juce::ChangeListener
    {
    public:
        LayerWave (KickPreview& kp, int layerIndex) : preview (kp), layer (layerIndex) { preview.addChangeListener (this); }
        ~LayerWave() override { preview.removeChangeListener (this); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat();
            g.setColour (dark ? p.background.withAlpha (0.6f) : p.background);
            g.fillRoundedRectangle (r, 4.0f);
            r = r.reduced (4.0f, 4.0f);
            g.setColour (p.line);
            g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);

            const auto& res = preview.result();
            const auto& data = res.layers[(size_t) layer];
            if (data.empty()) return;
            const int len = std::min ((int) data.size(), (int) (std::max (0.08f, preview.kickDuration()) * res.sampleRate));
            const int n = (int) r.getWidth();
            float maxV = 0.0f;
            std::vector<float> hi ((size_t) n), lo ((size_t) n);
            for (int x = 0; x < n; ++x)
            {
                float a = 0.0f, b = 0.0f;
                for (int s = x * len / n; s < std::max (x * len / n + 1, (x + 1) * len / n) && s < len; ++s)
                {
                    a = std::max (a, data[(size_t) s]);
                    b = std::min (b, data[(size_t) s]);
                }
                hi[(size_t) x] = a; lo[(size_t) x] = b;
                maxV = std::max (maxV, std::max (a, -b));
            }
            // All layers share one scale, so their balance is visible
            const float scale = 1.0f / std::max (0.05f, globalMax());
            juce::Path path;
            path.startNewSubPath (r.getX(), r.getCentreY());
            for (int x = 0; x < n; ++x) path.lineTo (r.getX() + (float) x, r.getCentreY() - hi[(size_t) x] * scale * r.getHeight() * 0.5f);
            for (int x = n - 1; x >= 0; --x) path.lineTo (r.getX() + (float) x, r.getCentreY() - lo[(size_t) x] * scale * r.getHeight() * 0.5f);
            path.closeSubPath();
            const auto col = layerColour (layer, dark);
            g.setColour (col.withAlpha (0.55f));
            g.fillPath (path);
            g.setColour (col);
            g.strokePath (path, juce::PathStrokeType (0.6f));
            juce::ignoreUnused (maxV);
        }

    private:
        float globalMax() const
        {
            float m = 0.0f;
            for (const auto& l : preview.result().layers)
                for (float v : l) m = std::max (m, std::abs (v));
            return m;
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        KickPreview& preview;
        int layer;
    };

    // One layer with all of its controls.
    class LayerColumn : public juce::Component
    {
    public:
        LayerColumn (TordenProcessor& p, KickPreview& kp, int layerIndex)
            : chip (p, layerIndex), wave (kp, layerIndex), layer (layerIndex)
        {
            addAndMakeVisible (chip);
            addAndMakeVisible (wave);
            chip.selected = true;
            rows = buildLayerRows (layer, p.apvts);
            rows.character.addTo (*this);
            rows.common.addTo (*this);
            for (auto* row : { &rows.character, &rows.common })
                for (auto& [c, u] : row->items)
                    if (auto* k = dynamic_cast<Knob*> (c.get())) k->setLabelHeight (14);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);
            // A thin layer-coloured line on top
            g.setColour (layerColour (layer, dark));
            g.fillRoundedRectangle (r.getX() + 16.0f, r.getY(), r.getWidth() - 32.0f, 2.0f, 1.0f);
            g.setColour (p.line.withAlpha (0.6f));
            g.fillRect (14.0f, dividerY, r.getWidth() - 28.0f, 1.0f);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (10, 10);
            chip.setBounds (r.removeFromTop (28));
            r.removeFromTop (8);
            wave.setBounds (r.removeFromTop (56));
            r.removeFromTop (6);
            flow (rows.character, r);
            dividerY = (float) r.getY() + 2.0f;
            r.removeFromTop (8);
            flow (rows.common, r);
        }

    private:
        // Rows of 4 units: knobs take 1, selectors take 2-4
        static void flow (ControlRow& row, juce::Rectangle<int>& area)
        {
            const int unitW = area.getWidth() / 4;
            int used = 0;
            std::vector<std::pair<juce::Component*, int>> line;
            auto place = [&]
            {
                if (line.empty()) return;
                bool anyKnob = false;
                for (auto& [c, u] : line) anyKnob |= dynamic_cast<Knob*> (c) != nullptr;
                const int h = anyKnob ? 60 : 42;
                auto rowArea = area.removeFromTop (h);
                for (auto& [c, u] : line)
                {
                    auto cell = rowArea.removeFromLeft (unitW * u);
                    if (dynamic_cast<Knob*> (c) == nullptr) cell = cell.withSizeKeepingCentre (cell.getWidth() - 6, 40);
                    c->setBounds (cell);
                }
                area.removeFromTop (2);
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

        LayerChip chip;
        LayerWave wave;
        LayerRows rows;
        int layer;
        float dividerY = 0.0f;
    };

    class LayersPage : public juce::Component
    {
    public:
        LayersPage (TordenProcessor& p, KickPreview& kp)
        {
            for (int l = 0; l < 4; ++l)
            {
                columns[(size_t) l] = std::make_unique<LayerColumn> (p, kp, l);
                addAndMakeVisible (*columns[(size_t) l]);
            }
        }

        void resized() override
        {
            auto r = getLocalBounds();
            const int w = (r.getWidth() - 3 * 12) / 4;
            for (auto& c : columns)
            {
                c->setBounds (r.removeFromLeft (w));
                r.removeFromLeft (12);
            }
        }

    private:
        std::array<std::unique_ptr<LayerColumn>, 4> columns;
    };
}
