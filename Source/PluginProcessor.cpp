#include "PluginProcessor.h"
#include "UI/WebViewEditor.h"

namespace param_ids
{
    static constexpr auto gear   = "gear";     // which effect
    static constexpr auto style  = "style";    // style within the effect
    static constexpr auto clutch = "clutch";   // intensity macro
    static constexpr auto k1     = "k1";       // effect-specific knob 1
    static constexpr auto k2     = "k2";       // effect-specific knob 2
    static constexpr auto mix    = "mix";
    static constexpr auto output = "output";
}

juce::AudioProcessorValueTreeState::ParameterLayout OctaneProcessor::createParameterLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterChoice> (
        ParameterID { param_ids::gear, 2 }, "Gear",
        StringArray { "1 - Drive", "2 - Reverb", "3 - Delay", "4 - Pitch", "5 - Filter", "R - Clean" },
        0));

    layout.add (std::make_unique<AudioParameterInt> (
        ParameterID { param_ids::style, 2 }, "Style", 0, 4, 0));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::clutch, 1 }, "Intensity",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::k1, 2 }, "Param 1",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

    layout.add (std::make_unique<AudioParameterFloat> (
        ParameterID { param_ids::k2, 2 }, "Param 2",
        NormalisableRange<float> (0.0f, 1.0f, 0.001f), 0.5f));

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
    effects[GearDrive]  = std::make_unique<DriveEffect>();
    effects[GearReverb] = nullptr;                       // Phase 2 (FDN)
    effects[GearDelay]  = std::make_unique<DelayEffect>();
    effects[GearPitch]  = nullptr;                       // Phase 2 (Signalsmith)
    effects[GearFilter] = std::make_unique<FilterEffect>();
    effects[GearClean]  = nullptr;                       // dry passthrough

    pGear   = apvts.getRawParameterValue (param_ids::gear);
    pStyle  = apvts.getRawParameterValue (param_ids::style);
    pClutch = apvts.getRawParameterValue (param_ids::clutch);
    pK1     = apvts.getRawParameterValue (param_ids::k1);
    pK2     = apvts.getRawParameterValue (param_ids::k2);
    pMix    = apvts.getRawParameterValue (param_ids::mix);
    pOutput = apvts.getRawParameterValue (param_ids::output);
}

void OctaneProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    juce::dsp::ProcessSpec spec { sampleRate,
                                  (juce::uint32) samplesPerBlock,
                                  (juce::uint32) juce::jmax (1, getTotalNumInputChannels()) };

    int maxLat = 0;
    for (auto& e : effects)
        if (e) { e->prepare (spec); maxLat = juce::jmax (maxLat, e->latencySamples()); }

    reportedLatency = maxLat;
    dryWet.prepare (spec);
    dryWet.setMixingRule (juce::dsp::DryWetMixingRule::linear);
    dryWet.setWetLatency ((float) maxLat);
    setLatencySamples (maxLat);

    xfadeScratch.setSize (juce::jmax (1, getTotalNumInputChannels()), samplesPerBlock, false, false, true);
    xfadeLen  = juce::jmax (1, (int) (0.015 * sampleRate));   // 15 ms equal-power crossfade
    xfadeLeft = 0;
    activeGear = fromGear = (int) pGear->load (std::memory_order_relaxed);

   #if DEMO_BUILD
    demoSampleRate = sampleRate;
   #endif
}

void OctaneProcessor::releaseResources()
{
    for (auto& e : effects) if (e) e->reset();
    dryWet.reset();
}

void OctaneProcessor::runEffect (int gear, juce::dsp::AudioBlock<float>& block, const EffectContext& ctx)
{
    if (gear >= 0 && gear < NumGears && effects[(size_t) gear] != nullptr)
        effects[(size_t) gear]->process (block, ctx);
    // else: dry passthrough (Reverb/Pitch stubs + Clean) — leave the block untouched
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
    const float mix     = pMix->load (std::memory_order_relaxed);
    const float outDb   = pOutput->load (std::memory_order_relaxed);

    // host tempo/transport for synced engines
    double bpm = 120.0; bool playing = false;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = *b;
            playing = pos->getIsPlaying();
        }

    // build the shared effect context (k1 doubles as Drive's tone)
    EffectContext ctx;
    ctx.style   = (int) pStyle->load (std::memory_order_relaxed);
    ctx.clutch  = pClutch->load (std::memory_order_relaxed);
    ctx.k1      = pK1->load (std::memory_order_relaxed);
    ctx.k2      = pK2->load (std::memory_order_relaxed);
    ctx.tone    = ctx.k1 * 2.0f - 1.0f;
    ctx.mix     = mix;
    ctx.bpm     = bpm;
    ctx.playing = playing;

    // start a crossfade when the gear changes (tails of the old effect ring through the fade)
    if (gearIdx != activeGear && xfadeLeft == 0)
    {
        fromGear   = activeGear;
        activeGear = gearIdx;
        xfadeLeft  = xfadeLen;
    }

    juce::dsp::AudioBlock<float> block (buffer);
    dryWet.setWetMixProportion (mix);
    dryWet.pushDrySamples (block);

    const bool fading = xfadeLeft > 0;
    if (fading)
        xfadeScratch.makeCopyOf (buffer, true);   // snapshot input BEFORE the active engine runs

    runEffect (activeGear, block, ctx);           // buffer -> active engine's wet

    if (fading)
    {
        juce::dsp::AudioBlock<float> fromBlk (xfadeScratch);
        runEffect (fromGear, fromBlk, ctx);       // scratch -> outgoing engine's wet
        const int startPos = xfadeLen - xfadeLeft;
        const int chs = juce::jmin (buffer.getNumChannels(), xfadeScratch.getNumChannels());
        for (int i = 0; i < numSamples; ++i)
        {
            const float p    = juce::jlimit (0.0f, 1.0f, (float) (startPos + i) / (float) xfadeLen);
            const float gIn  = std::sin (p * juce::MathConstants<float>::halfPi);
            const float gOut = std::cos (p * juce::MathConstants<float>::halfPi);
            for (int ch = 0; ch < chs; ++ch)
                buffer.getWritePointer (ch)[i] =
                    buffer.getWritePointer (ch)[i] * gIn + xfadeScratch.getSample (ch, i) * gOut;
        }
        xfadeLeft = juce::jmax (0, xfadeLeft - numSamples);
    }

    dryWet.mixWetSamples (block);

    // global output trim
    const float outGain = juce::Decibels::decibelsToGain (outDb);
    if (std::abs (outGain - 1.0f) > 1.0e-6f)
        buffer.applyGain (outGain);

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
