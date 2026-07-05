#pragma once

#include <juce_dsp/juce_dsp.h>

/*
    Effect — the common interface every FIZZFUEL engine implements. One engine
    per gear; the processor keeps them all prepared and routes audio to the
    active one, crossfading on gear changes. Each engine is isolated and
    independently testable (see the CI harness).

    Contract:
      - process() runs in place on `block` (already the active channels).
      - It must be denormal-safe and NaN-free under any parameter values.
      - Silence in must yield silence out (< -140 dBFS) — the v1 DC lesson.
      - latencySamples() is constant after prepare(); the processor pads all
        engines to a common max so reported PDC never changes across gears.
*/
struct EffectContext
{
    int   style   = 0;      // 0..numStyles()-1
    float clutch  = 0.0f;   // 0..1  — the effect's intensity macro
    float k1      = 0.5f;   // effect-specific knob 1 (0..1 normalized)
    float k2      = 0.5f;   // effect-specific knob 2
    float k3      = 0.5f;   // effect-specific knob 3
    float tone    = 0.0f;   // -1..+1 global tilt
    float mix     = 1.0f;   // 0..1 dry/wet (applied by the router, not the engine)
    double bpm    = 120.0;  // host tempo for synced engines
    bool  playing = false;  // host transport state
};

class Effect
{
public:
    virtual ~Effect() = default;

    virtual void prepare (const juce::dsp::ProcessSpec& spec) = 0;
    virtual void reset() = 0;

    /** Fixed latency in samples (constant after prepare). */
    virtual int  latencySamples() const { return 0; }

    /** Number of selectable styles this engine exposes. */
    virtual int  numStyles() const { return 1; }

    /** Human-readable style name (for UI / tests). */
    virtual const char* styleName (int) const { return ""; }

    /** Process wet in place. Dry/wet mixing is done by the router. */
    virtual void process (juce::dsp::AudioBlock<float>& block, const EffectContext& ctx) = 0;
};
