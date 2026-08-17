#include "BandEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"

namespace
{
    void styleRotarySlider (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (juce::Font (juce::FontOptions (11.0f)));
    }
}

BandEditor::BandEditor (ParaDynAudioProcessor& owner) : processor (owner)
{
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (juce::Font (juce::FontOptions (15.0f).withStyle ("Bold")));
    addAndMakeVisible (titleLabel);

    filterTypeBox.addItemList (filterTypeNames(), 1);
    channelModeBox.addItemList (channelModeNames(), 1);
    slopeBox.addItemList (filterSlopeNames(), 1);
    addAndMakeVisible (filterTypeBox);
    addAndMakeVisible (channelModeBox);
    addAndMakeVisible (slopeBox);

    styleRotarySlider (frequencySlider, frequencyLabel, "Freq");
    styleRotarySlider (gainSlider, gainLabel, "Gain");
    styleRotarySlider (qSlider, qLabel, "Q");
    for (auto* s : { &frequencySlider, &gainSlider, &qSlider })
        addAndMakeVisible (s);
    for (auto* l : { &frequencyLabel, &gainLabel, &qLabel })
        addAndMakeVisible (l);

    addAndMakeVisible (dynamicToggle);

    styleRotarySlider (thresholdSlider, thresholdLabel, "Threshold");
    styleRotarySlider (ratioSlider, ratioLabel, "Ratio");
    styleRotarySlider (attackSlider, attackLabel, "Attack");
    styleRotarySlider (releaseSlider, releaseLabel, "Release");
    styleRotarySlider (kneeSlider, kneeLabel, "Knee");
    for (auto* s : { &thresholdSlider, &ratioSlider, &attackSlider, &releaseSlider, &kneeSlider })
        addAndMakeVisible (s);
    for (auto* l : { &thresholdLabel, &ratioLabel, &attackLabel, &releaseLabel, &kneeLabel })
        addAndMakeVisible (l);

    addAndMakeVisible (soloButton);
    addAndMakeVisible (muteButton);
    addAndMakeVisible (removeButton);
    addAndMakeVisible (addButton);

    removeButton.onClick = [this] { if (shownSlot >= 0) processor.removeBand (shownSlot); };
    addButton.onClick = [this] { processor.addBand(); };

    startTimerHz (20);
}

BandEditor::~BandEditor() { stopTimer(); }

void BandEditor::timerCallback()
{
    const auto slot = processor.getSelectedBand();
    if (slot != shownSlot)
        rebuildForSelectedBand (slot);
    updateDynamicControlsEnabled();
    updateSlopeControlVisible();
}

void BandEditor::updateDynamicControlsEnabled()
{
    const auto enabled = shownSlot >= 0 && dynamicToggle.getToggleState();
    for (auto* s : { &thresholdSlider, &ratioSlider, &attackSlider, &releaseSlider, &kneeSlider })
        s->setEnabled (enabled);
    for (auto* l : { &thresholdLabel, &ratioLabel, &attackLabel, &releaseLabel, &kneeLabel })
        l->setEnabled (enabled);
}

void BandEditor::updateSlopeControlVisible()
{
    // Slope (6/12/18/24 dB/oct) only means something for High Pass / Low Pass; Bell
    // and Shelf stay a single fixed 2-pole stage regardless of this parameter's value.
    const auto type = shownSlot >= 0
        ? static_cast<FilterType> (filterTypeBox.getSelectedItemIndex())
        : FilterType::Bell;
    const auto shouldShow = shownSlot >= 0 && (type == FilterType::HighPass || type == FilterType::LowPass);
    if (shouldShow == slopeControlShown)
        return;
    slopeControlShown = shouldShow;
    slopeBox.setVisible (shouldShow);
    resized();
}

void BandEditor::rebuildForSelectedBand (int slot)
{
    shownSlot = slot;
    filterTypeAttachment.reset(); channelModeAttachment.reset(); slopeAttachment.reset();
    frequencyAttachment.reset(); gainAttachment.reset(); qAttachment.reset();
    dynamicAttachment.reset(); soloAttachment.reset(); muteAttachment.reset();
    thresholdAttachment.reset(); ratioAttachment.reset(); attackAttachment.reset();
    releaseAttachment.reset(); kneeAttachment.reset();

    const auto hasBand = slot >= 0;
    for (auto* c : { (juce::Component*) &filterTypeBox, (juce::Component*) &channelModeBox,
                     (juce::Component*) &frequencySlider, (juce::Component*) &gainSlider, (juce::Component*) &qSlider,
                     (juce::Component*) &dynamicToggle, (juce::Component*) &soloButton, (juce::Component*) &muteButton,
                     (juce::Component*) &removeButton })
        c->setVisible (hasBand);
    addButton.setVisible (! hasBand);
    slopeControlShown = false;
    slopeBox.setVisible (false);

    if (! hasBand)
    {
        titleLabel.setText ("No band selected", juce::dontSendNotification);
        return;
    }

    titleLabel.setText ("Band " + juce::String (slot + 1), juce::dontSendNotification);
    auto& apvts = processor.apvts;
    filterTypeAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::filterType (slot), filterTypeBox);
    slopeAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::filterSlope (slot), slopeBox);
    channelModeAttachment = std::make_unique<ComboAttachment> (apvts, ParamIDs::channelMode (slot), channelModeBox);
    frequencyAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::frequency (slot), frequencySlider);
    gainAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::gain (slot), gainSlider);
    qAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::q (slot), qSlider);
    dynamicAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::dynamicEnabled (slot), dynamicToggle);
    soloAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::solo (slot), soloButton);
    muteAttachment = std::make_unique<ButtonAttachment> (apvts, ParamIDs::mute (slot), muteButton);
    thresholdAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::threshold (slot), thresholdSlider);
    ratioAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::ratio (slot), ratioSlider);
    attackAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::attack (slot), attackSlider);
    releaseAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::release (slot), releaseSlider);
    kneeAttachment = std::make_unique<SliderAttachment> (apvts, ParamIDs::knee (slot), kneeSlider);
    resized();
}

void BandEditor::resized()
{
    auto bounds = getLocalBounds().reduced (8);
    titleLabel.setBounds (bounds.removeFromTop (22));

    if (shownSlot < 0)
    {
        addButton.setBounds (bounds.removeFromTop (28).withSizeKeepingCentre (120, 26));
        return;
    }

    auto topRow = bounds.removeFromTop (26);
    if (slopeControlShown)
    {
        filterTypeBox.setBounds (topRow.removeFromLeft (topRow.getWidth() / 3).reduced (2, 0));
        slopeBox.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2).reduced (2, 0));
        channelModeBox.setBounds (topRow.reduced (2, 0));
    }
    else
    {
        filterTypeBox.setBounds (topRow.removeFromLeft (topRow.getWidth() / 2).reduced (2, 0));
        channelModeBox.setBounds (topRow.reduced (2, 0));
    }

    auto mainKnobs = bounds.removeFromTop (86);
    const auto knobWidth = mainKnobs.getWidth() / 3;
    for (auto pair : { std::pair<juce::Slider*, juce::Label*> { &frequencySlider, &frequencyLabel },
                       std::pair<juce::Slider*, juce::Label*> { &gainSlider, &gainLabel },
                       std::pair<juce::Slider*, juce::Label*> { &qSlider, &qLabel } })
    {
        auto column = mainKnobs.removeFromLeft (knobWidth);
        pair.second->setBounds (column.removeFromTop (14));
        pair.first->setBounds (column);
    }

    auto toggleRow = bounds.removeFromTop (26);
    dynamicToggle.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 3));
    soloButton.setBounds (toggleRow.removeFromLeft (toggleRow.getWidth() / 2));
    muteButton.setBounds (toggleRow);

    auto dynamicKnobs = bounds.removeFromTop (86);
    const auto dynamicKnobWidth = dynamicKnobs.getWidth() / 5;
    for (auto pair : { std::pair<juce::Slider*, juce::Label*> { &thresholdSlider, &thresholdLabel },
                       std::pair<juce::Slider*, juce::Label*> { &ratioSlider, &ratioLabel },
                       std::pair<juce::Slider*, juce::Label*> { &attackSlider, &attackLabel },
                       std::pair<juce::Slider*, juce::Label*> { &releaseSlider, &releaseLabel },
                       std::pair<juce::Slider*, juce::Label*> { &kneeSlider, &kneeLabel } })
    {
        auto column = dynamicKnobs.removeFromLeft (dynamicKnobWidth);
        pair.second->setBounds (column.removeFromTop (14));
        pair.first->setBounds (column);
    }

    removeButton.setBounds (bounds.removeFromTop (26).withSizeKeepingCentre (120, 24));
}

void BandEditor::paint (juce::Graphics& g)
{
    g.setColour (juce::Colour (0xff0e1013));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);
    g.setColour (juce::Colour (0xff2a2e33));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}
