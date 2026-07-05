#pragma once

#include "Effect.h"
#include <array>
#include <vector>
#include <cmath>

/*
    ReverbEffect — 8-line Feedback Delay Network reverb.

    Signal:  input → pre-delay → 2 input-diffusion allpasses → FDN tank
             FDN: 8 delay lines, feedback mixed by an 8×8 Hadamard matrix
             (lossless/energy-preserving → smooth, dense tail), each line
             damped by a one-pole low-pass for natural HF decay. Stereo output
             taps use decorrelated sign patterns for width.

    Styles (Room/Hall/Plate/Canyon) set base size + decay + damping + pre-delay;
    k1 scales size, k2 sets tone (damping), clutch sets decay (RT60), mix by router.

    Linear with a contractive (<1) feedback loop → silence in = silence out,
    and it can never blow up (Hadamard is unitary, loop gain < 1).
*/
class ReverbEffect : public Effect
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) override
    {
        sampleRate = spec.sampleRate;
        const double k = sampleRate / 48000.0;

        for (int i = 0; i < N; ++i)
        {
            baseLen[i] = (int) std::round (kBase[i] * k);
            const int maxLen = (int) (baseLen[i] * 1.6) + 4;
            lineL[i].assign ((size_t) maxLen, 0.0f);
            lineR[i].assign ((size_t) maxLen, 0.0f);
            widx[i] = 0;
            dampL[i] = dampR[i] = 0.0f;
        }
        for (int a = 0; a < NAP; ++a)
        {
            apLenL[a] = (int) std::round (kAp[a] * k);
            apLenR[a] = (int) std::round (kAp[a] * 1.18 * k);   // decorrelate R
            apL[a].assign ((size_t) apLenL[a] + 4, 0.0f);
            apR[a].assign ((size_t) apLenR[a] + 4, 0.0f);
            apiL[a] = apiR[a] = 0;
        }
        preMax = (int) (0.1 * sampleRate) + 4;
        preL.assign ((size_t) preMax, 0.0f);
        preR.assign ((size_t) preMax, 0.0f);
        preIdx = 0;

        smDecay.reset (sampleRate, 0.05);
        smDamp.reset  (sampleRate, 0.05);
        reset();
    }

    void reset() override
    {
        for (int i = 0; i < N; ++i)
        {
            std::fill (lineL[i].begin(), lineL[i].end(), 0.0f);
            std::fill (lineR[i].begin(), lineR[i].end(), 0.0f);
            dampL[i] = dampR[i] = 0.0f; widx[i] = 0;
        }
        for (int a = 0; a < NAP; ++a)
        {
            std::fill (apL[a].begin(), apL[a].end(), 0.0f);
            std::fill (apR[a].begin(), apR[a].end(), 0.0f);
            apiL[a] = apiR[a] = 0;
        }
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        preIdx = 0;
    }

    int numStyles() const override { return 4; }
    const char* styleName (int s) const override
    {
        static const char* n[] = { "Room", "Hall", "Plate", "Canyon" };
        return n[juce::jlimit (0, 3, s)];
    }

    void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) override
    {
        const int style = juce::jlimit (0, 3, ctx.style);
        const int chans = juce::jmin ((int) block.getNumChannels(), 2);
        const int n     = (int) block.getNumSamples();

        // --- per-style base + user controls ---
        const float sizeScale = kStyleSize[style] * (0.7f + juce::jlimit (0.0f, 1.0f, ctx.k1) * 0.6f);
        const float decay     = juce::jlimit (0.20f, 0.985f,
                                    kStyleDecay[style] + (ctx.clutch - 0.5f) * 0.5f);
        // damping one-pole coeff: brighter (k2 high) = less damping
        const float dampHz = 1500.0f * std::pow (12.0f, juce::jlimit (0.0f, 1.0f, ctx.k2) * kStyleBright[style]);
        const float dampA  = std::exp (-2.0f * juce::MathConstants<float>::pi
                                       * juce::jmin (dampHz, (float) sampleRate * 0.45f) / (float) sampleRate);
        const int preSamp = juce::jlimit (0, preMax - 2,
                                          (int) (kStylePre[style] * sampleRate));

        smDecay.setTargetValue (decay);
        smDamp.setTargetValue (dampA);

        // effective (scaled) delay lengths for this block
        int len[N];
        for (int i = 0; i < N; ++i)
            len[i] = juce::jlimit (2, (int) lineL[i].size() - 1,
                                   (int) (baseLen[i] * sizeScale));

        const float inGain  = 0.35f;
        const float outGain = 0.6f;

        for (int s = 0; s < n; ++s)
        {
            const float g = smDecay.getNextValue();
            const float a = smDamp.getNextValue();

            float inL = block.getChannelPointer (0)[s];
            float inR = chans > 1 ? block.getChannelPointer (1)[s] : inL;

            // pre-delay
            const int rp = (preIdx - preSamp + preMax) % preMax;
            float pdL = preL[(size_t) rp], pdR = preR[(size_t) rp];
            preL[(size_t) preIdx] = inL; preR[(size_t) preIdx] = inR;
            preIdx = (preIdx + 1) % preMax;

            // input diffusion allpasses
            float xL = pdL, xR = pdR;
            for (int ap = 0; ap < NAP; ++ap)
            {
                xL = allpass (apL[ap], apiL[ap], apLenL[ap], xL, 0.7f);
                xR = allpass (apR[ap], apiR[ap], apLenR[ap], xR, 0.7f);
            }

            // read the 8 lines
            float dL[N], dR[N];
            for (int i = 0; i < N; ++i)
            {
                const int r = (widx[i] - len[i] + (int) lineL[i].size()) % (int) lineL[i].size();
                dL[i] = lineL[i][(size_t) r];
                dR[i] = lineR[i][(size_t) r];
            }

            // decorrelated stereo output taps
            float outL = 0.0f, outR = 0.0f;
            for (int i = 0; i < N; ++i)
            {
                outL += dL[i] * ((i & 1) ? -1.0f : 1.0f);
                outR += dR[i] * ((i & 2) ? -1.0f : 1.0f);
            }
            block.getChannelPointer (0)[s] = outL * outGain;
            if (chans > 1) block.getChannelPointer (1)[s] = outR * outGain;

            // Hadamard-mix the feedback, damp, inject input, write back
            float hL[N], hR[N];
            hadamard8 (dL, hL);
            hadamard8 (dR, hR);
            for (int i = 0; i < N; ++i)
            {
                dampL[i] = (1.0f - a) * (hL[i] * g) + a * dampL[i];
                dampR[i] = (1.0f - a) * (hR[i] * g) + a * dampR[i];
                lineL[i][(size_t) widx[i]] = xL * inGain + dampL[i];
                lineR[i][(size_t) widx[i]] = xR * inGain + dampR[i];
                widx[i] = (widx[i] + 1) % (int) lineL[i].size();
            }
        }
    }

private:
    static constexpr int N = 8, NAP = 2;

    static inline float allpass (std::vector<float>& buf, int& idx, int len, float x, float g)
    {
        const int r = (idx - len + (int) buf.size()) % (int) buf.size();
        const float d = buf[(size_t) r];
        const float y = -g * x + d;
        buf[(size_t) idx] = x + g * y;
        idx = (idx + 1) % (int) buf.size();
        return y;
    }

    static inline void hadamard8 (const float* in, float* out)
    {
        float a[8];
        for (int i = 0; i < 8; ++i) a[i] = in[i];
        // 3 butterfly stages
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += (s << 1))
                for (int j = 0; j < s; ++j)
                {
                    const float u = a[i + j], v = a[i + j + s];
                    a[i + j] = u + v; a[i + j + s] = u - v;
                }
        const float norm = 0.35355339f;   // 1/sqrt(8)
        for (int i = 0; i < 8; ++i) out[i] = a[i] * norm;
    }

    // base line lengths @48k (prime-ish), input-diffusion allpass lengths
    static constexpr std::array<int, N>   kBase { { 1129, 1367, 1601, 1867, 2053, 2251, 2399, 2617 } };
    static constexpr std::array<int, NAP> kAp   { { 353, 587 } };
    // per-style: size scale, base decay, pre-delay (s), brightness exponent
    static constexpr std::array<float, 4> kStyleSize   { { 0.42f, 0.80f, 0.55f, 1.00f } };
    static constexpr std::array<float, 4> kStyleDecay  { { 0.72f, 0.86f, 0.80f, 0.93f } };
    static constexpr std::array<float, 4> kStylePre    { { 0.005f, 0.02f, 0.0f, 0.04f } };
    static constexpr std::array<float, 4> kStyleBright { { 1.0f, 0.9f, 1.4f, 0.8f } };

    double sampleRate = 48000.0;
    std::array<std::vector<float>, N> lineL, lineR;
    std::array<int, N> baseLen { {} }, widx { {} };
    std::array<float, N> dampL { {} }, dampR { {} };
    std::array<std::vector<float>, NAP> apL, apR;
    std::array<int, NAP> apLenL { {} }, apLenR { {} }, apiL { {} }, apiR { {} };
    std::vector<float> preL, preR; int preMax = 0, preIdx = 0;
    juce::SmoothedValue<float> smDecay, smDamp;
};
