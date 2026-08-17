#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>
#include "SpectrumAnalyzer.h"
#include "BandEditor.h"

class ParaDynAudioProcessor;

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

    ParaDynAudioProcessor& audioProcessor;

    juce::ToggleButton bypassButton { "Bypass" };
    juce::Slider outputSlider;
    juce::Label outputLabel;
    juce::ComboBox oversamplingBox;
    juce::ComboBox rangeBox;
    juce::Label rangeLabel;
    juce::Label titleLabel;

    SpectrumAnalyzer spectrumAnalyzer;
    juce::Viewport bandStripViewport;
    juce::Component bandStripContent;
    std::vector<std::unique_ptr<juce::TextButton>> bandButtons;
    std::vector<int> bandButtonSlots;
    juce::TextButton addBandButton { "+" };
    int lastBandCount = -1;

    BandEditor bandEditor;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    std::unique_ptr<ButtonAttachment> bypassAttachment;
    std::unique_ptr<SliderAttachment> outputAttachment;
    std::unique_ptr<ComboAttachment> oversamplingAttachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaDynAudioProcessorEditor)
};
