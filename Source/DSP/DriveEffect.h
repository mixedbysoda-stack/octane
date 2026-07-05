#pragma once

#include "Effect.h"
#include "GearEngine.h"

/*
    DriveEffect — the saturation engine as a gear. Wraps the DC-fixed
    GearEngine. In V2 the 5 flavors become this gear's *styles* (the gear no
    longer picks the flavor — the style selector does). Clutch = drive amount.

    NB: license/demo gating is done globally by the processor before routing,
    so this engine never runs the "clean" branch and takes no license gate.
    Follow-up (v2.0 tuning): raise GearEngine oversampling 4x→8x + soften the
    per-style curves to kill the residual fizz.
*/
class DriveEffect : public Effect
{
public:
    void prepare (const juce::dsp::ProcessSpec& spec) override { eng.prepare (spec); }
    void reset() override                                      { eng.reset(); }
    int  latencySamples() const override                      { return eng.getLatencySamples(); }
    int  numStyles() const override                           { return GearEngine::kNumGears; }  // 5

    const char* styleName (int s) const override
    {
        static const char* n[] = { "Tape", "Tube", "Console", "Clip", "Fuzz" };
        return n[juce::jlimit (0, GearEngine::kNumGears - 1, s)];
    }

    void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) override
    {
        const int chans = (int) block.getNumChannels();
        float* ptrs[8];
        for (int ch = 0; ch < chans && ch < 8; ++ch)
            ptrs[ch] = block.getChannelPointer ((size_t) ch);

        juce::AudioBuffer<float> buf (ptrs, juce::jmin (chans, 8), (int) block.getNumSamples());

        const float morphPos = (float) juce::jlimit (0, GearEngine::kNumGears - 1, ctx.style);
        const float drive    = juce::jlimit (0.0f, 1.0f, ctx.clutch) * 10.0f;   // clutch = drive amount

        eng.process (buf, morphPos, /*clean*/ false, drive, ctx.tone, /*outputDb*/ 0.0f, nullptr);
    }

private:
    GearEngine eng;
};
