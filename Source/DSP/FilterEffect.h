#pragma once

#include "Effect.h"
#include <array>
#include <cmath>

/*
    FilterEffect — resonant filter + tone shaper. Four styles:
      0 Telephone : band-limited 300 Hz–3.4 kHz "phone" band (HP∘LP cascade),
                    clutch narrows the band toward a mid honk.
      1 LP Sweep  : resonant low-pass; k1 = base freq, k2 = resonance,
                    clutch sweeps the cutoff above the base.
      2 Tilt      : tilt EQ around a ~700 Hz pivot; clutch/tone = tilt amount
                    (negative = darker, positive = brighter).
      3 Reso      : sharp resonant band-pass peak; k1 = freq, k2 = Q,
                    clutch sweeps the peak.

    Linear/time-invariant per block → silence in = silence out for free
    (no bias, unlike Drive). True-stereo (independent state per channel).
    Zero latency.
*/
class FilterEffect : public Effect
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        for (auto& f : hp)     f.prepare (spec);
        for (auto& f : lp)     f.prepare (spec);
        for (auto& f : band)   f.prepare (spec);
        for (auto& f : shLo)   f.reset();
        for (auto& f : shHi)   f.reset();

        for (auto& f : hp)   f.setType (juce::dsp::StateVariableTPTFilterType::highpass);
        for (auto& f : lp)   f.setType (juce::dsp::StateVariableTPTFilterType::lowpass);
        for (auto& f : band) f.setType (juce::dsp::StateVariableTPTFilterType::bandpass);

        smFreq.reset (sampleRate, 0.02);
        smReso.reset (sampleRate, 0.02);
        smTilt.reset (sampleRate, 0.02);
        lastTiltDb = -1.0e9f;
        reset();
    }

    void reset() override
    {
        for (auto& f : hp)   f.reset();
        for (auto& f : lp)   f.reset();
        for (auto& f : band) f.reset();
        for (auto& f : shLo) f.reset();
        for (auto& f : shHi) f.reset();
    }

    int  numStyles() const override { return 4; }
    const char* styleName (int s) const override
    {
        static const char* n[] = { "Telephone", "LP Sweep", "Tilt", "Reso" };
        return n[juce::jlimit (0, 3, s)];
    }

    void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) override
    {
        const int style = juce::jlimit (0, 3, ctx.style);
        const int chans = (int) block.getNumChannels();
        const int n     = (int) block.getNumSamples();

        // k1 → base frequency, log 30 Hz .. 18 kHz
        const float baseHz = 30.0f * std::pow (600.0f, juce::jlimit (0.0f, 1.0f, ctx.k1));
        // clutch sweeps the cutoff up to +3 octaves above the base
        const float sweptHz = juce::jlimit (20.0f, (float) sampleRate * 0.45f,
                                            baseHz * std::pow (2.0f, ctx.clutch * 3.0f));
        const float reso    = 0.5f + juce::jlimit (0.0f, 1.0f, ctx.k2) * 9.5f;   // Q 0.5..10

        switch (style)
        {
            case 0: processTelephone (block, ctx, chans, n); break;
            case 1: processSweep     (block, chans, n, sweptHz, reso); break;
            case 2: processTilt      (block, ctx, chans, n); break;
            default: processReso     (block, chans, n, sweptHz, reso); break;
        }
    }

private:
    // --- style 0: telephone band ---
    void processTelephone (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx, int chans, int n)
    {
        // clutch narrows the band toward ~1 kHz
        const float hpHz = 300.0f + ctx.clutch * 500.0f;    // 300 -> 800
        const float lpHz = 3400.0f - ctx.clutch * 2000.0f;  // 3400 -> 1400
        for (int ch = 0; ch < chans; ++ch)
        {
            auto& H = hp[(size_t) juce::jmin (ch, 1)];
            auto& L = lp[(size_t) juce::jmin (ch, 1)];
            H.setCutoffFrequency (hpHz); H.setResonance (0.9f);
            L.setCutoffFrequency (lpHz); L.setResonance (0.9f);
            auto* d = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
                d[i] = L.processSample (ch, H.processSample (ch, d[i]));
        }
    }

    // --- style 1: resonant low-pass sweep ---
    void processSweep (juce::dsp::AudioBlock<float>& block, int chans, int n, float hz, float q)
    {
        smFreq.setTargetValue (hz); smReso.setTargetValue (q);
        for (int i = 0; i < n; ++i)
        {
            const float f = smFreq.getNextValue();
            const float r = smReso.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& L = lp[(size_t) juce::jmin (ch, 1)];
                L.setCutoffFrequency (f); L.setResonance (r);
                auto* d = block.getChannelPointer ((size_t) ch);
                d[i] = L.processSample (ch, d[i]);
            }
        }
    }

    // --- style 2: tilt EQ around ~700 Hz ---
    void processTilt (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx, int chans, int n)
    {
        // k2 sets the tilt amount (0=flat mid, <0.5 darker, >0.5 brighter); clutch adds slope
        const float tiltDb = juce::jlimit (-12.0f, 12.0f,
                                           ((ctx.k2 * 2.0f - 1.0f) + (ctx.clutch - 0.5f)) * 9.0f);
        if (std::abs (tiltDb - lastTiltDb) > 0.05f)
        {
            lastTiltDb = tiltDb;
            auto lo = juce::dsp::IIR::Coefficients<float>::makeLowShelf  ((float) sampleRate, 250.0f, 0.707f,
                          juce::Decibels::decibelsToGain (-tiltDb));
            auto hi = juce::dsp::IIR::Coefficients<float>::makeHighShelf ((float) sampleRate, 2500.0f, 0.707f,
                          juce::Decibels::decibelsToGain ( tiltDb));
            for (auto& f : shLo) f.coefficients = lo;
            for (auto& f : shHi) f.coefficients = hi;
        }
        for (int ch = 0; ch < chans; ++ch)
        {
            auto& lo = shLo[(size_t) juce::jmin (ch, 1)];
            auto& hi = shHi[(size_t) juce::jmin (ch, 1)];
            auto* d = block.getChannelPointer ((size_t) ch);
            for (int i = 0; i < n; ++i)
                d[i] = hi.processSample (lo.processSample (d[i]));
        }
    }

    // --- style 3: sharp resonant band-pass peak ---
    void processReso (juce::dsp::AudioBlock<float>& block, int chans, int n, float hz, float q)
    {
        const float qq = juce::jmax (2.0f, q);
        smFreq.setTargetValue (hz); smReso.setTargetValue (qq);
        for (int i = 0; i < n; ++i)
        {
            const float f = smFreq.getNextValue();
            const float r = smReso.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
            {
                auto& B = band[(size_t) juce::jmin (ch, 1)];
                B.setCutoffFrequency (f); B.setResonance (r);
                auto* d = block.getChannelPointer ((size_t) ch);
                d[i] = B.processSample (ch, d[i]);
            }
        }
    }

    double sampleRate = 44100.0;
    std::array<juce::dsp::StateVariableTPTFilter<float>, 2> hp, lp, band;
    std::array<juce::dsp::IIR::Filter<float>, 2> shLo, shHi;
    juce::SmoothedValue<float> smFreq, smReso, smTilt;
    float lastTiltDb = -1.0e9f;
};
