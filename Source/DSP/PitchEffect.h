#pragma once

#include "Effect.h"
#include <array>
#include <vector>
#include <cmath>

/*
    PitchEffect — real-time pitch shifter via two crossfading delay taps.

    Two read pointers, half a window out of phase, each fading in on a raised-
    cosine window so that as one tap wraps at the buffer edge the other is at
    full gain → continuous, click-free repitch (the classic "delay-line pitch
    shift", à la simple Eventide/H910-style). Latency = one window, reported so
    the host compensates.

    Styles:  -Oct (÷2)  ·  +5th (×1.498)  ·  Detune (±~15c, thickens with dry)
             ·  Shimmer (+Oct with regeneration → cascading octaves)
    k1 = fine trim (±1 semitone)   k2 = tone (LP on the wet)   clutch = feedback
    (regeneration; drives the shimmer). Output is WET; router blends dry/wet.

    Silence in → silence out (linear, contractive feedback).
*/
class PitchEffect : public Effect
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        win = (int) (0.050 * sampleRate);                 // 50 ms window
        const int bufLen = win * 2 + 8;
        for (auto& b : buf) b.assign ((size_t) bufLen, 0.0f);
        widx = 0; phase = 0.0f;
        for (auto& z : toneZ) z = 0.0f;
        smRatio.reset (sampleRate, 0.03);
        reset();
    }

    void reset() override
    {
        for (auto& b : buf) std::fill (b.begin(), b.end(), 0.0f);
        for (auto& z : toneZ) z = 0.0f;
        widx = 0; phase = 0.0f;
    }

    // The delay-line window is inherent algorithmic delay, not reported PDC:
    // compensating it would force a 50 ms latency on all effects (or a PDC jump
    // on gear switch). Left uncompensated like most harmonizers — a musical
    // characteristic of the pitch voice.
    int latencySamples() const override { return 0; }
    int numStyles() const override { return 4; }
    const char* styleName (int s) const override
    {
        static const char* n[] = { "-Oct", "+5th", "Detune", "Shimmer" };
        return n[juce::jlimit (0, 3, s)];
    }

    void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) override
    {
        const int style = juce::jlimit (0, 3, ctx.style);
        const int chans = juce::jmin ((int) block.getNumChannels(), 2);
        const int n     = (int) block.getNumSamples();
        const int blen  = (int) buf[0].size();

        // base ratio per style + fine trim (k1: ±1 semitone)
        static const float baseSemi[4] = { -12.0f, 7.0195f, 0.15f, 12.0f };
        const float semis = baseSemi[style] + (juce::jlimit (0.0f, 1.0f, ctx.k1) * 2.0f - 1.0f);
        smRatio.setTargetValue (std::pow (2.0f, semis / 12.0f));

        const float fb = (style == 3 ? 0.2f + ctx.clutch * 0.6f : ctx.clutch * 0.4f);   // shimmer regenerates
        const float toneHz = 800.0f * std::pow (16.0f, juce::jlimit (0.0f, 1.0f, ctx.k2));
        const float toneA  = std::exp (-2.0f * juce::MathConstants<float>::pi
                                       * juce::jmin (toneHz, (float) sampleRate * 0.45f) / (float) sampleRate);

        for (int s = 0; s < n; ++s)
        {
            const float ratio = smRatio.getNextValue();
            float wet[2] = { 0.0f, 0.0f };

            // two crossfading taps
            for (int t = 0; t < 2; ++t)
            {
                float p = phase + 0.5f * t;
                p -= std::floor (p);                       // frac
                const float delay = p * (float) win;
                const float gain  = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * p);
                for (int ch = 0; ch < chans; ++ch)
                    wet[ch] += readInterp (buf[(size_t) ch], (float) widx - delay, blen) * gain;
            }

            for (int ch = 0; ch < chans; ++ch)
            {
                // tone LP on the wet
                toneZ[(size_t) ch] = (1.0f - toneA) * wet[ch] + toneA * toneZ[(size_t) ch];
                const float w = toneZ[(size_t) ch];
                const float in = block.getChannelPointer ((size_t) ch)[s];
                // write input + regenerated (pitched) feedback into the buffer
                buf[(size_t) ch][(size_t) widx] = in + std::tanh (w * fb);
                block.getChannelPointer ((size_t) ch)[s] = w;    // WET out
            }

            widx = (widx + 1) % blen;
            phase += (1.0f - ratio) / (float) win;
            phase -= std::floor (phase);
        }
    }

private:
    static inline float readInterp (const std::vector<float>& b, float pos, int len)
    {
        while (pos < 0.0f)      pos += (float) len;
        while (pos >= (float) len) pos -= (float) len;
        const int i0 = (int) pos;
        const int i1 = (i0 + 1) % len;
        const float f = pos - (float) i0;
        return b[(size_t) i0] * (1.0f - f) + b[(size_t) i1] * f;
    }

    double sampleRate = 48000.0;
    int win = 2400, widx = 0;
    float phase = 0.0f;
    std::array<std::vector<float>, 2> buf;
    std::array<float, 2> toneZ { { 0.0f, 0.0f } };
    juce::SmoothedValue<float> smRatio;
};
