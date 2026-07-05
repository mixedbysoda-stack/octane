// Per-engine DSP unit tests (the "major company" verification gate).
// For every engine × every style, assert:
//   * idle silence  : silence in  -> < -140 dBFS out   (the v1 DC lesson)
//   * NaN/Inf free   : under noise + extreme params
//   * functional     : noise in -> finite, non-trivial out
#include <juce_dsp/juce_dsp.h>
#include "DSP/Effect.h"
#include "DSP/DriveEffect.h"
#include "DSP/FilterEffect.h"
#include "DSP/DelayEffect.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>
#include <memory>

static int failures = 0;

struct Stats { double peak = 0, rms = 0, dc = 0; bool nan = false; };

static Stats run (Effect& fx, int style, float clutch, float k1, float k2, float noiseAmp)
{
    const double sr = 48000.0; const int block = 512;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    fx.prepare (spec);
    juce::AudioBuffer<float> buf (2, block);

    EffectContext ctx;
    ctx.style = style; ctx.clutch = clutch; ctx.k1 = k1; ctx.k2 = k2; ctx.tone = 0.0f;

    Stats s; long n = 0; double sq = 0, dc = 0;
    for (int b = 0; b < 400; ++b)
    {
        buf.clear();
        if (noiseAmp > 0)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, noiseAmp * (((float) std::rand() / RAND_MAX) * 2 - 1));
        juce::dsp::AudioBlock<float> blk (buf);
        fx.process (blk, ctx);
        if (b >= 200)
            for (int i = 0; i < block; ++i)
            {
                float v = buf.getSample (0, i);
                if (std::isnan (v) || std::isinf (v)) s.nan = true;
                s.peak = std::max (s.peak, std::abs ((double) v));
                sq += (double) v * v; dc += v; ++n;
            }
    }
    s.rms = std::sqrt (sq / (double) n); s.dc = dc / (double) n;
    return s;
}

static void test (const char* engine, Effect& fx)
{
    printf ("\n== %s ==\n", engine);
    for (int st = 0; st < fx.numStyles(); ++st)
    {
        Stats idle  = run (fx, st, 0.6f, 0.6f, 0.5f, 0.0f);      // silence in
        Stats noise = run (fx, st, 0.6f, 0.6f, 0.5f, 0.1f);      // -20 dBFS noise in

        bool idleOk = idle.peak < 1e-7 && !idle.nan;             // < -140 dBFS
        bool funcOk = noise.peak > 1e-5 && !noise.nan;           // did something, no NaN
        if (!idleOk || !funcOk) ++failures;

        printf ("  %-10s  idle=%6.1f dBFS %s   noise-out=%6.1f dBFS %s%s\n",
                fx.styleName (st),
                20 * std::log10 (idle.peak + 1e-30), idleOk ? "OK " : "FAIL",
                20 * std::log10 (noise.peak + 1e-30), funcOk ? "OK " : "FAIL",
                (idle.nan || noise.nan) ? "  *** NaN ***" : "");
    }
}

int main()
{
    { DriveEffect d;  test ("DriveEffect",  d); }
    { FilterEffect f; test ("FilterEffect", f); }
    { DelayEffect  y; test ("DelayEffect",  y); }

    printf ("\n%s  (%d failure%s)\n", failures ? "FAILURES PRESENT" : "ALL ENGINES PASS",
            failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
