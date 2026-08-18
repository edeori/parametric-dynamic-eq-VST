#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// Moth Production house skin for ParaDyn EQ, ported 1:1 from the approved
// Mockup/paradyneq-ui.html reference (see Mockup/PORTING.md for the ratio table this
// was built against). Cyan/lavender accent pair is this plugin's own palette (sampled
// from the mockup), everything else -- background/panel/text colours, the knob-image
// rotation technique, the moth-logo minification fix -- follows the shared pattern
// documented in MothProduction-Shared-VST/UI/patterns/header-layout.md and the
// MultiSqueezeLookAndFeel precedent.
class ParaDynLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    ParaDynLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int, int, int, int, float, float, float, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool, bool) override;
    void drawComboBox (juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Font getPopupMenuFont() override;

    // Loaded once (generous intermediate size, dilated + hard-thresholded) per the
    // documented moth_logo.png minification fix -- see header-layout.md. Callers scale
    // the returned image down to their own final draw size, never straight from source.
    static juce::Image loadMothLogo (int pixelHeight);

    // Marks a TextButton as belonging to the horizontal band-tab strip so
    // drawButtonBackground/drawButtonText can use the strip's own selected/solo chrome
    // instead of the generic button treatment.
    static constexpr const char* bandTabProperty = "bandTab";
    static constexpr const char* soloedProperty = "soloed";
    // Marks a TextButton as the primary/chrome action in its group (e.g. the licensing
    // overlay's Activate button) so it always renders filled, regardless of toggle state --
    // every other plain TextButton (Add band, Remove, Trial) keeps the default outlined look.
    static constexpr const char* chromeButtonProperty = "chromeButton";

    static const juce::Colour background, panel, accent, accentHot, accent2, accent2Hot,
        textPrimary, textDim;

    juce::Image backgroundImage;
    juce::Image knobImage;
};
