#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

class ParaDynAudioProcessor;

class MatchEQPanel final : public juce::Component, private juce::Timer
{
public:
    explicit MatchEQPanel (ParaDynAudioProcessor&);
    void resized() override;
    void paint (juce::Graphics&) override;

private:
    void timerCallback() override;
    void chooseReferenceFile (bool save);
    void settingsChanged();
    void markStateChanged();
    ParaDynAudioProcessor& processor;
    juce::TextButton reference { "Learn Reference" }, target { "Learn Target" }, stop { "Stop & Keep" },
                     apply { "Apply Match" }, undo { "Undo Match" }, save { "Save Ref..." }, load { "Load Ref..." };
    juce::Slider amount, smoothing, limit;
    juce::Label amountLabel, smoothingLabel, limitLabel, status;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String actionMessage, previousEngineMessage;
    bool wasLearning = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchEQPanel)
};
