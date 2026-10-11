#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>

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
        /* Effects */                                                                                                \
        X (SatOn,       "sat_on",       "Saturation On",     B, 0, 1, 0,         0, None, "")                        \
        X (SatMode,     "sat_mode",     "Saturation Mode",   C, 0, 5, 0,         0, None, "Tape|Tube|Diode|Fold|Hard|Rectify") \
        X (SatDrive,    "sat_drive",    "Saturation Drive",  F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (SatBias,     "sat_bias",     "Saturation Bias",   F, -1, 1, 0,        0, Bip, "")                         \
        X (SatTone,     "sat_tone",     "Saturation Tone",   F, -1, 1, 0,        0, Bip, "")                         \
        X (SatOs,       "sat_os",       "Oversampling",      C, 0, 2, 1,         0, None, "1x|2x|4x")                \
        X (SatMix,      "sat_mix",      "Saturation Mix",    F, 0, 1, 1,         0, Pct, "")                         \
        X (SatOut,      "sat_out",      "Saturation Output", F, -24, 12, 0,      0, Db, "")                          \
        X (CrushOn,     "crush_on",     "Crush On",          B, 0, 1, 0,         0, None, "")                        \
        X (CrushBits,   "crush_bits",   "Crush Bits",        F, 1, 16, 12,       0, Bits, "")                        \
        X (CrushRate,   "crush_rate",   "Crush Downsample",  F, 1, 40, 1,        6, Mult, "")                        \
        X (CrushJitter, "crush_jitter", "Crush Jitter",      F, 0, 1, 0,         0, Pct, "")                         \
        X (CrushMix,    "crush_mix",    "Crush Mix",         F, 0, 1, 1,         0, Pct, "")                         \
        X (TransOn,     "trans_on",     "Transient On",      B, 0, 1, 0,         0, None, "")                        \
        X (TransAttack, "trans_attack", "Transient Attack",  F, -1, 1, 0.3f,     0, Bip, "")                         \
        X (TransSustain,"trans_sustain","Transient Sustain", F, -1, 1, 0,        0, Bip, "")                         \
        X (TransSpeed,  "trans_speed",  "Transient Speed",   F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (TransOut,    "trans_out",    "Transient Output",  F, -12, 12, 0,      0, Db, "")                          \
        X (CompOn,      "comp_on",      "Compressor On",     B, 0, 1, 0,         0, None, "")                        \
        X (CompThresh,  "comp_thresh",  "Comp Threshold",    F, -48, 0, -18,     0, Db, "")                          \
        X (CompRatio,   "comp_ratio",   "Comp Ratio",        F, 1, 20, 4,        4, Ratio, "")                       \
        X (CompAttack,  "comp_attack",  "Comp Attack",       F, 0.1f, 100, 10,   10, Ms, "")                         \
        X (CompRelease, "comp_release", "Comp Release",      F, 10, 1000, 120,   120, Ms, "")                        \
        X (CompKnee,    "comp_knee",    "Comp Knee",         F, 0, 18, 6,        0, Db, "")                          \
        X (CompMakeup,  "comp_makeup",  "Comp Makeup",       F, 0, 24, 0,        0, Db, "")                          \
        X (CompMix,     "comp_mix",     "Comp Mix",          F, 0, 1, 1,         0, Pct, "")                         \
        X (CompHpf,     "comp_hpf",     "Comp Sidechain HPF",F, 20, 500, 20,     -1, Hz, "")                         \
        X (EqOn,        "eq_on",        "EQ On",             B, 0, 1, 0,         0, None, "")                        \
        X (EqLowCut,    "eq_lowcut",    "EQ Low Cut",        F, 20, 1000, 20,    -1, Hz, "")                         \
        X (EqLowFreq,   "eq_low_freq",  "EQ Low Freq",       F, 20, 500, 60,     -1, Hz, "")                         \
        X (EqLowGain,   "eq_low_gain",  "EQ Low Gain",       F, -18, 18, 0,      0, Db, "")                          \
        X (EqMidFreq,   "eq_mid_freq",  "EQ Mid Freq",       F, 40, 8000, 250,   -1, Hz, "")                         \
        X (EqMidGain,   "eq_mid_gain",  "EQ Mid Gain",       F, -18, 18, 0,      0, Db, "")                          \
        X (EqMidQ,      "eq_mid_q",     "EQ Mid Q",          F, 0.2f, 10, 1,     1, Plain, "")                       \
        X (EqMid2Freq,  "eq_mid2_freq", "EQ Mid 2 Freq",     F, 200, 16000, 3000,-1, Hz, "")                         \
        X (EqMid2Gain,  "eq_mid2_gain", "EQ Mid 2 Gain",     F, -18, 18, 0,      0, Db, "")                          \
        X (EqMid2Q,     "eq_mid2_q",    "EQ Mid 2 Q",        F, 0.2f, 10, 1,     1, Plain, "")                       \
        X (EqHighFreq,  "eq_high_freq", "EQ High Freq",      F, 1000, 20000, 8000,-1, Hz, "")                        \
        X (EqHighGain,  "eq_high_gain", "EQ High Gain",      F, -18, 18, 0,      0, Db, "")                          \
        X (EqHighCut,   "eq_highcut",   "EQ High Cut",       F, 1000, 20000, 20000,-1, Hz, "")                       \
        X (FiltOn,      "filt_on",      "Filter On",         B, 0, 1, 0,         0, None, "")                        \
        X (FiltType,    "filt_type",    "Filter Type",       C, 0, 4, 0,         0, None, "Ladder|Low Pass|Band Pass|High Pass|Notch") \
        X (FiltCutoff,  "filt_cutoff",  "Filter Cutoff",     F, 20, 20000, 2000, -1, Hz, "")                         \
        X (FiltReso,    "filt_reso",    "Filter Resonance",  F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (FiltEnv,     "filt_env",     "Filter Env Amount", F, -6, 6, 0,        0, Oct, "")                         \
        X (FiltDecay,   "filt_decay",   "Filter Env Decay",  F, 0.005f, 2, 0.15f, 0.15f, Time, "")                   \
        X (FiltDrive,   "filt_drive",   "Filter Drive",      F, 0, 1, 0,         0, Pct, "")                         \
        X (FiltMix,     "filt_mix",     "Filter Mix",        F, 0, 1, 1,         0, Pct, "")                         \
        X (VerbOn,      "verb_on",      "Reverb On",         B, 0, 1, 0,         0, None, "")                        \
        X (VerbMode,    "verb_mode",    "Reverb Mode",       C, 0, 2, 0,         0, None, "Room|Plate|Hall")         \
        X (VerbSize,    "verb_size",    "Reverb Size",       F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (VerbDecay,   "verb_decay",   "Reverb Decay",      F, 0.1f, 10, 0.8f,  1, Time, "")                        \
        X (VerbDamp,    "verb_damp",    "Reverb Damping",    F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (VerbPre,     "verb_predelay","Reverb Pre-Delay",  F, 0, 0.2f, 0.01f,  0.04f, Time, "")                    \
        X (VerbLowCut,  "verb_lowcut",  "Reverb Low Cut",    F, 20, 1000, 200,   -1, Hz, "")                         \
        X (VerbWidth,   "verb_width",   "Reverb Width",      F, 0, 1, 1,         0, Pct, "")                         \
        X (VerbDuck,    "verb_duck",    "Reverb Ducking",    F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (VerbMix,     "verb_mix",     "Reverb Mix",        F, 0, 1, 0.2f,      0, Pct, "")                         \
        X (DlyOn,       "dly_on",       "Delay On",          B, 0, 1, 0,         0, None, "")                        \
        X (DlySync,     "dly_sync",     "Delay Sync",        B, 0, 1, 1,         0, None, "")                        \
        X (DlyBeats,    "dly_beats",    "Delay Note",        C, 0, 10, 5,        0, None, "1/32|1/16T|1/16|1/8T|1/16D|1/8|1/4T|1/8D|1/4|1/4D|1/2") \
        X (DlyTime,     "dly_time",     "Delay Time",        F, 1, 2000, 250,    250, Ms, "")                        \
        X (DlyFeedback, "dly_fb",       "Delay Feedback",    F, 0, 0.95f, 0.3f,  0, Pct, "")                         \
        X (DlyPingPong, "dly_pingpong", "Delay Ping-Pong",   B, 0, 1, 0,         0, None, "")                        \
        X (DlyDamp,     "dly_damp",     "Delay Damping",     F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (DlyLowCut,   "dly_lowcut",   "Delay Low Cut",     F, 20, 2000, 150,   -1, Hz, "")                         \
        X (DlyDuck,     "dly_duck",     "Delay Ducking",     F, 0, 1, 0.4f,      0, Pct, "")                         \
        X (DlyMix,      "dly_mix",      "Delay Mix",         F, 0, 1, 0.15f,     0, Pct, "")                         \
        X (ChrOn,       "chr_on",       "Motion On",         B, 0, 1, 0,         0, None, "")                        \
        X (ChrMode,     "chr_mode",     "Motion Mode",       C, 0, 2, 0,         0, None, "Chorus|Phaser|Flanger")   \
        X (ChrRate,     "chr_rate",     "Motion Rate",       F, 0.02f, 10, 0.5f, -1, Hz, "")                         \
        X (ChrDepth,    "chr_depth",    "Motion Depth",      F, 0, 1, 0.5f,      0, Pct, "")                         \
        X (ChrFeedback, "chr_fb",       "Motion Feedback",   F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (ChrMix,      "chr_mix",      "Motion Mix",        F, 0, 1, 0.3f,      0, Pct, "")                         \
        X (WidthOn,     "width_on",     "Width On",          B, 0, 1, 0,         0, None, "")                        \
        X (WidthAmount, "width_amount", "Stereo Width",      F, 0, 2, 1,         0, Pct, "")                         \
        X (WidthMono,   "width_mono",   "Mono Below",        F, 20, 500, 120,    -1, Hz, "")                         \
        X (ClipOn,      "clip_on",      "Clipper On",        B, 0, 1, 1,         0, None, "")                        \
        X (ClipMode,    "clip_mode",    "Clipper Mode",      C, 0, 2, 0,         0, None, "Soft|Hard|Tape")          \
        X (ClipDrive,   "clip_drive",   "Clipper Drive",     F, 0, 24, 0,        0, Db, "")                          \
        X (ClipCeiling, "clip_ceiling", "Clipper Ceiling",   F, -12, 0, -0.3f,   0, Db, "")                          \
        /* Step sequencer */                                                                                         \
        X (SeqOn,       "seq_on",       "Step Mode",         B, 0, 1, 0,         0, None, "")                        \
        X (SeqLength,   "seq_length",   "Steps",             I, 1, 32, 16,       0, Steps, "")                       \
        X (SeqRate,     "seq_rate",     "Step Rate",         C, 0, 4, 2,         0, None, "1/8|1/8T|1/16|1/16T|1/32") \
        X (SeqSwing,    "seq_swing",    "Swing",             F, 0, 0.75f, 0,     0, Pct, "")                         \
        X (SeqBpm,      "seq_bpm",      "Internal Tempo",    F, 60, 200, 128,    0, Bpm, "")

    #define TORDEN_MOD_SLOT(X, n)                                                                                    \
        X (Mod##n##Src, "mod" #n "_src", "Mod " #n " Source", C, 0, 7, 0, 0, None, "None|Velocity|Note|Random|Macro 1|Macro 2|Macro 3|Macro 4") \
        X (Mod##n##Dst, "mod" #n "_dst", "Mod " #n " Target", C, 0, 0, 0, 0, None, "@targets")                     \
        X (Mod##n##Amt, "mod" #n "_amt", "Mod " #n " Amount", F, -1, 1, 0, 0, Bip, "")

    #define TORDEN_ENUM_ENTRY(e, ...) e,
    enum Id : int { TORDEN_PARAMS (TORDEN_ENUM_ENTRY) Count };
    #undef TORDEN_ENUM_ENTRY

    enum class Kind { Float, Int, Choice, Bool };
    enum class Format { None, Pct, Bip, Hz, Time, Ms, Db, Semi, Cents, Pan, Mult, Deg, Oct, Bits, Ratio, Shape, Note, Bpm, Steps, Plain };

    struct Spec
    {
        Id id;
        const char* paramId;
        const char* name;
        Kind kind;
        float min, max, def, curve;
        Format format;
        juce::StringArray choices;
    };

    const std::array<Spec, Count>& specs();
    inline const Spec& spec (Id id) { return specs()[(size_t) id]; }
    inline juce::String paramId (Id id) { return spec (id).paramId; }

    // Mapping between plain values and 0..1 (same maths as the host sees)
    float toNormalised (const Spec&, float plain);
    float fromNormalised (const Spec&, float normalised);
    juce::String formatValue (const Spec&, float plain);

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

    juce::String noteName (int midiNote);

    // Plain values of all parameters, read once per audio block.
    struct Values
    {
        std::array<float, Count> v {};
        float operator[] (Id id) const { return v[(size_t) id]; }
        float& operator[] (Id id) { return v[(size_t) id]; }
        int i (Id id) const { return (int) std::lround (v[(size_t) id]); }
        bool b (Id id) const { return v[(size_t) id] > 0.5f; }
    };

    class Reader
    {
    public:
        explicit Reader (juce::AudioProcessorValueTreeState& state)
        {
            for (int i = 0; i < Count; ++i)
            {
                raw[(size_t) i] = state.getRawParameterValue (specs()[(size_t) i].paramId);
                jassert (raw[(size_t) i] != nullptr);
            }
        }

        void read (Values& out) const
        {
            for (size_t i = 0; i < raw.size(); ++i)
                out.v[i] = raw[i]->load (std::memory_order_relaxed);
        }

    private:
        std::array<std::atomic<float>*, Count> raw {};
    };

    // Defaults as Values (for tests and offline rendering)
    Values defaults();
}
