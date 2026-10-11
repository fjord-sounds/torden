#pragma once
#include "PluginProcessor.h"
#include "ui/LookAndFeelTorden.h"
#include "ui/KickPage.h"
#include "ui/LayersPage.h"
#include "ui/FxPage.h"
#include "ui/ModPage.h"
#include "ui/StepPage.h"
#include "ui/Footer.h"
#include "ui/PresetBar.h"
#include "ui/PresetBrowser.h"
#include "ui/Header.h"

// The actual UI at a fixed size (1100 x 740). The editor scales it as a whole.
class TordenView : public juce::Component,
                   public juce::TooltipClient,
                   private juce::ChangeListener,
                   private juce::Timer
{
public:
    static constexpr int baseWidth = 1100, baseHeight = 740;

    explicit TordenView (TordenProcessor&);
    ~TordenView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;
    juce::String getTooltip() override;

    // For the screenshot tool (0 Kick, 1 Layers, 2 FX, 3 Mod, 4 Step)
    void selectPage (int index) { showPage ((Page) juce::jlimit (0, (int) tabNames.size() - 1, index)); }
    void showBrowser (bool shouldShow) { setBrowserVisible (shouldShow); }
    void showAbout() { about.open(); }
    void showSaveDialog() { saveDialog.open(); }
    void tickAnimations();
    torden::ui::KickPage& getKickPage() { return kickPage; }
    torden::ui::FxPage& getFxPage() { return fxPage; }

private:
    enum class Page { Kick, Layers, FX, Mod, Step };

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;
    void applyTheme();
    void showPage (Page);
    void setBrowserVisible (bool);
    juce::Rectangle<int> tabBounds (int index) const;
    juce::Rectangle<int> themeToggleBounds() const;
    juce::Rectangle<int> githubBounds() const { return themeToggleBounds().translated (-36, 0); }
    juce::Rectangle<int> redoBounds() const { return githubBounds().translated (-44, 0); }
    juce::Rectangle<int> undoBounds() const { return redoBounds().translated (-30, 0); }
    juce::Rectangle<int> logoBounds() const { return { margin, 0, 104, headerHeight }; }

    TordenProcessor& proc;
    torden::ui::LookAndFeelTorden lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };

    torden::ui::KickPreview preview;
    torden::ui::KickPage kickPage;
    torden::ui::LayersPage layersPage;
    torden::ui::FxPage fxPage;
    torden::ui::ModPage modPage;
    torden::ui::StepPage stepPage;
    torden::ui::Footer footer;
    torden::ui::PresetBar presetBar;
    torden::ui::Knob master;
    torden::ui::PresetBrowser browser;
    torden::ui::SaveDialog saveDialog;
    torden::ui::LightningMeter levelMeter;
    torden::ui::AboutPanel about;
    int logoClicks = 0;
    uint32_t lastHits = 0;
    float logoFlash = 0.0f;

    Page page = Page::Kick;
    int hoveredTab = -1;
    enum class HeaderHover { None, Logo, Theme, Github, Undo, Redo } hover = HeaderHover::None;

    static inline const juce::StringArray tabNames { "Kick", "Layers", "FX", "Mod", "Step" };
    static constexpr int headerHeight = 64, tabHeight = 40, footerHeight = 84, margin = 16;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TordenView)
};

// Plugin window: freely scalable (75 - 150 %) with a fixed aspect ratio, the size is remembered.
class TordenEditor : public juce::AudioProcessorEditor
{
public:
    explicit TordenEditor (TordenProcessor&);
    void resized() override;
    TordenView& getView() { return view; }

private:
    TordenProcessor& proc;
    TordenView view;
    bool ready = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TordenEditor)
};
