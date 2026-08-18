#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "SpectrumAnalyzer.h"
#include "BandEditor.h"
#include "ParaDynLookAndFeel.h"

#if PARADYNEQ_ENABLE_LICENSING
#include "Licensing/LicenseManager.h"
#endif

class ParaDynAudioProcessor;

#if PARADYNEQ_ENABLE_LICENSING
// Full-editor dimmed backdrop + centred popup panel. getPopupBounds() is the single source of
// truth both this component's own paint() and the editor's resized() read from, so the drawn
// panel and its content layout can never desync (see DeathWings' own overlay for the precedent
// this was ported from).
class ParaDynLicenseOverlayComponent final : public juce::Component
{
public:
    juce::Rectangle<float> getPopupBounds() const;
    void paint (juce::Graphics&) override;
};
#endif

// A rotary Slider that stays permanently compact (NoTextBox) but reports double-clicks
// via onDoubleClicked, so the editor can pop up a separate, fully self-contained editable
// value field on demand -- see outputValueEditor in the editor class below. A plain
// Slider::TextBoxBelow was tried first, driven by showing/hiding via colour swaps, but
// the internal text-entry Label's focus handling meant the box didn't reliably close
// itself again once an edit session ended (focus landed back on the Slider itself after
// its child editor was destroyed) -- a separate Label, driven by Label::onEditorHide
// (which fires unconditionally whenever its own edit session ends, regardless of commit/
// cancel/click-away), sidesteps that entirely.
class RevealableKnob final : public juce::Slider
{
public:
    using juce::Slider::Slider;
    std::function<void()> onDoubleClicked;

private:
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        juce::Slider::mouseDoubleClick (e);
        if (onDoubleClicked != nullptr)
            onDoubleClicked();
    }
};

class ParaDynAudioProcessorEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit ParaDynAudioProcessorEditor (ParaDynAudioProcessor&);
    ~ParaDynAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void rebuildBandStrip();
    void updateBandButtonStates();
#if PARADYNEQ_ENABLE_LICENSING
    void updateLicenseUi();
    void updateLicenseOverlayVisibility();
    void showActivationDialog();
#endif

    ParaDynAudioProcessor& audioProcessor;
    ParaDynLookAndFeel lookAndFeel;

    // Design-resolution ratio scaling -- everything in resized() is expressed against
    // this fixed box and multiplied by sx/sy, matching Mockup/PORTING.md and the
    // AdaptiveGate-VST PedalViewComponent precedent, so the layout scales cleanly with
    // the window instead of relying on absolute pixel constants.
    static constexpr float designWidth = 1180.0f;
    static constexpr float designHeight = 715.0f;

    juce::Label manufacturerLabel;
    juce::Label titleLabel;
    juce::Image mothLogo;
    juce::Rectangle<int> logoBounds;

    juce::ToggleButton bypassButton { "Bypass" };
    juce::ComboBox oversamplingBox;
    juce::ComboBox rangeBox;
    juce::Label rangeLabel;
    juce::Label oversamplingLabel;
    RevealableKnob outputKnob;
    juce::Label outputLabel;
    // Separate overlay editor for the Output knob's exact value -- see RevealableKnob's
    // comment above for why this isn't just the Slider's own TextBoxBelow. Hidden and
    // textless until a double-click on the knob populates and reveals it.
    juce::Label outputValueEditor;
    juce::TooltipWindow tooltipWindow { this, 500 };

    SpectrumAnalyzer spectrumAnalyzer;
    juce::Viewport bandStripViewport;
    juce::Component bandStripContent;
    std::vector<std::unique_ptr<juce::TextButton>> bandButtons;
    std::vector<int> bandButtonSlots;
    juce::TextButton addBandButton { "+" };
    int lastBandCount = -1;

    BandEditor bandEditor;

#if PARADYNEQ_ENABLE_LICENSING
    ParaDynLicenseOverlayComponent licenseOverlay;
    juce::Label licenseOverlayTitleLabel;
    juce::Label licenseOverlayStatusLabel;
    juce::TextButton activateButton { "ACTIVATE" };
    juce::TextButton trialButton { "TRIAL" };
    bool licenseOverlayDismissed = false;
#endif

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ComboAttachment> oversamplingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaDynAudioProcessorEditor)
};
