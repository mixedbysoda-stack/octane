#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "LicenseManager.h"

class ActivationDialog : public juce::Component
{
public:
    explicit ActivationDialog(LicenseManager& lm);
    ~ActivationDialog() override = default;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void visibilityChanged() override;

private:
    LicenseManager& licenseManager;

    juce::Label titleLabel;
    juce::Label instructionLabel;
    juce::TextEditor keyInput;
    juce::TextButton activateButton;
    juce::Label statusLabel;
    juce::HyperlinkButton buyLink;
    juce::HyperlinkButton helpLink;

    void onActivateClicked();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ActivationDialog)
};
