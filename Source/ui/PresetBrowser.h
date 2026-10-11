#pragma once
#include "Theme.h"
#include "../presets/PresetManager.h"

namespace torden::ui
{
    // Category labels for the UI (internally the short names are kept).
    inline juce::String categoryLabel (const juce::String& c)
    {
        static const std::map<juce::String, juce::String> labels {
            { "Acoustic", "Acoustic" }, { "Cinematic", "Cinematic" } };
        const auto it = labels.find (c);
        return it != labels.end() ? it->second : c;
    }

    inline juce::Path starPath (juce::Rectangle<float> r)
    {
        juce::Path p;
        p.addStar (r.getCentre(), 5, r.getWidth() * 0.22f, r.getWidth() * 0.5f, 0.0f);
        return p;
    }

    // Simple pill button (primary = filled with the accent).
    class PillButton : public juce::Component
    {
    public:
        PillButton (juce::String t, bool isPrimary) : text (std::move (t)), primary (isPrimary)
        {
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const float rad = r.getHeight() * 0.5f;
            if (primary)
            {
                g.setColour (hover ? p.accent.brighter (0.1f) : p.accent);
                g.fillRoundedRectangle (r, rad);
                g.setColour (p.panel);
            }
            else
            {
                g.setColour (hover ? p.textDim : p.line);
                g.drawRoundedRectangle (r, rad, 1.0f);
                g.setColour (p.text);
            }
            drawSpacedText (g, text.toUpperCase(), r, 10.5f, juce::Justification::centred, 0.18f, true);
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }
        void mouseUp (const juce::MouseEvent& e) override
        {
            if (getLocalBounds().contains (e.getPosition()) && onClick) onClick();
        }

        std::function<void()> onClick;

    private:
        juce::String text;
        bool primary, hover = false;
    };

    inline void styleTextEditor (juce::TextEditor& ed)
    {
        const auto& p = Theme::get().palette();
        ed.setColour (juce::TextEditor::backgroundColourId, p.background);
        ed.setColour (juce::TextEditor::textColourId, p.text);
        ed.setColour (juce::TextEditor::outlineColourId, p.line);
        ed.setColour (juce::TextEditor::focusedOutlineColourId, p.accent);
        ed.setColour (juce::TextEditor::highlightColourId, p.accentSoft);
        ed.setColour (juce::TextEditor::highlightedTextColourId, p.text);
        ed.setColour (juce::CaretComponent::caretColourId, p.accent);
        ed.setTextToShowWhenEmpty (ed.getTextToShowWhenEmpty(), p.textDim);
        ed.setFont (font (14.0f));
        ed.setIndents (18, 0);
        ed.setJustification (juce::Justification::centredLeft);
        ed.applyFontToAllText (font (14.0f));
        ed.applyColourToAllText (p.text);
    }

    // ------------------------------------------------------------------------------------------
    // Save dialog: card with a name field and category chips.
    class SaveDialog : public juce::Component
    {
    public:
        explicit SaveDialog (PresetManager& pm) : presets (pm), save ("Save", true), cancel ("Cancel", false)
        {
            name.setTextToShowWhenEmpty ("Preset name", juce::Colours::grey);
            name.onReturnKey = [this] { commit(); };
            name.onEscapeKey = [this] { close(); };
            addAndMakeVisible (name);
            tags.setTextToShowWhenEmpty ("Tags, e.g. Warm, Wide, Evolving", juce::Colours::grey);
            tags.onReturnKey = [this] { commit(); };
            tags.onEscapeKey = [this] { close(); };
            addAndMakeVisible (tags);
            addAndMakeVisible (save);
            addAndMakeVisible (cancel);
            save.onClick = [this] { commit(); };
            cancel.onClick = [this] { close(); };
        }

        void open()
        {
            const auto current = presets.getCurrentName();
            name.setText (current == "Init" ? juce::String() : current, false);
            const int c = categories().indexOf (presets.getCurrentCategory());
            category = c >= 0 ? c : 0;
            tags.setText (presets.getCurrentTags().joinIntoString (", "), false);
            styleTextEditor (name);
            styleTextEditor (tags);
            setVisible (true);
            toFront (true);
            name.grabKeyboardFocus();
            name.selectAll();
        }

        void close() { setVisible (false); if (onClose) onClose(); }

        std::function<void()> onClose;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            g.fillAll (p.background.withAlpha (0.82f));

            const auto c = card().toFloat();
            g.setColour (juce::Colours::black.withAlpha (0.06f));
            g.fillRoundedRectangle (c.translated (0.0f, 6.0f).expanded (2.0f), 14.0f);
            g.setColour (p.panel);
            g.fillRoundedRectangle (c, 12.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (c, 12.0f, 1.0f);

            auto inner = card().reduced (28, 24);
            g.setColour (p.text);
            drawSpacedText (g, "SAVE PRESET", inner.removeFromTop (22).toFloat(), 12.0f, juce::Justification::centredLeft, 0.25f, true);
            g.setColour (p.textDim);
            g.setFont (font (13.0f));
            g.drawText ("Your sound will appear under \"My Presets\".", inner.removeFromTop (24).toFloat(), juce::Justification::centredLeft);

            g.setColour (p.textDim);
            drawSpacedText (g, "CATEGORY", chipsLabelArea().toFloat(), 10.0f, juce::Justification::centredLeft, 0.2f);
            drawSpacedText (g, "TAGS", chipsLabelArea().toFloat().translated (0.0f, 98.0f), 10.0f, juce::Justification::centredLeft, 0.2f);

            for (int i = 0; i < categories().size(); ++i)
            {
                const auto r = chipBounds (i).toFloat();
                const bool on = i == category, hov = i == hoverChip;
                if (on)
                {
                    g.setColour (p.accent);
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                }
                else
                {
                    g.setColour (hov ? p.textDim : p.line);
                    g.drawRoundedRectangle (r.reduced (0.5f), r.getHeight() * 0.5f, 1.0f);
                }
                g.setColour (on ? p.panel : p.text);
                drawSpacedText (g, categoryLabel (categories()[i]).toUpperCase(), r, 10.0f, juce::Justification::centred, 0.12f, on);
            }
        }

        void resized() override
        {
            auto inner = card().reduced (28, 24);
            inner.removeFromTop (58);
            name.setBounds (inner.removeFromTop (40));
            tags.setBounds (card().reduced (28, 0).withY (card().getY() + 270).withHeight (38));
            auto buttons = inner.removeFromBottom (34);
            save.setBounds (buttons.removeFromRight (120));
            buttons.removeFromRight (10);
            cancel.setBounds (buttons.removeFromRight (120));
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int h = chipAt (e.getPosition());
            if (h != hoverChip) { hoverChip = h; repaint(); }
            setMouseCursor (h >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int c = chipAt (e.getPosition());
            if (c >= 0) { category = c; repaint(); }
            else if (! card().contains (e.getPosition())) close();
        }

    private:
        static juce::StringArray categories()
        {
            auto c = PresetManager::categoryOrder;
            c.removeString ("User");
            return c;
        }

        juce::Rectangle<int> card() const { return getLocalBounds().withSizeKeepingCentre (520, 400); }
        juce::Rectangle<int> chipsLabelArea() const { return card().reduced (28, 0).withY (card().getY() + 152).withHeight (18); }

        juce::Rectangle<int> chipBounds (int i) const
        {
            constexpr int perRow = 5, h = 28, gap = 8;
            const auto area = card().reduced (28, 0);
            const int w = (area.getWidth() - (perRow - 1) * gap) / perRow;
            return { area.getX() + (i % perRow) * (w + gap), card().getY() + 174 + (i / perRow) * (h + gap), w, h };
        }

        int chipAt (juce::Point<int> pos) const
        {
            for (int i = 0; i < categories().size(); ++i)
                if (chipBounds (i).contains (pos)) return i;
            return -1;
        }

        void commit()
        {
            auto tagList = juce::StringArray::fromTokens (tags.getText(), ",", "");
            tagList.trim();
            tagList.removeEmptyStrings();
            tagList.removeDuplicates (true);
            if (presets.saveUserPreset (name.getText(), categories()[category], tagList))
                close();
            else
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Torden",
                                                        "The preset could not be saved.");
        }

        PresetManager& presets;
        juce::TextEditor name, tags;
        PillButton save, cancel;
        int category = 0, hoverChip = -1;
    };

    // ------------------------------------------------------------------------------------------
    // Tile grid with all presets of the selected category.
    class PresetGrid : public juce::Component
    {
    public:
        explicit PresetGrid (PresetManager& pm) : presets (pm) {}

        void setItems (std::vector<int> newItems, int availableWidth)
        {
            items = std::move (newItems);
            columns = std::max (1, (availableWidth + gap) / (minTileW + gap));
            tileW = (availableWidth - (columns - 1) * gap) / columns;
            const int rows = ((int) items.size() + columns - 1) / columns;
            setSize (availableWidth, std::max (1, rows * (tileH + gap)));
            repaint();
        }

        std::function<void()> onDoubleClick;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const auto clip = g.getClipBounds();
            const auto& list = presets.getPresets();

            for (int i = 0; i < (int) items.size(); ++i)
            {
                const auto r = tileBounds (i);
                if (! r.intersects (clip))
                    continue;

                const int index = items[(size_t) i];
                const auto& preset = list[(size_t) index];
                const bool current = index == presets.getCurrentIndex();
                const bool hov = i == hovered;
                auto rf = r.toFloat().reduced (0.5f);

                g.setColour (current ? p.accentSoft : p.panel);
                g.fillRoundedRectangle (rf, 8.0f);
                g.setColour (current ? p.accent : (hov ? p.textDim : p.line));
                g.drawRoundedRectangle (rf, 8.0f, 1.0f);

                if (current)
                {
                    g.setColour (p.accent);
                    g.fillRoundedRectangle (rf.getX() + 10.0f, rf.getCentreY() - 9.0f, 2.0f, 18.0f, 1.0f);
                }

                auto text = r.reduced (20, 8).withTrimmedRight (20);
                g.setColour (p.text);
                g.setFont (font (14.0f, current));
                g.drawText (preset.name, text.removeFromTop (text.getHeight() / 2 + 3).toFloat(), juce::Justification::bottomLeft, true);
                g.setColour (current ? p.accent : p.textDim);
                const auto cat = categoryLabel (preset.category).toUpperCase() + (preset.isFactory ? juce::String() : utf8 ("  ·  USER"));
                drawSpacedText (g, cat, text.toFloat(), 9.5f, juce::Justification::topLeft, 0.2f);
                if (! preset.tags.isEmpty())
                {
                    const float cw = juce::GlyphArrangement::getStringWidth (font (9.5f, Weight::Medium).withExtraKerningFactor (0.2f), cat);
                    juce::StringArray first;
                    for (int t = 0; t < std::min (3, preset.tags.size()); ++t) first.add (preset.tags[t].toLowerCase());
                    g.setColour (p.textDim.withAlpha (0.85f));
                    g.setFont (font (10.5f));
                    g.drawText (utf8 ("·  ") + first.joinIntoString (", "), text.toFloat().withTrimmedLeft (cw + 8.0f).withTrimmedTop (-1.0f),
                                juce::Justification::topLeft, true);
                }

                const bool fav = presets.isFavorite (index);
                if (fav || hov)
                {
                    const auto star = starBounds (r).toFloat();
                    g.setColour (fav ? p.accent : (hoverStar ? p.text : p.textDim.withAlpha (0.6f)));
                    if (fav) g.fillPath (starPath (star));
                    else     g.strokePath (starPath (star), juce::PathStrokeType (1.1f));
                }
            }

            if (items.empty())
            {
                g.setColour (p.textDim);
                g.setFont (font (13.0f));
                g.drawText ("Nothing here yet.", getLocalBounds().withHeight (80), juce::Justification::centred);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int i = tileAt (e.getPosition());
            const bool star = i >= 0 && starBounds (tileBounds (i)).expanded (4).contains (e.getPosition());
            if (i != hovered || star != hoverStar)
            {
                hovered = i;
                hoverStar = star;
                setMouseCursor (i >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = -1; hoverStar = false; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = tileAt (e.getPosition());
            if (i < 0)
                return;
            const int index = items[(size_t) i];

            if (e.mods.isPopupMenu())
                return showContextMenu (index);

            if (starBounds (tileBounds (i)).expanded (4).contains (e.getPosition()))
                presets.toggleFavorite (index);
            else
                presets.load (index);
        }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (tileAt (e.getPosition()) >= 0 && onDoubleClick)
                onDoubleClick();
        }

    private:
        juce::Rectangle<int> tileBounds (int i) const
        {
            return { (i % columns) * (tileW + gap), (i / columns) * (tileH + gap), tileW, tileH };
        }

        static juce::Rectangle<int> starBounds (juce::Rectangle<int> tile)
        {
            return { tile.getRight() - 30, tile.getCentreY() - 8, 16, 16 };
        }

        int tileAt (juce::Point<int> pos) const
        {
            for (int i = 0; i < (int) items.size(); ++i)
                if (tileBounds (i).contains (pos)) return i;
            return -1;
        }

        void showContextMenu (int index)
        {
            const auto& preset = presets.getPresets()[(size_t) index];
            juce::PopupMenu m;
            m.addItem (1, "Load");
            m.addItem (2, presets.isFavorite (index) ? "Remove from Favorites" : "Add to Favorites");
            if (! preset.isFactory)
            {
                m.addSeparator();
                m.addItem (3, (juce::SystemStats::getOperatingSystemType() & juce::SystemStats::Windows) != 0 ? "Show in Explorer"
                                                                                                          : "Show in Finder");
                m.addItem (4, "Delete");
            }

            m.showMenuAsync (juce::PopupMenu::Options(), [this, index, file = preset.file] (int r)
            {
                if (r == 1) presets.load (index);
                if (r == 2) presets.toggleFavorite (index);
                if (r == 3) file.revealToUser();
                if (r == 4) presets.deleteUserPreset (index);
            });
        }

        static constexpr int minTileW = 190, tileH = 58, gap = 10;
        PresetManager& presets;
        std::vector<int> items;
        int columns = 4, tileW = 200, hovered = -1;
        bool hoverStar = false;
    };

    // ------------------------------------------------------------------------------------------
    // Tag filter: chips in a flowing layout. Several selected tags narrow the list down (all must match).
    // Tags that would give no result in the current view are dimmed.
    class TagFilterBar : public juce::Component
    {
    public:
        std::function<void()> onChange;

        const juce::StringArray& getSelected() const { return selected; }
        void clear() { selected.clear(); if (onChange) onChange(); }

        void setCounts (std::map<juce::String, int> c) { counts = std::move (c); repaint(); }

        // Lays out the chips for the given width and returns the needed height.
        int layout (int width)
        {
            chips.clear();
            const auto f = chipFont();
            int x = 0, y = 0;
            auto place = [&] (const juce::String& tag)
            {
                const int w = (int) juce::GlyphArrangement::getStringWidth (f, tag) + 22;
                if (x + w > width && x > 0) { x = 0; y += chipH + gap; }
                chips.push_back ({ tag, { x, y, w, chipH } });
                x += w + gap;
            };
            for (const auto& t : PresetManager::tagOrder) place (t);
            place (clearLabel);
            return y + chipH;
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            g.setFont (chipFont());
            for (const auto& c : chips)
            {
                const bool isClear = c.tag == clearLabel;
                if (isClear && selected.isEmpty()) continue;
                const bool on = selected.contains (c.tag);
                const int n = isClear ? 1 : countOf (c.tag);
                const bool hov = c.tag == hovered;
                auto r = c.bounds.toFloat().reduced (0.5f);
                if (on)
                {
                    g.setColour (p.accent);
                    g.fillRoundedRectangle (r, r.getHeight() * 0.5f);
                }
                else
                {
                    g.setColour (hov && n > 0 ? p.textDim : p.line);
                    g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
                }
                g.setColour (on ? p.panel : (isClear ? p.accent : (n > 0 ? p.text : p.textDim.withAlpha (0.5f))));
                g.drawText (c.tag, r, juce::Justification::centred);
            }
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const auto t = chipAt (e.getPosition());
            if (t != hovered)
            {
                hovered = t;
                setMouseCursor (t.isNotEmpty() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint();
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hovered = {}; repaint(); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const auto t = chipAt (e.getPosition());
            if (t.isEmpty()) return;
            if (t == clearLabel)                 selected.clear();
            else if (selected.contains (t))      selected.removeString (t);
            else if (countOf (t) > 0)            selected.add (t);
            else                                 return;
            if (onChange) onChange();
            repaint();
        }

    private:
        struct Chip { juce::String tag; juce::Rectangle<int> bounds; };
        static constexpr int chipH = 22, gap = 6;
        static inline const juce::String clearLabel { "Clear tags" };

        static juce::Font chipFont() { return font (11.5f, Weight::Medium); }

        int countOf (const juce::String& t) const
        {
            const auto it = counts.find (t);
            return it != counts.end() ? it->second : 0;
        }

        juce::String chipAt (juce::Point<int> pos) const
        {
            for (const auto& c : chips)
                if (c.bounds.contains (pos) && (c.tag != clearLabel || ! selected.isEmpty())) return c.tag;
            return {};
        }

        std::vector<Chip> chips;
        juce::StringArray selected;
        std::map<juce::String, int> counts;
        juce::String hovered;
    };

    // ------------------------------------------------------------------------------------------
    // The browser: categories on the left, search at the top, tiles on the right.
    class PresetBrowser : public juce::Component,
                          private juce::ChangeListener
    {
    public:
        explicit PresetBrowser (PresetManager& pm)
            : presets (pm), grid (pm), saveButton ("Save", true), closeButton ("Done", false)
        {
            search.setTextToShowWhenEmpty ("Search names and tags ...", juce::Colours::grey);
            search.onTextChange = [this] { refresh(); };
            search.onEscapeKey = [this] { if (search.isEmpty()) close(); else search.clear(); refresh(); };
            addAndMakeVisible (search);

            tagBar.onChange = [this] { refresh(); viewport.setViewPosition (0, 0); };
            addAndMakeVisible (tagBar);

            viewport.setViewedComponent (&grid, false);
            viewport.setScrollBarsShown (true, false);
            viewport.setScrollBarThickness (6);
            addAndMakeVisible (viewport);

            addAndMakeVisible (saveButton);
            addAndMakeVisible (closeButton);
            closeButton.onClick = [this] { close(); };
            saveButton.onClick = [this] { if (onSave) onSave(); };
            grid.onDoubleClick = [this] { close(); };

            presets.addChangeListener (this);
        }

        ~PresetBrowser() override { presets.removeChangeListener (this); }

        std::function<void()> onSave, onClose;

        void open()
        {
            styleTextEditor (search);
            // Starts in the category of the active preset
            const auto cat = presets.getCurrentCategory();
            if (cat.isNotEmpty() && selected != favoritesId && selected != userId)
                selected = categories().indexOf (cat) + 1;
            setVisible (true);
            refresh();
            scrollToCurrent();
        }

        void close() { setVisible (false); if (onClose) onClose(); }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            g.fillAll (p.background);

            // Left column
            auto side = sideArea();
            g.setColour (p.panel);
            g.fillRoundedRectangle (side.toFloat(), 10.0f);
            g.setColour (p.line);
            g.drawRoundedRectangle (side.toFloat().reduced (0.5f), 10.0f, 1.0f);

            g.setColour (p.text);
            drawSpacedText (g, "PRESETS", side.reduced (18, 0).removeFromTop (52).toFloat(), 12.0f, juce::Justification::centredLeft, 0.3f, true);

            for (const auto& row : rows())
            {
                const auto r = rowBounds (row.slot).toFloat();
                const bool on = row.id == selected, hov = row.id == hoverRow;
                if (on)
                {
                    g.setColour (p.accentSoft);
                    g.fillRoundedRectangle (r, 6.0f);
                }
                g.setColour (on ? p.accent : (hov ? p.text : p.text.withAlpha (0.8f)));
                auto tr = r.reduced (12.0f, 0.0f);
                if (row.id == favoritesId)
                {
                    g.fillPath (starPath (tr.removeFromLeft (12.0f).withSizeKeepingCentre (12.0f, 12.0f)));
                    tr.removeFromLeft (8.0f);
                }
                g.setFont (font (13.5f, on));
                g.drawText (row.label, tr, juce::Justification::centredLeft);
                g.setColour (on ? p.accent : p.textDim);
                g.setFont (font (11.0f));
                g.drawText (juce::String (count (row.id)), tr, juce::Justification::centredRight);
            }

            // Divider before Favorites / My Presets
            rows();
            g.setColour (p.line);
            const auto sep = rowBounds (separatorSlot);
            g.fillRect (sep.getX() + 12, sep.getY() + sep.getHeight() / 2, sep.getWidth() - 24, 1);

            // Header on the right: title + count
            auto head = mainArea().removeFromTop (headHeight).toFloat();
            const auto title = titleFor (selected);
            const auto titleFont = font (22.0f);
            const float tw = juce::GlyphArrangement::getStringWidth (titleFont, title);
            g.setColour (p.text);
            g.setFont (titleFont);
            g.drawText (title, head, juce::Justification::centredLeft);
            g.setColour (p.textDim);
            g.setFont (font (12.0f));
            g.drawText (juce::String (shown), head.withTrimmedLeft (tw + 10.0f).withTrimmedTop (4.0f), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto head = mainArea().removeFromTop (headHeight).withTrimmedLeft (380);
            closeButton.setBounds (head.removeFromRight (96).withSizeKeepingCentre (96, 32));
            head.removeFromRight (8);
            saveButton.setBounds (head.removeFromRight (110).withSizeKeepingCentre (110, 32));
            head.removeFromRight (16);
            search.setBounds (head.withSizeKeepingCentre (head.getWidth(), 36));

            auto body = mainArea().withTrimmedTop (headHeight + 6);
            const int tagH = tagBar.layout (body.getWidth());
            tagBar.setBounds (body.removeFromTop (tagH));
            body.removeFromTop (12);
            viewport.setBounds (body);
            refresh();
        }

        void mouseMove (const juce::MouseEvent& e) override
        {
            const int r = rowAt (e.getPosition());
            if (r != hoverRow)
            {
                hoverRow = r;
                setMouseCursor (r != noneId ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
                repaint (sideArea());
            }
        }

        void mouseExit (const juce::MouseEvent&) override { hoverRow = noneId; repaint (sideArea()); }

        void mouseDown (const juce::MouseEvent& e) override
        {
            const int r = rowAt (e.getPosition());
            if (r != noneId)
            {
                selected = r;
                refresh();
                viewport.setViewPosition (0, 0);
            }
        }

    private:
        static constexpr int allId = 0, favoritesId = 100, userId = 101, noneId = -1;
        static constexpr int sideWidth = 200, headHeight = 52, rowHeight = 30;

        struct Row { int id, slot; juce::String label; };

        static juce::StringArray categories()
        {
            auto c = PresetManager::categoryOrder;
            c.removeString ("User");
            return c;
        }

        std::vector<Row> rows() const
        {
            std::vector<Row> r { { allId, 0, "All" } };
            const auto cats = categories();
            int slot = 1;
            for (int i = 0; i < cats.size(); ++i)
                if (count (i + 1) > 0)
                    r.push_back ({ i + 1, slot++, categoryLabel (cats[i]) });
            separatorSlot = slot;
            r.push_back ({ favoritesId, slot + 1, "Favorites" });
            r.push_back ({ userId, slot + 2, "My Presets" });
            return r;
        }

        juce::String titleFor (int id) const
        {
            if (id == allId) return "All Presets";
            if (id == favoritesId) return "Favorites";
            if (id == userId) return "My Presets";
            return categoryLabel (categories()[id - 1]);
        }

        bool matches (int id, int index) const
        {
            const auto& p = presets.getPresets()[(size_t) index];
            if (id == favoritesId) return presets.isFavorite (index);
            if (id == userId)      return ! p.isFactory;
            if (id == allId)       return true;
            return p.isFactory && p.category == categories()[id - 1];
        }

        int count (int id) const
        {
            int n = 0;
            for (int i = 0; i < (int) presets.getPresets().size(); ++i)
                n += matches (id, i) ? 1 : 0;
            return n;
        }

        juce::Rectangle<int> sideArea() const { return getLocalBounds().removeFromLeft (sideWidth); }
        juce::Rectangle<int> mainArea() const { return getLocalBounds().withTrimmedLeft (sideWidth + 20); }

        juce::Rectangle<int> rowBounds (int slot) const
        {
            return sideArea().reduced (8, 0).withY (52 + slot * rowHeight).withHeight (rowHeight);
        }

        int rowAt (juce::Point<int> pos) const
        {
            for (const auto& r : rows())
                if (rowBounds (r.slot).contains (pos)) return r.id;
            return noneId;
        }

        // Every word of the search must appear in the name, the category or a tag.
        static bool matchesQuery (const PresetManager::Preset& p, const juce::StringArray& words)
        {
            for (const auto& w : words)
            {
                bool found = p.name.containsIgnoreCase (w) || categoryLabel (p.category).containsIgnoreCase (w);
                for (const auto& t : p.tags)
                    found = found || t.containsIgnoreCase (w);
                if (! found) return false;
            }
            return true;
        }

        void refresh()
        {
            auto words = juce::StringArray::fromTokens (search.getText(), " ,", "");
            words.removeEmptyStrings();
            const auto& selectedTags = tagBar.getSelected();
            std::vector<int> items;
            std::map<juce::String, int> tagCounts;
            const auto& list = presets.getPresets();
            for (int i = 0; i < (int) list.size(); ++i)
            {
                const auto& p = list[(size_t) i];
                // An active search looks through all presets.
                const bool inScope = ! words.isEmpty() || matches (selected, i);
                if (! inScope || ! matchesQuery (p, words))
                    continue;
                bool hasAll = true;
                for (const auto& t : selectedTags)
                    hasAll = hasAll && p.tags.contains (t);
                if (! hasAll)
                    continue;
                items.push_back (i);
                for (const auto& t : p.tags)
                    ++tagCounts[t];
            }
            shown = (int) items.size();
            tagBar.setCounts (std::move (tagCounts));
            grid.setItems (std::move (items), std::max (100, viewport.getWidth() - 12));
            repaint();
        }

        void scrollToCurrent()
        {
            viewport.setViewPosition (0, 0);
        }

        void changeListenerCallback (juce::ChangeBroadcaster*) override
        {
            if (isVisible()) refresh();
        }

        PresetManager& presets;
        juce::TextEditor search;
        TagFilterBar tagBar;
        juce::Viewport viewport;
        PresetGrid grid;
        PillButton saveButton, closeButton;
        int selected = allId, hoverRow = noneId, shown = 0;
        mutable int separatorSlot = 0;
    };
}
