#pragma once
#include "PluginProcessor.h"
#include "ui/KickPage.h"
#include "ui/LayersPage.h"
#include "ui/ModPage.h"
#include "ui/StepPage.h"
#include "ui/Footer.h"
#include <fjord_base/fx/FxPage.h>

// The actual UI at a fixed size (1100 x 740) inside the shared frame of fjord-base (header, presets, About).
// The editor scales it as a whole.
class TordenView : public torden::ui::PluginView
{
public:
    static constexpr int width = 1100, height = 740;

    explicit TordenView (TordenProcessor&);

    // For the screenshot tool (0 Kick, 1 Layers, 2 FX, 3 Mod, 4 Step)
    void selectPage (int index) { showPage (index); }
    torden::ui::KickPage& getKickPage() { return kickPage; }
    fjord::base::fx::FxPage& getFxPage() { return fxPage; }

protected:
    juce::Rectangle<int> layoutContent (juce::Rectangle<int>) override;
    void tickContent (bool offscreen) override;

private:
    static torden::ui::ViewSetup makeSetup (TordenProcessor&);

    TordenProcessor& proc;
    torden::ui::KickPreview preview;
    torden::ui::KickPage kickPage;
    torden::ui::LayersPage layersPage;
    fjord::base::fx::FxPage fxPage;
    torden::ui::ModPage modPage;
    torden::ui::StepPage stepPage;
    torden::ui::Footer footer;

    static constexpr int footerHeight = 84;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TordenView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class TordenEditor : public torden::ui::ScaledEditor<TordenView, TordenProcessor>
{
public:
    using ScaledEditor::ScaledEditor;
};
