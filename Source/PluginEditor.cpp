#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "Parameters.h"
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
}

ParaDynAudioProcessorEditor::ParaDynAudioProcessorEditor (ParaDynAudioProcessor& owner)
    : AudioProcessorEditor (owner), audioProcessor (owner), spectrumAnalyzer (owner), bandEditor (owner)
{
    titleLabel.setText ("ParaDyn EQ", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    addAndMakeVisible (titleLabel);

    addAndMakeVisible (bypassButton);
    bypassAttachment = std::make_unique<ButtonAttachment> (audioProcessor.apvts, ParamIDs::bypass, bypassButton);

    outputSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    outputSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 20);
    outputLabel.setText ("Output", juce::dontSendNotification);
    addAndMakeVisible (outputSlider);
    addAndMakeVisible (outputLabel);
    outputAttachment = std::make_unique<SliderAttachment> (audioProcessor.apvts, ParamIDs::output, outputSlider);

    oversamplingBox.addItemList (oversamplingNames(), 1);
    addAndMakeVisible (oversamplingBox);
    oversamplingAttachment = std::make_unique<ComboAttachment> (audioProcessor.apvts, ParamIDs::oversampling, oversamplingBox);

    // Display-only zoom for the spectrum panel's gain axis -- not an audio parameter,
    // so it's wired directly rather than through an APVTS attachment. A tighter range
    // gives more pixels per dB, i.e. more precise node dragging.
    rangeLabel.setText ("Range", juce::dontSendNotification);
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

    setResizable (true, true);
    setResizeLimits (760, 600, 1800, 1200);
    setSize (1080, 700);

    startTimerHz (10);
}

ParaDynAudioProcessorEditor::~ParaDynAudioProcessorEditor() { stopTimer(); }

void ParaDynAudioProcessorEditor::timerCallback()
{
    const auto count = audioProcessor.getNumBands();
    if (count != lastBandCount)
        rebuildBandStrip();
    else
        updateBandButtonStates();
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
    static const juce::Colour soloColour { 0xffffcc33 };
    const auto selected = audioProcessor.getSelectedBand();
    for (size_t i = 0; i < bandButtons.size(); ++i)
    {
        const auto slot = bandButtonSlots[i];
        auto& button = *bandButtons[i];
        button.setToggleState (slot == selected, juce::dontSendNotification);
        if (audioProcessor.isBandSoloed (slot))
            button.setColour (juce::TextButton::buttonColourId, soloColour);
        else
            button.removeColour (juce::TextButton::buttonColourId);
    }
}

void ParaDynAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff07080a));
}

void ParaDynAudioProcessorEditor::resized()
{
    auto bounds = getLocalBounds().reduced (10);

    auto topBar = bounds.removeFromTop (32);
    titleLabel.setBounds (topBar.removeFromLeft (160));
    bypassButton.setBounds (topBar.removeFromLeft (80));
    topBar.removeFromLeft (12);
    oversamplingBox.setBounds (topBar.removeFromRight (90));
    topBar.removeFromRight (8);
    outputLabel.setBounds (topBar.removeFromRight (52));
    outputSlider.setBounds (topBar.removeFromRight (180));
    topBar.removeFromRight (12);
    rangeBox.setBounds (topBar.removeFromRight (100));
    topBar.removeFromRight (6);
    rangeLabel.setBounds (topBar.removeFromRight (44));

    bounds.removeFromTop (8);

    // The band editor's dynamic-mode controls need a fixed minimum height (title +
    // type row + 3 main knobs + toggle row + 5 dynamic knobs + remove button), so it
    // gets a fixed slice from the bottom and the spectrum analyzer takes whatever
    // remains -- rather than a proportional split that could clip the dynamic knobs
    // on a small window.
    bandEditor.setBounds (bounds.removeFromBottom (300));
    bounds.removeFromBottom (8);
    bandStripViewport.setBounds (bounds.removeFromBottom (38));
    bounds.removeFromBottom (8);
    spectrumAnalyzer.setBounds (bounds);

    rebuildBandStrip();
}
