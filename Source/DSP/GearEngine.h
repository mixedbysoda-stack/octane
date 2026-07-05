#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include <array>
#include <cmath>

/*
    GearEngine — FIZZFUEL's morphing 5-circuit saturation core.

    Architecture: ONE parameterized nonlinear circuit whose coefficients are
    interpolated between per-gear tables (true morph, not parallel blend).
    The clutch parameter slides the operating point between the current gear
    and the next one, so "gear 2.4" is a real tube→console hybrid circuit.

    Signal path (per block):
      input trim (user drive)
        → 4x oversample
        → waveshaper  y = tanh(k·(x·g + bias)),  k/g/bias lerped from tables
          (+ optional wavefold term for the fuzz end of the range)
        → downsample
        → DC blocker (removes bias offset)
        → post low-pass  (gear character)
        → low shelf 120 Hz (gear character)
        → presence shelf 3 kHz (gear character)
        → tone tilt (user, ±4.5 dB complementary shelves)
        → makeup gain (gear-lerped static compensation)

    Gear R (clean) still runs the oversampler with the shaper set to identity,
    so plugin latency is constant across all gears — no PDC glitches when
    slamming into R for an A/B.
*/
class GearEngine
{
public:
    GearEngine() = default;

    static constexpr int kNumGears = 5;

    struct GearParams
    {
        float driveMul;   // gain into the shaper
        float bias;       // asymmetry -> even harmonics
        float hardness;   // 0 = soft tanh knee, 1 = near-hard clip
        float fold;       // wavefold blend (fuzz chaos)
        float postLPHz;   // character low-pass
        float lowShelfDb; // 120 Hz shelf
        float presenceDb; // 3 kHz shelf
        float makeupDb;   // static gain compensation
    };

    // Circuit tables: 1 TAPE / 2 TUBE / 3 CONSOLE / 4 CLIP / 5 FUZZ
    static constexpr std::array<GearParams, kNumGears> kGears { {
        { 1.2f, 0.04f, 0.06f, 0.00f, 12500.0f,  1.5f, -1.0f,  -1.0f },  // TAPE
        { 1.7f, 0.18f, 0.18f, 0.00f, 16000.0f,  0.5f, -0.5f,  -3.0f },  // TUBE
        { 2.3f, 0.08f, 0.42f, 0.00f, 18500.0f,  1.0f,  1.5f,  -5.0f },  // CONSOLE
        { 3.4f, 0.03f, 0.85f, 0.06f, 20000.0f,  0.0f,  2.0f,  -7.5f },  // CLIP
        { 5.5f, 0.30f, 0.95f, 0.30f,  8500.0f,  2.0f,  1.0f, -11.0f },  // FUZZ
    } };

    void prepare (const juce::dsp::ProcessSpec& spec)
    {
        sampleRate  = spec.sampleRate;
        numChannels = (int) spec.numChannels;

        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            spec.numChannels, kOversampleOrder,
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true);
        oversampling->initProcessing (spec.maximumBlockSize);

        for (auto& f : dcBlockers)   f.reset();
        for (auto& f : postLP)       f.reset();
        for (auto& f : lowShelf)     f.reset();
        for (auto& f : presence)     f.reset();
        for (auto& f : tiltLow)      f.reset();
        for (auto& f : tiltHigh)     f.reset();

        const double osRate = sampleRate * (1 << kOversampleOrder);
        for (auto& f : dcBlockers)
            f.coefficients = juce::dsp::IIR::Coefficients<float>::makeHighPass (osRate, 10.0f);

        smoothedPos.reset (sampleRate, 0.05);
        smoothedDrive.reset (sampleRate, 0.05);
        smoothedMakeup.reset (sampleRate, 0.05);
        smoothedOutput.reset (sampleRate, 0.05);

        lastPostLPHz = lastLowShelfDb = lastPresenceDb = lastToneTilt = -1.0e9f;
        updatePostFilters (kGears[2].postLPHz, kGears[2].lowShelfDb, kGears[2].presenceDb, 0.0f);
    }

    void reset()
    {
        if (oversampling) oversampling->reset();
        for (auto& f : dcBlockers) f.reset();
        for (auto& f : postLP)     f.reset();
        for (auto& f : lowShelf)   f.reset();
        for (auto& f : presence)   f.reset();
        for (auto& f : tiltLow)    f.reset();
        for (auto& f : tiltHigh)   f.reset();
    }

    int getLatencySamples() const
    {
        return oversampling ? (int) std::round (oversampling->getLatencyInSamples()) : 0;
    }

    /*  morphPos: 0..4 continuous gear position (gearIndex + clutch, clamped)
        clean:    gear R — identity shaper, keep latency
        drive:    user drive 0..10
        tone:     user tilt -1..+1
        outputDb: user output trim                                              */
    void process (juce::AudioBuffer<float>& buffer,
                  float morphPos, bool clean, float drive, float tone, float outputDb,
                  const std::atomic<bool>* licenseGate)
    {
        const int numSamples = buffer.getNumSamples();
        const int chans      = juce::jmin (numChannels, buffer.getNumChannels());

        // Anti-patch spot check #2: unlicensed = identity circuit.
        if (licenseGate != nullptr && ! licenseGate->load (std::memory_order_relaxed))
            clean = true;

        // --- interpolate the circuit from the gear tables -------------------
        const float pos  = juce::jlimit (0.0f, (float) (kNumGears - 1), morphPos);
        const int   iLo  = (int) pos;
        const int   iHi  = juce::jmin (iLo + 1, kNumGears - 1);
        const float frac = pos - (float) iLo;

        const auto lerp = [frac] (float a, float b) { return a + (b - a) * frac; };
        const auto& lo = kGears[(size_t) iLo];
        const auto& hi = kGears[(size_t) iHi];

        GearParams g {
            lerp (lo.driveMul,   hi.driveMul),
            lerp (lo.bias,       hi.bias),
            lerp (lo.hardness,   hi.hardness),
            lerp (lo.fold,       hi.fold),
            lerp (lo.postLPHz,   hi.postLPHz),
            lerp (lo.lowShelfDb, hi.lowShelfDb),
            lerp (lo.presenceDb, hi.presenceDb),
            lerp (lo.makeupDb,   hi.makeupDb),
        };

        // user drive 0..10 -> x0.5 .. x5 into the shaper (log-ish feel)
        const float userDrive = 0.5f + drive * 0.45f;

        smoothedDrive.setTargetValue (clean ? 1.0f : g.driveMul * userDrive);
        smoothedMakeup.setTargetValue (clean ? 1.0f
                                             : juce::Decibels::decibelsToGain (g.makeupDb
                                                   - juce::Decibels::gainToDecibels (userDrive) * 0.5f));
        smoothedOutput.setTargetValue (juce::Decibels::decibelsToGain (outputDb));

        // --- nonlinear stage at 4x ------------------------------------------
        juce::dsp::AudioBlock<float> block (buffer.getArrayOfWritePointers(), (size_t) chans,
                                            (size_t) numSamples);
        auto osBlock = oversampling->processSamplesUp (block);

        const float k        = 1.0f + 9.0f * std::pow (g.hardness, 1.5f);
        const float bias     = clean ? 0.0f : g.bias;
        const float fold     = clean ? 0.0f : g.fold;

        // The bias term intentionally offsets the waveshaper input to make even
        // harmonics — but at idle (silence in) that leaves a constant DC pedestal
        // of tanh(k·bias)/tanh(k) at the output. Relying on the post DC-blocker to
        // remove it only kills ~half and lights the meter at standby. Subtract the
        // exact pedestal here so silence maps to true zero; the DC blocker then only
        // has to mop up the tiny signal-dependent asymmetry DC.
        const float dcPedestal = clean ? 0.0f
            : (1.0f - fold) * (std::tanh (k * bias) / std::tanh (k))
              + fold * std::sin (4.7f * bias);
        const int   osChans  = (int) osBlock.getNumChannels();
        const int   osLen    = (int) osBlock.getNumSamples();

        for (int ch = 0; ch < osChans; ++ch)
        {
            float* data = osBlock.getChannelPointer ((size_t) ch);
            auto&  dcF  = dcBlockers[(size_t) juce::jmin (ch, 1)];

            // per-sample drive smoothing only on channel 0's clock; snapshot for ch>0
            if (ch == 0) driveSnapshot = smoothedDrive.getCurrentValue();

            for (int i = 0; i < osLen; ++i)
            {
                const float dr = (ch == 0 ? smoothedDrive.getNextValue() : driveSnapshot);
                float x = data[i] * dr + bias;

                float y;
                if (clean)
                    y = data[i];                     // identity — R gear / unlicensed
                else
                {
                    y = std::tanh (k * x) / std::tanh (k);
                    if (fold > 0.0001f)
                        y = (1.0f - fold) * y
                          + fold * std::sin (4.7f * juce::jlimit (-1.2f, 1.2f, x));
                    y -= dcPedestal;                 // remove the bias DC pedestal analytically
                    y = dcF.processSample (y);       // safety net for signal-dependent asymmetry DC
                }
                data[i] = y;
            }
            if (ch == 0) driveSnapshot = smoothedDrive.getCurrentValue();
        }

        oversampling->processSamplesDown (block);

        // --- post EQ + makeup at base rate ----------------------------------
        updatePostFilters (clean ? 20000.0f : g.postLPHz,
                           clean ? 0.0f     : g.lowShelfDb,
                           clean ? 0.0f     : g.presenceDb,
                           clean ? 0.0f     : tone);

        for (int ch = 0; ch < chans; ++ch)
        {
            float* data = buffer.getWritePointer (ch);
            auto& lpF = postLP[(size_t) juce::jmin (ch, 1)];
            auto& lsF = lowShelf[(size_t) juce::jmin (ch, 1)];
            auto& prF = presence[(size_t) juce::jmin (ch, 1)];
            auto& tlF = tiltLow[(size_t) juce::jmin (ch, 1)];
            auto& thF = tiltHigh[(size_t) juce::jmin (ch, 1)];

            for (int i = 0; i < numSamples; ++i)
            {
                float y = data[i];
                y = lpF.processSample (y);
                y = lsF.processSample (y);
                y = prF.processSample (y);
                y = tlF.processSample (y);
                y = thF.processSample (y);
                data[i] = y;
            }
        }

        // makeup + output trim (block-smoothed)
        for (int i = 0; i < numSamples; ++i)
        {
            const float gGain = smoothedMakeup.getNextValue() * smoothedOutput.getNextValue();
            for (int ch = 0; ch < chans; ++ch)
                buffer.getWritePointer (ch)[i] *= gGain;
        }
    }

private:
    static constexpr size_t kOversampleOrder = 2;   // 4x

    void updatePostFilters (float lpHz, float lsDb, float prDb, float toneTilt)
    {
        const bool dirty = std::abs (lpHz - lastPostLPHz) > 1.0f
                        || std::abs (lsDb - lastLowShelfDb) > 0.01f
                        || std::abs (prDb - lastPresenceDb) > 0.01f
                        || std::abs (toneTilt - lastToneTilt) > 0.001f;
        if (! dirty) return;

        lastPostLPHz = lpHz; lastLowShelfDb = lsDb;
        lastPresenceDb = prDb; lastToneTilt = toneTilt;

        const float nyq  = (float) sampleRate * 0.45f;
        const float lp   = juce::jmin (lpHz, nyq);
        const float tilt = toneTilt * 4.5f;   // ±4.5 dB

        auto lpC = juce::dsp::IIR::Coefficients<float>::makeLowPass  ((float) sampleRate, lp, 0.707f);
        auto lsC = juce::dsp::IIR::Coefficients<float>::makeLowShelf ((float) sampleRate, 120.0f, 0.707f,
                       juce::Decibels::decibelsToGain (lsDb));
        auto prC = juce::dsp::IIR::Coefficients<float>::makeHighShelf ((float) sampleRate, 3000.0f, 0.707f,
                       juce::Decibels::decibelsToGain (prDb));
        auto tlC = juce::dsp::IIR::Coefficients<float>::makeLowShelf ((float) sampleRate, 250.0f, 0.707f,
                       juce::Decibels::decibelsToGain (-tilt));
        auto thC = juce::dsp::IIR::Coefficients<float>::makeHighShelf ((float) sampleRate, 2500.0f, 0.707f,
                       juce::Decibels::decibelsToGain (tilt));

        for (auto& f : postLP)   f.coefficients = lpC;
        for (auto& f : lowShelf) f.coefficients = lsC;
        for (auto& f : presence) f.coefficients = prC;
        for (auto& f : tiltLow)  f.coefficients = tlC;
        for (auto& f : tiltHigh) f.coefficients = thC;
    }

    double sampleRate  = 44100.0;
    int    numChannels = 2;
    float  driveSnapshot = 1.0f;

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    std::array<juce::dsp::IIR::Filter<float>, 2> dcBlockers, postLP, lowShelf,
                                                 presence, tiltLow, tiltHigh;

    juce::SmoothedValue<float> smoothedPos, smoothedDrive, smoothedMakeup, smoothedOutput;

    float lastPostLPHz = -1.0e9f, lastLowShelfDb = -1.0e9f,
          lastPresenceDb = -1.0e9f, lastToneTilt = -1.0e9f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearEngine)
};
