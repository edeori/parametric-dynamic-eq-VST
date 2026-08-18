#include "ParaDynLookAndFeel.h"
#include "ParaDynFonts.h"
#include "BinaryData.h"
#include <cmath>

const juce::Colour ParaDynLookAndFeel::background { 0xff17171a };
const juce::Colour ParaDynLookAndFeel::panel { 0xff232326 };
const juce::Colour ParaDynLookAndFeel::accent { 0xff56d8f5 };
const juce::Colour ParaDynLookAndFeel::accentHot { 0xff8be9ff };
const juce::Colour ParaDynLookAndFeel::accent2 { 0xff9a91d6 };
const juce::Colour ParaDynLookAndFeel::accent2Hot { 0xffc3bbf0 };
const juce::Colour ParaDynLookAndFeel::textPrimary { 0xffececec };
const juce::Colour ParaDynLookAndFeel::textDim { 0xff9a9a9e };

ParaDynLookAndFeel::ParaDynLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, accent);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff0c0c0e));
    setColour (juce::Slider::textBoxOutlineColourId, accent.withAlpha (0.4f));
    setColour (juce::ComboBox::backgroundColourId, panel);
    setColour (juce::ComboBox::textColourId, textPrimary);
    setColour (juce::ComboBox::outlineColourId, accent.withAlpha (0.35f));
    setColour (juce::ComboBox::arrowColourId, accent);
    setColour (juce::PopupMenu::backgroundColourId, juce::Colour (0xff1c1c20));
    setColour (juce::PopupMenu::textColourId, textPrimary);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, accent.withAlpha (0.3f));
    setColour (juce::Label::textColourId, textPrimary);
    setColour (juce::ToggleButton::textColourId, textPrimary);
    setColour (juce::ToggleButton::tickColourId, accentHot);
    setColour (juce::ToggleButton::tickDisabledColourId, textDim);

    backgroundImage = juce::ImageFileFormat::loadFrom (BinaryData::background_png, (size_t) BinaryData::background_pngSize);
    knobImage = juce::ImageFileFormat::loadFrom (BinaryData::knob_png, (size_t) BinaryData::knob_pngSize);
}

void ParaDynLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                            float position, float start, float end, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
    const auto centre = bounds.getCentre();
    // Ticks/arc must stay inside the component's own clip bounds, so the visual knob
    // face is drawn smaller than the full slider bounds (0.82 factor) -- callers size
    // the Slider component itself larger than the desired visual face diameter to
    // compensate, matching the sibling LookAndFeel pattern this was ported from.
    const auto fullRadius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto radius = fullRadius * 0.76f;
    const auto angle = start + position * (end - start);

    juce::Path inactiveArc;
    inactiveArc.addCentredArc (centre.x, centre.y, fullRadius * 0.82f, fullRadius * 0.82f, 0.0f, start, end, true);
    g.setColour (juce::Colour (0xff33333a));
    g.strokePath (inactiveArc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    if (slider.isEnabled())
    {
        juce::Path valueArc;
        valueArc.addCentredArc (centre.x, centre.y, fullRadius * 0.82f, fullRadius * 0.82f, 0.0f, start, angle, true);
        g.setColour (accent);
        g.strokePath (valueArc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Procedural tick ring, matching moth-ui.css's ".moth-knob::before" repeating-conic
    // ring: 16 static ticks evenly spaced across the same start..end sweep.
    constexpr int numTicks = 16;
    for (int tick = 0; tick < numTicks; ++tick)
    {
        const auto tickAngle = start + (float) tick / (float) (numTicks - 1) * (end - start);
        const auto inner = juce::Point<float> (centre.x + fullRadius * 0.87f * std::sin (tickAngle),
                                                centre.y - fullRadius * 0.87f * std::cos (tickAngle));
        const auto outer = juce::Point<float> (centre.x + fullRadius * std::sin (tickAngle),
                                                centre.y - fullRadius * std::cos (tickAngle));
        g.setColour (tickAngle <= angle && slider.isEnabled() ? accent : juce::Colour (0xff3a3a42));
        g.drawLine ({ inner, outer }, 1.3f);
    }

    auto knobBounds = juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (centre);
    if (knobImage.isValid())
    {
        g.saveState();
        juce::Path clip;
        clip.addEllipse (knobBounds);
        g.reduceClipRegion (clip);
        // The pointer/glow is baked into the source art (verified pixel-centred, see
        // MothProduction-Shared-VST/UI/Components/README.md), so the whole image
        // rotates to indicate value rather than drawing a separate needle on top.
        g.addTransform (juce::AffineTransform::rotation (angle - (start + end) * 0.5f, centre.x, centre.y));
        g.setOpacity (slider.isEnabled() ? 1.0f : 0.5f);
        g.drawImage (knobImage, knobBounds);
        g.restoreState();
    }
    else
    {
        g.setColour (panel.brighter (0.15f));
        g.fillEllipse (knobBounds);
    }
}

void ParaDynLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                                const juce::Colour&, bool highlighted, bool down)
{
    auto bounds = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto isBandTab = (bool) button.getProperties().getWithDefault (bandTabProperty, false);
    const auto isSoloed = (bool) button.getProperties().getWithDefault (soloedProperty, false);
    const auto isChrome = (bool) button.getProperties().getWithDefault (chromeButtonProperty, false);
    const auto on = button.getToggleState() || isChrome;

    if (isBandTab)
    {
        if (isSoloed)
            g.setColour (juce::Colour (0xffffcc33).withAlpha (0.85f));
        else if (on)
            g.setColour (accent.withAlpha (0.85f));
        else
            g.setColour (juce::Colour (0xff1b1b1f));
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (isSoloed ? juce::Colour (0xffffcc33) : (on ? accentHot : juce::Colour (0xff3a3a42)));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }
    else
    {
        g.setColour (on ? accent.withAlpha (0.88f) : juce::Colour (0xff1b1b1f));
        g.fillRoundedRectangle (bounds, 5.0f);
        g.setColour (on ? accent : juce::Colour (0xff3a3a42));
        g.drawRoundedRectangle (bounds, 5.0f, 1.0f);
    }

    if (highlighted || down)
    {
        g.setColour (juce::Colours::white.withAlpha (down ? 0.14f : 0.07f));
        g.fillRoundedRectangle (bounds, isBandTab ? 4.0f : 5.0f);
    }
}

void ParaDynLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto isBandTab = (bool) button.getProperties().getWithDefault (bandTabProperty, false);
    const auto isSoloed = (bool) button.getProperties().getWithDefault (soloedProperty, false);
    const auto isChrome = (bool) button.getProperties().getWithDefault (chromeButtonProperty, false);
    g.setFont (getTextButtonFont (button, button.getHeight()));
    if (isBandTab)
        g.setColour ((button.getToggleState() || isSoloed) ? background : textPrimary);
    else
        g.setColour ((button.getToggleState() || isChrome) ? background : accent);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (5, 2), juce::Justification::centred, 1);
}

void ParaDynLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool, bool)
{
    auto bounds = button.getLocalBounds().toFloat();
    const auto boxSize = juce::jmin (16.0f, bounds.getHeight() - 2.0f);
    auto box = juce::Rectangle<float> (bounds.getX() + 1.0f, bounds.getCentreY() - boxSize * 0.5f, boxSize, boxSize);

    g.setColour (button.getToggleState() ? accent.withAlpha (0.85f) : juce::Colour (0xff1b1b1f));
    g.fillRoundedRectangle (box, 3.0f);
    g.setColour (button.getToggleState() ? accentHot : juce::Colour (0xff3a3a42));
    g.drawRoundedRectangle (box, 3.0f, 1.0f);

    if (button.getToggleState())
    {
        juce::Path tick;
        tick.startNewSubPath (box.getX() + box.getWidth() * 0.2f, box.getCentreY());
        tick.lineTo (box.getX() + box.getWidth() * 0.42f, box.getBottom() - box.getHeight() * 0.22f);
        tick.lineTo (box.getRight() - box.getWidth() * 0.18f, box.getY() + box.getHeight() * 0.22f);
        g.strokePath (tick, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    if (button.getButtonText().isNotEmpty())
    {
        g.setColour (button.findColour (juce::ToggleButton::textColourId));
        g.setFont (ParaDynFonts::uiMedium (juce::jmin (13.0f, bounds.getHeight() * 0.5f)));
        g.drawFittedText (button.getButtonText(),
                           bounds.getSmallestIntegerContainer().withTrimmedLeft ((int) (box.getRight() + 6.0f)),
                           juce::Justification::centredLeft, 1);
    }
}

void ParaDynLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                        int, int, int, int, juce::ComboBox& box)
{
    auto bounds = juce::Rectangle<float> (0, 0, (float) width, (float) height).reduced (0.5f);
    g.setColour (panel);
    g.fillRoundedRectangle (bounds, 4.0f);
    g.setColour (accent.withAlpha (0.4f));
    g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

    auto arrowZone = juce::Rectangle<float> ((float) width - 18.0f, 0.0f, 14.0f, (float) height);
    juce::Path arrow;
    arrow.startNewSubPath (arrowZone.getX(), arrowZone.getCentreY() - 3.0f);
    arrow.lineTo (arrowZone.getCentreX(), arrowZone.getCentreY() + 3.0f);
    arrow.lineTo (arrowZone.getRight(), arrowZone.getCentreY() - 3.0f);
    g.setColour (box.findColour (juce::ComboBox::arrowColourId));
    g.strokePath (arrow, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

juce::Font ParaDynLookAndFeel::getLabelFont (juce::Label& label) { return label.getFont(); }

juce::Font ParaDynLookAndFeel::getTextButtonFont (juce::TextButton&, int height)
{
    return ParaDynFonts::uiMedium (juce::jmin (13.0f, height * 0.5f));
}

juce::Font ParaDynLookAndFeel::getComboBoxFont (juce::ComboBox&) { return ParaDynFonts::uiMedium (13.0f); }
juce::Font ParaDynLookAndFeel::getPopupMenuFont() { return ParaDynFonts::uiLight (14.0f); }

juce::Image ParaDynLookAndFeel::loadMothLogo (int pixelHeight)
{
    auto raw = juce::ImageFileFormat::loadFrom (BinaryData::moth_logo_png, (size_t) BinaryData::moth_logo_pngSize);
    int width = juce::roundToInt ((float) pixelHeight * (592.0f / 380.0f));
    auto scaled = raw.rescaled (width, pixelHeight, juce::Graphics::highResamplingQuality)
                      .convertedToFormat (juce::Image::ARGB);

    juce::Image result (juce::Image::ARGB, width, pixelHeight, true);
    juce::Image::BitmapData srcData (scaled, juce::Image::BitmapData::readOnly);
    juce::Image::BitmapData dstData (result, juce::Image::BitmapData::writeOnly);

    constexpr juce::uint8 threshold = 40;
    const int dilate = juce::jmax (1, pixelHeight / 60);

    for (int y = 0; y < dstData.height; ++y)
        for (int x = 0; x < dstData.width; ++x)
        {
            bool opaque = false;
            for (int dy = -dilate; dy <= dilate && ! opaque; ++dy)
                for (int dx = -dilate; dx <= dilate && ! opaque; ++dx)
                {
                    int sx = x + dx, sy = y + dy;
                    if (sx >= 0 && sy >= 0 && sx < srcData.width && sy < srcData.height
                        && ((juce::PixelARGB*) srcData.getPixelPointer (sx, sy))->getAlpha() >= threshold)
                        opaque = true;
                }
            auto* dst = (juce::PixelARGB*) dstData.getPixelPointer (x, y);
            juce::uint8 v = opaque ? 255 : 0;
            dst->setARGB (v, v, v, v);
        }
    return result;
}
