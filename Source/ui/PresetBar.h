#pragma once
#include "Theme.h"
#include "../presets/PresetManager.h"
#include "Controls.h"

namespace torden::ui
{
    // Horizontal preset bar:  ‹   TECHNO · Granite   ›  |  ···  ⤓
    // Clicking the centre opens the preset browser, the dots open the options menu, the icon on the right saves.
    class PresetBar : public juce::Component,
                      public juce::SettableTooltipClient,
                      private juce::ChangeListener
    {
    public:
        explicit PresetBar (PresetManager& pm) : presets (pm)
        {
            presets.addChangeListener (this);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        ~PresetBar() override { presets.removeChangeListener (this); }

        std::function<void()> onOpenBrowser, onSave;

        void setBrowserOpen (bool open) { browserOpen = open; repaint(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = r.getHeight() * 0.5f;

            g.setColour (p.panel);
            g.fillRoundedRectangle (r, rad);
            g.setColour (browserOpen ? p.accent : (hover != Zone::None ? p.textDim : p.line));
            g.drawRoundedRectangle (r, rad, 1.0f);

            drawChevron (g, zoneBounds (Zone::Prev), true,  hover == Zone::Prev ? p.accent : p.textDim);
            drawChevron (g, zoneBounds (Zone::Next), false, hover == Zone::Next ? p.accent : p.textDim);

            // Divider, lock + save icons
            const auto lock = zoneBounds (Zone::Lock).toFloat();
            const auto save = zoneBounds (Zone::Save).toFloat();
            g.setColour (p.line);
            g.fillRect (lock.getX(), lock.getY() + 9.0f, 1.0f, lock.getHeight() - 18.0f);
            g.setColour (hover == Zone::Lock ? p.accent : p.textDim);
            for (int i = -1; i <= 1; ++i)
                g.fillEllipse (lock.getCentreX() + 2.0f + (float) i * 5.0f - 1.3f, lock.getCentreY() - 1.3f, 2.6f, 2.6f);
            drawSaveIcon (g, save, hover == Zone::Save ? p.accent : p.textDim);

            // Category + name, centred together
            const auto category = presets.getCurrentCategory().toUpperCase();
            const auto name = presets.getCurrentName();
            const auto catFont = font (10.5f, true).withExtraKerningFactor (0.22f);
            const auto nameFont = font (15.0f);
            const float catW = category.isEmpty() ? 0.0f : juce::GlyphArrangement::getStringWidth (catFont, category) + 22.0f;
            const float nameW = juce::GlyphArrangement::getStringWidth (nameFont, name);
            auto centre = zoneBounds (Zone::Centre).toFloat();
            float x = std::max (centre.getX(), centre.getCentreX() - (catW + nameW) * 0.5f);

            if (category.isNotEmpty())
            {
                g.setColour (p.accent);
                g.setFont (catFont);
                g.drawText (category, juce::Rectangle<float> (x, centre.getY(), catW, centre.getHeight()),
                            juce::Justification::centredLeft, false);
                g.setColour (p.line);
                g.fillEllipse (x + catW - 13.0f, centre.getCentreY() - 1.5f, 3.0f, 3.0f);
                x += catW;
            }

            g.setColour (p.text);
            g.setFont (nameFont);
            g.drawText (name, juce::Rectangle<float> (x, centre.getY(), centre.getRight() - x, centre.getHeight()),
                        juce::Justification::centredLeft, true);
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto z = zoneAt (e.getPosition());
            if (z != hover)
            {
                hover = z;
                setTooltip (z == Zone::Save ? "Save preset" : z == Zone::Centre ? "Browse presets" : z == Zone::Lock ? "Options" : "");
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hover = Zone::None; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            switch (zoneAt (e.getPosition()))
            {
                case Zone::Prev:   presets.previous(); break;
                case Zone::Next:   presets.next();     break;
                case Zone::Centre: if (onOpenBrowser) onOpenBrowser(); break;
                case Zone::Save:   if (onSave) onSave(); break;
                case Zone::Lock:   showOptions(); break;
                case Zone::None:   break;
            }
        }

    private:
        enum class Zone { None, Prev, Centre, Next, Lock, Save };

        void showOptions()
        {
            juce::PopupMenu m;
            m.addItem (1, "Init sound");
            m.addSeparator();
            m.addItem (2, "Load step patterns with presets", true, presets.getLoadPatterns());
            m.addSeparator();
            m.addItem (3, "Show user preset folder");
            m.addItem (4, "Rescan presets");
            m.addSeparator();
            m.addItem (5, "Reduce motion", true, Theme::get().reduceMotion());
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r == 1) presets.loadInit();
                if (r == 2) presets.setLoadPatterns (! presets.getLoadPatterns());
                if (r == 3)
                {
                    auto folder = PresetManager::getUserPresetFolder();
                    folder.createDirectory();
                    folder.revealToUser();
                }
                if (r == 4) presets.rescan();
                if (r == 5) Theme::get().setFlag ("reduceMotion", ! Theme::get().reduceMotion());
                repaint();
            });
        }

        juce::Rectangle<int> zoneBounds (Zone z) const
        {
            auto r = getLocalBounds();
            const int h = getHeight();
            auto save = r.removeFromRight (h);
            auto lock = r.removeFromRight (h - 2);
            auto prev = r.removeFromLeft (h + 4);
            auto next = r.removeFromRight (h);
            switch (z)
            {
                case Zone::Lock: return lock;
                case Zone::Prev: return prev;
                case Zone::Next: return next;
                case Zone::Save: return save;
                case Zone::Centre: return r;
                case Zone::None: break;
            }
            return {};
        }

        Zone zoneAt (juce::Point<int> pos) const
        {
            for (auto z : { Zone::Prev, Zone::Next, Zone::Lock, Zone::Save, Zone::Centre })
                if (zoneBounds (z).contains (pos)) return z;
            return Zone::None;
        }

        static void drawChevron (juce::Graphics& g, juce::Rectangle<int> area, bool left, juce::Colour c)
        {
            const auto ctr = area.toFloat().getCentre();
            const float s = 4.0f, dir = left ? 1.0f : -1.0f;
            juce::Path path;
            path.startNewSubPath (ctr.x + s * 0.5f * dir, ctr.y - s);
            path.lineTo (ctr.x - s * 0.5f * dir, ctr.y);
            path.lineTo (ctr.x + s * 0.5f * dir, ctr.y + s);
            g.setColour (c);
            g.strokePath (path, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // Arrow down into a tray – "save"
        static void drawSaveIcon (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour c)
        {
            const auto ctr = area.getCentre();
            juce::Path p;
            p.startNewSubPath (ctr.x, ctr.y - 6.0f);
            p.lineTo (ctr.x, ctr.y + 2.0f);
            p.startNewSubPath (ctr.x - 3.5f, ctr.y - 1.5f);
            p.lineTo (ctr.x, ctr.y + 2.0f);
            p.lineTo (ctr.x + 3.5f, ctr.y - 1.5f);
            p.startNewSubPath (ctr.x - 6.0f, ctr.y + 2.0f);
            p.lineTo (ctr.x - 6.0f, ctr.y + 6.0f);
            p.lineTo (ctr.x + 6.0f, ctr.y + 6.0f);
            p.lineTo (ctr.x + 6.0f, ctr.y + 2.0f);
            g.setColour (c);
            g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }

        PresetManager& presets;
        Zone hover = Zone::None;
        bool browserOpen = false;
    };
}
