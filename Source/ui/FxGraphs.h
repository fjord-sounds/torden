#pragma once
#include "Controls.h"
#include "../PluginProcessor.h"

namespace torden::ui
{
    namespace P = params;
    using fx::FxType;

    inline float freqToX (float hz, juce::Rectangle<float> r) { return r.getX() + r.getWidth() * std::log (hz / 20.0f) / std::log (1000.0f); }
    inline float xToFreq (float x, juce::Rectangle<float> r) { return 20.0f * std::pow (1000.0f, (x - r.getX()) / r.getWidth()); }

    inline void strokeGlow (juce::Graphics& g, const juce::Path& path, juce::Colour c, float width, bool glow)
    {
        if (glow)
        {
            g.setColour (c.withAlpha (0.12f));
            g.strokePath (path, juce::PathStrokeType (width * 4.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
        g.setColour (c);
        g.strokePath (path, juce::PathStrokeType (width, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Live state the graphs animate with (one per tile and one for the big editor).
    struct FxAnim
    {
        struct Particle { float x, y, vx, vy, life; };
        std::vector<Particle> particles;
        std::vector<float> history;      // compressor reduction / transient gain over time
        std::vector<float> echoes;       // ages of delay echo rings
        uint32_t lastHits = 0;
        float time = 0.0f, satLevel = 0.0f;
        juce::Random rnd { 3 };

        void tick (TordenProcessor& proc, float dt, int type)
        {
            time += dt;
            const auto& m = proc.fxMeters;
            const bool hit = proc.getHitCount() != lastHits;
            lastHits = proc.getHitCount();
            const float vel = proc.getLastVelocity();

            if (type == (int) FxType::Reverb)
            {
                if (hit)
                    for (int i = 0; i < 40; ++i)
                    {
                        const float a = rnd.nextFloat() * dsp::kTwoPi, s = 0.15f + rnd.nextFloat() * 0.6f;
                        particles.push_back ({ 0.5f, 0.55f, std::cos (a) * s * vel, std::sin (a) * s * vel * 0.7f, 1.0f });
                    }
                const float decay = proc.apvts.getRawParameterValue ("verb_decay")->load();
                for (auto& p : particles)
                {
                    p.x += p.vx * dt; p.y += p.vy * dt;
                    p.vx *= 0.97f; p.vy *= 0.97f;
                    if (p.x < 0.04f || p.x > 0.96f) p.vx = -p.vx;
                    if (p.y < 0.08f || p.y > 0.92f) p.vy = -p.vy;
                    p.life -= dt / std::max (0.2f, decay);
                }
                particles.erase (std::remove_if (particles.begin(), particles.end(), [] (const Particle& p) { return p.life <= 0.0f; }), particles.end());
                if (particles.size() > 400) particles.erase (particles.begin(), particles.begin() + (long) (particles.size() - 400));
            }
            if (type == (int) FxType::Delay)
            {
                if (hit) echoes.push_back (0.0f);
                for (auto& e : echoes) e += dt;
                echoes.erase (std::remove_if (echoes.begin(), echoes.end(), [] (float e) { return e > 6.0f; }), echoes.end());
            }
            if (type == (int) FxType::Compressor || type == (int) FxType::Transient)
            {
                const float v = type == (int) FxType::Compressor ? m.compReduction.load() : m.transientGain.load();
                history.push_back (v);
                if (history.size() > 160) history.erase (history.begin());
            }
            if (type == (int) FxType::Saturation)
            {
                const float in = m.in[0].load();
                satLevel = std::max (in, satLevel * 0.9f);
            }
        }
    };

    // Draws one effect's picture. 'mini' = the small version on the tiles.
    inline void drawFxGraph (juce::Graphics& g, int type, juce::Rectangle<float> r, const P::Values& v, TordenProcessor& proc,
                             FxAnim& anim, bool mini, double sampleRate = 48000.0)
    {
        const auto& p = Theme::get().palette();
        const bool dark = Theme::get().isDark();
        const bool on = v.b (fx::fxOnParam (type));
        const auto col = on ? p.accent : p.textDim;
        const float lw = mini ? 1.3f : 1.8f;

        auto gridLines = [&] (int nx, int ny)
        {
            if (mini) return;
            g.setColour (p.line.withAlpha (0.5f));
            for (int i = 1; i < nx; ++i) g.fillRect (r.getX() + r.getWidth() * (float) i / (float) nx, r.getY(), 1.0f, r.getHeight());
            for (int i = 1; i < ny; ++i) g.fillRect (r.getX(), r.getY() + r.getHeight() * (float) i / (float) ny, r.getWidth(), 1.0f);
        };

        switch ((FxType) type)
        {
            case FxType::Saturation:
            {
                gridLines (4, 4);
                const int mode = v.i (P::SatMode);
                const float gain = fx::Saturation::driveGain (v[P::SatDrive]);
                const float comp = 1.0f / (1.0f + v[P::SatDrive] * 2.2f);
                juce::Path curve;
                for (int i = 0; i <= 100; ++i)
                {
                    const float x = -1.0f + 2.0f * (float) i / 100.0f;
                    const float y = juce::jlimit (-1.2f, 1.2f, fx::Saturation::shape (mode, x * gain, v[P::SatBias]) * comp * juce::Decibels::decibelsToGain (v[P::SatOut]));
                    const auto pt = juce::Point<float> (r.getX() + (x + 1.0f) * 0.5f * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.42f);
                    if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                }
                g.setColour (p.line);
                g.drawLine (r.getX(), r.getBottom(), r.getRight(), r.getY(), 0.8f);
                strokeGlow (g, curve, col, lw, ! mini);
                // The signal riding the curve
                const float x = juce::jlimit (0.0f, 1.0f, anim.satLevel);
                const float y = fx::Saturation::shape (mode, x * gain, v[P::SatBias]) * comp;
                const auto dot = juce::Point<float> (r.getX() + (x + 1.0f) * 0.5f * r.getWidth(), r.getCentreY() - juce::jlimit (-1.2f, 1.2f, y) * r.getHeight() * 0.42f);
                if (on && x > 0.01f)
                {
                    g.setColour (col.withAlpha (0.3f));
                    g.fillEllipse (juce::Rectangle<float> (mini ? 10.0f : 18.0f, mini ? 10.0f : 18.0f).withCentre (dot));
                    g.setColour (col);
                    g.fillEllipse (juce::Rectangle<float> (mini ? 4.0f : 7.0f, mini ? 4.0f : 7.0f).withCentre (dot));
                }
                break;
            }
            case FxType::Crush:
            {
                gridLines (4, 4);
                const float q = std::pow (2.0f, std::min (v[P::CrushBits], 6.0f) - 1.0f);
                const float rate = v[P::CrushRate];
                juce::Path wave, steps;
                float hold = 0.0f, ph = 1.0f;
                for (int i = 0; i <= 200; ++i)
                {
                    const float t = (float) i / 200.0f;
                    const float s = std::sin (t * dsp::kTwoPi * 2.0f) * std::exp (-t * 1.5f);
                    ph += 1.0f / std::max (1.0f, rate * 0.35f);
                    if (ph >= 1.0f) { ph -= 1.0f; hold = std::round (s * q) / q; }
                    const float x = r.getX() + t * r.getWidth();
                    if (i == 0) { wave.startNewSubPath (x, r.getCentreY() - s * r.getHeight() * 0.42f); steps.startNewSubPath (x, r.getCentreY() - hold * r.getHeight() * 0.42f); }
                    wave.lineTo (x, r.getCentreY() - s * r.getHeight() * 0.42f);
                    steps.lineTo (x, r.getCentreY() - hold * r.getHeight() * 0.42f);
                }
                g.setColour (p.textDim.withAlpha (0.4f));
                g.strokePath (wave, juce::PathStrokeType (1.0f));
                strokeGlow (g, steps, col, lw, ! mini);
                break;
            }
            case FxType::Transient:
            case FxType::Compressor:
            {
                const bool comp = type == (int) FxType::Compressor;
                if (comp && ! mini)
                {
                    // Static curve on the left, reduction history on the right
                    auto curveArea = r.removeFromLeft (r.getHeight()).reduced (4.0f);
                    r.removeFromLeft (16.0f);
                    g.setColour (p.line.withAlpha (0.5f));
                    g.drawRect (curveArea, 1.0f);
                    juce::Path curve;
                    for (int i = 0; i <= 60; ++i)
                    {
                        const float inDb = -60.0f + (float) i;
                        const float outDb = inDb - fx::Compressor::reduction (inDb, v[P::CompThresh], v[P::CompRatio], v[P::CompKnee]);
                        const auto pt = juce::Point<float> (curveArea.getX() + (inDb + 60.0f) / 60.0f * curveArea.getWidth(),
                                                            curveArea.getBottom() - (outDb + 60.0f) / 60.0f * curveArea.getHeight());
                        if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                    }
                    g.setColour (p.line);
                    g.drawLine (curveArea.getX(), curveArea.getBottom(), curveArea.getRight(), curveArea.getY(), 0.8f);
                    strokeGlow (g, curve, col, lw, true);
                    const float tx = curveArea.getX() + (v[P::CompThresh] + 60.0f) / 60.0f * curveArea.getWidth();
                    g.setColour (p.textDim.withAlpha (0.6f));
                    g.fillRect (tx, curveArea.getY(), 1.0f, curveArea.getHeight());
                }
                gridLines (6, 4);
                const float range = comp ? 18.0f : 12.0f;
                juce::Path trace;
                const auto& h = anim.history;
                for (size_t i = 0; i < h.size(); ++i)
                {
                    const float x = r.getX() + r.getWidth() * (float) i / 159.0f + r.getWidth() * (float) (160 - h.size()) / 159.0f;
                    const float y = comp ? r.getY() + juce::jlimit (0.0f, 1.0f, h[i] / range) * r.getHeight()
                                         : r.getCentreY() - juce::jlimit (-1.0f, 1.0f, h[i] / range) * r.getHeight() * 0.5f;
                    if (i == 0) trace.startNewSubPath (x, y); else trace.lineTo (x, y);
                }
                if (! comp)
                {
                    g.setColour (p.line);
                    g.fillRect (r.getX(), r.getCentreY(), r.getWidth(), 1.0f);
                }
                if (! h.empty())
                {
                    if (comp)
                    {
                        juce::Path fill (trace);
                        fill.lineTo (r.getRight(), r.getY());
                        fill.lineTo (r.getX() + r.getWidth() * (float) (160 - h.size()) / 159.0f, r.getY());
                        fill.closeSubPath();
                        g.setColour (col.withAlpha (0.15f));
                        g.fillPath (fill);
                    }
                    strokeGlow (g, trace, col, lw, ! mini);
                }
                if (! mini)
                {
                    g.setColour (p.textDim);
                    g.setFont (font (9.5f, Weight::Medium));
                    g.drawText (comp ? "GAIN REDUCTION" : "GAIN", r.reduced (6.0f), juce::Justification::topLeft);
                }
                break;
            }
            case FxType::Eq:
            {
                fx::EqDesign eq;
                eq.design (sampleRate, v);
                if (! mini)
                {
                    g.setColour (p.line.withAlpha (0.5f));
                    for (float hz : { 50.0f, 100.0f, 200.0f, 500.0f, 1000.0f, 2000.0f, 5000.0f, 10000.0f })
                        g.fillRect (freqToX (hz, r), r.getY(), 1.0f, r.getHeight());
                    for (float db : { -12.0f, 0.0f, 12.0f })
                        g.fillRect (r.getX(), r.getCentreY() - db / 20.0f * r.getHeight() * 0.5f, r.getWidth(), 1.0f);
                }
                juce::Path curve;
                const int n = std::max (40, (int) r.getWidth());
                for (int i = 0; i <= n; ++i)
                {
                    const float x = r.getX() + r.getWidth() * (float) i / (float) n;
                    const float db = (float) eq.magnitudeDb (xToFreq (x, r), sampleRate);
                    const float y = r.getCentreY() - juce::jlimit (-24.0f, 24.0f, db) / 20.0f * r.getHeight() * 0.5f;
                    if (i == 0) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                }
                juce::Path fill (curve);
                fill.lineTo (r.getRight(), r.getCentreY());
                fill.lineTo (r.getX(), r.getCentreY());
                fill.closeSubPath();
                g.setColour (col.withAlpha (0.12f));
                g.fillPath (fill);
                strokeGlow (g, curve, col, lw, ! mini);
                break;
            }
            case FxType::Filter:
            {
                gridLines (6, 4);
                const int t = v.i (P::FiltType);
                const float envNow = proc.fxMeters.filterEnv.load();
                auto response = [&] (float cutoff, juce::Colour c, float width)
                {
                    fx::Biquad b1, b2;
                    const double q = 0.6 + v[P::FiltReso] * 6.0;
                    const double fc = juce::jlimit (20.0, sampleRate * 0.45, (double) cutoff);
                    switch (t)
                    {
                        case 0: b1.lowpass (sampleRate, fc, 0.54); b2.lowpass (sampleRate, fc, 0.6 + v[P::FiltReso] * 5.0); break;
                        case 1: b1.lowpass (sampleRate, fc, q); break;
                        case 2: b1.bandpass (sampleRate, fc, q); break;
                        case 3: b1.highpass (sampleRate, fc, q); break;
                        default: b1.notch (sampleRate, fc, q * 0.5); break;
                    }
                    juce::Path path;
                    const int n = std::max (40, (int) r.getWidth());
                    for (int i = 0; i <= n; ++i)
                    {
                        const float x = r.getX() + r.getWidth() * (float) i / (float) n;
                        const double f = xToFreq (x, r);
                        double mag = b1.magnitude (f, sampleRate);
                        if (t == 0) mag *= b2.magnitude (f, sampleRate);
                        const float db = (float) (20.0 * std::log10 (std::max (1.0e-6, mag)));
                        const float y = r.getY() + r.getHeight() * 0.3f - juce::jlimit (-60.0f, 24.0f, db) / 60.0f * r.getHeight() * 0.7f;
                        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, std::min (y, r.getBottom()));
                    }
                    strokeGlow (g, path, c, width, ! mini);
                };
                const float base = v[P::FiltCutoff];
                if (std::abs (v[P::FiltEnv]) > 0.01f)
                {
                    // Where the envelope starts the sweep (faint) and where it is right now
                    g.setOpacity (1.0f);
                    response (base * std::exp2 (v[P::FiltEnv]), col.withAlpha (0.25f), 1.0f);
                    if (on && envNow > 0.01f) response (base * std::exp2 (v[P::FiltEnv] * envNow), col.withAlpha (0.6f), 1.2f);
                }
                response (base, col, lw);
                break;
            }
            case FxType::Reverb:
            {
                // A room seen from above: size draws the walls, particles bounce and fade with the decay
                const float size = 0.35f + 0.65f * v[P::VerbSize];
                auto room = r.withSizeKeepingCentre (r.getWidth() * size, r.getHeight() * (0.4f + 0.6f * size));
                g.setColour (p.line);
                g.drawRoundedRectangle (room, mini ? 3.0f : 6.0f, 1.0f);
                for (const auto& pt : anim.particles)
                {
                    const auto c = stormAt (dark, 1.0f - pt.life).withAlpha (pt.life * (on ? 0.9f : 0.3f));
                    g.setColour (c);
                    const float d = mini ? 2.0f : 3.0f + 2.0f * pt.life;
                    g.fillEllipse (room.getX() + pt.x * room.getWidth() - d * 0.5f, room.getY() + pt.y * room.getHeight() - d * 0.5f, d, d);
                }
                if (anim.particles.empty())
                {
                    g.setColour (col.withAlpha (0.5f));
                    g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre (room.getCentre()));
                }
                break;
            }
            case FxType::Delay:
            {
                // Taps as bars falling with the feedback, and rings rippling out after each hit
                const float seconds = v.b (P::DlySync) ? (float) (fx::delayBeats (v.i (P::DlyBeats)) * 60.0 / std::max (20.0, proc.getBpm()))
                                                       : v[P::DlyTime] * 0.001f;
                const float window = std::max (seconds * 6.0f, 0.5f);
                float amp = v[P::DlyMix] * 1.4f;
                g.setColour (p.line);
                g.fillRect (r.getX(), r.getBottom() - 1.0f, r.getWidth(), 1.0f);
                for (int k = 1; k <= 8 && seconds * (float) k < window; ++k)
                {
                    const float x = r.getX() + r.getWidth() * (seconds * (float) k / window);
                    const float h = juce::jlimit (0.03f, 1.0f, amp) * r.getHeight() * 0.85f;
                    g.setColour (col.withAlpha (0.25f + 0.6f * juce::jlimit (0.0f, 1.0f, amp)));
                    g.fillRoundedRectangle (x - 1.5f, r.getBottom() - h, 3.0f, h, 1.5f);
                    amp *= v[P::DlyFeedback];
                }
                for (float age : anim.echoes)
                    for (int k = 1; k <= 8; ++k)
                    {
                        const float rel = age - seconds * (float) k;
                        if (rel < 0.0f || rel > 0.6f) continue;
                        const float x = r.getX() + r.getWidth() * (seconds * (float) k / window);
                        const float rad = rel * (mini ? 30.0f : 80.0f);
                        const float a = (1.0f - rel / 0.6f) * std::pow (v[P::DlyFeedback], (float) (k - 1)) * (on ? 0.9f : 0.3f);
                        g.setColour (col.withAlpha (a));
                        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre ({ x, r.getBottom() - 4.0f }), 1.0f);
                    }
                break;
            }
            case FxType::Motion:
            {
                gridLines (4, 2);
                const int mode = v.i (P::ChrMode);
                const float depth = v[P::ChrDepth];
                const float phase = proc.fxMeters.motionPhase.load();
                juce::Path wave;
                for (int i = 0; i <= 120; ++i)
                {
                    const float x = (float) i / 120.0f;
                    const float lfo = 0.5f - 0.5f * std::cos (dsp::kTwoPi * (x * 2.0f + phase));
                    const float y = mode == 1 ? std::sin (x * 40.0f * (0.6f + lfo * depth)) * 0.6f
                                  : std::sin (x * dsp::kTwoPi * 6.0f + lfo * depth * 4.0f) * (0.4f + 0.3f * lfo);
                    const auto pt = juce::Point<float> (r.getX() + x * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.4f);
                    if (i == 0) wave.startNewSubPath (pt); else wave.lineTo (pt);
                }
                strokeGlow (g, wave, col, lw, ! mini);
                break;
            }
            case FxType::Count:   // the clipper
            {
                gridLines (4, 4);
                const int mode = v.i (P::ClipMode);
                const float drive = juce::Decibels::decibelsToGain (v[P::ClipDrive]);
                const float ceil = juce::Decibels::decibelsToGain (v[P::ClipCeiling]);
                juce::Path curve;
                for (int i = 0; i <= 100; ++i)
                {
                    const float x = -1.5f + 3.0f * (float) i / 100.0f;
                    const float y = juce::jlimit (-1.0f, 1.0f, fx::Clipper::shape (mode, x * drive / ceil)) * ceil;
                    const auto pt = juce::Point<float> (r.getX() + (x + 1.5f) / 3.0f * r.getWidth(), r.getCentreY() - y * r.getHeight() * 0.45f);
                    if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                }
                g.setColour (p.line);
                for (float c : { ceil, -ceil })
                    g.fillRect (r.getX(), r.getCentreY() - c * r.getHeight() * 0.45f, r.getWidth(), 1.0f);
                strokeGlow (g, curve, col, lw, ! mini);
                const float over = proc.fxMeters.clipOver.load();
                if (on && over > 1.0f && ! mini)
                {
                    g.setColour (juce::Colour (0xffe0703a));
                    g.setFont (font (10.0f, Weight::SemiBold));
                    g.drawText ("CLIPPING " + juce::String (juce::Decibels::gainToDecibels (over), 1) + " dB", r.reduced (6.0f), juce::Justification::topRight);
                }
                break;
            }
            case FxType::Width:
            default:
            {
                // Stereo field: half circle, the width opens the fan; below the mono frequency everything sits in the centre
                const auto c = juce::Point<float> (r.getCentreX(), r.getBottom() - 2.0f);
                const float rad = std::min (r.getWidth() * 0.5f, r.getHeight()) - 4.0f;
                juce::Path arc;
                arc.addCentredArc (c.x, c.y, rad, rad, 0.0f, -dsp::kPi * 0.5f, dsp::kPi * 0.5f, true);
                g.setColour (p.line);
                g.strokePath (arc, juce::PathStrokeType (1.0f));
                const float spread = juce::jlimit (0.0f, 1.0f, v[P::WidthAmount] * 0.5f) * dsp::kPi * 0.5f;
                juce::Path fan;
                fan.startNewSubPath (c);
                fan.addCentredArc (c.x, c.y, rad, rad, 0.0f, -spread, spread, false);
                fan.closeSubPath();
                g.setColour (col.withAlpha (0.18f));
                g.fillPath (fan);
                g.setColour (col);
                g.strokePath (fan, juce::PathStrokeType (lw));
                const float mono = rad * (0.15f + 0.6f * std::log (v[P::WidthMono] / 20.0f) / std::log (25.0f));
                g.setColour (p.accent.withAlpha (0.5f));
                g.fillEllipse (juce::Rectangle<float> (mono * 2.0f, mono * 2.0f).withCentre (c));
                break;
            }
        }
    }
}
