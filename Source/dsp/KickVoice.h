#pragma once
#include <vector>
#include "DspCommon.h"
#include "Envelope.h"
#include "../Parameters.h"

namespace torden::dsp
{
    using params::Values;
    namespace P = params;

    // Everything a single hit needs besides the parameters.
    struct HitInfo
    {
        float note = 36.0f;          // MIDI note (only used in tracked pitch mode)
        float velocity = 1.0f;       // 0..1
        float pitchOffset = 0.0f;    // semitones (step sequencer)
        float decayScale = 1.0f;     // stretches all envelopes (step sequencer)
    };

    enum Layer { BodyLayer, ClickLayer, SubLayer, NoiseLayer };

    // Values of one layer's shared controls, read live every block.
    struct LayerControls
    {
        P::Id on, level, pan, delay, invert, decay, drive, ftype, cutoff, reso;
    };

    inline const std::array<LayerControls, kNumLayers>& layerControls()
    {
        static const std::array<LayerControls, kNumLayers> c { {
            { P::BodyOn,  P::BodyLevel,  P::BodyPan,  P::BodyDelay,  P::BodyInvert,  P::BodyDecay,  P::BodyDrive,  P::BodyFType,  P::BodyCutoff,  P::BodyReso },
            { P::ClickOn, P::ClickLevel, P::ClickPan, P::ClickDelay, P::ClickInvert, P::ClickDecay, P::ClickDrive, P::ClickFType, P::ClickCutoff, P::ClickReso },
            { P::SubOn,   P::SubLevel,   P::SubPan,   P::SubDelay,   P::SubInvert,   P::SubDecay,   P::SubDrive,   P::SubFType,   P::SubCutoff,   P::SubReso },
            { P::NoiseOn, P::NoiseLevel, P::NoisePan, P::NoiseDelay, P::NoiseInvert, P::NoiseDecay, P::NoiseDrive, P::NoiseFType, P::NoiseCutoff, P::NoiseReso } } };
        return c;
    }

    // Base frequency of the body (the pitch the sweep ends on), from tune, pitch mode and the hit.
    inline float bodyBaseHz (const Values& v, const HitInfo& hit)
    {
        float semis = v[P::Tune] + v[P::Fine] * 0.01f + hit.pitchOffset;
        if (v.i (P::PitchMode) == 1)
            semis += hit.note - (float) v.i (P::RootNote);
        return v[P::BodyTune] * std::exp2 (semis / 12.0f);
    }

    // Saturation used inside the layers: tanh normalised so that full scale stays full scale.
    inline float layerDrive (float x, float amount)
    {
        if (amount < 0.001f) return x;
        const float g = 1.0f + amount * amount * 24.0f;
        return std::tanh (x * g) / std::tanh (g);
    }

    // One kick hit: four layers, each with its own envelopes, filter, drive and pan.
    class KickVoice
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            comb.assign ((size_t) juce::nextPowerOfTwo ((int) (sr / 30.0f) + 8), 0.0f);
            for (auto& s : smooth)
                for (auto& x : s)
                    x.setTime (0.004f, sr);
            active = false;
        }

        bool isActive() const { return active; }
        float age() const { return (float) samplesPlayed / sr; }

        // Starts a hit. 'v' is the parameter set modulated for this hit (velocity, random, macros ...).
        void start (const Values& v, const EnvelopeStore& envs, const HitInfo& h, uint32_t seed)
        {
            hit = h;
            rng.seed (seed);
            active = true;
            fading = false;
            fadeGain = 1.0f;
            samplesPlayed = 0;

            const float length = v[P::Length] * h.decayScale;
            const float vel = std::clamp (h.velocity, 0.0f, 1.0f);
            velLevel = 1.0f - v[P::VelLevel] * (1.0f - vel);
            velPitch = 1.0f - v[P::VelPitch] * (1.0f - vel);
            velClick = 1.0f - v[P::VelClick] * (1.0f - vel);

            amp[BodyLayer].start (envs.read (Env::BodyAmp), length * v[P::BodyDecay], sr);
            amp[ClickLayer].start (envs.read (Env::ClickAmp), length * v[P::ClickDecay], sr);
            amp[SubLayer].start (envs.read (Env::SubAmp), length * v[P::SubDecay], sr);
            amp[NoiseLayer].start (envs.read (Env::NoiseAmp), length * v[P::NoiseDecay], sr);
            pitchEnv.start (envs.read (Env::Pitch), v[P::BodyPTime] * h.decayScale, sr);
            noiseFilterEnv.start (envs.read (Env::NoiseFilter), length * v[P::NoiseDecay], sr);
            driveEnv.start (envs.read (Env::Drive), length, sr);

            baseHz = bodyBaseHz (v, h);
            pitchDepth = v[P::BodyPitch] * velPitch;
            const float startPhase = v[P::BodyPhase] / 360.0f;
            bodyPhase = startPhase;
            harmPhase2 = harmPhase3 = 0.0f;
            subPhase = startPhase;
            fmFeedback = 0.0f;
            clickPhase = clickModPhase = 0.0f;
            clickTime = 0.0f;
            clickCount = 0;
            pink.fill (0.0f);
            std::fill (comb.begin(), comb.end(), 0.0f);
            combWrite = 0;
            modes[0].reset(); modes[1].reset();

            for (int l = 0; l < kNumLayers; ++l)
            {
                const auto& c = layerControls()[(size_t) l];
                on[(size_t) l] = v.b (c.on);
                invert[(size_t) l] = v.b (c.invert) ? -1.0f : 1.0f;
                delaySamples[(size_t) l] = (int) (v[c.delay] * 0.001f * sr);
                filters[(size_t) l].reset();
                // Start the smoothers on the current values (no fade-in on the first samples)
                smooth[(size_t) l][0].reset (v[c.level]);
                smooth[(size_t) l][1].reset (v[c.cutoff]);
                smooth[(size_t) l][2].reset (v[c.drive]);
            }
            postFilter[0].reset(); postFilter[1].reset();
        }

        // Quick fade-out (3 ms) so a new hit can start without a click.
        void choke()
        {
            if (! active) return;
            fading = true;
            fadeStep = 1.0f / (0.003f * sr);
        }

        void kill() { active = false; }

        // Adds the voice to L/R. 'v' = the parameters of this block (live controls: level, pan, filter, shape ...).
        // 'layerOut' (optional) receives the four layers before panning – used by the kick display.
        void render (float* outL, float* outR, int n, const Values& v, std::array<float*, kNumLayers>* layerOut = nullptr)
        {
            if (! active) return;

            std::array<std::array<float, 2>, kNumLayers> pans;
            std::array<int, kNumLayers> ftype;
            std::array<float, kNumLayers> reso;
            for (int l = 0; l < kNumLayers; ++l)
            {
                const auto& c = layerControls()[(size_t) l];
                pans[(size_t) l] = panGains (v[c.pan]);
                ftype[(size_t) l] = v.i (c.ftype);
                reso[(size_t) l] = v[c.reso];
            }

            const float shape = v[P::BodyShape], fold = v[P::BodyFold], fm = v[P::BodyFm], harm = v[P::BodyHarm];
            const float driveEnvAmt = v[P::DriveEnv];
            const int clickMode = v.i (P::ClickMode);
            const float clickHz = v[P::ClickPitch], clickColor = v[P::ClickColor];
            const int subWave = v.i (P::SubWave);
            const bool subFixed = v.i (P::SubMode) == 1;
            const float subOct = std::exp2 ((float) v.i (P::SubOctave));
            const float subHz = v[P::SubHz], subFollow = v[P::SubFollow];
            const int noiseType = v.i (P::NoiseType);
            const float noiseTune = v[P::NoiseTune], noiseRes = v[P::NoiseRes], noiseFEnv = v[P::NoiseFEnv];
            const float nyquist = sr * 0.45f;

            for (int i = 0; i < n; ++i)
            {
                std::array<float, kNumLayers> out {};
                const float driveMod = driveEnv.next();
                const float driveScale = 1.0f - driveEnvAmt + driveEnvAmt * driveMod;
                const float pitchSemis = pitchDepth * pitchEnv.next();

                // ---- Body
                if (on[BodyLayer] && tickDelay (BodyLayer))
                {
                    const float hz = std::min (baseHz * std::exp2 (pitchSemis / 12.0f), nyquist);
                    const float dt = hz / sr;
                    float ph = bodyPhase + fm * 0.35f * fmFeedback;
                    ph -= std::floor (ph);
                    float x = morph (ph, dt, shape);
                    if (harm > 0.001f)
                    {
                        x += harm * (0.55f * std::sin (kTwoPi * harmPhase2) + 0.3f * std::sin (kTwoPi * harmPhase3));
                        harmPhase2 += std::min (dt * 2.0f, 0.45f); harmPhase2 -= std::floor (harmPhase2);
                        harmPhase3 += std::min (dt * 3.0f, 0.45f); harmPhase3 -= std::floor (harmPhase3);
                    }
                    if (fold > 0.001f)
                        x = x * (1.0f - fold) + fold * std::sin (x * (1.0f + fold * 4.0f) * kPi * 0.5f);
                    fmFeedback += (x - fmFeedback) * 0.5f;
                    bodyPhase += dt;
                    bodyPhase -= std::floor (bodyPhase);
                    x = layerDrive (x, smooth[BodyLayer][2].process (v[P::BodyDrive]) * driveScale);
                    out[BodyLayer] = x * amp[BodyLayer].next();
                }

                // ---- Click
                if (on[ClickLayer] && tickDelay (ClickLayer))
                {
                    out[ClickLayer] = clickSample (clickMode, clickHz, clickColor) * amp[ClickLayer].next() * velClick;
                    out[ClickLayer] = layerDrive (out[ClickLayer], smooth[ClickLayer][2].process (v[P::ClickDrive]));
                }

                // ---- Sub
                if (on[SubLayer] && tickDelay (SubLayer))
                {
                    const float hz = subFixed ? subHz : std::min (baseHz * subOct * std::exp2 (subFollow * pitchSemis / 12.0f), nyquist);
                    float x;
                    switch (subWave)
                    {
                        case 1:  x = subPhase < 0.5f ? 4.0f * subPhase - 1.0f : 3.0f - 4.0f * subPhase; x = -x; break;
                        case 2:  x = std::tanh (1.8f * std::sin (kTwoPi * subPhase)) / std::tanh (1.8f); break;
                        default: x = std::sin (kTwoPi * subPhase); break;
                    }
                    subPhase += hz / sr;
                    subPhase -= std::floor (subPhase);
                    x = layerDrive (x, smooth[SubLayer][2].process (v[P::SubDrive]) * driveScale);
                    out[SubLayer] = x * amp[SubLayer].next();
                }

                // ---- Noise / tail
                if (on[NoiseLayer] && tickDelay (NoiseLayer))
                {
                    const float a = amp[NoiseLayer].next();
                    float x = noiseSample (noiseType, noiseTune, noiseRes, a);
                    const float f = noiseFilterEnv.next();
                    const float cut = smooth[NoiseLayer][1].process (v[P::NoiseCutoff]) * std::exp2 (noiseFEnv * f);
                    x = filters[NoiseLayer].process (x, cut, reso[NoiseLayer], sr, ftype[NoiseLayer]);
                    x = layerDrive (x, smooth[NoiseLayer][2].process (v[P::NoiseDrive]));
                    out[NoiseLayer] = x * a;
                }

                // ---- Layer filters, level, pan
                float l = 0.0f, r = 0.0f;
                for (int k = 0; k < kNumLayers; ++k)
                {
                    if (! on[(size_t) k]) { if (layerOut) (*layerOut)[(size_t) k][i] = 0.0f; continue; }
                    const auto& c = layerControls()[(size_t) k];
                    float x = out[(size_t) k];
                    if (k != NoiseLayer)
                    {
                        const float cut = smooth[(size_t) k][1].process (v[c.cutoff]);
                        if (cut < 19900.0f || ftype[(size_t) k] != 0)
                            x = filters[(size_t) k].process (x, cut, reso[(size_t) k], sr, ftype[(size_t) k]);
                    }
                    x *= smooth[(size_t) k][0].process (v[c.level]) * invert[(size_t) k] * velLevel * fadeGain;
                    if (layerOut) (*layerOut)[(size_t) k][i] = x;
                    l += x * pans[(size_t) k][0];
                    r += x * pans[(size_t) k][1];
                }

                outL[i] += l;
                outR[i] += r;

                if (fading)
                {
                    fadeGain -= fadeStep;
                    if (fadeGain <= 0.0f) { active = false; fadeGain = 0.0f; clearRest (layerOut, i + 1, n); return; }
                }

                ++samplesPlayed;
                if ((samplesPlayed & 255) == 0 && isSilent())
                {
                    active = false;
                    clearRest (layerOut, i + 1, n);
                    return;
                }
            }
        }

    private:
        bool tickDelay (int layer)
        {
            auto& d = delaySamples[(size_t) layer];
            if (d <= 0) return true;
            --d;
            return false;
        }

        void clearRest (std::array<float*, kNumLayers>* layerOut, int from, int n)
        {
            if (layerOut != nullptr)
                for (auto* p : *layerOut)
                    std::fill (p + from, p + n, 0.0f);
        }

        // All amplitude envelopes have run out (and the delays are over) – the voice is done.
        bool isSilent() const
        {
            for (int l = 0; l < kNumLayers; ++l)
                if (on[(size_t) l] && (delaySamples[(size_t) l] > 0 || ! amp[(size_t) l].finished() || amp[(size_t) l].current() > 1.0e-4f))
                    return false;
            // Resonators (metallic / membrane) ring a little longer than their envelope – the envelope is the gate
            return true;
        }

        // Sine -> triangle -> saw -> square, crossfaded; saw and square are band-limited (PolyBLEP).
        static float morph (float ph, float dt, float shape)
        {
            const int a = std::clamp ((int) shape, 0, 3);
            const float f = shape - (float) a;
            auto wave = [ph, dt] (int w)
            {
                switch (w)
                {
                    case 0:  return std::sin (kTwoPi * ph);
                    case 1:  { const float t = ph + 0.25f - std::floor (ph + 0.25f);
                               return t < 0.5f ? 4.0f * t - 1.0f : 3.0f - 4.0f * t; }
                    case 2:  { float t = ph + 0.5f; t -= std::floor (t);
                               return 2.0f * t - 1.0f - polyBlep (t, dt); }
                    default: { float s = ph < 0.5f ? 1.0f : -1.0f;
                               s += polyBlep (ph, dt);
                               float t2 = ph + 0.5f; t2 -= std::floor (t2);
                               s -= polyBlep (t2, dt);
                               return s * 0.85f; }
                }
            };
            if (f < 0.0005f || a == 3) return wave (a);
            return wave (a) * (1.0f - f) + wave (a + 1) * f;
        }

        float clickSample (int mode, float hz, float color)
        {
            const float t = clickTime;
            clickTime += 1.0f / sr;
            const float nyq = sr * 0.45f;
            switch (mode)
            {
                case 0:   // Impulse: a single pulse ringing through a resonant band-pass
                {
                    const float in = clickCount == 0 ? 1.0f : (clickCount == 1 ? -0.6f : 0.0f);
                    ++clickCount;
                    return postFilter[0].process (in * 6.0f, std::min (hz, nyq), 0.3f + 0.65f * color, sr, Svf::Band);
                }
                case 1:   // Beater: a fast downward chirp onto the click pitch
                {
                    const float ratio = 2.0f + color * 8.0f;
                    const float f = std::min (hz * (1.0f + (ratio - 1.0f) * std::exp (-t / 0.0015f)), nyq);
                    clickPhase += f / sr;
                    clickPhase -= std::floor (clickPhase);
                    return std::sin (kTwoPi * clickPhase);
                }
                case 2:   // Noise burst, band-passed around the click pitch
                {
                    const float x = rng.nextBipolar();
                    return postFilter[0].process (x * 1.6f, std::min (hz, nyq), 0.15f + 0.7f * color, sr, Svf::Band) * (1.4f - color * 0.6f);
                }
                case 3:   // FM blip: inharmonic modulator, index falling fast
                {
                    const float index = (1.0f + color * 6.0f) * std::exp (-t / 0.004f);
                    clickModPhase += std::min (hz * 1.414f, nyq) / sr;
                    clickModPhase -= std::floor (clickModPhase);
                    clickPhase += std::min (hz, nyq) / sr;
                    clickPhase -= std::floor (clickPhase);
                    return std::sin (kTwoPi * clickPhase + index * std::sin (kTwoPi * clickModPhase));
                }
                case 4:   // Snap: high-passed noise plus a short square
                {
                    const float noise = postFilter[0].process (rng.nextBipolar(), std::min (hz, nyq), 0.2f, sr, Svf::High);
                    clickPhase += std::min (hz * 0.5f, nyq) / sr;
                    clickPhase -= std::floor (clickPhase);
                    const float sq = clickPhase < 0.5f ? 0.6f : -0.6f;
                    return noise * (1.0f - color * 0.7f) + sq * color;
                }
                default:  // Tick: a few cycles of a soft square, the colour sets how many
                {
                    clickPhase += std::min (hz, nyq) / sr;
                    const float cycles = 1.0f + color * 5.0f;
                    if (clickPhase > cycles) return 0.0f;
                    return std::tanh (4.0f * std::sin (kTwoPi * clickPhase));
                }
            }
        }

        float noiseSample (int type, float tune, float res, float env)
        {
            const float white = rng.nextBipolar();
            switch (type)
            {
                case 1:   // Pink (Paul Kellet's economical filter)
                {
                    pink[0] = 0.99765f * pink[0] + white * 0.0990460f;
                    pink[1] = 0.96300f * pink[1] + white * 0.2965164f;
                    pink[2] = 0.57000f * pink[2] + white * 1.0526913f;
                    return (pink[0] + pink[1] + pink[2] + white * 0.1848f) * 0.25f;
                }
                case 2:   // Crackle: sparse random impulses, density from the resonance knob
                {
                    const float density = 0.002f + res * res * 0.06f;
                    return rng.next01() < density ? rng.nextBipolar() * 2.0f : 0.0f;
                }
                case 3:   // Metallic: noise exciting a tuned comb resonator
                {
                    const int size = (int) comb.size();
                    const float delay = std::clamp (sr / std::max (20.0f, tune), 2.0f, (float) size - 2.0f);
                    float pos = (float) combWrite - delay;
                    while (pos < 0.0f) pos += (float) size;
                    const int i0 = (int) pos;
                    const float frac = pos - (float) i0;
                    const float d = comb[(size_t) i0] + frac * (comb[(size_t) ((i0 + 1) & (size - 1))] - comb[(size_t) i0]);
                    const float y = white * 0.35f * env + d * (0.6f + 0.38f * res);
                    comb[(size_t) combWrite] = softClip (y);
                    combWrite = (combWrite + 1) & (size - 1);
                    return y;
                }
                case 4:   // Membrane: two resonant modes of a drum skin (ratio 1 : 1.594), excited by noise
                {
                    const float exc = white * env;
                    const float q = 0.85f + 0.14f * res;
                    const float m1 = modes[0].process (exc, std::min (tune, sr * 0.45f), q, sr, Svf::Band);
                    const float m2 = modes[1].process (exc, std::min (tune * 1.594f, sr * 0.45f), q, sr, Svf::Band);
                    return (m1 + 0.6f * m2) * 1.5f;
                }
                default:
                    return white * 0.7f;
            }
        }

        float sr = 44100.0f;
        bool active = false, fading = false;
        float fadeGain = 1.0f, fadeStep = 0.0f;
        int64_t samplesPlayed = 0;
        HitInfo hit;
        Random rng;

        std::array<EnvPlayer, kNumLayers> amp;
        EnvPlayer pitchEnv, noiseFilterEnv, driveEnv;
        std::array<bool, kNumLayers> on {};
        std::array<float, kNumLayers> invert {};
        std::array<int, kNumLayers> delaySamples {};
        std::array<Svf, kNumLayers> filters;
        std::array<std::array<Smoother, 3>, kNumLayers> smooth;   // level, cutoff, drive
        float velLevel = 1.0f, velPitch = 1.0f, velClick = 1.0f;

        float baseHz = 50.0f, pitchDepth = 0.0f;
        float bodyPhase = 0.0f, harmPhase2 = 0.0f, harmPhase3 = 0.0f, fmFeedback = 0.0f;
        float subPhase = 0.0f;
        float clickPhase = 0.0f, clickModPhase = 0.0f, clickTime = 0.0f;
        int clickCount = 0;
        std::array<Svf, 2> postFilter;
        std::array<float, 3> pink {};
        std::vector<float> comb;
        int combWrite = 0;
        std::array<Svf, 2> modes;
    };
}
