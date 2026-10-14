#pragma once
#include <fjord_base/presets/Locks.h>
#include <fjord_base/ui/LockMenu.h>
#include "../dsp/Envelope.h"

namespace torden::locks
{
    // Parts of a kick that stay when another sound preset is loaded: a layer, the kick settings, an envelope, the
    // modulation, the step pattern or the effect chain. The mechanism (LockSet, padlock menu, LockButton, preset manager
    // hook) is fjord-base; this is Torden's list. The switches are properties of the project state – their names never
    // change. Envelopes and the pattern are not parameters: PresetManager::applyExtras asks isLockedProperty().
    struct Ids
    {
        int body, click, sub, noise, kick, matrix, macros, step;
        std::array<int, dsp::kNumEnvs> envelopes;
    };

    // Property of an envelope's lock ("envLock_body_amp" …); PresetManager keeps that envelope when it is locked.
    inline juce::String envProperty (int env) { return juce::String ("envLock_") + dsp::envName ((dsp::Env) env); }

    inline const std::pair<const fjord::base::locks::LockSet&, Ids>& setup()
    {
        static const auto pair = []
        {
            static fjord::base::locks::LockSet set;
            Ids ids {};
            ids.body  = set.add ({ "bodyLock",  "Body",  "body_" });
            ids.click = set.add ({ "clickLock", "Click", "click_" });
            ids.sub   = set.add ({ "subLock",   "Sub",   "sub_" });
            ids.noise = set.add ({ "noiseLock", "Noise", "noise_" });
            ids.kick  = set.add ({ "kickLock",  "Kick settings (pitch, tune, length, velocity)",
                                   "pitch_mode|root_note|tune|fine|length|vel_|retrigger|drive_env" });
            for (int e = 0; e < dsp::kNumEnvs; ++e)
                ids.envelopes[(size_t) e] = set.add ({ envProperty (e), juce::String (dsp::envLabel ((dsp::Env) e)) + " envelope", {} });
            ids.matrix = set.add ({ "matrixLock", "Modulation matrix", "mod1_|mod2_|mod3_|mod4_|mod5_|mod6_|mod7_|mod8_" });
            ids.macros = set.add ({ "macroLock",  "Macros", "macro" });
            ids.step   = set.add ({ "stepLock",   "Step sequencer", "seq_on|seq_length|seq_rate|seq_swing" });
            set.addEffectGroups();
            return std::pair<const fjord::base::locks::LockSet&, Ids> { set, ids };
        }();
        return pair;
    }

    inline const fjord::base::locks::LockSet& set() { return setup().first; }
    inline const Ids& ids() { return setup().second; }

    inline fjord::base::ui::MenuLayout menuLayout()
    {
        const auto& i = ids();
        std::vector<int> envelopes (i.envelopes.begin(), i.envelopes.end());
        fjord::base::ui::MenuLayout layout;
        layout.header ("Keep when switching presets")
              .submenu ("Layers", { i.body, i.click, i.sub, i.noise }, "All layers")
              .item (i.kick)
              .submenu ("Envelopes", envelopes, "All envelopes")
              .submenu ("Modulation", { i.matrix, i.macros })
              .item (i.step)
              .separator()
              .effects (set());
        return layout;
    }
}
