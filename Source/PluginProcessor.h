#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/GearEngine.h"
#include "Licensing/LicenseManager.h"

class OctaneProcessor : public juce::AudioProcessor
{
public:
    OctaneProcessor();
    ~OctaneProcessor() override = default;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                        { return true; }

    const juce::String getName() const override            { return "FIZZFUEL"; }
    bool acceptsMidi() const override                      { return false; }
    bool producesMidi() const override                     { return false; }
    bool isMidiEffect() const override                     { return false; }
    double getTailLengthSeconds() const override           { return 0.0; }

    int getNumPrograms() override                          { return 1; }
    int getCurrentProgram() override                       { return 0; }
    void setCurrentProgram (int) override                  {}
    const juce::String getProgramName (int) override       { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    // ---- FIZZFUEL ----
    juce::AudioProcessorValueTreeState& getAPVTS()         { return apvts; }
    LicenseManager& getLicenseManager()                    { return licenseManager; }
    float getInputLevel() const                            { return inputLevel.load (std::memory_order_relaxed); }

    // UI scale persisted alongside parameters (not host-automatable)
    float getUIScale() const                               { return uiScale.load (std::memory_order_relaxed); }
    void  setUIScale (float s)                             { uiScale.store (s, std::memory_order_relaxed); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    juce::AudioProcessorValueTreeState apvts;
    GearEngine engine;
    juce::dsp::DryWetMixer<float> dryWet { 512 };
    LicenseManager licenseManager;

    std::atomic<float> inputLevel { 0.0f };
    std::atomic<float> uiScale    { 1.0f };

    // cached parameter pointers (audio-thread safe)
    std::atomic<float>* pGear   = nullptr;
    std::atomic<float>* pClutch = nullptr;
    std::atomic<float>* pDrive  = nullptr;
    std::atomic<float>* pTone   = nullptr;
    std::atomic<float>* pMix    = nullptr;
    std::atomic<float>* pOutput = nullptr;

   #if DEMO_BUILD
    // 60 s play / 10 s mute demo cycle with a short crossfade
    juce::int64 demoSampleCounter = 0;
    double demoSampleRate = 44100.0;
   #endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaneProcessor)
};
