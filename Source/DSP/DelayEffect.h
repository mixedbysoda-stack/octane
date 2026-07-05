#pragma once

#include "Effect.h"
#include <cmath>
#include <array>

/*
    DelayEffect — tempo-synced echo with tone-filtered, soft-limited feedback.
    Styles = note divisions:
       0 Slap  : fixed ~90 ms, single-ish repeat
       1 1/8   : eighth note
       2 Dot 8 : dotted eighth (the "U2" delay)
       3 Long  : quarter note
    k1  = fine time scale (0.5..1.5×)     k2 = tone (dark..bright feedback)
    clutch = feedback amount (0..0.95, soft-limited so it never runs away)

    Output is WET only (the echoes); the router blends dry/wet via ctx.mix.
    Silence in → the line and feedback decay to silence → idle-silent for free.
*/
class DelayEffect : public Effect
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        const int maxSamps = (int) (2.6 * sampleRate) + 4;   // covers slow tempi + Long
        line.setMaximumDelayInSamples (maxSamps);
        line.prepare (spec);
        smDelay.reset (sampleRate, 0.08);   // glide delay time (tape-ish)
        smFb.reset (sampleRate, 0.02);
        for (auto& z : fbState) z = 0.0f;
        reset();
    }

    void reset() override
    {
        line.reset();
        for (auto& z : fbState) z = 0.0f;
    }

    int numStyles() const override { return 4; }
    const char* styleName (int s) const override
    {
        static const char* n[] = { "Slap", "1/8", "Dot 8", "Long" };
        return n[juce::jlimit (0, 3, s)];
    }

    void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) override
    {
        const int chans = juce::jmin ((int) block.getNumChannels(), 2);
        const int n     = (int) block.getNumSamples();

        // --- delay time from style + tempo ---
        const double beat = 60.0 / juce::jlimit (30.0, 300.0, ctx.bpm);
        double secs;
        switch (juce::jlimit (0, 3, ctx.style))
        {
            case 0:  secs = 0.09;            break;   // Slap
            case 1:  secs = beat * 0.5;      break;   // 1/8
            case 2:  secs = beat * 0.75;     break;   // dotted 1/8
            default: secs = beat * 1.0;      break;   // Long
        }
        const float scale = 0.5f + juce::jlimit (0.0f, 1.0f, ctx.k1);   // 0.5..1.5x
        const float delaySamps = juce::jlimit (4.0f, (float) line.getMaximumDelayInSamples() - 4.0f,
                                               (float) (secs * scale * sampleRate));
        smDelay.setTargetValue (delaySamps);

        const float fb = juce::jlimit (0.0f, 0.95f, ctx.clutch * 0.95f);
        smFb.setTargetValue (fb);

        // feedback tone: one-pole LP coefficient from k2 (dark->bright)
        const float cutoff = 400.0f * std::pow (40.0f, juce::jlimit (0.0f, 1.0f, ctx.k2));  // 400..16k
        const float a = std::exp (-2.0f * juce::MathConstants<float>::pi * cutoff / (float) sampleRate);

        for (int i = 0; i < n; ++i)
        {
            const float dt = smDelay.getNextValue();
            const float g  = smFb.getNextValue();
            line.setDelay (dt);
            for (int ch = 0; ch < chans; ++ch)
            {
                float in  = block.getChannelPointer ((size_t) ch)[i];
                float wet = line.popSample (ch);
                // tone-filtered, soft-limited feedback (tanh keeps it from ever blowing up)
                float& z = fbState[(size_t) ch];
                z = (1.0f - a) * wet + a * z;                 // one-pole LP on the repeat
                float fbIn = in + std::tanh (z * g);
                line.pushSample (ch, fbIn);
                block.getChannelPointer ((size_t) ch)[i] = wet;   // WET out (router mixes)
            }
        }
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line { 1 << 18 };
    juce::SmoothedValue<float> smDelay, smFb;
    std::array<float, 2> fbState { { 0.0f, 0.0f } };
};
