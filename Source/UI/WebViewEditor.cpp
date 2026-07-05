#include "WebViewEditor.h"
#include <OctaneWebUIBinary.h>

namespace
{
    juce::RangedAudioParameter& param (OctaneProcessor& p, const juce::String& id)
    {
        auto* prm = p.getAPVTS().getParameter (id);
        jassert (prm != nullptr);
        return *prm;
    }
}

OctaneWebViewEditor::OctaneWebViewEditor (OctaneProcessor& proc)
    : juce::AudioProcessorEditor (proc), processor (proc)
{
    setResizable (false, false);
    applyScale (processor.getUIScale());

   #if DEMO_BUILD
    createWebView();
   #else
    if (processor.getLicenseManager().isActivated())
        createWebView();
    else
    {
        activationDialog = std::make_unique<ActivationDialog> (processor.getLicenseManager());
        addAndMakeVisible (*activationDialog);
        resized();   // setSize() ran before the dialog existed — give it bounds now
    }
   #endif

    startTimerHz (30);
}

OctaneWebViewEditor::~OctaneWebViewEditor()
{
    stopTimer();
    // Attachments reference the relays + webview — tear down in order.
    gearAttach.reset(); clutchAttach.reset(); driveAttach.reset();
    toneAttach.reset(); mixAttach.reset();    outputAttach.reset();
    webView.reset();
}

void OctaneWebViewEditor::createWebView()
{
    juce::WebBrowserComponent::Options opts;

   #if JUCE_WINDOWS
    opts = opts
        .withBackend (juce::WebBrowserComponent::Options::Backend::webview2)
        .withWinWebView2Options (juce::WebBrowserComponent::Options::WinWebView2{}
            .withUserDataFolder (juce::File::getSpecialLocation (juce::File::tempDirectory)));
   #endif

    using NF   = juce::WebBrowserComponent::NativeFunctionCompletion;
    using Args = const juce::Array<juce::var>&;

    opts = opts.withNativeIntegrationEnabled()
        .withResourceProvider ([this] (const auto& url) { return provideResource (url); },
                               juce::URL ("https://octane.carbonatedaudio.com").getOrigin())
        .withOptionsFrom (gearRelay)
        .withOptionsFrom (clutchRelay)
        .withOptionsFrom (driveRelay)
        .withOptionsFrom (toneRelay)
        .withOptionsFrom (mixRelay)
        .withOptionsFrom (outputRelay)
        .withNativeFunction (juce::Identifier ("setUIScale"),
            [this] (Args args, NF complete)
            {
                if (args.size() > 0)
                {
                    const auto s = juce::jlimit (0.75f, 1.5f, (float) (double) args[0]);
                    processor.setUIScale (s);
                    applyScale (s);
                }
                complete ({});
            })
        .withNativeFunction (juce::Identifier ("getUIScale"),
            [this] (Args, NF complete) { complete (juce::var ((double) processor.getUIScale())); });

    webView = std::make_unique<juce::WebBrowserComponent> (opts);
    addAndMakeVisible (*webView);

    gearAttach   = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "gear"),   gearRelay,   nullptr);
    clutchAttach = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "clutch"), clutchRelay, nullptr);
    driveAttach  = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "drive"),  driveRelay,  nullptr);
    toneAttach   = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "tone"),   toneRelay,   nullptr);
    mixAttach    = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "mix"),    mixRelay,    nullptr);
    outputAttach = std::make_unique<juce::WebSliderParameterAttachment> (param (processor, "output"), outputRelay, nullptr);

    webView->goToURL (juce::WebBrowserComponent::getResourceProviderRoot());
    resized();
}

std::optional<juce::WebBrowserComponent::Resource>
OctaneWebViewEditor::provideResource (const juce::String& url)
{
    const auto make = [] (const char* data, int size, const char* mime)
    {
        std::vector<std::byte> bytes ((size_t) size);
        std::memcpy (bytes.data(), data, (size_t) size);
        return juce::WebBrowserComponent::Resource { std::move (bytes), juce::String (mime) };
    };

    if (url == "/" || url == "/index.html")
        return make (OctaneWebUI::index_html, OctaneWebUI::index_htmlSize, "text/html");

    if (url == "/juce/index.js")
        return make (OctaneWebUI::index_js, OctaneWebUI::index_jsSize, "text/javascript");

    if (url == "/juce/check_native_interop.js")
        return make (OctaneWebUI::check_native_interop_js,
                     OctaneWebUI::check_native_interop_jsSize, "text/javascript");

    return std::nullopt;
}

void OctaneWebViewEditor::applyScale (float scale)
{
    setSize (juce::roundToInt (kBaseWidth * scale), juce::roundToInt (kBaseHeight * scale));
}

void OctaneWebViewEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff0d0a1a));   // brand void — shown behind dialog / while loading
}

void OctaneWebViewEditor::resized()
{
    if (webView != nullptr)
        webView->setBounds (getLocalBounds());
    if (activationDialog != nullptr)
        activationDialog->setBounds (getLocalBounds());
}

void OctaneWebViewEditor::timerCallback()
{
   #if ! DEMO_BUILD
    // Activation just succeeded? Swap the dialog out for the real UI.
    if (webView == nullptr && processor.getLicenseManager().isActivated())
    {
        activationDialog.reset();
        createWebView();
        return;
    }
   #endif

    if (webView != nullptr)
    {
        // Drive the tach: level in dB mapped UI-side to needle sweep.
        webView->emitEventIfBrowserIsVisible (juce::Identifier ("levelUpdate"),
                                              juce::var ((double) processor.getInputLevel()));
    }
}
