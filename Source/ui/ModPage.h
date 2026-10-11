#pragma once
#include "LayerControls.h"
#include "PresetBrowser.h"

namespace torden::ui
{
    namespace P = params;

    inline juce::StringArray macroNames (const juce::ValueTree& state)
    {
        auto names = juce::StringArray::fromTokens (state.getProperty ("macroNames").toString(), ",", "");
        while (names.size() < P::kNumMacros) names.add ("Macro " + juce::String (names.size() + 1));
        for (int i = 0; i < P::kNumMacros; ++i)
            if (names[i].trim().isEmpty()) names.set (i, "Macro " + juce::String (i + 1));
        return names;
    }

    // Group of a parameter for the target menu
    inline juce::String paramGroup (const P::Spec& s)
    {
        const juce::String id = s.paramId;
        static const std::pair<const char*, const char*> groups[] = {
            { "body_", "Body" }, { "click_", "Click" }, { "sub_", "Sub" }, { "noise_", "Noise" }, { "sat_", "Saturation" },
            { "crush_", "Crush" }, { "trans_", "Transient" }, { "comp_", "Compressor" }, { "eq_", "EQ" }, { "filt_", "Filter" },
            { "verb_", "Reverb" }, { "dly_", "Delay" }, { "chr_", "Motion" }, { "width_", "Width" }, { "clip_", "Clipper" } };
        for (const auto& [prefix, name] : groups)
            if (id.startsWith (prefix)) return name;
        return "Kick";
    }

    // Shows the current target; a click opens a grouped menu of every modulatable parameter.
    class TargetSelector : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        TargetSelector (APVTS& s, P::Id dstParam) : id (dstParam)
        {
            auto* param = s.getParameter (P::paramId (id));
            attachment = std::make_unique<juce::ParameterAttachment> (*param, [this] (float v) { index = juce::roundToInt (v); repaint(); }, nullptr);
            attachment->sendInitialUpdate();
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        int getIndex() const { return index; }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            g.setColour (hover ? p.textDim : p.line);
            g.drawRoundedRectangle (r, r.getHeight() * 0.5f, 1.0f);
            const auto& targets = P::modTargets();
            if (index > 0 && index <= (int) targets.size())
            {
                const auto& spec = P::spec (targets[(size_t) index - 1]);
                g.setColour (p.textDim);
                const auto group = paramGroup (spec).toUpperCase();
                const auto gf = font (9.0f, Weight::SemiBold).withExtraKerningFactor (0.16f);
                const float gw = juce::GlyphArrangement::getStringWidth (gf, group);
                g.setFont (gf);
                g.drawText (group, r.withTrimmedLeft (14.0f).withWidth (gw + 4.0f), juce::Justification::centredLeft);
                g.setColour (p.text);
                g.setFont (font (12.5f));
                juce::String name = spec.name;
                if (name.startsWithIgnoreCase (paramGroup (spec) + " ")) name = name.substring (paramGroup (spec).length() + 1);
                g.drawText (name, r.withTrimmedLeft (22.0f + gw).withTrimmedRight (20.0f), juce::Justification::centredLeft, true);
            }
            else
            {
                g.setColour (p.textDim);
                g.setFont (font (12.5f));
                g.drawText (utf8 ("choose target \xe2\x80\xa6"), r.withTrimmedLeft (14.0f), juce::Justification::centredLeft);
            }
            juce::Path arrow;
            const float ax = r.getRight() - 14.0f, ay = r.getCentreY();
            arrow.startNewSubPath (ax - 3.5f, ay - 1.5f);
            arrow.lineTo (ax, ay + 2.0f);
            arrow.lineTo (ax + 3.5f, ay - 1.5f);
            g.setColour (p.textDim);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));
        }

        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override  { hover = false; repaint(); }

        void mouseDown (const juce::MouseEvent&) override
        {
            juce::PopupMenu m;
            m.addItem (1, "None", true, index == 0);
            std::map<juce::String, juce::PopupMenu> groups;
            juce::StringArray order;
            const auto& targets = P::modTargets();
            for (size_t i = 0; i < targets.size(); ++i)
            {
                const auto& spec = P::spec (targets[i]);
                const auto group = paramGroup (spec);
                if (! order.contains (group)) order.add (group);
                juce::String name = spec.name;
                if (name.startsWithIgnoreCase (group + " ")) name = name.substring (group.length() + 1);
                groups[group].addItem ((int) i + 2, name, true, index == (int) i + 1);
            }
            for (const auto& gName : order)
                m.addSubMenu (gName, groups[gName]);
            m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
            {
                if (r > 0) attachment->setValueAsCompleteGesture ((float) (r - 1));
            });
        }

    private:
        P::Id id;
        std::unique_ptr<juce::ParameterAttachment> attachment;
        int index = 0;
        bool hover = false;
    };

    // ---------------------------------------------------------------------------------------------
    // One macro: big knob with a glowing ring, an editable name, and the number of targets.
    class MacroControl : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        MacroControl (TordenProcessor& p, int macroIndex)
            : proc (p), index (macroIndex), knob (p.apvts, P::paramId (P::macro (macroIndex)), "")
        {
            addAndMakeVisible (knob);
            knob.setLabelHeight (0);
            knob.addMouseListener (this, true);
            setTooltip ("Double-click the name to rename");
        }

        std::function<void (int)> onHover;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            const bool dark = Theme::get().isDark();
            const float value = proc.apvts.getRawParameterValue (P::paramId (P::macro (index)))->load();
            const auto kb = knob.getBounds().toFloat();
            const auto c = kb.getCentre();
            const float rad = std::min (kb.getWidth(), kb.getHeight()) * 0.5f;
            // Glow ring: brightness by value
            g.setGradientFill (juce::ColourGradient (p.accent.withAlpha ((dark ? 0.28f : 0.18f) * (0.25f + value)), c,
                                                     p.accent.withAlpha (0.0f), c.translated (rad * 1.35f, 0.0f), true));
            g.fillEllipse (juce::Rectangle<float> (rad * 2.7f, rad * 2.7f).withCentre (c));

            auto text = getLocalBounds().toFloat().removeFromBottom (38.0f);
            g.setColour (p.text);
            drawSpacedText (g, macroNames (proc.apvts.state)[index].toUpperCase(), text.removeFromTop (18.0f), 11.0f,
                            juce::Justification::centred, 0.2f, true);
            int targets = 0;
            for (int s = 0; s < P::kNumModSlots; ++s)
                if (juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::modSrc (s)))->load()) == (int) P::ModSource::Macro1 + index
                    && proc.apvts.getRawParameterValue (P::paramId (P::modDst (s)))->load() > 0.5f)
                    ++targets;
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            g.drawText (juce::String (juce::roundToInt (value * 100.0f)) + utf8 (" %  \xc2\xb7  ") + juce::String (targets)
                        + (targets == 1 ? " target" : " targets"), text, juce::Justification::centredTop);
        }

        void resized() override
        {
            auto r = getLocalBounds().withTrimmedBottom (44);
            const int size = std::min (120, std::min (r.getWidth(), r.getHeight()) - 40);
            knob.setBounds (r.withSizeKeepingCentre (size, size).translated (0, 8));
        }

        void mouseEnter (const juce::MouseEvent&) override { if (onHover) onHover (index); }
        void mouseExit (const juce::MouseEvent&) override  { if (onHover) onHover (-1); }

        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (e.originalComponent != this) return;
            editor = std::make_unique<juce::TextEditor>();
            styleTextEditor (*editor);
            editor->setJustification (juce::Justification::centred);
            editor->setText (macroNames (proc.apvts.state)[index]);
            editor->selectAll();
            addAndMakeVisible (*editor);
            editor->setBounds (getLocalBounds().removeFromBottom (40).removeFromTop (26).reduced (8, 0));
            editor->grabKeyboardFocus();
            auto commit = [this]
            {
                auto names = macroNames (proc.apvts.state);
                names.set (index, editor->getText().removeCharacters (",").trim().substring (0, 16));
                proc.apvts.state.setProperty ("macroNames", names.joinIntoString (","), &proc.undoManager);
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroControl> (this)]
                                                 { if (safe != nullptr) { safe->editor.reset(); safe->repaint(); } });
            };
            editor->onReturnKey = commit;
            editor->onFocusLost = commit;
            editor->onEscapeKey = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<MacroControl> (this)]
                                                                              { if (safe != nullptr) safe->editor.reset(); }); };
        }

    private:
        TordenProcessor& proc;
        int index;
        Knob knob;
        std::unique_ptr<juce::TextEditor> editor;
    };

    // ---------------------------------------------------------------------------------------------
    // One matrix slot: source -> target, amount, and a live bar of what it does right now.
    class ModSlot : public juce::Component
    {
    public:
        ModSlot (TordenProcessor& p, int slotIndex)
            : proc (p), slot (slotIndex), source (p.apvts, P::paramId (P::modSrc (slotIndex))), target (p.apvts, P::modDst (slotIndex))
        {
            addAndMakeVisible (source);
            addAndMakeVisible (target);
            amount.setSliderStyle (juce::Slider::LinearHorizontal);
            amount.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            addAndMakeVisible (amount);
            auto* param = p.apvts.getParameter (P::paramId (P::modAmt (slotIndex)));
            attachment = std::make_unique<juce::SliderParameterAttachment> (*param, amount, nullptr);
            amount.setDoubleClickReturnValue (true, 0.0);
            amount.onValueChange = [this] { repaint(); };
        }

        int highlightMacro = -1;

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            auto r = getLocalBounds().toFloat().reduced (0.5f);
            const int src = juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::modSrc (slot)))->load());
            const bool lit = highlightMacro >= 0 && src == (int) P::ModSource::Macro1 + highlightMacro;
            g.setColour (lit ? p.accentSoft : p.panel);
            g.fillRoundedRectangle (r, 6.0f);
            g.setColour (lit ? p.accent : p.line);
            g.drawRoundedRectangle (r, 6.0f, 1.0f);

            g.setColour (p.textDim);
            g.setFont (font (11.0f, Weight::SemiBold));
            g.drawText (juce::String (slot + 1), r.removeFromLeft (30.0f), juce::Justification::centred);

            // Arrow between source and target
            const auto a = source.getBounds().toFloat();
            const float ax = a.getRight() + 9.0f, ay = a.getCentreY();
            juce::Path arrow;
            arrow.startNewSubPath (ax - 4.0f, ay);
            arrow.lineTo (ax + 4.0f, ay);
            arrow.startNewSubPath (ax + 1.0f, ay - 3.0f);
            arrow.lineTo (ax + 4.0f, ay);
            arrow.lineTo (ax + 1.0f, ay + 3.0f);
            g.strokePath (arrow, juce::PathStrokeType (1.2f));

            // Live value: amount x source
            auto bar = activityBounds();
            g.setColour (p.track);
            g.fillRoundedRectangle (bar, 2.0f);
            const float v = juce::jlimit (-1.0f, 1.0f, current);
            if (std::abs (v) > 0.002f)
            {
                const float cx = bar.getCentreX(), x = cx + v * bar.getWidth() * 0.5f;
                g.setColour (p.accent.withAlpha (0.4f + 0.6f * std::abs (v)));
                g.fillRoundedRectangle (std::min (cx, x), bar.getY(), std::abs (x - cx), bar.getHeight(), 2.0f);
            }
            g.setColour (p.textDim);
            g.setFont (font (10.5f));
            const float amt = (float) amount.getValue();
            g.drawText ((amt > 0.005f ? "+" : "") + juce::String (juce::roundToInt (amt * 100.0f)) + " %",
                        juce::Rectangle<float> (bar.getRight() + 6.0f, (float) amount.getY(), 48.0f, (float) amount.getHeight()), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto r = getLocalBounds().reduced (8, 10).withTrimmedLeft (26);
            source.setBounds (r.removeFromLeft (124).withSizeKeepingCentre (124, 26));
            r.removeFromLeft (22);
            target.setBounds (r.removeFromLeft (230).withSizeKeepingCentre (230, 28));
            r.removeFromLeft (20);
            r.removeFromRight (50);
            amount.setBounds (r.withSizeKeepingCentre (r.getWidth(), 22).translated (0, -4));
        }

        void tick()
        {
            const int src = juce::roundToInt (proc.apvts.getRawParameterValue (P::paramId (P::modSrc (slot)))->load());
            float value = 0.0f;
            const auto kind = (P::ModSource) src;
            if (kind == P::ModSource::Velocity)    value = proc.getLastVelocity();
            else if (kind == P::ModSource::Note)   value = proc.getLastNoteSource();
            else if (kind == P::ModSource::Random) value = proc.getLastRandomSource();
            else if (src >= (int) P::ModSource::Macro1)
                value = proc.apvts.getRawParameterValue (P::paramId (P::macro (src - (int) P::ModSource::Macro1)))->load();
            const float goal = target_ (value);
            if (std::abs (goal - current) > 0.001f) { current += (goal - current) * 0.35f; repaint (activityBounds().expanded (2.0f).toNearestInt()); }
        }

    private:
        float target_ (float sourceValue) const
        {
            return target.getIndex() > 0 ? (float) amount.getValue() * sourceValue : 0.0f;
        }

        juce::Rectangle<float> activityBounds() const
        {
            const auto a = amount.getBounds().toFloat();
            return { a.getX(), a.getBottom() + 3.0f, a.getWidth(), 3.0f };
        }

        TordenProcessor& proc;
        int slot;
        Choice source;
        TargetSelector target;
        juce::Slider amount;
        std::unique_ptr<juce::SliderParameterAttachment> attachment;
        float current = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------
    class ModPage : public juce::Component, private juce::Timer
    {
    public:
        explicit ModPage (TordenProcessor& p)
        {
            for (int m = 0; m < P::kNumMacros; ++m)
            {
                macros[(size_t) m] = std::make_unique<MacroControl> (p, m);
                macros[(size_t) m]->onHover = [this] (int idx) { for (auto& s : slots) { s->highlightMacro = idx; s->repaint(); } };
                addAndMakeVisible (*macros[(size_t) m]);
            }
            for (int s = 0; s < P::kNumModSlots; ++s)
            {
                slots[(size_t) s] = std::make_unique<ModSlot> (p, s);
                addAndMakeVisible (*slots[(size_t) s]);
            }
            startTimerHz (30);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& p = Theme::get().palette();
            for (auto area : { macroArea(), matrixArea() })
            {
                auto r = area.toFloat().reduced (0.5f);
                g.setColour (p.panel);
                g.fillRoundedRectangle (r, 6.0f);
                g.setColour (p.line);
                g.drawRoundedRectangle (r, 6.0f, 1.0f);
            }
            g.setColour (p.text);
            drawSpacedText (g, "MACROS", macroArea().reduced (16, 0).removeFromTop (36).toFloat(), 11.0f, juce::Justification::centredLeft, 0.25f, true);
            drawSpacedText (g, "MODULATION", matrixArea().reduced (16, 0).removeFromTop (36).toFloat(), 11.0f, juce::Justification::centredLeft, 0.25f, true);
            g.setColour (p.textDim);
            g.setFont (font (11.0f));
            g.drawText ("Sources: velocity, note, a random value per hit and the four macros. Amounts work on the knob's range.",
                        matrixArea().reduced (16, 0).removeFromTop (36).withTrimmedLeft (130).toFloat(), juce::Justification::centredLeft);
        }

        void resized() override
        {
            auto m = macroArea().reduced (12).withTrimmedTop (30);
            const int h = m.getHeight() / 2, w = m.getWidth() / 2;
            for (int i = 0; i < P::kNumMacros; ++i)
                macros[(size_t) i]->setBounds (m.getX() + (i % 2) * w, m.getY() + (i / 2) * h, w, h);
            auto r = matrixArea().reduced (12).withTrimmedTop (30);
            const int sh = r.getHeight() / P::kNumModSlots;
            for (auto& s : slots)
                s->setBounds (r.removeFromTop (sh).reduced (0, 3));
        }

    private:
        juce::Rectangle<int> macroArea() const  { return getLocalBounds().removeFromLeft (380); }
        juce::Rectangle<int> matrixArea() const { return getLocalBounds().withTrimmedLeft (392); }

        void timerCallback() override
        {
            if (! isShowing()) return;
            for (auto& s : slots) s->tick();
            for (auto& m : macros) m->repaint();
        }

        std::array<std::unique_ptr<MacroControl>, P::kNumMacros> macros;
        std::array<std::unique_ptr<ModSlot>, P::kNumModSlots> slots;
    };
}
