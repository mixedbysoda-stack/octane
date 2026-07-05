#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "../PluginProcessor.h"
#include "../Licensing/ActivationDialog.h"

/*
    FIZZFUEL editor — JUCE 8 WebView hosting the single-file gearbox UI.

    Licensing gate: on paid builds that aren't activated yet, the editor shows
    the native ActivationDialog INSTEAD of the webview (a native WKWebView /
    WebView2 sits above JUCE-drawn children, so overlaying doesn't work — we
    swap components instead). A timer polls the license state and swaps the
    webview in the moment activation succeeds.
*/
class OctaneWebViewEditor : public juce::AudioProcessorEditor,
                            private juce::Timer
{
public:
    explicit OctaneWebViewEditor (OctaneProcessor&);
    ~OctaneWebViewEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void createWebView();
    void applyScale (float scale);
    std::optional<juce::WebBrowserComponent::Resource> provideResource (const juce::String& url);

    OctaneProcessor& processor;

    // Relays must outlive the WebBrowserComponent built from them.
    juce::WebSliderRelay gearRelay   { "gear" };
    juce::WebSliderRelay styleRelay  { "style" };
    juce::WebSliderRelay clutchRelay { "clutch" };
    juce::WebSliderRelay k1Relay     { "k1" };
    juce::WebSliderRelay k2Relay     { "k2" };
    juce::WebSliderRelay mixRelay    { "mix" };
    juce::WebSliderRelay outputRelay { "output" };

    std::unique_ptr<juce::WebBrowserComponent> webView;

    std::unique_ptr<juce::WebSliderParameterAttachment> gearAttach, styleAttach, clutchAttach,
        k1Attach, k2Attach, mixAttach, outputAttach;

    std::unique_ptr<ActivationDialog> activationDialog;

    static constexpr int kBaseWidth  = 420;
    static constexpr int kBaseHeight = 700;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OctaneWebViewEditor)
};
