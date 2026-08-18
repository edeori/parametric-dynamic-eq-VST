#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
#include "ParaDynFonts.h"
#include <array>

namespace
{
    // Plain click selects the band via the normal Button click cycle; Cmd-click (or,
    // on macOS, Ctrl-click / a trackpad secondary-click gesture -- see isPopupMenu()
    // below) instead solos the band for as long as the mouse stays down (released on
    // mouseUp), bypassing the click cycle entirely. Overriding mouseDown/mouseUp
    // directly (rather than checking juce::ModifierKeys::currentModifiers inside
    // onClick) is deliberate: the global current-modifiers snapshot is not reliably
    // up to date by the time a Button's onClick fires, especially when the editor is
    // hosted inside a DAW window -- the MouseEvent's own `mods` is always accurate for
    // the event that produced it.
    class BandStripButton final : public juce::TextButton
    {
    public:
        using juce::TextButton::TextButton;
        std::function<void (bool)> onSoloEngage;

        void mouseDown (const juce::MouseEvent& e) override
        {
            // isPopupMenu() covers a real right-click *and*, on macOS, a Ctrl-click --
            // macOS reinterprets Control-click as a right-click at the OS level, so
            // isCtrlDown() alone never fires for it (also true for a trackpad's
            // secondary-click/two-finger-tap gesture, since that's the same OS event).
            if (e.mods.isCommandDown() || e.mods.isPopupMenu())
            {
                soloHeld = true;
                if (onSoloEngage != nullptr)
                    onSoloEngage (true);
                return;
            }
            juce::TextButton::mouseDown (e);
        }

        void mouseUp (const juce::MouseEvent& e) override
        {
            if (soloHeld)
            {
                soloHeld = false;
                if (onSoloEngage != nullptr)
                    onSoloEngage (false);
                return;
            }
            juce::TextButton::mouseUp (e);
        }

    private:
        bool soloHeld = false;
    };

    juce::String withLetterSpacing (const juce::String& text)
    {
        juce::String result;
        for (int i = 0; i < text.length(); ++i)
        {
            result << text[i];
            if (i < text.length() - 1)
                result << " ";
        }
        return result;
    }
}

#if PARADYNEQ_ENABLE_LICENSING
juce::Rectangle<float> ParaDynLicenseOverlayComponent::getPopupBounds() const
{
    return getLocalBounds().toFloat().withSizeKeepingCentre (
        (float) getWidth() * 0.55f, (float) getHeight() * 0.34f);
}

void ParaDynLicenseOverlayComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff020304).withAlpha (0.96f));
    g.fillRoundedRectangle (bounds, 10.0f);

    // popup comes from getPopupBounds() -- the single source of truth the editor's
    // resized() also reads from, so the drawn panel and its laid-out contents can
    // never desync (see ParaDynLookAndFeel/PluginEditor.h's comment on this).
    auto popup = getPopupBounds();
    g.setColour (ParaDynLookAndFeel::panel);
    g.fillRoundedRectangle (popup, 14.0f);

    auto shadow = popup.translated (0.0f, 4.0f);
    g.setColour (juce::Colours::black.withAlpha (0.55f));
    g.drawRoundedRectangle (shadow, 14.0f, 5.0f);

    g.setColour (ParaDynLookAndFeel::accentHot.withAlpha (0.4f));
    g.drawRoundedRectangle (popup, 14.0f, 1.4f);
}
#endif

ParaDynAudioProcessorEditor::ParaDynAudioProcessorEditor (ParaDynAudioProcessor& owner)
    : AudioProcessorEditor (owner), audioProcessor (owner), spectrumAnalyzer (owner), bandEditor (owner)
{
    setLookAndFeel (&lookAndFeel);

    manufacturerLabel.setText (withLetterSpacing ("MOTH PRODUCTION"), juce::dontSendNotification);
    manufacturerLabel.setFont (ParaDynFonts::uiLight (10.0f));
    manufacturerLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    addAndMakeVisible (manufacturerLabel);

    // titleLabel itself stays invisible/textless -- its bounds and font are used as the
    // layout anchor for the two-colour AttributedString drawn directly in paint()
    // (Label::setText() can't mix colours within one string).
    addChildComponent (titleLabel);

    mothLogo = ParaDynLookAndFeel::loadMothLogo (110);

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::bypass, bypassButton);

    outputKnob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    // Permanently compact -- no reserved text-box strip at all, unlike the BandEditor
    // knobs. The editable value field is the separate outputValueEditor Label below,
    // shown only for the duration of an edit (see RevealableKnob's comment for why).
    outputKnob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    outputKnob.setTooltip ("Output Gain, -24 to +24 dB. Double-click the knob to type an exact value.");

    // Styled to match the BandEditor knobs' own value boxes (accent-outlined, dark
    // fill) even though this one isn't drawn by ParaDynLookAndFeel's slider textbox --
    // it's a plain Label, shown/hidden manually rather than living permanently in the
    // layout, since the knob it belongs to has no reserved space for it.
    outputValueEditor.setJustificationType (juce::Justification::centred);
    outputValueEditor.setFont (ParaDynFonts::numeric (13.0f));
    outputValueEditor.setColour (juce::Label::textColourId, ParaDynLookAndFeel::accent);
    outputValueEditor.setColour (juce::Label::backgroundColourId, juce::Colour (0xff0c0c0e));
    outputValueEditor.setColour (juce::Label::outlineColourId, ParaDynLookAndFeel::accent.withAlpha (0.4f));
    outputValueEditor.setColour (juce::TextEditor::textColourId, ParaDynLookAndFeel::accent);
    outputValueEditor.setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff0c0c0e));
    outputValueEditor.setColour (juce::TextEditor::outlineColourId, ParaDynLookAndFeel::accent);
    // lossOfFocusDiscardsChanges = false: clicking away commits the typed value, same as
    // the BandEditor knobs' own text boxes.
    outputValueEditor.setEditable (false, false, false);
    outputValueEditor.onTextChange = [this]
    {
        // Fires only once the edit session's new text has actually been committed to
        // the Label (see RevealableKnob's comment) -- never on cancel/unchanged text.
        outputKnob.setValue (juce::jlimit ((double) outputKnob.getMinimum(), (double) outputKnob.getMaximum(),
                                            outputValueEditor.getText().getDoubleValue()),
                              juce::sendNotificationSync);
    };
    outputValueEditor.onEditorHide = [this] { outputValueEditor.setVisible (false); };
    outputKnob.onDoubleClicked = [this]
    {
        outputValueEditor.setText (juce::String (outputKnob.getValue(), 2), juce::dontSendNotification);
        outputValueEditor.setVisible (true);
        outputValueEditor.toFront (false);
        outputValueEditor.showEditor();
    };
    addChildComponent (outputValueEditor);

    outputLabel.setJustificationType (juce::Justification::centred);
    outputLabel.setFont (ParaDynFonts::uiLight (10.0f));
    outputLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    outputLabel.setText ("Output", juce::dontSendNotification);
    addAndMakeVisible (outputKnob);
    addAndMakeVisible (outputLabel);
    outputAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::output, outputKnob);

    oversamplingBox.addItemList (oversamplingNames(), 1);
    addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<ComboAttachment> (audioProcessor.apvts, ParamIDs::oversampling, oversamplingBox);
    oversamplingLabel.setText ("Oversampling", juce::dontSendNotification);
    oversamplingLabel.setFont (ParaDynFonts::uiLight (10.0f));
    oversamplingLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    addAndMakeVisible (oversamplingLabel);

    // Display-only zoom for the spectrum panel's gain axis -- not an audio parameter,
    // so it's wired directly rather than through an APVTS attachment. A tighter range
    // gives more pixels per dB, i.e. more precise node dragging.
    rangeLabel.setText ("Range", juce::dontSendNotification);
    rangeLabel.setFont (ParaDynFonts::uiLight (10.0f));
    rangeLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    rangeBox.addItemList ({ "+/-3 dB", "+/-6 dB", "+/-12 dB", "+/-24 dB", "+/-48 dB" }, 1);
    rangeBox.setSelectedItemIndex (3, juce::dontSendNotification); // +/-24 dB, matching the previous fixed range
    rangeBox.onChange = [this]
    {
        static constexpr std::array<float, 5> ranges { 3.0f, 6.0f, 12.0f, 24.0f, 48.0f };
        const auto index = (size_t) juce::jlimit (0, (int) ranges.size() - 1, rangeBox.getSelectedItemIndex());
        spectrumAnalyzer.setGainRangeDb (ranges[index]);
    };
    addAndMakeVisible (rangeBox);
    addAndMakeVisible (rangeLabel);

    addAndMakeVisible (spectrumAnalyzer);

    bandStripViewport.setViewedComponent (&bandStripContent, false);
    bandStripViewport.setScrollBarsShown (false, true);
    addAndMakeVisible (bandStripViewport);
    addBandButton.onClick = [this] { audioProcessor.addBand(); };
    bandStripContent.addAndMakeVisible (addBandButton);

    addAndMakeVisible (bandEditor);

#if PARADYNEQ_ENABLE_LICENSING
    licenseOverlay.setInterceptsMouseClicks (true, true);
    addAndMakeVisible (licenseOverlay);

    licenseOverlayTitleLabel.setText ("Unlock ParaDyn EQ", juce::dontSendNotification);
    licenseOverlayTitleLabel.setFont (ParaDynFonts::title (20.0f));
    licenseOverlayTitleLabel.setJustificationType (juce::Justification::centred);
    licenseOverlayTitleLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textPrimary);
    licenseOverlay.addAndMakeVisible (licenseOverlayTitleLabel);

    licenseOverlayStatusLabel.setFont (ParaDynFonts::uiLight (13.0f));
    licenseOverlayStatusLabel.setJustificationType (juce::Justification::centred);
    licenseOverlayStatusLabel.setColour (juce::Label::textColourId, ParaDynLookAndFeel::textDim);
    licenseOverlay.addAndMakeVisible (licenseOverlayStatusLabel);

    // Activate reads as the primary action (chrome fill); Trial ("continue without
    // activating") stays the plain outlined default -- see chromeButtonProperty.
    activateButton.getProperties().set (ParaDynLookAndFeel::chromeButtonProperty, true);
    activateButton.onClick = [this] { showActivationDialog(); };
    licenseOverlay.addAndMakeVisible (activateButton);

    trialButton.onClick = [this]
    {
        licenseOverlayDismissed = true;
        updateLicenseOverlayVisibility();
        repaint();
    };
    licenseOverlay.addAndMakeVisible (trialButton);

    updateLicenseUi();
#endif

    setResizable (true, true);
    setResizeLimits (760, 600, 1800, 1200);
    // Lock to the mockup's own aspect ratio -- every rect in resized() is a ratio of
    // the same 1180x715 design box (see Mockup/PORTING.md), which only stays correct
    // (round knobs, not ellipses) if sx == sy at every window size.
    if (auto* constrainer = getConstrainer())
        constrainer->setFixedAspectRatio (designWidth / designHeight);
    setSize (1080, (int) (1080.0f * designHeight / designWidth));

    startTimerHz (10);
}

ParaDynAudioProcessorEditor::~ParaDynAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void ParaDynAudioProcessorEditor::timerCallback()
{
    const auto count = audioProcessor.getNumBands();
    if (count != lastBandCount)
        rebuildBandStrip();
    else
        updateBandButtonStates();

#if PARADYNEQ_ENABLE_LICENSING
    updateLicenseUi();
#endif
}

void ParaDynAudioProcessorEditor::rebuildBandStrip()
{
    lastBandCount = audioProcessor.getNumBands();
    bandButtons.clear();
    bandButtonSlots.clear();

    constexpr int buttonWidth = 74, buttonHeight = 26, spacing = 6;
    int x = spacing;
    for (auto slot : audioProcessor.getActiveBandSlots())
    {
        auto button = std::make_unique<BandStripButton> ("Band " + juce::String (slot + 1));
        button->setBounds (x, spacing, buttonWidth, buttonHeight);
        button->setToggleable (true);
        button->setClickingTogglesState (false);
        button->getProperties().set (ParaDynLookAndFeel::bandTabProperty, true);
        // Plain click selects the band; Ctrl/Cmd-click momentarily solos it for as
        // long as the mouse stays down -- see BandStripButton's mouseDown/mouseUp.
        button->onClick = [this, slot] { audioProcessor.setSelectedBand (slot); };
        button->onSoloEngage = [this, slot] (bool engage)
        {
            audioProcessor.setSolo (slot, engage);
            updateBandButtonStates();
        };
        bandStripContent.addAndMakeVisible (*button);
        bandButtons.push_back (std::move (button));
        bandButtonSlots.push_back (slot);
        x += buttonWidth + spacing;
    }
    addBandButton.setBounds (x, spacing, buttonHeight, buttonHeight);
    bandStripContent.setSize (x + buttonHeight + spacing, buttonHeight + spacing * 2);
    updateBandButtonStates();
}

void ParaDynAudioProcessorEditor::updateBandButtonStates()
{
    const auto selected = audioProcessor.getSelectedBand();
    for (size_t i = 0; i < bandButtons.size(); ++i)
    {
        const auto slot = bandButtonSlots[i];
        auto& button = *bandButtons[i];
        button.setToggleState (slot == selected, juce::dontSendNotification);
        button.getProperties().set (ParaDynLookAndFeel::soloedProperty, audioProcessor.isBandSoloed (slot));
        button.repaint();
    }
}

#if PARADYNEQ_ENABLE_LICENSING
void ParaDynAudioProcessorEditor::updateLicenseUi()
{
    auto& manager = audioProcessor.getLicenseManager();
    const auto status = manager.getStatus();

    if (status == licensing::LicenseStatus::TrialActive)
        licenseOverlayStatusLabel.setText (juce::String (manager.getDaysRemainingInTrial()) + " trial day(s) remaining",
                                            juce::dontSendNotification);
    else
        licenseOverlayStatusLabel.setText (manager.getUserVisibleStatusMessage(), juce::dontSendNotification);

    if (status == licensing::LicenseStatus::Activated || status == licensing::LicenseStatus::OfflineGracePeriod)
        licenseOverlayDismissed = false;

    updateLicenseOverlayVisibility();
}

void ParaDynAudioProcessorEditor::updateLicenseOverlayVisibility()
{
    const auto status = audioProcessor.getLicenseManager().getStatus();
    const auto visible = status != licensing::LicenseStatus::Activated
                       && status != licensing::LicenseStatus::OfflineGracePeriod
                       && ! licenseOverlayDismissed;

    for (auto* component : { (juce::Component*) &licenseOverlayTitleLabel,
                             (juce::Component*) &licenseOverlayStatusLabel,
                             (juce::Component*) &activateButton,
                             (juce::Component*) &trialButton })
        component->setVisible (visible);

    licenseOverlay.setVisible (visible);

    if (visible)
        licenseOverlay.toFront (false);
}

void ParaDynAudioProcessorEditor::showActivationDialog()
{
    auto dialog = std::make_unique<juce::AlertWindow> ("Activate ParaDyn EQ",
                                                        "Enter your license key.",
                                                        juce::AlertWindow::NoIcon);
    dialog->addTextEditor ("licenseKey", {}, "License Key");
    dialog->addButton ("Activate", 1, juce::KeyPress (juce::KeyPress::returnKey));
    dialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    dialog->enterModalState (true, juce::ModalCallbackFunction::create ([this, releasedDialog = dialog.release()] (int result)
    {
        std::unique_ptr<juce::AlertWindow> ownedDialog (releasedDialog);

        if (result != 1)
            return;

        const auto key = ownedDialog->getTextEditorContents ("licenseKey").trim();
        if (key.isEmpty())
            return;

        audioProcessor.getLicenseManager().startActivation (key, [this] (const licensing::LicenseManager::OperationResult&)
        {
            licenseOverlayDismissed = false;
            updateLicenseUi();
            repaint();
        });
    }));
}
#endif

void ParaDynAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (ParaDynLookAndFeel::background);
    if (lookAndFeel.backgroundImage.isValid())
        g.drawImage (lookAndFeel.backgroundImage, getLocalBounds().toFloat());

    // Two-colour title ("PARA DYN " + accent-coloured "EQ", matching the mockup's
    // gradient-text span) -- juce::AttributedString::draw() silently produced no
    // glyphs for this custom-loaded typeface in testing, so it's built from two plain
    // g.drawText() calls placed side by side instead, using the font's own measured
    // width to find where the second run starts. titleLabel itself stays
    // invisible/textless; only its bounds/font are used as the layout anchor.
    const auto titleFont = ParaDynFonts::title (titleLabel.getFont().getHeight());
    g.setFont (titleFont);
    auto titleArea = titleLabel.getBounds();
    const juce::String firstPart ("PARA DYN ");
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText (titleFont, firstPart, 0.0f, 0.0f);
    const auto firstWidth = juce::roundToInt (glyphs.getBoundingBox (0, -1, true).getWidth());
    g.setColour (ParaDynLookAndFeel::textPrimary);
    g.drawText (firstPart, titleArea, juce::Justification::centredLeft, false);
    g.setColour (ParaDynLookAndFeel::accent);
    g.drawText ("EQ", titleArea.withTrimmedLeft (firstWidth), juce::Justification::centredLeft, false);

    if (! mothLogo.isNull())
        g.drawImage (mothLogo, logoBounds.toFloat());
}

void ParaDynAudioProcessorEditor::resized()
{
    const auto sx = (float) getWidth() / designWidth;
    const auto sy = (float) getHeight() / designHeight;
    auto bounds = [sx, sy] (float x, float y, float w, float h)
    {
        return juce::Rectangle<int> (juce::roundToInt (x * sx), juce::roundToInt (y * sy),
                                      juce::roundToInt (w * sx), juce::roundToInt (h * sy));
    };

    const float headerH = designHeight * (78.0f / 715.0f);
    const float sidePad = designWidth * 0.0246f;
    const float brandInset = 22.0f;

    // --- Header ---------------------------------------------------------------
    const float logoH = headerH * 0.37f;
    const float logoW = logoH * (592.0f / 380.0f);
    logoBounds = bounds ((designWidth - logoW) * 0.5f, (headerH - logoH) * 0.5f, logoW, logoH);

    manufacturerLabel.setBounds (bounds (sidePad + brandInset, 10.0f, 200.0f, 14.0f));
    titleLabel.setBounds (bounds (sidePad + brandInset, headerH * 0.38f, 260.0f, headerH * 0.56f));
    manufacturerLabel.setFont (ParaDynFonts::uiLight (10.0f * sy));
    titleLabel.setFont (ParaDynFonts::title (22.0f * sy));

    // Header controls sit right-aligned, ending brandInset px in from the side padding
    // (the background art's corner screw), same idea mirrored from the brand side.
    float rx = designWidth - sidePad - brandInset;
    // ParaDynLookAndFeel::drawRotarySlider derives its tick radius from the component's
    // own bounds (fullRadius = min(w,h)*0.5) and never draws past that, so the knob
    // component is sized straight to the mockup's 34px face -- no oversizing needed.
    const float knobW = 64.0f;
    const float knobFaceH = 34.0f;
    rx -= knobW;
    // Align the rotary face's own vertical centre with the Range/Oversampling combo
    // boxes' centre (headerH*0.42 top + half their headerH*0.4 height), rather than
    // the header band's own centre -- that previously left the knob sitting visibly
    // higher than its neighbours.
    const float comboCentreY = headerH * 0.42f + headerH * 0.4f * 0.5f;
    const float knobY = comboCentreY - knobFaceH * 0.5f;
    // Same top and height as the Range/Oversampling captions (headerH*0.06 / *0.34) so
    // its vertical centre actually lines up with theirs -- a shorter box here, even at
    // the same top y, centres its text higher since Label centres within its own bounds.
    outputLabel.setBounds (bounds (rx, headerH * 0.06f, knobW, headerH * 0.34f));
    outputKnob.setBounds (bounds (rx, knobY, knobW, knobFaceH));
    // The value editor overlay isn't part of the header's permanent layout (only shown
    // for the duration of an edit), so it's fine for its bounds to extend a little below
    // the header row into the gap above the spectrum panel.
    outputValueEditor.setBounds (bounds (rx, knobY + knobFaceH + 2.0f, knobW, 16.0f));
    rx -= 14.0f;

    const float oversamplingW = 74.0f;
    rx -= oversamplingW;
    oversamplingBox.setBounds (bounds (rx, headerH * 0.42f, oversamplingW, headerH * 0.4f));
    oversamplingLabel.setBounds (bounds (rx, headerH * 0.06f, oversamplingW, headerH * 0.34f));
    rx -= 10.0f;

    const float rangeW = 74.0f;
    rx -= rangeW;
    rangeBox.setBounds (bounds (rx, headerH * 0.42f, rangeW, headerH * 0.4f));
    rangeLabel.setBounds (bounds (rx, headerH * 0.06f, rangeW, headerH * 0.34f));
    rx -= 14.0f;

    const float bypassW = 70.0f;
    rx -= bypassW;
    bypassButton.setBounds (bounds (rx, headerH * 0.36f, bypassW, headerH * 0.34f));

    // --- Content rows (637px design box = 715 - 78 header, see PORTING.md) ----
    float rowY = headerH;
    rowY += 3.0f; // gap
    const float spectrumH = 339.0f;
    spectrumAnalyzer.setBounds (bounds (sidePad, rowY, designWidth - sidePad * 2.0f, spectrumH));
    rowY += spectrumH + 8.0f;

    const float tabsH = 34.0f;
    bandStripViewport.setBounds (bounds (sidePad, rowY, designWidth - sidePad * 2.0f, tabsH));
    rowY += tabsH + 6.0f;

    const float editorH = 192.0f;
    bandEditor.setBounds (bounds (sidePad, rowY, designWidth - sidePad * 2.0f, editorH));

#if PARADYNEQ_ENABLE_LICENSING
    licenseOverlay.setBounds (getLocalBounds());

    // popup comes from the overlay's own getPopupBounds() -- the single source of truth
    // its paint() also uses, so the drawn panel and the labels/buttons positioned here
    // can never desync.
    auto popup = licenseOverlay.getPopupBounds().toNearestInt().reduced (16, 18);
    licenseOverlayTitleLabel.setBounds (popup.removeFromTop (30));
    popup.removeFromTop (6);
    licenseOverlayStatusLabel.setBounds (popup.removeFromTop (40));

    auto licenseButtonRow = popup.removeFromBottom (32).reduced (30, 0);
    constexpr int licenseButtonGap = 10;
    const auto licenseButtonWidth = (licenseButtonRow.getWidth() - licenseButtonGap) / 2;
    activateButton.setBounds (licenseButtonRow.removeFromLeft (licenseButtonWidth));
    licenseButtonRow.removeFromLeft (licenseButtonGap);
    trialButton.setBounds (licenseButtonRow.removeFromLeft (licenseButtonWidth));
#endif

    repaint();
    rebuildBandStrip();
}
