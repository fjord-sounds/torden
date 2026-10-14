#include "PluginEditor.h"
#include "presets/Locks.h"

using namespace torden;

ui::ViewSetup TordenView::makeSetup (TordenProcessor& p)
{
    ui::ViewSetup s { p.apvts, p.presetManager };
    s.undo = &p.undoManager;
    s.masterParamId = params::paramId (params::MasterGain);
    s.tabs = { "Kick", "Layers", "FX", "Mod", "Step" };
    s.peaks = [&p] { return p.takeOutputPeaks(); };
    s.noteCounter = [&p] { return p.getHitCount(); };
    s.lastVelocity = [&p] { return p.getLastVelocity(); };
    s.debugReport = [&p] { return p.getDebugReport(); };
    s.panic = [&p] { p.requestPanic(); };
    return s;
}

TordenView::TordenView (TordenProcessor& p)
    : PluginView (makeSetup (p), width, height),
      proc (p),
      preview (p),
      kickPage (p, preview),
      layersPage (p, preview),
      fxPage ({ p.apvts, &p.undoManager, p.fxMeters, [&p] { return p.getFxOrder(); }, [&p] (const fjord::base::fx::Order& o) { p.setFxOrder (o); }, &p.chainPresets }),
      modPage (p),
      stepPage (p),
      footer (p, preview)
{
    // The padlock in the preset bar: parts of the kick that stay when switching presets
    fjord::base::ui::LockMenu::attach (getPresetBar(), p.apvts, locks::set(), locks::menuLayout());
    addAndMakeVisible (footer);
    setPages ({ &kickPage, &layersPage, &fxPage, &modPage, &stepPage });
}

void TordenView::tickContent (bool offscreen)
{
    if (! offscreen) return;
    // Offscreen (screenshots): drive the pages' own animations by hand
    kickPage.tick();
    if (stepPage.isVisible()) stepPage.tick();
    footer.tick();
}

juce::Rectangle<int> TordenView::layoutContent (juce::Rectangle<int> r)
{
    footer.setBounds (r.removeFromBottom (footerHeight + margin).withTrimmedBottom (margin));
    r.removeFromBottom (12);
    r.removeFromTop (4);
    for (auto* c : std::initializer_list<juce::Component*> { &kickPage, &layersPage, &fxPage, &modPage, &stepPage })
        c->setBounds (r);
    return r.withBottom (footer.getY() - 12);
}
