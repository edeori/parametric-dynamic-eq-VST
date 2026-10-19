#include "MatchEQPanel.h"
#include "PluginProcessor.h"
#include "ParaDynLookAndFeel.h"
#include "ParaDynFonts.h"

MatchEQPanel::MatchEQPanel (ParaDynAudioProcessor& owner) : processor (owner)
{
    for (auto* button : { &reference, &target, &stop, &apply, &undo, &save, &load })
        addAndMakeVisible (*button);
    reference.setTooltip ("Play a representative passage. Learns the input before EQ; stops after 12 seconds of audio.");
    target.setTooltip ("Learn the destination track's input before EQ. The reference is preserved.");
    stop.setTooltip ("Keep the analysed audio (minimum 1 second). Silence is ignored.");
    apply.setTooltip ("Replace current EQ bands with 16 editable stereo bells. Undo Match restores the previous bands.");
    undo.setTooltip ("Restore the EQ bands from before the most recent Apply Match.");
    save.setTooltip ("Save the learned reference for use in another plugin instance or project.");
    reference.onClick = [this] { actionMessage.clear(); processor.getMatchEQ().startCapture (MatchEQ::Capture::reference); timerCallback(); };
    target.onClick = [this] { actionMessage.clear(); processor.getMatchEQ().startCapture (MatchEQ::Capture::target); timerCallback(); };
    stop.onClick = [this] { actionMessage.clear(); processor.getMatchEQ().stopCapture(); markStateChanged(); timerCallback(); };
    apply.onClick = [this]
    {
        const auto result = processor.applyMatchEQ();
        actionMessage = result.wasOk() ? "Match applied. Bands are editable; change options and Apply again to refit."
                                      : result.getErrorMessage();
        if (result.wasOk() && processor.apvts.getRawParameterValue (ParamIDs::bypass)->load() >= 0.5f)
            actionMessage = "Match applied. Turn off Bypass to hear the correction.";
        timerCallback();
    };
    undo.onClick = [this]
    {
        if (processor.undoMatchEQ()) actionMessage = "Previous EQ bands restored.";
        timerCallback();
    };
    save.onClick = [this] { chooseReferenceFile (true); };
    load.onClick = [this] { chooseReferenceFile (false); };

    amount.setRange (0.0, 100.0, 1.0); amount.setTextValueSuffix (" %");
    smoothing.setRange (0.15, 2.0, 0.05); smoothing.setTextValueSuffix (" oct");
    limit.setRange (1.0, 18.0, 0.5); limit.setTextValueSuffix (" dB");
    amount.setTooltip ("Correction strength. Press Apply Match after changing this value.");
    smoothing.setTooltip ("Spectral smoothing width in octaves. Higher values make broader corrections. Press Apply Match.");
    limit.setTooltip ("Maximum boost/cut of the fitted EQ curve. Press Apply Match.");
    for (auto* slider : { &amount, &smoothing, &limit })
    {
        slider->setSliderStyle (juce::Slider::LinearHorizontal);
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 20);
        slider->onValueChange = [this] { settingsChanged(); };
        addAndMakeVisible (*slider);
    }
    amountLabel.setText ("Amount", juce::dontSendNotification);
    smoothingLabel.setText ("Smoothing", juce::dontSendNotification);
    limitLabel.setText ("Max +/-", juce::dontSendNotification);
    for (auto* label : { &amountLabel, &smoothingLabel, &limitLabel, &status })
    {
        label->setFont (ParaDynFonts::uiLight (12.0f));
        label->setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
        addAndMakeVisible (*label);
    }
    timerCallback();
    startTimerHz (10);
}

void MatchEQPanel::markStateChanged()
{
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
}

void MatchEQPanel::settingsChanged()
{
    processor.getMatchEQ().setSettings ({ (float) amount.getValue(), (float) smoothing.getValue(), (float) limit.getValue() });
    actionMessage = "Options changed. Press Apply Match to update the EQ bands.";
    markStateChanged();
}

void MatchEQPanel::timerCallback()
{
    const auto state = processor.getMatchEQ().getStatus();
    const auto learning = state.capture != MatchEQ::Capture::none;
    if (wasLearning && ! learning) markStateChanged();
    wasLearning = learning;
    if (state.message != previousEngineMessage) actionMessage.clear();
    previousEngineMessage = state.message;
    reference.setEnabled (! learning);
    target.setEnabled (! learning);
    stop.setEnabled (learning);
    apply.setEnabled (! learning && state.hasReference && state.hasTarget);
    undo.setEnabled (! learning && processor.canUndoMatchEQ());
    save.setEnabled (! learning && state.hasReference && chooser == nullptr);
    load.setEnabled (! learning && chooser == nullptr);
    for (auto* button : { &reference, &target, &stop, &apply, &undo, &save, &load })
        button->setAlpha (button->isEnabled() ? 1.0f : 0.35f);
    reference.setToggleState (state.capture == MatchEQ::Capture::reference, juce::dontSendNotification);
    target.setToggleState (state.capture == MatchEQ::Capture::target, juce::dontSendNotification);
    reference.setButtonText (state.hasReference ? "Relearn Reference" : "Learn Reference");
    target.setButtonText (state.hasTarget ? "Relearn Target" : "Learn Target");
    for (auto* slider : { &amount, &smoothing, &limit }) slider->setEnabled (! learning);
    const auto s = processor.getMatchEQ().getSettings();
    if (! amount.isMouseButtonDown()) amount.setValue (s.amount, juce::dontSendNotification);
    if (! smoothing.isMouseButtonDown()) smoothing.setValue (s.smoothing, juce::dontSendNotification);
    if (! limit.isMouseButtonDown()) limit.setValue (s.limitDb, juce::dontSendNotification);
    auto text = actionMessage.isNotEmpty() ? actionMessage : state.message;
    if (learning) text += "  " + juce::String (state.seconds, 1) + " / 12 s - play audio, then Stop & Keep.";
    else text = juce::String (state.hasReference ? "Ref: ready  |  " : "Ref: empty  |  ")
              + (state.hasTarget ? "Target: ready  |  " : "Target: empty  |  ") + text;
    status.setText (text, juce::dontSendNotification);
    status.setTooltip (text);
}

void MatchEQPanel::chooseReferenceFile (bool saving)
{
    chooser = std::make_unique<juce::FileChooser> (saving ? "Save Match EQ reference" : "Load Match EQ reference",
                                                 saving ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                                              .getChildFile ("Reference.pdeqref") : juce::File(), "*.pdeqref");
    const auto flags = saving ? juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting
                              : juce::FileBrowserComponent::openMode;
    chooser->launchAsync (flags | juce::FileBrowserComponent::canSelectFiles,
        [safe = juce::Component::SafePointer<MatchEQPanel> (this), saving] (const juce::FileChooser& dialog)
        {
            if (safe == nullptr) return;
            auto file = dialog.getResult();
            if (file != juce::File())
            {
                // Do not silently change a selected filename after the chooser's
                // overwrite confirmation; its exact path is the authorised one.
                const auto result = saving ? safe->processor.getMatchEQ().saveReference (file)
                                           : safe->processor.getMatchEQ().loadReference (file);
                safe->previousEngineMessage = safe->processor.getMatchEQ().getStatus().message;
                safe->actionMessage = result.wasOk() ? (saving ? "Reference saved." : "Reference loaded. Learn the destination track.")
                                                    : result.getErrorMessage();
                if (result.wasOk() && ! saving) safe->markStateChanged();
            }
            safe->chooser.reset();
            safe->timerCallback();
        });
}

void MatchEQPanel::resized()
{
    auto area = getLocalBounds().reduced (6, 3);
    auto row = area.removeFromTop (juce::roundToInt (getHeight() * 0.32f));
    const auto width = (float) row.getWidth();
    int index = 0;
    const float shares[] { 0.17f, 0.15f, 0.13f, 0.15f, 0.14f, 0.13f, 0.13f };
    for (auto* button : { &reference, &target, &stop, &apply, &undo, &save, &load })
        button->setBounds (row.removeFromLeft (juce::roundToInt (width * shares[index++])).reduced (2, 0));
    area.removeFromTop (3);
    auto options = area.removeFromTop (juce::roundToInt (getHeight() * 0.3f));
    const auto third = options.getWidth() / 3;
    auto layoutSlider = [third] (juce::Rectangle<int> r, juce::Label& label, juce::Slider& slider)
    {
        label.setBounds (r.removeFromLeft (juce::jmin (70, third / 3)));
        slider.setBounds (r.reduced (4, 0));
    };
    layoutSlider (options.removeFromLeft (third), amountLabel, amount);
    layoutSlider (options.removeFromLeft (third), smoothingLabel, smoothing);
    layoutSlider (options, limitLabel, limit);
    status.setBounds (area);
}

void MatchEQPanel::paint (juce::Graphics& g)
{
    g.setColour (ParaDynLookAndFeel::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    g.setColour (ParaDynLookAndFeel::accent.withAlpha (0.25f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);
}
