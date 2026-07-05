#include "PluginProcessor.h"
#include "UI/WebViewEditor.h"

namespace param_ids
{
    static constexpr auto gear   = "gear";
    static constexpr auto clutch = "clutch";
    static constexpr auto drive  = "drive";
    static constexpr auto tone   = "tone";
    static constexpr auto mix    = "mix";
    static constexpr auto output = "output";
}

juce::AudioProcessorValueTreeState::ParameterLayout OctaneProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param_ids::gear, 1 }, "Gear",
        StringArray { "1 - Tape", "2 - Tube", "3 - Console", "4 - Clip", "5 - Fuzz", "R - Clean" },
        2));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::clutch, 1 }, "Clutch",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::drive, 1 }, "Drive",
        NormalisableRange<float> (0.0f, 10.0f, 0.01f), 3.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::tone, 1 }, "Tone",
        NormalisableRange<float> (-1.0f, 1.0f, 0.001f), 0.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::mix, 1 }, "Mix",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 1.0f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::output, 1 }, "Output",
        NormalisableRange<float> (-12.0f, 12.0f, 0.1f), 0.0f));

    return layout;
}

OctaneProcessor::OctaneProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "OCTANE", createParameterLayout())
{
    pGear   = apvts.getRawParameterValue (param_ids::gear);
    pClutch = apvts.getRawParameterValue (param_ids::clutch);
    pDrive  = apvts.getRawParameterValue (param_ids::drive);
    pTone   = apvts.getRawParameterValue (param_ids::tone);
    pMix    = apvts.getRawParameterValue (param_ids::mix);
    pOutput = apvts.getRawParameterValue (param_ids::output);
}

void OctaneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumInputChannels()) };
    engine.prepare (spec);

    dryWet.prepare (spec);
    dryWet.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    dryWet.setWetLatency ((float) engine.getLatencySamples());

    setLatencySamples (engine.getLatencySamples());

   #if DEMO_BUILD
    demoSampleRate = sampleRate;
   #endif
}

void OctaneProcessor::releaseResources()
{
    engine.reset();
    dryWet.reset();
}

bool OctaneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    if (in != out) return false;
    return in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo();
}

void OctaneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (numSamples == 0) return;

    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, numSamples);

    // ---- tach metering (pre-processing input peak) ----
    {
        float peak = 0.0f;
        for (int ch = 0; ch < juce::jmin (2, buffer.getNumChannels()); ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
        const float prev = inputLevel.load (std::memory_order_relaxed);
        inputLevel.store (peak > prev ? peak : prev * 0.85f + peak * 0.15f,
                          std::memory_order_relaxed);
    }

   #if ! DEMO_BUILD
    // Anti-patch check #1: unlicensed paid build = hard dry bypass.
    if (! licenseManager.isActivated())
        return;
   #endif

    const int   gearIdx = (int) pGear->load (std::memory_order_relaxed);
    const bool  cleanR  = (gearIdx == 5);
    const float clutch  = pClutch->load (std::memory_order_relaxed);
    const float drive   = pDrive->load (std::memory_order_relaxed);
    const float tone    = pTone->load (std::memory_order_relaxed);
    const float mix     = pMix->load (std::memory_order_relaxed);
    const float outDb   = pOutput->load (std::memory_order_relaxed);

    // morph position: gear 1..5 -> 0..4, clutch slides toward the next circuit
    const float morphPos = cleanR ? 0.0f
                                  : juce::jmin ((float) gearIdx + clutch,
                                                (float) (GearEngine::kNumGears - 1));

    juce::dsp::AudioBlock<float> block (buffer);
    dryWet.setWetMixProportion (cleanR ? 1.0f : mix);
    dryWet.pushDrySamples (block);

    engine.process (buffer, morphPos, cleanR, drive, tone, cleanR ? 0.0f : outDb,
                    licenseManager.getActivatedFlagPtr());

    dryWet.mixWetSamples (block);

   #if DEMO_BUILD
    // 60 s play / 10 s mute cycle, 2048-sample crossfades
    {
        constexpr double playSecs = 60.0, muteSecs = 10.0;
        constexpr int fadeLen = 2048;
        const auto cycleLen = (juce::int64) ((playSecs + muteSecs) * demoSampleRate);
        const auto playLen  = (juce::int64) (playSecs * demoSampleRate);

        for (int i = 0; i < numSamples; ++i)
        {
            const auto posInCycle = (demoSampleCounter + i) % cycleLen;
            float gain = 1.0f;

            if (posInCycle >= playLen)
            {
                const auto muted = posInCycle - playLen;
                const auto muteLen = cycleLen - playLen;
                if (muted < fadeLen)                       // fade out into mute
                    gain = 1.0f - (float) muted / (float) fadeLen;
                else if (muted >= muteLen - fadeLen)       // fade back in
                    gain = (float) (muted - (muteLen - fadeLen)) / (float) fadeLen;
                else
                    gain = 0.0f;
            }

            if (gain < 1.0f)
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    buffer.getWritePointer (ch)[i] *= gain;
        }
        demoSampleCounter += numSamples;
    }
   #endif
}

juce::AudioProcessorEditor* OctaneProcessor::createEditor()
{
    return new OctaneWebViewEditor (*this);
}

void OctaneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("uiScale", (double) uiScale.load(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void OctaneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
    {
        auto state = juce::ValueTree::fromXml (*xml);
        if (state.isValid())
        {
            uiScale.store ((float) (double) state.getProperty ("uiScale", 1.0));
            apvts.replaceState (state);
        }
    }
}

// This creates new instances of the plugin
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new OctaneProcessor();
}
