#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>

class ParaDynAudioProcessor;

// Detail panel for the currently-selected band: type/channel-mode/freq/gain/Q, the
// dynamic-mode toggle, and its threshold/ratio/attack/release/knee controls -- which
// are only enabled while that band's Dynamic toggle is on (dynamic mode is never on
// by default, per the request). Rebuilds its parameter attachments whenever the
// selected band slot changes, since MultiSqueeze-style per-band parameters are
// identified by slot-suffixed IDs.
class BandEditor final : public juce::Component, private juce::Timer
{
public:
    explicit BandEditor (ParaDynAudioProcessor&);
    ~BandEditor() override;

    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void rebuildForSelectedBand (int slot);
    void updateDynamicControlsEnabled();
    void updateSlopeControlVisible();

    ParaDynAudioProcessor& processor;
    int shownSlot = -2; // force initial rebuild
    bool slopeControlShown = false;

    juce::Label titleLabel;
    juce::ComboBox filterTypeBox, channelModeBox, slopeBox;
    juce::Slider frequencySlider, gainSlider, qSlider;
    juce::Label frequencyLabel, gainLabel, qLabel;
    juce::ToggleButton dynamicToggle { "Dynamic" };
    juce::Slider thresholdSlider, ratioSlider, attackSlider, releaseSlider, kneeSlider;
    juce::Label thresholdLabel, ratioLabel, attackLabel, releaseLabel, kneeLabel;
    juce::ToggleButton soloButton { "Solo" }, muteButton { "Mute" };
    juce::TextButton removeButton { "Remove" };
    juce::TextButton addButton { "Add band" };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ComboAttachment> filterTypeAttachment, channelModeAttachment, slopeAttachment;
    std::unique_ptr<SliderAttachment> frequencyAttachment, gainAttachment, qAttachment;
    std::unique_ptr<ButtonAttachment> dynamicAttachment, soloAttachment, muteAttachment;
    std::unique_ptr<SliderAttachment> thresholdAttachment, ratioAttachment, attackAttachment,
        releaseAttachment, kneeAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BandEditor)
};
