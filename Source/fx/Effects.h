#pragma once
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <complex>
#include "../dsp/DspCommon.h"
#include "../Parameters.h"

namespace torden::fx
{
    using dsp::Smoother;
    using dsp::softClip;
    using dsp::kPi;
    using dsp::kTwoPi;
    using params::Values;
    namespace P = params;

    struct StereoBuffer
    {
        float* l;
        float* r;
        int n;
    };

    // ---------------------------------------------------------------------------------------------
    // Helpers (delay line and biquad from Fjord)

    class DelayLine
    {
    public:
        void prepare (int maxSamples)
        {
            int size = 1;
            while (size < maxSamples + 4) size <<= 1;
            buffer.assign ((size_t) size, 0.0f);
            mask = size - 1;
            write = 0;
        }

        void clear() { std::fill (buffer.begin(), buffer.end(), 0.0f); }
        void push (float x) { buffer[(size_t) write] = x; write = (write + 1) & mask; }

        float read (float delay) const
        {
            const float pos = (float) write - delay;
            const int i = (int) std::floor (pos);
            const float f = pos - (float) i;
            const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
            const float c1 = 0.5f * (x1 - xm1);
            const float c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2;
            const float c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
            return ((c3 * f + c2) * f + c1) * f + x0;
        }

        int maxDelay() const { return mask - 4; }

    private:
        float at (int i) const { return buffer[(size_t) (i & mask)]; }
        std::vector<float> buffer;
        int mask = 0, write = 0;
    };

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        std::array<float, 2> z1 {}, z2 {};

        float process (float x, int ch)
        {
            const float y = b0 * x + z1[(size_t) ch];
            z1[(size_t) ch] = b1 * x - a1 * y + z2[(size_t) ch];
            z2[(size_t) ch] = b2 * x - a2 * y;
            return y;
        }

        void reset() { z1.fill (0.0f); z2.fill (0.0f); }

        void set (double nb0, double nb1, double nb2, double na0, double na1, double na2)
        {
            b0 = (float) (nb0 / na0); b1 = (float) (nb1 / na0); b2 = (float) (nb2 / na0);
            a1 = (float) (na1 / na0); a2 = (float) (na2 / na0);
        }

        void highpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 + c) / 2, -(1 + c), (1 + c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void lowpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set ((1 - c) / 2, 1 - c, (1 - c) / 2, 1 + a, -2 * c, 1 - a);
        }

        void bandpass (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (a, 0, -a, 1 + a, -2 * c, 1 - a);
        }

        void notch (double sr, double f, double q)
        {
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (1, -2 * c, 1, 1 + a, -2 * c, 1 - a);
        }

        void peak (double sr, double f, double q, double gainDb)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), a = std::sin (w) / (2.0 * q);
            set (1 + a * A, -2 * c, 1 - a * A, 1 + a / A, -2 * c, 1 - a / A);
        }

        void shelf (double sr, double f, double gainDb, bool high)
        {
            const double A = std::pow (10.0, gainDb / 40.0);
            const double w = 2.0 * juce::MathConstants<double>::pi * f / sr, c = std::cos (w), s = std::sin (w);
            const double a = s / 2.0 * std::sqrt (2.0), sq = 2.0 * std::sqrt (A) * a;
            if (high)
                set (A * ((A + 1) + (A - 1) * c + sq), -2 * A * ((A - 1) + (A + 1) * c), A * ((A + 1) + (A - 1) * c - sq),
                     (A + 1) - (A - 1) * c + sq, 2 * ((A - 1) - (A + 1) * c), (A + 1) - (A - 1) * c - sq);
            else
                set (A * ((A + 1) - (A - 1) * c + sq), 2 * A * ((A - 1) - (A + 1) * c), A * ((A + 1) - (A - 1) * c - sq),
                     (A + 1) + (A - 1) * c + sq, -2 * ((A - 1) + (A + 1) * c), (A + 1) + (A - 1) * c - sq);
        }

        // Magnitude response (for graphs)
        double magnitude (double freq, double sr) const
        {
            const std::complex<double> z = std::polar (1.0, -2.0 * juce::MathConstants<double>::pi * freq / sr);
            const auto num = (double) b0 + (double) b1 * z + (double) b2 * z * z;
            const auto den = 1.0 + (double) a1 * z + (double) a2 * z * z;
            return std::abs (num / den);
        }
    };

    inline void equalPowerMix (float mix, float& dryGain, float& wetGain)
    {
        dryGain = std::cos (mix * kPi * 0.5f);
        wetGain = std::sin (mix * kPi * 0.5f);
    }

    inline float onePoleCoef (float hz, float sr) { return 1.0f - std::exp (-kTwoPi * std::min (hz, sr * 0.45f) / sr); }

    // ---------------------------------------------------------------------------------------------
    // Saturation: six transfer curves, bias, tilt tone, 1x / 2x / 4x oversampling

    class Saturation
    {
    public:
        void prepare (double sr, int maxBlock)
        {
            sampleRate = (float) sr;
            for (auto& o : oversamplers) o->initProcessing ((size_t) maxBlock);
            dry.setSize (2, maxBlock);
            mix.setTime (0.02f, sampleRate);
            reset();
        }

        void reset() { for (auto& o : oversamplers) o->reset(); tilt.fill (0.0f); lastGain = -1.0f; }

        // The transfer curve, shared with the UI graph.
        static float shape (int mode, float x, float bias)
        {
            const float b = bias * 0.5f;
            switch (mode)
            {
                case 0:  return std::tanh (x + b) - std::tanh (b);                                             // Tape
                case 1:  { const float y = x + b;                                                              // Tube: asymmetric
                           return (y >= 0.0f ? 1.0f - std::exp (-y) : -(1.0f - std::exp (1.4f * y)) / 1.4f)
                                  - (b >= 0.0f ? 1.0f - std::exp (-b) : -(1.0f - std::exp (1.4f * b)) / 1.4f); }
                case 2:  { const float y = x + b;                                                              // Diode: soft one side, hard the other
                           return (y > 0.0f ? std::tanh (y * 1.4f) / 1.4f * 1.2f : -std::min (0.55f, -y * 0.9f)) ; }
                case 3:  return std::sin ((x + b) * 1.5f) - std::sin (b * 1.5f);                               // Fold
                case 4:  return std::clamp (x + b, -1.0f, 1.0f) - std::clamp (b, -1.0f, 1.0f);                 // Hard
                default: { const float y = std::tanh (x);                                                      // Rectify (octave up)
                           return y * (0.5f - b * 0.5f) + std::abs (y) * (0.5f + b * 0.5f) - (0.25f + 0.25f * b); }
            }
        }

        static float driveGain (float drive) { return std::pow (10.0f, drive * 36.0f / 20.0f); }

        void process (StereoBuffer b, const Values& v)
        {
            dry.copyFrom (0, 0, b.l, b.n);
            dry.copyFrom (1, 0, b.r, b.n);

            const int mode = v.i (P::SatMode);
            const float bias = v[P::SatBias];
            const float gain = driveGain (v[P::SatDrive]);
            const float comp = 1.0f / (1.0f + v[P::SatDrive] * 2.2f);
            if (lastGain < 0.0f) lastGain = gain;

            const int os = std::clamp (v.i (P::SatOs), 0, 2);
            float* chans[] = { b.l, b.r };
            juce::dsp::AudioBlock<float> block (chans, 2, (size_t) b.n);
            auto run = [&] (juce::dsp::AudioBlock<float> blk)
            {
                const auto n = blk.getNumSamples();
                const float step = (gain - lastGain) / (float) n;
                for (size_t ch = 0; ch < 2; ++ch)
                {
                    auto* d = blk.getChannelPointer (ch);
                    float g = lastGain;
                    for (size_t i = 0; i < n; ++i)
                    {
                        g += step;
                        d[i] = shape (mode, d[i] * g, bias) * comp;
                    }
                }
            };
            if (os == 0)
                run (block);
            else
            {
                auto& o = *oversamplers[(size_t) os - 1];
                run (o.processSamplesUp (block));
                o.processSamplesDown (block);
            }
            lastGain = gain;

            // Tilt tone around 900 Hz, output level, dry/wet
            const float t = v[P::SatTone];
            const float lowG = 1.0f - std::max (0.0f, t) * 0.6f, highG = 1.0f + t * (t > 0.0f ? 1.0f : 0.8f);
            const float c = onePoleCoef (900.0f, sampleRate);
            const float out = juce::Decibels::decibelsToGain (v[P::SatOut]);
            float inPeak = 0.0f, outPeak = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                tilt[0] += (b.l[i] - tilt[0]) * c;
                tilt[1] += (b.r[i] - tilt[1]) * c;
                const float wl = (tilt[0] * lowG + (b.l[i] - tilt[0]) * highG) * out;
                const float wr = (tilt[1] * lowG + (b.r[i] - tilt[1]) * highG) * out;
                const float m = mix.process (v[P::SatMix]);
                const float dl = dry.getSample (0, i), dr = dry.getSample (1, i);
                inPeak = std::max (inPeak, std::max (std::abs (dl), std::abs (dr)));
                b.l[i] = dl * (1.0f - m) + wl * m;
                b.r[i] = dr * (1.0f - m) + wr * m;
                outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
            }
            lastIn = inPeak;
            lastOut = outPeak;
        }

        float lastIn = 0.0f, lastOut = 0.0f;

    private:
        float sampleRate = 44100.0f, lastGain = -1.0f;
        std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, 2> oversamplers {
            std::make_unique<juce::dsp::Oversampling<float>> (2, 1, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true),
            std::make_unique<juce::dsp::Oversampling<float>> (2, 2, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true) };
        juce::AudioBuffer<float> dry;
        std::array<float, 2> tilt {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Bitcrusher: bit depth, sample rate reduction with jitter

    class Crush
    {
    public:
        void prepare (double sr) { mix.setTime (0.02f, (float) sr); reset(); }
        void reset() { hold = { 0.0f, 0.0f }; phase = 1.0f; rng.seed (77); }

        void process (StereoBuffer b, const Values& v)
        {
            const float q = std::pow (2.0f, v[P::CrushBits] - 1.0f);
            const float baseStep = 1.0f / std::max (1.0f, v[P::CrushRate]);
            const float jitter = v[P::CrushJitter];
            float step = baseStep;
            for (int i = 0; i < b.n; ++i)
            {
                phase += step;
                if (phase >= 1.0f)
                {
                    phase -= 1.0f;
                    hold[0] = std::round (b.l[i] * q) / q;
                    hold[1] = std::round (b.r[i] * q) / q;
                    step = baseStep * (1.0f + jitter * 0.9f * rng.nextBipolar());
                }
                const float m = mix.process (v[P::CrushMix]);
                b.l[i] += (hold[0] - b.l[i]) * m;
                b.r[i] += (hold[1] - b.r[i]) * m;
            }
        }

    private:
        std::array<float, 2> hold {};
        float phase = 1.0f;
        dsp::Random rng;
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // Transient shaper: compares fast and slow envelope followers (level independent)

    class TransientShaper
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; reset(); }
        void reset() { e.fill (0.0f); gainDb = 0.0f; }

        void process (StereoBuffer b, const Values& v)
        {
            const float speed = v[P::TransSpeed];
            auto coef = [this] (float seconds) { return std::exp (-1.0f / (std::max (0.00005f, seconds) * sampleRate)); };
            const float fastA = coef (0.0005f), slowA = coef (0.004f + (1.0f - speed) * 0.036f), rel = coef (0.12f);
            const float susLong = coef (0.04f + (1.0f - speed) * 0.3f), susShort = coef (0.006f), att = coef (0.001f);
            const float attack = v[P::TransAttack], sustain = v[P::TransSustain];
            const float out = juce::Decibels::decibelsToGain (v[P::TransOut]);
            float minG = 0.0f, maxG = 0.0f;

            auto follow = [] (float& s, float x, float a, float r) { s = x > s ? x + (s - x) * a : x + (s - x) * r; };

            for (int i = 0; i < b.n; ++i)
            {
                const float x = std::max (std::abs (b.l[i]), std::abs (b.r[i]));
                follow (e[0], x, fastA, rel);
                follow (e[1], x, slowA, rel);
                follow (e[2], x, att, susLong);
                follow (e[3], x, att, susShort);
                const float dA = std::max (0.0f, e[0] - e[1]) / std::max (1.0e-5f, e[0]);
                const float dS = std::max (0.0f, e[2] - e[3]) / std::max (1.0e-5f, e[2]);
                const float target = attack * 18.0f * dA + sustain * 18.0f * dS;
                gainDb += (target - gainDb) * 0.2f;
                const float g = juce::Decibels::decibelsToGain (gainDb) * out;
                b.l[i] *= g;
                b.r[i] *= g;
                minG = std::min (minG, gainDb);
                maxG = std::max (maxG, gainDb);
            }
            lastGainDb = std::abs (maxG) > std::abs (minG) ? maxG : minG;
        }

        float lastGainDb = 0.0f;

    private:
        float sampleRate = 44100.0f, gainDb = 0.0f;
        std::array<float, 4> e {};
    };

    // ---------------------------------------------------------------------------------------------
    // Compressor: stereo linked, soft knee, parallel mix, sidechain high-pass (from Fjord)

    class Compressor
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; mix.setTime (0.02f, sampleRate); reset(); }
        void reset() { envDb = 0.0f; hpX = { 0.0f, 0.0f }; hpY = { 0.0f, 0.0f }; }

        static float reduction (float levelDb, float threshold, float ratio, float knee)
        {
            const float over = levelDb - threshold;
            const float slope = 1.0f - 1.0f / std::max (1.0f, ratio);
            if (knee < 0.01f || over >= knee * 0.5f) return std::max (0.0f, over) * slope;
            if (over <= -knee * 0.5f) return 0.0f;
            const float k = over + knee * 0.5f;
            return slope * k * k / (2.0f * knee);
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float att = std::exp (-1.0f / (std::max (0.0001f, v[P::CompAttack] * 0.001f) * sampleRate));
            const float rel = std::exp (-1.0f / (std::max (0.005f, v[P::CompRelease] * 0.001f) * sampleRate));
            const float makeup = juce::Decibels::decibelsToGain (v[P::CompMakeup]);
            const float hpfHz = v[P::CompHpf];
            const bool hpf = hpfHz > 20.5f;
            const float hpA = 1.0f / (1.0f + kTwoPi * hpfHz / sampleRate);
            const float threshold = v[P::CompThresh], ratio = v[P::CompRatio], knee = v[P::CompKnee];
            float maxRed = 0.0f, inPeak = 0.0f, outPeak = 0.0f;

            for (int i = 0; i < b.n; ++i)
            {
                float dl = b.l[i], dr = b.r[i];
                inPeak = std::max (inPeak, std::max (std::abs (dl), std::abs (dr)));
                if (hpf)
                {
                    hpY[0] = hpA * (hpY[0] + dl - hpX[0]); hpX[0] = dl; dl = hpY[0];
                    hpY[1] = hpA * (hpY[1] + dr - hpX[1]); hpX[1] = dr; dr = hpY[1];
                }
                const float level = juce::Decibels::gainToDecibels (std::max (std::abs (dl), std::abs (dr)), -120.0f);
                const float red = reduction (level, threshold, ratio, knee);
                envDb = red > envDb ? red + (envDb - red) * att : red + (envDb - red) * rel;
                maxRed = std::max (maxRed, envDb);
                const float g = juce::Decibels::decibelsToGain (-envDb) * makeup;
                const float m = mix.process (v[P::CompMix]);
                b.l[i] = b.l[i] * (1.0f - m) + b.l[i] * g * m;
                b.r[i] = b.r[i] * (1.0f - m) + b.r[i] * g * m;
                outPeak = std::max (outPeak, std::max (std::abs (b.l[i]), std::abs (b.r[i])));
            }
            lastReduction = maxRed;
            lastIn = inPeak;
            lastOut = outPeak;
        }

        float lastReduction = 0.0f, lastIn = 0.0f, lastOut = 0.0f;

    private:
        float sampleRate = 44100.0f, envDb = 0.0f;
        std::array<float, 2> hpX {}, hpY {};
        Smoother mix;
    };

    // ---------------------------------------------------------------------------------------------
    // EQ: low cut (24 dB), low shelf, two peaks, high shelf, high cut (24 dB). The design is shared
    // with the EQ graph so it shows exactly what the DSP does.

    struct EqDesign
    {
        static constexpr int kNumFilters = 8;
        std::array<Biquad, kNumFilters> filters;
        std::array<bool, kNumFilters> use {};
        std::array<int, kNumFilters> band {};   // 0 low cut, 1 low, 2 mid, 3 mid 2, 4 high, 5 high cut

        void design (double sr, const Values& v)
        {
            const double safe = sr * 0.45;
            constexpr double q24a = 0.5412, q24b = 1.3066;
            auto f = [safe] (float hz) { return std::min ((double) hz, safe); };
            filters[0].highpass (sr, f (v[P::EqLowCut]), q24a);
            filters[1].highpass (sr, f (v[P::EqLowCut]), q24b);
            filters[2].shelf (sr, f (v[P::EqLowFreq]), v[P::EqLowGain], false);
            filters[3].peak (sr, f (v[P::EqMidFreq]), v[P::EqMidQ], v[P::EqMidGain]);
            filters[4].peak (sr, f (v[P::EqMid2Freq]), v[P::EqMid2Q], v[P::EqMid2Gain]);
            filters[5].shelf (sr, f (v[P::EqHighFreq]), v[P::EqHighGain], true);
            filters[6].lowpass (sr, f (v[P::EqHighCut]), q24a);
            filters[7].lowpass (sr, f (v[P::EqHighCut]), q24b);
            const bool lowCut = v[P::EqLowCut] > 20.5f, highCut = v[P::EqHighCut] < 19900.0f;
            use = { lowCut, lowCut, std::abs (v[P::EqLowGain]) > 0.01f, std::abs (v[P::EqMidGain]) > 0.01f,
                    std::abs (v[P::EqMid2Gain]) > 0.01f, std::abs (v[P::EqHighGain]) > 0.01f, highCut, highCut };
            band = { 0, 0, 1, 2, 3, 4, 5, 5 };
        }

        double magnitudeDb (double freq, double sr, int onlyBand = -1) const
        {
            double db = 0.0;
            for (size_t k = 0; k < filters.size(); ++k)
                if (use[k] && (onlyBand < 0 || band[k] == onlyBand))
                    db += 20.0 * std::log10 (std::max (1.0e-9, filters[k].magnitude (freq, sr)));
            return db;
        }
    };

    class Eq
    {
    public:
        void prepare (double sr) { sampleRate = sr; reset(); lastKey = -1.0f; }
        void reset() { for (auto& f : eq.filters) f.reset(); }

        void process (StereoBuffer b, const Values& v)
        {
            float key = 0.0f;
            for (auto id : { P::EqLowCut, P::EqLowFreq, P::EqLowGain, P::EqMidFreq, P::EqMidGain, P::EqMidQ,
                             P::EqMid2Freq, P::EqMid2Gain, P::EqMid2Q, P::EqHighFreq, P::EqHighGain, P::EqHighCut })
                key = key * 1.0001f + v[id] * (float) (1 + (int) id);
            if (! juce::exactlyEqual (key, lastKey))
            {
                lastKey = key;
                eq.design (sampleRate, v);
            }
            for (int i = 0; i < b.n; ++i)
            {
                float l = b.l[i], r = b.r[i];
                for (size_t k = 0; k < eq.filters.size(); ++k)
                    if (eq.use[k]) { l = eq.filters[k].process (l, 0); r = eq.filters[k].process (r, 1); }
                b.l[i] = l;
                b.r[i] = r;
            }
        }

    private:
        double sampleRate = 44100.0;
        EqDesign eq;
        float lastKey = -1.0f;
    };

    // ---------------------------------------------------------------------------------------------
    // Filter: ladder (4-pole, saturating) or SVF, with an envelope that restarts on every hit

    class Filter
    {
    public:
        void prepare (double sr) { sampleRate = (float) sr; mix.setTime (0.02f, sampleRate); cutoff.setTime (0.01f, sampleRate); reset(); }
        void reset() { for (auto& s : ladder) s.fill (0.0f); for (auto& s : svf) s.reset(); env = 0.0f; cutoff.reset (-1.0f); }

        void trigger() { env = 1.0f; }

        void process (StereoBuffer b, const Values& v, const int* hits, int numHits)
        {
            const int type = v.i (P::FiltType);
            const float reso = v[P::FiltReso], amount = v[P::FiltEnv];
            const float decay = std::exp (-1.0f / (std::max (0.002f, v[P::FiltDecay]) * sampleRate));
            const float drive = 1.0f + v[P::FiltDrive] * 6.0f;
            if (cutoff.value < 0.0f) cutoff.reset (v[P::FiltCutoff]);
            int h = 0;
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                while (h < numHits && hits[h] <= i) { env = 1.0f; ++h; }
                const float fc = std::clamp (cutoff.process (v[P::FiltCutoff]) * std::exp2 (amount * env), 20.0f, sampleRate * 0.45f);
                env *= decay;
                const float m = mix.process (v[P::FiltMix]);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float x = io[ch][i];
                    float y;
                    if (type == 0)
                        y = processLadder (ch, x * drive, fc, reso) / std::sqrt (drive);
                    else
                    {
                        const int svfType = type == 1 ? dsp::Svf::Low : type == 2 ? dsp::Svf::Band : type == 3 ? dsp::Svf::High : dsp::Svf::Notch;
                        y = svf[(size_t) ch].process (softClip (x * drive) / std::sqrt (drive), fc, reso, sampleRate, svfType);
                    }
                    io[ch][i] = x * (1.0f - m) + y * m;
                }
            }
        }

        float currentEnv() const { return env; }

    private:
        float processLadder (int ch, float x, float fc, float reso)
        {
            auto& s = ladder[(size_t) ch];
            const float g = 1.0f - std::exp (-kTwoPi * fc / sampleRate);
            const float k = reso * 3.9f;
            float in = std::tanh (x - k * (s[3] - 0.5f * x * 0.0f));
            for (int i = 0; i < 4; ++i)
            {
                s[(size_t) i] += g * (std::tanh (in) - std::tanh (s[(size_t) i]));
                in = s[(size_t) i];
            }
            return s[3] * (1.0f + k * 0.35f);
        }

        float sampleRate = 44100.0f, env = 0.0f;
        std::array<std::array<float, 4>, 2> ladder {};
        std::array<dsp::Svf, 2> svf;
        Smoother mix, cutoff;
    };

    // ---------------------------------------------------------------------------------------------
    // Reverb: 8-line FDN (Hadamard) from Fjord, with room / plate / hall voicings, input low cut and ducking

    class Reverb
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            const float scale = sampleRate / 44100.0f;
            for (size_t i = 0; i < 8; ++i)
                lines[i].prepare ((int) (baseLengths[i] * 3.6f * scale) + 64);
            for (size_t c = 0; c < 2; ++c)
                for (size_t k = 0; k < 4; ++k)
                    diffusers[c][k].prepare ((int) (diffLengths[c][k] * 1.4f * scale) + 4);
            predelay.prepare ((int) (0.3 * sr));
            mix.setTime (0.03f, sampleRate);
            size.setTime (0.25f, sampleRate);
            size.reset (-1.0f);
            reset();
        }

        void reset()
        {
            for (auto& l : lines) l.clear();
            for (auto& c : diffusers) for (auto& d : c) d.clear();
            predelay.clear();
            damp.fill (0.0f);
            hp = { 0.0f, 0.0f };
            duckEnv = 0.0f;
        }

        void process (StereoBuffer b, const Values& v)
        {
            const float scale = sampleRate / 44100.0f;
            const int mode = v.i (P::VerbMode);
            const float modeLen = mode == 0 ? 0.55f : mode == 1 ? 0.85f : 1.35f;
            const float modeDamp = mode == 1 ? 0.5f : 1.0f;            // plates are brighter
            const float diffusion = mode == 1 ? 0.72f : 0.62f;
            if (size.value < 0.0f) size.reset (v[P::VerbSize]);
            const float dampC = onePoleCoef (16000.0f * std::pow (0.08f, v[P::VerbDamp] * modeDamp), sampleRate);
            const float preSamples = std::max (1.0f, v[P::VerbPre] * sampleRate);
            const float t60 = std::max (0.1f, v[P::VerbDecay]);
            const float hpC = onePoleCoef (v[P::VerbLowCut], sampleRate);
            const float duckAmt = v[P::VerbDuck], width = v[P::VerbWidth];
            const float duckAtt = 1.0f - std::exp (-1.0f / (0.002f * sampleRate));
            const float duckRel = 1.0f - std::exp (-1.0f / (0.15f * sampleRate));

            std::array<float, 8> decayGain {};
            for (size_t k = 0; k < 8; ++k)
                decayGain[k] = std::pow (10.0f, -3.0f * baseLengths[k] * (0.3f + 2.0f * size.value) * modeLen * scale / (t60 * sampleRate));

            for (int i = 0; i < b.n; ++i)
            {
                const float sz = size.process (v[P::VerbSize]);
                const float lenScale = (0.3f + 2.0f * sz) * modeLen * scale;

                float in = (b.l[i] + b.r[i]) * 0.5f;
                hp[0] += (in - hp[0]) * hpC;
                in -= hp[0];                                            // low cut: keeps the sub out of the room
                predelay.push (in);
                const float pre = predelay.read (preSamples);
                float x[2] = { pre, pre };
                for (size_t c = 0; c < 2; ++c)
                    for (size_t k = 0; k < 4; ++k)
                        x[c] = diffusers[c][k].process (x[c], diffusion);

                std::array<float, 8> y {};
                for (size_t k = 0; k < 8; ++k)
                {
                    const float len = baseLengths[k] * lenScale;
                    const float mod = 5.0f * scale * std::sin (modPhase[k]);
                    modPhase[k] += modRates[k] * kTwoPi / sampleRate;
                    if (modPhase[k] > kTwoPi) modPhase[k] -= kTwoPi;
                    const float out = lines[k].read (std::max (4.0f, len + mod));
                    damp[k] += (out - damp[k]) * dampC;
                    y[k] = damp[k] * decayGain[k];
                }

                std::array<float, 8> h = y;
                for (int len = 1; len < 8; len <<= 1)
                    for (int s = 0; s < 8; s += len * 2)
                        for (int j = s; j < s + len; ++j)
                        {
                            const float a = h[(size_t) j], c = h[(size_t) (j + len)];
                            h[(size_t) j] = a + c;
                            h[(size_t) (j + len)] = a - c;
                        }

                constexpr float norm = 0.35355339f;
                for (size_t k = 0; k < 8; ++k)
                    lines[k].push (h[k] * norm + x[k % 2] * 0.5f);

                const float outL = (y[0] + y[2] + y[4] + y[6] - y[1] * 0.5f) * 0.45f;
                const float outR = (y[1] + y[3] + y[5] + y[7] - y[0] * 0.5f) * 0.45f;
                const float mid = 0.5f * (outL + outR), side = 0.5f * (outL - outR) * width;

                const float level = std::max (std::abs (b.l[i]), std::abs (b.r[i]));
                duckEnv += (level - duckEnv) * (level > duckEnv ? duckAtt : duckRel);
                const float duck = 1.0f - duckAmt * std::min (1.0f, duckEnv * 3.0f);

                float dg, wg;
                equalPowerMix (mix.process (v[P::VerbMix]), dg, wg);
                dg = std::max (dg, 0.7f);    // the kick itself stays present, the room is added
                b.l[i] = b.l[i] * dg + (mid + side) * wg * duck;
                b.r[i] = b.r[i] * dg + (mid - side) * wg * duck;
                tailLevel = tailLevel * 0.9995f + std::abs (mid) * 0.0005f;
            }
        }

        float tailLevel = 0.0f;   // for the visual

    private:
        struct Allpass
        {
            DelayLine line;
            int length = 1;
            void prepare (int len) { line.prepare (len + 4); length = len; }
            void clear() { line.clear(); }
            float process (float x, float g)
            {
                const float d = line.read ((float) length);
                const float v = x + g * d;
                line.push (v);
                return d - g * v;
            }
        };

        static constexpr float baseLengths[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static constexpr float diffLengths[2][4] = { { 142, 107, 379, 277 }, { 151, 113, 367, 283 } };
        static constexpr float modRates[8] = { 0.11f, 0.17f, 0.23f, 0.29f, 0.31f, 0.37f, 0.41f, 0.47f };

        float sampleRate = 44100.0f, duckEnv = 0.0f;
        std::array<DelayLine, 8> lines;
        std::array<std::array<Allpass, 4>, 2> diffusers;
        DelayLine predelay;
        std::array<float, 8> damp {}, modPhase { 0.0f, 0.8f, 1.6f, 2.4f, 3.2f, 4.0f, 4.8f, 5.6f };
        std::array<float, 2> hp {};
        Smoother mix, size;
    };

    // ---------------------------------------------------------------------------------------------
    // Delay: tempo synced or free, ping-pong, low cut and damping in the feedback, ducking (from Fjord)

    inline double delayBeats (int index)
    {
        static const double beats[] = { 0.125, 1.0 / 6.0, 0.25, 1.0 / 3.0, 0.375, 0.5, 2.0 / 3.0, 0.75, 1.0, 1.5, 2.0 };
        return beats[std::clamp (index, 0, 10)];
    }

    class Delay
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (4.1 * sr));
            mix.setTime (0.03f, sampleRate);
            time.setTime (0.08f, sampleRate);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            lp.fill (0.0f);
            hp.fill (0.0f);
            duckEnv = 0.0f;
            time.reset (-1.0f);
        }

        void process (StereoBuffer b, const Values& v, double bpm)
        {
            const float seconds = v.b (P::DlySync) ? (float) (delayBeats (v.i (P::DlyBeats)) * 60.0 / std::max (20.0, bpm))
                                                   : v[P::DlyTime] * 0.001f;
            const float target = std::clamp (seconds * sampleRate, 16.0f, (float) lines[0].maxDelay() - 4.0f);
            if (time.value < 0.0f) time.reset (target);

            const float lpc = onePoleCoef (18000.0f * std::pow (0.07f, v[P::DlyDamp]), sampleRate);
            const float hpc = onePoleCoef (v[P::DlyLowCut], sampleRate);
            const float duckAtt = 1.0f - std::exp (-1.0f / (0.005f * sampleRate));
            const float duckRel = 1.0f - std::exp (-1.0f / (0.3f * sampleRate));
            const float fb = v[P::DlyFeedback], duckAmt = v[P::DlyDuck];
            const bool pingPong = v.b (P::DlyPingPong);

            for (int i = 0; i < b.n; ++i)
            {
                const float d = time.process (target);
                const float wl = lines[0].read (d), wr = lines[1].read (d);

                auto colour = [&] (float x, int ch)
                {
                    lp[(size_t) ch] += (x - lp[(size_t) ch]) * lpc;
                    hp[(size_t) ch] += (lp[(size_t) ch] - hp[(size_t) ch]) * hpc;
                    return softClip (lp[(size_t) ch] - hp[(size_t) ch]);
                };
                const float cl = colour (wl, 0), cr = colour (wr, 1);

                const float inL = b.l[i], inR = b.r[i];
                if (pingPong)
                {
                    lines[0].push ((inL + inR) * 0.5f + cr * fb);
                    lines[1].push (cl * fb);
                }
                else
                {
                    lines[0].push (inL + cl * fb);
                    lines[1].push (inR + cr * fb);
                }

                const float level = std::max (std::abs (inL), std::abs (inR));
                duckEnv += (level - duckEnv) * (level > duckEnv ? duckAtt : duckRel);
                const float duck = 1.0f - duckAmt * std::min (1.0f, duckEnv * 4.0f);

                float dg, wg;
                equalPowerMix (mix.process (v[P::DlyMix]), dg, wg);
                dg = std::max (dg, 0.7f);
                b.l[i] = inL * dg + cl * wg * duck;
                b.r[i] = inR * dg + cr * wg * duck;
            }
        }

    private:
        float sampleRate = 44100.0f, duckEnv = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<float, 2> lp {}, hp {};
        Smoother mix, time;
    };

    // ---------------------------------------------------------------------------------------------
    // Motion: chorus, phaser or flanger – mostly for tails and noise layers

    class Motion
    {
    public:
        void prepare (double sr)
        {
            sampleRate = (float) sr;
            for (auto& d : lines) d.prepare ((int) (0.04 * sr));
            mix.setTime (0.02f, sampleRate);
            depth.setTime (0.05f, sampleRate);
            reset();
        }

        void reset()
        {
            for (auto& d : lines) d.clear();
            for (auto& s : allpass) s.fill (0.0f);
            last = { 0.0f, 0.0f };
        }

        void process (StereoBuffer b, const Values& v)
        {
            const int mode = v.i (P::ChrMode);
            const float inc = v[P::ChrRate] / sampleRate;
            const float fb = v[P::ChrFeedback];
            float* io[] = { b.l, b.r };

            for (int i = 0; i < b.n; ++i)
            {
                const float dep = depth.process (v[P::ChrDepth]);
                const float m = mix.process (v[P::ChrMix]);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float ph = phase + (ch == 0 ? 0.0f : 0.25f);
                    const float lfo = 0.5f - 0.5f * std::cos (kTwoPi * ph);
                    const float x = io[ch][i];
                    float y;
                    if (mode == 0)          // Chorus
                    {
                        lines[(size_t) ch].push (x + last[(size_t) ch] * fb * 0.4f);
                        y = lines[(size_t) ch].read ((0.007f + dep * 0.008f * lfo) * sampleRate);
                    }
                    else if (mode == 1)     // Phaser: 6 allpass stages
                    {
                        const float fc = std::min (160.0f * std::exp2 (lfo * dep * 5.0f), sampleRate * 0.4f);
                        const float t = std::tan (kPi * fc / sampleRate);
                        const float coef = (t - 1.0f) / (t + 1.0f);
                        y = x + last[(size_t) ch] * fb * 0.85f;
                        for (auto& s : allpass[(size_t) ch])
                        {
                            const float out = coef * y + s;
                            s = y - coef * out;
                            y = out;
                        }
                    }
                    else                    // Flanger
                    {
                        lines[(size_t) ch].push (softClip (x + last[(size_t) ch] * fb * 0.9f));
                        y = lines[(size_t) ch].read ((0.0004f + dep * 0.0055f * lfo) * sampleRate + 1.0f);
                    }
                    last[(size_t) ch] = softClip (y);
                    io[ch][i] = x * (1.0f - 0.5f * m) + y * 0.5f * m;
                }
                phase += inc;
                if (phase >= 1.0f) phase -= 1.0f;
            }
        }

        float getPhase() const { return phase; }

    private:
        float sampleRate = 44100.0f, phase = 0.0f;
        std::array<DelayLine, 2> lines;
        std::array<std::array<float, 6>, 2> allpass {};
        std::array<float, 2> last {};
        Smoother mix, depth;
    };

    // ---------------------------------------------------------------------------------------------
    // Width: mid/side, with everything below the crossover kept mono (the kick's low end stays centred)

    class Width
    {
    public:
        void prepare (double sr) { sampleRate = sr; width.setTime (0.03f, (float) sr); width.reset (1.0f); reset(); lastFreq = -1.0f; }
        void reset() { for (auto& f : hp) f.reset(); }

        void process (StereoBuffer b, const Values& v)
        {
            if (! juce::exactlyEqual (v[P::WidthMono], lastFreq))
            {
                lastFreq = v[P::WidthMono];
                hp[0].highpass (sampleRate, lastFreq, 0.5412);
                hp[1].highpass (sampleRate, lastFreq, 1.3066);
            }
            float corrNum = 0.0f, corrL = 0.0f, corrR = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                const float w = width.process (v[P::WidthAmount]);
                const float mid = 0.5f * (b.l[i] + b.r[i]);
                float side = 0.5f * (b.l[i] - b.r[i]);
                side = hp[1].process (hp[0].process (side, 0), 0) * w;
                b.l[i] = mid + side;
                b.r[i] = mid - side;
                corrNum += b.l[i] * b.r[i]; corrL += b.l[i] * b.l[i]; corrR += b.r[i] * b.r[i];
            }
            juce::ignoreUnused (corrNum, corrL, corrR);
        }

    private:
        double sampleRate = 44100.0;
        float lastFreq = -1.0f;
        std::array<Biquad, 2> hp;
        Smoother width;
    };

    // ---------------------------------------------------------------------------------------------
    // Clipper at the very end: soft, hard or tape, with drive and ceiling (not part of the reorderable chain)

    class Clipper
    {
    public:
        static float shape (int mode, float x)
        {
            switch (mode)
            {
                case 1:  return std::clamp (x, -1.0f, 1.0f);
                case 2:  return std::tanh (x);
                default: return softClip (x * 1.15f) / softClip (1.15f) * (std::abs (x) < 1.0f ? 1.0f : 1.0f);
            }
        }

        void process (StereoBuffer b, const Values& v)
        {
            const int mode = v.i (P::ClipMode);
            const float drive = juce::Decibels::decibelsToGain (v[P::ClipDrive]);
            const float ceil = juce::Decibels::decibelsToGain (v[P::ClipCeiling]);
            float red = 0.0f;
            for (int i = 0; i < b.n; ++i)
            {
                const float xl = b.l[i] * drive / ceil, xr = b.r[i] * drive / ceil;
                b.l[i] = std::clamp (shape (mode, xl), -1.0f, 1.0f) * ceil;
                b.r[i] = std::clamp (shape (mode, xr), -1.0f, 1.0f) * ceil;
                red = std::max (red, std::max (std::abs (xl), std::abs (xr)));
            }
            lastOver = red;
        }

        float lastOver = 0.0f;   // peak before clipping, relative to the ceiling (> 1 = clipping)
    };
}
