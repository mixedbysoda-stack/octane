// Idle-noise measurement harness — feeds the GearEngine pure silence (and a
// -100 dBFS noise floor) and reports output peak / RMS / DC. Reproduces the
// "hiss at standby" report with numbers instead of guesses.
#include <juce_dsp/juce_dsp.h>
#include "DSP/GearEngine.h"
#include <cstdio>
#include <cmath>
#include <cstdlib>

static void measure (const char* label, float gearPos, bool clean, float drive, float noiseAmp)
{
    const double sr = 48000.0;
    const int block = 512;
    GearEngine eng;
    juce::dsp::ProcessSpec spec { sr, (juce::uint32) block, 2 };
    eng.prepare (spec);
    juce::AudioBuffer<float> buf (2, block);

    double peak = 0.0, sumsq = 0.0, dc = 0.0;
    long n = 0;
    bool sawNaN = false;

    for (int b = 0; b < 400; ++b)   // ~4.3 s; measure only the settled second half
    {
        buf.clear();
        if (noiseAmp > 0.0f)
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < block; ++i)
                    buf.setSample (ch, i, noiseAmp * (((float) std::rand() / RAND_MAX) * 2.0f - 1.0f));

        eng.process (buf, gearPos, clean, drive, 0.0f, 0.0f, nullptr);

        if (b >= 200)
            for (int i = 0; i < block; ++i)
            {
                float v = buf.getSample (0, i);
                if (std::isnan (v) || std::isinf (v)) sawNaN = true;
                double a = std::abs ((double) v);
                peak = std::max (peak, a);
                sumsq += (double) v * v;
                dc += v;
                ++n;
            }
    }

    double rms = std::sqrt (sumsq / (double) n);
    printf ("%-28s peak=%.2e (%6.1f dBFS)  rms=%.2e (%6.1f dBFS)  dc=%+.2e%s\n",
            label, peak, 20.0 * std::log10 (peak + 1e-30), rms, 20.0 * std::log10 (rms + 1e-30),
            dc / (double) n, sawNaN ? "  *** NaN/Inf ***" : "");
}

int main()
{
    printf ("=== SILENCE IN (digital zero) — output should be ~ -inf dBFS ===\n");
    measure ("gear1 tape   drive 3",  0.0f, false, 3.0f,  0.0f);
    measure ("gear3 console drive 3", 2.0f, false, 3.0f,  0.0f);
    measure ("gear5 fuzz   drive 3",  4.0f, false, 3.0f,  0.0f);
    measure ("gear5 fuzz   drive 10", 4.0f, false, 10.0f, 0.0f);
    measure ("R clean       drive 3", 0.0f, true,  3.0f,  0.0f);

    printf ("\n=== -100 dBFS NOISE FLOOR IN (~1e-5) — shows the idle gain ===\n");
    measure ("gear3 console drive 3",  2.0f, false, 3.0f,  1e-5f);
    measure ("gear5 fuzz   drive 10",  4.0f, false, 10.0f, 1e-5f);
    return 0;
}
