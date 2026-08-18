#include "BandEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "ParaDynFonts.h"
#include "ParaDynLookAndFeel.h"

namespace
{
    void styleRotarySlider (juce::Slider& slider, juce::Label& label, const juce::String& text)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 64, 16);
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setFont (ParaDynFonts::uiLight (11.0f));
        label.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    }
}

BandEditor::BandEditor (ParaDynAudioProcessor& owner) : processor (owner)
{
    titleLabel.setJustificationType (juce::Justification::centredLeft);
    titleLabel.setFont (ParaDynFonts::uiMedium (13.0f));
    titleLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textPrimary);
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
    for (auto* c : { (juce::Component*) &titleLabel, (juce::Component*) &filterTypeBox, (juce::Component*) &channelModeBox,
                     (juce::Component*) &frequencySlider, (juce::Component*) &gainSlider, (juce::Component*) &qSlider,
                     (juce::Component*) &dynamicToggle, (juce::Component*) &soloButton, (juce::Component*) &muteButton,
                     (juce::Component*) &removeButton })
        c->setVisible (hasBand);
    addButton.setVisible (! hasBand);
    slopeControlShown = false;
    slopeBox.setVisible (false);

    if (! hasBand)
    {
        titleLabel.setText ({}, juce::dontSendNotification);
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

namespace
{
    // One "knob stack": small label above, rotary knob, its own text-box readout below --
    // matches Mockup/paradyneq-ui.html's .paradyn-knob-stack. Slider's own TextBoxBelow
    // serves as the readout, so this just places label+knob (the text box is included in
    // the slider's own bounds via setTextBoxStyle in styleRotarySlider above).
    void layoutKnobStack (juce::Rectangle<int> column, juce::Slider& slider, juce::Label& label)
    {
        label.setBounds (column.removeFromTop (14));
        slider.setBounds (column);
    }

    void layoutKnobRow (juce::Rectangle<int> row, std::initializer_list<std::pair<juce::Slider*, juce::Label*>> knobs)
    {
        const auto n = (int) knobs.size();
        if (n == 0) return;
        const auto colWidth = row.getWidth() / n;
        int x = row.getX();
        for (auto& pair : knobs)
        {
            layoutKnobStack ({ x, row.getY(), colWidth, row.getHeight() }, *pair.first, *pair.second);
            x += colWidth;
        }
    }
}

void BandEditor::resized()
{
    // Two-column layout ported from Mockup/paradyneq-ui.html's .paradyn-band-editor
    // (43.6fr main / 4fr divider / 52.4fr dynamic, see Mockup/PORTING.md) -- replaces
    // the previous full-width-stacked-rows layout, which needed ~270px of vertical
    // space and no longer fits the mockup's 192px-tall band-editor row.
    auto bounds = getLocalBounds().reduced (14, 10);

    if (shownSlot < 0)
    {
        addButton.setBounds (bounds.withSizeKeepingCentre (120, 26));
        return;
    }

    auto titleRow = bounds.removeFromTop (18);
    titleLabel.setBounds (titleRow.removeFromLeft (100));
    removeButton.setBounds (titleRow.removeFromRight (60));
    bounds.removeFromTop (4);

    const auto dividerX = bounds.getX() + juce::roundToInt (bounds.getWidth() * (43.6f / 100.0f));
    auto mainColumn = bounds.withWidth (dividerX - bounds.getX() - 6);
    auto dynamicColumn = bounds.withX (dividerX + 6).withWidth (bounds.getRight() - dividerX - 6);

    auto typeRow = mainColumn.removeFromTop (24);
    if (slopeControlShown)
    {
        filterTypeBox.setBounds (typeRow.removeFromLeft (typeRow.getWidth() / 3).reduced (2, 0));
        slopeBox.setBounds (typeRow.removeFromLeft (typeRow.getWidth() / 2).reduced (2, 0));
        channelModeBox.setBounds (typeRow.reduced (2, 0));
    }
    else
    {
        filterTypeBox.setBounds (typeRow.removeFromLeft (typeRow.getWidth() / 2).reduced (2, 0));
        channelModeBox.setBounds (typeRow.reduced (2, 0));
    }
    mainColumn.removeFromTop (10);
    layoutKnobRow (mainColumn, { { &frequencySlider, &frequencyLabel }, { &gainSlider, &gainLabel }, { &qSlider, &qLabel } });

    auto toggleRow = dynamicColumn.removeFromTop (24);
    const auto toggleWidth = toggleRow.getWidth() / 3;
    dynamicToggle.setBounds (toggleRow.removeFromLeft (toggleWidth));
    soloButton.setBounds (toggleRow.removeFromLeft (toggleWidth));
    muteButton.setBounds (toggleRow);
    dynamicColumn.removeFromTop (10);
    layoutKnobRow (dynamicColumn, { { &thresholdSlider, &thresholdLabel }, { &ratioSlider, &ratioLabel },
                                     { &attackSlider, &attackLabel }, { &releaseSlider, &releaseLabel },
                                     { &kneeSlider, &kneeLabel } });
}

void BandEditor::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0x8c05070a));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (juce::Colour (0xff262b32));
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.0f);

    if (shownSlot >= 0)
    {
        const auto dividerX = getLocalBounds().reduced (14, 10).getX()
            + juce::roundToInt (getLocalBounds().reduced (14, 10).getWidth() * (43.6f / 100.0f));
        juce::ColourGradient grad (juce::Colours::transparentBlack, (float) dividerX, bounds.getY(),
                                    juce::Colours::transparentBlack, (float) dividerX, bounds.getBottom(), false);
        grad.addColour (0.5, juce::Colour (0x29bcc6d6));
        g.setGradientFill (grad);
        g.fillRect (juce::Rectangle<float> ((float) dividerX + 2.0f, bounds.getY() + 8.0f, 1.0f, bounds.getHeight() - 16.0f));
    }
}
