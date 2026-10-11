#include "PluginEditor.h"

using namespace torden;

namespace
{
    const juce::URL githubUrl { "https://github.com/fjord-sounds/torden" };

    juce::Path githubMark()
    {
        static const juce::Path mark = juce::Drawable::parseSVGPath (
            "M8 0C3.58 0 0 3.58 0 8c0 3.54 2.29 6.53 5.47 7.59.4.07.55-.17.55-.38 0-.19-.01-.82-.01-1.49-2.01.37-2.53-.49"
            "-2.69-.94-.09-.23-.48-.94-.82-1.13-.28-.15-.68-.52-.01-.53.63-.01 1.08.58 1.23.82.72 1.21 1.87.87 2.33.66.07-.52"
            ".28-.87.51-1.07-1.78-.2-3.64-.89-3.64-3.95 0-.87.31-1.59.82-2.15-.08-.2-.36-1.02.08-2.12 0 0 .67-.21 2.2.82.64-.18"
            " 1.32-.27 2-.27.68 0 1.36.09 2 .27 1.53-1.04 2.2-.82 2.2-.82.44 1.1.16 1.92.08 2.12.51.56.82 1.27.82 2.15 0 3.07-1.87"
            " 3.75-3.65 3.95.29.25.54.73.54 1.48 0 1.07-.01 1.93-.01 2.2 0 .21.15.46.55.38A8.013 8.013 0 0016 8c0-4.42-3.58-8-8-8z");
        return mark;
    }

    // Curved arrow for undo (left) / redo (right)
    juce::Path undoArrow (juce::Rectangle<float> r, bool redo)
    {
        juce::Path p;
        const auto c = r.getCentre();
        const float rad = r.getWidth() * 0.32f;
        p.addCentredArc (c.x, c.y + 1.5f, rad, rad, 0.0f, -2.2f, 1.2f, true);
        const auto start = c.translated (0, 1.5f).getPointOnCircumference (rad, -2.2f);
        p.startNewSubPath (start.translated (-0.5f, -3.5f));
        p.lineTo (start);
        p.lineTo (start.translated (3.5f, 0.5f));
        if (redo)
            p.applyTransform (juce::AffineTransform::scale (-1.0f, 1.0f, c.x, c.y));
        return p;
    }
}

TordenView::TordenView (TordenProcessor& p)
    : proc (p),
      preview (p),
      kickPage (p, preview),
      layersPage (p, preview),
      fxPage (p),
      modPage (p),
      stepPage (p),
      footer (p, preview),
      presetBar (p.presetManager),
      master (p.apvts, params::paramId (params::MasterGain), "Master"),
      browser (p.presetManager),
      saveDialog (p.presetManager),
      levelMeter ([&p] { return p.takeOutputPeaks(); }, [&p] { return p.getHitCount(); }),
      about ([&p] { return p.getDebugReport(); }, [&p] { p.requestPanic(); })
{
    setLookAndFeel (&lookAndFeel);
    ui::Theme::get().addChangeListener (this);

    addAndMakeVisible (kickPage);
    addChildComponent (layersPage);
    addChildComponent (fxPage);
    addChildComponent (modPage);
    addChildComponent (stepPage);
    addAndMakeVisible (footer);
    addAndMakeVisible (presetBar);
    addAndMakeVisible (master);
    addAndMakeVisible (levelMeter);

    addChildComponent (browser);
    addChildComponent (saveDialog);
    addChildComponent (about);
    presetBar.onOpenBrowser = [this] { setBrowserVisible (! browser.isVisible()); };
    presetBar.onSave = [this] { saveDialog.open(); };
    browser.onSave = [this] { saveDialog.open(); };
    browser.onClose = [this] { presetBar.setBrowserOpen (false); };

    setWantsKeyboardFocus (false);
    applyTheme();
    showPage (Page::Kick);
    setSize (baseWidth, baseHeight);
    startTimerHz (30);
}

TordenView::~TordenView()
{
    ui::Theme::get().removeChangeListener (this);
    setLookAndFeel (nullptr);
}

void TordenView::changeListenerCallback (juce::ChangeBroadcaster*)
{
    applyTheme();
}

void TordenView::applyTheme()
{
    lookAndFeel.applyPalette();
    sendLookAndFeelChange();
    repaint();
}

void TordenView::timerCallback() { tickAnimations(); }

// The logo dot lights up with every hit
void TordenView::tickAnimations()
{
    if (! isTimerRunning() || ! isShowing())
    {
        // Offscreen (screenshots): drive the animations by hand
        kickPage.tick();
        if (fxPage.isVisible()) fxPage.tick();
        if (stepPage.isVisible()) stepPage.tick();
        footer.tick();
        levelMeter.tick();
    }
    const auto hits = proc.getHitCount();
    if (hits != lastHits)
    {
        lastHits = hits;
        logoFlash = 0.4f + 0.6f * proc.getLastVelocity();
        repaint (logoBounds());
    }
    else if (logoFlash > 0.0f)
    {
        logoFlash = std::max (0.0f, logoFlash - 0.08f);
        repaint (logoBounds());
    }
}

void TordenView::setBrowserVisible (bool shouldShow)
{
    if (shouldShow) browser.open();
    else            browser.close();
    presetBar.setBrowserOpen (shouldShow);
}

void TordenView::showPage (Page newPage)
{
    setBrowserVisible (false);
    page = newPage;
    kickPage.setVisible (page == Page::Kick);
    layersPage.setVisible (page == Page::Layers);
    fxPage.setVisible (page == Page::FX);
    modPage.setVisible (page == Page::Mod);
    stepPage.setVisible (page == Page::Step);
    repaint();
}

juce::Rectangle<int> TordenView::tabBounds (int index) const
{
    constexpr int tabW = 84;
    return { margin + index * tabW, headerHeight, tabW, tabHeight };
}

juce::Rectangle<int> TordenView::themeToggleBounds() const
{
    return { getWidth() - margin - 130, (headerHeight - 28) / 2, 28, 28 };
}

juce::String TordenView::getTooltip()
{
    switch (hover)
    {
        case HeaderHover::Logo:   return "Click: about and debug info  |  Double-click: stop all sound";
        case HeaderHover::Github: return "Torden on GitHub";
        case HeaderHover::Theme:  return "Light / dark";
        case HeaderHover::Undo:   return "Undo (Cmd+Z)";
        case HeaderHover::Redo:   return "Redo (Cmd+Shift+Z)";
        case HeaderHover::None:
        default:                  return {};
    }
}

void TordenView::paint (juce::Graphics& g)
{
    const auto& pal = ui::Theme::get().palette();
    const bool dark = ui::Theme::get().isDark();
    g.fillAll (pal.background);
    ui::drawStormGlow (g, getLocalBounds().toFloat().withHeight ((float) (headerHeight + tabHeight) * 1.6f), dark);

    // Logo: lower case, light, with the amber dot that flashes on every hit
    g.setColour (pal.text);
    const auto logoFont = ui::font (30.0f).withExtraKerningFactor (0.04f);
    g.setFont (logoFont);
    g.drawText ("torden", margin + 4, 0, 130, headerHeight, juce::Justification::centredLeft);
    const float logoW = juce::GlyphArrangement::getStringWidth (logoFont, "torden");
    const juce::Point<float> dot { (float) margin + 4.0f + logoW + 5.5f, (float) headerHeight * 0.5f + 7.5f };
    if (logoFlash > 0.0f)
    {
        const float rad = 6.0f + 16.0f * logoFlash;
        g.setGradientFill (juce::ColourGradient (pal.accent.withAlpha (0.55f * logoFlash), dot, pal.accent.withAlpha (0.0f),
                                                 dot.translated (rad, 0.0f), true));
        g.fillEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (dot));
    }
    g.setColour (pal.accent.interpolatedWith (dark ? juce::Colours::white : pal.accent, logoFlash * 0.5f));
    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (dot));

    // Tabs
    for (int i = 0; i < tabNames.size(); ++i)
    {
        const auto r = tabBounds (i).toFloat();
        const bool active = i == (int) page;
        g.setColour (active ? pal.text : (i == hoveredTab ? pal.text.withAlpha (0.75f) : pal.textDim));
        ui::drawSpacedText (g, tabNames[i].toUpperCase(), r, 11.5f, juce::Justification::centred, 0.22f, active);
        if (active)
        {
            g.setColour (pal.accent);
            g.fillRoundedRectangle (r.getCentreX() - 10.0f, r.getBottom() - 9.0f, 20.0f, 2.0f, 1.0f);
        }
    }

    // Undo / redo
    auto* um = &proc.undoManager;
    for (bool redo : { false, true })
    {
        const auto r = (redo ? redoBounds() : undoBounds()).toFloat().reduced (5.0f);
        const bool can = redo ? um->canRedo() : um->canUndo();
        const bool hov = hover == (redo ? HeaderHover::Redo : HeaderHover::Undo);
        g.setColour (can ? (hov ? pal.text : pal.textDim) : pal.line);
        g.strokePath (undoArrow (r, redo), juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Theme switch: sun / moon
    {
        const auto r = themeToggleBounds().toFloat().reduced (6.0f);
        g.setColour (hover == HeaderHover::Theme ? pal.text : pal.textDim);
        if (dark)
        {
            g.drawEllipse (r.reduced (2.0f), 1.4f);
            for (int i = 0; i < 8; ++i)
            {
                const float a = (float) i * juce::MathConstants<float>::twoPi / 8.0f;
                const auto c = r.getCentre();
                g.drawLine ({ c.getPointOnCircumference (r.getWidth() * 0.62f, a), c.getPointOnCircumference (r.getWidth() * 0.78f, a) }, 1.2f);
            }
        }
        else
        {
            juce::Path moon;
            moon.addEllipse (r);
            juce::Path cut;
            cut.addEllipse (r.translated (r.getWidth() * 0.38f, -r.getHeight() * 0.22f));
            moon.setUsingNonZeroWinding (false);
            moon.addPath (cut);
            g.fillPath (moon);
        }
    }

    {
        auto mark = githubMark();
        const auto r = githubBounds().toFloat().reduced (6.0f);
        mark.applyTransform (mark.getTransformToScaleToFit (r, true));
        g.setColour (hover == HeaderHover::Github ? pal.text : pal.textDim);
        g.fillPath (mark);
    }

    g.setColour (pal.line);
    g.fillRect (margin, headerHeight - 1, getWidth() - 2 * margin, 1);
}

void TordenView::resized()
{
    auto r = getLocalBounds();
    auto header = r.removeFromTop (headerHeight);
    master.setBounds (header.removeFromRight (margin + 92).withTrimmedRight (margin).reduced (0, 6));
    presetBar.setBounds (header.withSizeKeepingCentre (440, 36).withX ((getWidth() - 440) / 2));
    r.removeFromTop (tabHeight);

    r.reduce (margin, 0);
    footer.setBounds (r.removeFromBottom (footerHeight + margin).withTrimmedBottom (margin));
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* c : std::initializer_list<juce::Component*> { &kickPage, &layersPage, &fxPage, &modPage, &stepPage })
        c->setBounds (r);
    browser.setBounds (getLocalBounds().reduced (margin, 0).withTrimmedTop (headerHeight + 14).withBottom (footer.getY() - 12));
    saveDialog.setBounds (getLocalBounds());
    about.setBounds (getLocalBounds());
    levelMeter.setBounds (logoBounds().getRight() + 14, (headerHeight - 30) / 2, 170, 30);
}

void TordenView::mouseDown (const juce::MouseEvent& e)
{
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (e.getPosition()))
            return showPage ((Page) i);

    if (themeToggleBounds().contains (e.getPosition()))
        ui::Theme::get().setDark (! ui::Theme::get().isDark());
    else if (githubBounds().contains (e.getPosition()))
        githubUrl.launchInDefaultBrowser();
    else if (undoBounds().contains (e.getPosition()))
        proc.undoManager.undo();
    else if (redoBounds().contains (e.getPosition()))
        proc.undoManager.redo();
    repaint (0, 0, getWidth(), headerHeight);
}

void TordenView::mouseUp (const juce::MouseEvent& e)
{
    if (! logoBounds().contains (e.getPosition()) || e.getNumberOfClicks() > 1 || e.mouseWasDraggedSinceMouseDown())
        return;
    const int click = ++logoClicks;
    juce::Timer::callAfterDelay (juce::MouseEvent::getDoubleClickTimeout() + 20,
                                 [safe = juce::Component::SafePointer<TordenView> (this), click]
                                 {
                                     if (safe != nullptr && safe->logoClicks == click)
                                         safe->about.open();
                                 });
}

void TordenView::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (logoBounds().contains (e.getPosition()))
    {
        ++logoClicks;
        proc.requestPanic();
    }
}

void TordenView::mouseMove (const juce::MouseEvent& e)
{
    const auto pos = e.getPosition();
    int tab = -1;
    for (int i = 0; i < tabNames.size(); ++i)
        if (tabBounds (i).contains (pos))
            tab = i;

    auto h = HeaderHover::None;
    if (logoBounds().contains (pos))             h = HeaderHover::Logo;
    else if (themeToggleBounds().contains (pos)) h = HeaderHover::Theme;
    else if (githubBounds().contains (pos))      h = HeaderHover::Github;
    else if (undoBounds().contains (pos))        h = HeaderHover::Undo;
    else if (redoBounds().contains (pos))        h = HeaderHover::Redo;

    if (tab != hoveredTab || h != hover)
    {
        hoveredTab = tab;
        hover = h;
        setMouseCursor (tab >= 0 || h != HeaderHover::None ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint (0, 0, getWidth(), headerHeight + tabHeight);
    }
}

void TordenView::mouseExit (const juce::MouseEvent&)
{
    hoveredTab = -1;
    hover = HeaderHover::None;
    repaint (0, 0, getWidth(), headerHeight + tabHeight);
}

bool TordenView::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0)) { proc.undoManager.undo(); repaint(); return true; }
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0)) { proc.undoManager.redo(); repaint(); return true; }
    return false;
}

// ---------------------------------------------------------------------------------------------
TordenEditor::TordenEditor (TordenProcessor& p)
    : AudioProcessorEditor (p), proc (p), view (p)
{
    addAndMakeVisible (view);
    setResizable (true, true);
    setResizeLimits (TordenView::baseWidth * 3 / 4, TordenView::baseHeight * 3 / 4,
                     TordenView::baseWidth * 3 / 2, TordenView::baseHeight * 3 / 2);
    if (auto* c = getConstrainer())
        c->setFixedAspectRatio ((double) TordenView::baseWidth / TordenView::baseHeight);

    const auto& st = p.apvts.state;
    const float saved = st.hasProperty ("uiScale") ? (float) (double) st.getProperty ("uiScale") : ui::Theme::get().getScale();
    const float scale = juce::jlimit (0.75f, 1.5f, saved);
    setSize (juce::roundToInt (TordenView::baseWidth * scale), juce::roundToInt (TordenView::baseHeight * scale));
    ready = true;
}

void TordenEditor::resized()
{
    const float scale = (float) getWidth() / (float) TordenView::baseWidth;
    view.setTransform (juce::AffineTransform::scale (scale));
    view.setBounds (0, 0, TordenView::baseWidth, TordenView::baseHeight);

    if (ready && scale >= 0.74f && scale <= 1.51f)
    {
        ui::Theme::get().setScale (scale);
        proc.apvts.state.setProperty ("uiScale", scale, nullptr);
    }
}
