#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <fjord_base/params/Params.h>
#include <fjord_base/fx/FxParams.h>

namespace torden::params
{
    // Every parameter is listed once here. The columns are:
    //   enum name, parameter ID, display name, kind (F float, I int, C choice, B bool), min, max, default,
    //   curve (0 linear, -1 logarithmic, > 0 skewed so that this value sits in the middle), value format, choices
    //
    // Rules that keep projects and presets working:
    //   - IDs never change. New parameters go to the END of the list (the order defines mod-target indices).
    //   - Choice lists are append-only.
    //   - New parameters need neutral defaults (presets without them fall back to the default).

    #define TORDEN_LAYER_PARAMS(X, E, P, N, ON, CUT, FT, LVL)                                                        \
        X (E##On,     P "_on",     N " On",          B, 0, 1, ON,     0, None, "")                                   \
        X (E##Level,  P "_level",  N " Level",       F, 0, 1, LVL,    0, Pct, "")                                    \
        X (E##Pan,    P "_pan",    N " Pan",         F, -1, 1, 0,     0, Pan, "")                                    \
        X (E##Delay,  P "_delay",  N " Delay",       F, 0, 20, 0,     4, Ms, "")                                     \
        X (E##Invert, P "_invert", N " Invert",      B, 0, 1, 0,      0, None, "")                                   \
        X (E##Decay,  P "_decay",  N " Decay",       F, 0.1f, 4, 1,   1, Mult, "")                                   \
        X (E##Drive,  P "_drive",  N " Drive",       F, 0, 1, 0,      0, Pct, "")                                    \
        X (E##FType,  P "_ftype",  N " Filter Type", C, 0, 3, FT,     0, None, "Low Pass|High Pass|Band Pass|Notch") \
        X (E##Cutoff, P "_cutoff", N " Cutoff",      F, 20, 20000, CUT, -1, Hz, "")                                  \
        X (E##Reso,   P "_reso",   N " Resonance",   F, 0, 1, 0,      0, Pct, "")

    #define TORDEN_PARAMS(X)                                                                                         \
        /* Global */                                                                                                 \
        X (MasterGain,  "master_gain",  "Master",            F, -36, 6, 0,       0, Db, "")                          \
        X (PresetLevel, "preset_level", "Preset Level",      F, -24, 12, 0,      0, Db, "")                          \
        X (PitchMode,   "pitch_mode",   "Pitch Mode",        C, 0, 1, 0,         0, None, "Fixed|Tracked")           \
        X (RootNote,    "root_note",    "Root Note",         I, 24, 72, 36,      0, Note, "")                        \
        X (Tune,        "tune",         "Tune",              F, -24, 24, 0,      0, Semi, "")                        \
        X (Fine,        "fine",         "Fine",              F, -100, 100, 0,    0, Cents, "")                       \
        X (Length,      "length",       "Length",            F, 0.25f, 4, 1,     1, Mult, "")                        \
        X (VelLevel,    "vel_level",    "Velocity > Level",  F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (VelPitch,    "vel_pitch",    "Velocity > Pitch",  F, 0, 1, 0,         0, Pct, "")                         \
        X (VelClick,    "vel_click",    "Velocity > Click",  F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (Retrigger,   "retrigger",    "Retrigger",         C, 0, 1, 0,         0, None, "Choke|Overlap")           \
        X (DriveEnv,    "drive_env",    "Drive Envelope",    F, 0, 1, 0,         0, Pct, "")                         \
        /* Body: the tonal, pitch-swept core */                                                                      \
        TORDEN_LAYER_PARAMS (X, Body, "body", "Body", 1, 20000, 0, 0.7f)                                                   \
        X (BodyTune,    "body_tune",    "Body Tune",         F, 20, 200, 49,     -1, Hz, "")                         \
        X (BodyShape,   "body_shape",   "Body Shape",        F, 0, 3, 0,         0, Shape, "")                       \
        X (BodyFold,    "body_fold",    "Body Fold",         F, 0, 1, 0,         0, Pct, "")                         \
        X (BodyFm,      "body_fm",      "Body FM",           F, 0, 1, 0,         0, Pct, "")                         \
        X (BodyHarm,    "body_harm",    "Body Harmonics",    F, 0, 1, 0,         0, Pct, "")                         \
        X (BodyPhase,   "body_phase",   "Body Start Phase",  F, 0, 90, 0,        0, Deg, "")                         \
        X (BodyPitch,   "body_pitch",   "Pitch Depth",       F, 0, 72, 36,       0, Semi, "")                        \
        X (BodyPTime,   "body_ptime",   "Pitch Time",        F, 0.1f, 4, 1,      1, Mult, "")                        \
        /* Click: the transient */                                                                                   \
        TORDEN_LAYER_PARAMS (X, Click, "click", "Click", 1, 20000, 0, 0.35f)                                                \
        X (ClickMode,   "click_mode",   "Click Mode",        C, 0, 5, 1,         0, None, "Impulse|Beater|Noise|FM Blip|Snap|Tick") \
        X (ClickPitch,  "click_pitch",  "Click Pitch",       F, 100, 12000, 2500, -1, Hz, "")                        \
        X (ClickColor,  "click_color",  "Click Color",       F, 0, 1, 0.5f,      0, Pct, "")                         \
        /* Sub: the low sine underneath */                                                                           \
        TORDEN_LAYER_PARAMS (X, Sub, "sub", "Sub", 0, 20000, 0, 0.5f)                                                      \
        X (SubWave,     "sub_wave",     "Sub Wave",          C, 0, 2, 0,         0, None, "Sine|Triangle|Warm")      \
        X (SubMode,     "sub_mode",     "Sub Mode",          C, 0, 1, 0,         0, None, "Follow|Fixed")            \
        X (SubOctave,   "sub_octave",   "Sub Octave",        I, -2, 1, 0,        0, Oct, "")                         \
        X (SubHz,       "sub_hz",       "Sub Frequency",     F, 20, 120, 45,     -1, Hz, "")                         \
        X (SubFollow,   "sub_follow",   "Sub Pitch Follow",  F, 0, 1, 0.3f,      0, Pct, "")                         \
        /* Noise / tail: texture, room, membrane */                                                                  \
        TORDEN_LAYER_PARAMS (X, Noise, "noise", "Noise", 0, 8000, 0, 0.3f)                                                 \
        X (NoiseType,   "noise_type",   "Noise Type",        C, 0, 4, 0,         0, None, "White|Pink|Crackle|Metallic|Membrane") \
        X (NoiseTune,   "noise_tune",   "Noise Tune",        F, 40, 4000, 180,   -1, Hz, "")                         \
        X (NoiseRes,    "noise_res",    "Noise Resonance",   F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (NoiseFEnv,   "noise_fenv",   "Noise Filter Env",  F, -6, 6, 3,        0, Oct, "")                         \
        /* Macros and modulation matrix */                                                                           \
        X (Macro1, "macro1", "Macro 1", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro2, "macro2", "Macro 2", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro3, "macro3", "Macro 3", F, 0, 1, 0, 0, Pct, "")                                                      \
        X (Macro4, "macro4", "Macro 4", F, 0, 1, 0, 0, Pct, "")                                                      \
        TORDEN_MOD_SLOT (X, 1) TORDEN_MOD_SLOT (X, 2) TORDEN_MOD_SLOT (X, 3) TORDEN_MOD_SLOT (X, 4)                  \
        TORDEN_MOD_SLOT (X, 5) TORDEN_MOD_SLOT (X, 6) TORDEN_MOD_SLOT (X, 7) TORDEN_MOD_SLOT (X, 8)                  \
        /* Step sequencer */                                                                                         \
        X (SeqOn,       "seq_on",       "Step Mode",         B, 0, 1, 0,         0, None, "")                        \
        X (SeqLength,   "seq_length",   "Steps",             I, 1, 32, 16,       0, Steps, "")                       \
        X (SeqRate,     "seq_rate",     "Step Rate",         C, 0, 4, 2,         0, None, "1/8|1/8T|1/16|1/16T|1/32") \
        X (SeqSwing,    "seq_swing",    "Swing",             F, 0, 0.75f, 0,     0, Pct, "")                         \
        X (SeqBpm,      "seq_bpm",      "Internal Tempo",    F, 60, 200, 128,    0, Bpm, "")                         \
        /* Effects: the shared block of the family (fjord-base), always last */                                     \
        FJORD_FX_PARAMS (X)

    #define TORDEN_MOD_SLOT(X, n)                                                                                    \
        X (Mod##n##Src, "mod" #n "_src", "Mod " #n " Source", C, 0, 7, 0, 0, None, "None|Velocity|Note|Random|Macro 1|Macro 2|Macro 3|Macro 4") \
        X (Mod##n##Dst, "mod" #n "_dst", "Mod " #n " Target", C, 0, 0, 0, 0, None, "@targets")                     \
        X (Mod##n##Amt, "mod" #n "_amt", "Mod " #n " Amount", F, -1, 1, 0, 0, Bip, "")

    enum Id : int { TORDEN_PARAMS (FJORD_PARAM_ENUM) Count };

    // Kinds, formats, specs, value snapshots: the shared parameter machinery of fjord-base
    using Kind = fjord::base::params::Kind;
    using Format = fjord::base::params::Format;
    using Spec = fjord::base::params::Spec;
    using fjord::base::params::toNormalised;
    using fjord::base::params::fromNormalised;
    using fjord::base::params::formatValue;
    using fjord::base::params::noteName;

    const std::array<Spec, Count>& specs();
    inline const Spec& spec (Id id) { return specs()[(size_t) id]; }
    inline juce::String paramId (Id id) { return spec (id).paramId; }

    // Modulation targets: every continuous parameter except the ones that make no sense to modulate.
    const std::vector<Id>& modTargets();
    constexpr int kNumModSlots = 8;
    constexpr int kNumMacros = 4;
    inline Id modSrc (int slot) { return (Id) (Mod1Src + slot * 3); }
    inline Id modDst (int slot) { return (Id) (Mod1Dst + slot * 3); }
    inline Id modAmt (int slot) { return (Id) (Mod1Amt + slot * 3); }
    inline Id macro (int index) { return (Id) (Macro1 + index); }

    enum class ModSource { None, Velocity, Note, Random, Macro1, Macro2, Macro3, Macro4 };

    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

    // Plain values of all parameters, read once per audio block.
    using Values = fjord::base::params::ValuesT<Id, Count>;

    class Reader : public fjord::base::params::ReaderT<Id, Count>
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState& state) : ReaderT (state, specs()) {}
    };

    // The effect block inside the values (fjord-base effects read it directly)
    inline fjord::base::fx::Values fxValues (const Values& v) { return { v.data() + FxSatOn }; }
    static_assert (FxOutCeiling - FxSatOn + 1 == fjord::base::fx::NumParams, "the effect block must be complete and in order");

    // Defaults as Values (for tests and offline rendering)
    Values defaults();
}
