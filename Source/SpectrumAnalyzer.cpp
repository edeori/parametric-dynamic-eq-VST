#include "SpectrumAnalyzer.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <cmath>
#include <complex>

namespace
{
    constexpr float graphMinDb = -72.0f;
    constexpr float graphMaxDb = 12.0f;
    constexpr float releaseSmoothing = 0.76f;

    // Radically different, non-green hues so input/output are never confused at a
    // glance: input = electric blue, output = hot magenta.
    const juce::Colour inputColour { 0xff2e86ff };
    const juce::Colour outputColour { 0xffff2e8c };
    const juce::Colour referenceColour { 0xff42d9ed };
    const juce::Colour targetColour { 0xffffa052 };
    const juce::Colour responseColour { 0xffffffff };
    const juce::Colour eqCorrectionColour { 0xff82e040 }; // lime green: static EQ correction on gain scale
    const juce::Colour eqDynamicColour    { 0xffffb930 }; // amber: instantaneous dynamic effective response
    const juce::Colour nodeColour { 0xffb9c2cc };
    const juce::Colour selectedNodeColour { 0xffffcc33 };
}

SpectrumAnalyzer::SpectrumAnalyzer (ParaDynAudioProcessor& owner) : processor (owner)
{
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
    startTimerHz (30);
}

SpectrumAnalyzer::~SpectrumAnalyzer() { stopTimer(); }

juce::Rectangle<float> SpectrumAnalyzer::graphBounds() const
{
    auto bounds = getLocalBounds().toFloat();
    bounds.removeFromTop (10.0f);
    bounds.removeFromBottom (22.0f);
    bounds.removeFromLeft (28.0f);
    bounds.removeFromRight (38.0f);
    return bounds;
}

float SpectrumAnalyzer::frequencyToX (float frequency) const
{
    const auto bounds = graphBounds();
    const auto proportion = std::log (juce::jlimit (ParaDynConstants::minFrequencyHz,
                                                     ParaDynConstants::maxFrequencyHz, frequency)
                                       / ParaDynConstants::minFrequencyHz)
                             / std::log (ParaDynConstants::maxFrequencyHz / ParaDynConstants::minFrequencyHz);
    return bounds.getX() + proportion * bounds.getWidth();
}

float SpectrumAnalyzer::xToFrequency (float x) const
{
    const auto bounds = graphBounds();
    const auto proportion = juce::jlimit (0.0f, 1.0f, (x - bounds.getX()) / bounds.getWidth());
    return ParaDynConstants::minFrequencyHz
           * std::pow (ParaDynConstants::maxFrequencyHz / ParaDynConstants::minFrequencyHz, proportion);
}

float SpectrumAnalyzer::spectrumDbToY (float db) const
{
    return juce::jmap (juce::jlimit (graphMinDb, graphMaxDb, db), graphMinDb, graphMaxDb,
                       graphBounds().getBottom(), graphBounds().getY());
}

float SpectrumAnalyzer::gainToY (float gain) const
{
    return juce::jmap (juce::jlimit (-gainRangeDb, gainRangeDb, gain), -gainRangeDb, gainRangeDb,
                       graphBounds().getBottom(), graphBounds().getY());
}

float SpectrumAnalyzer::yToGain (float y) const
{
    return juce::jlimit (-gainRangeDb, gainRangeDb,
                         juce::jmap (y, graphBounds().getBottom(), graphBounds().getY(), -gainRangeDb, gainRangeDb));
}

void SpectrumAnalyzer::setGainRangeDb (float newRangeDb) noexcept
{
    gainRangeDb = juce::jlimit (1.0f, 48.0f, newRangeDb);
    repaint();
}

juce::String SpectrumAnalyzer::formatFrequency (float frequency)
{
    return frequency >= 1000.0f ? juce::String (frequency / 1000.0f, 2) + " kHz"
                                : juce::String (juce::roundToInt (frequency)) + " Hz";
}

int SpectrumAnalyzer::bandNodeNear (float x, float y) const
{
    int nearest = -1;
    auto distance = 12.0f;
    for (auto slot : processor.getActiveBandSlots())
    {
        const auto p = processor.getBandParams (slot);
        const auto nodeX = frequencyToX (p.frequencyHz);
        const auto nodeY = gainToY (p.gainDb);
        const auto candidate = std::hypot (nodeX - x, nodeY - y);
        if (candidate < distance) { nearest = slot; distance = candidate; }
    }
    return nearest;
}

void SpectrumAnalyzer::mouseDown (const juce::MouseEvent& event)
{
    const auto hitBand = bandNodeNear (event.position.x, event.position.y);
    freqGestureStarted = false;
    gainGestureStarted = false;

    // Cmd-click (or, on macOS, Ctrl-click / a trackpad's secondary-click gesture --
    // JUCE reports both of those as isPopupMenu(), *not* isCtrlDown(): macOS itself
    // reinterprets a Control-click as a right-click at the OS level, so checking
    // isCtrlDown() directly never fires there) momentarily solos the node's band for
    // as long as the mouse stays down, released in mouseUp -- and the node stays
    // draggable at the same time, so you can preview-adjust it by ear. Cmd allows free
    // frequency+gain movement; the popup-menu gesture (Ctrl/secondary-click) locks the
    // drag to gain only, matching the "vertical only" request.
    if (hitBand >= 0 && (event.mods.isCommandDown() || event.mods.isPopupMenu()))
    {
        momentarySoloSlot = hitBand;
        processor.setSolo (hitBand, true);
        draggedBand = hitBand;
        draggingGain = true;
        verticalOnlyDrag = event.mods.isPopupMenu();
        processor.setSelectedBand (hitBand);
        repaint();
        return;
    }

    draggedBand = hitBand;
    draggingGain = draggedBand >= 0;
    verticalOnlyDrag = false;
    if (draggedBand >= 0)
        processor.setSelectedBand (draggedBand);
    repaint();
}

void SpectrumAnalyzer::mouseDrag (const juce::MouseEvent& event)
{
    if (draggedBand < 0 || ! draggingGain)
        return;

    if (! verticalOnlyDrag)
    {
        if (auto* freqParam = processor.apvts.getParameter (ParamIDs::frequency (draggedBand)))
        {
            if (! freqGestureStarted) { freqParam->beginChangeGesture(); freqGestureStarted = true; }
            freqParam->setValueNotifyingHost (processor.apvts.getParameterRange (freqParam->getParameterID())
                                                  .convertTo0to1 (xToFrequency (event.position.x)));
        }
    }
    if (auto* gainParam = processor.apvts.getParameter (ParamIDs::gain (draggedBand)))
    {
        if (! gainGestureStarted) { gainParam->beginChangeGesture(); gainGestureStarted = true; }
        gainParam->setValueNotifyingHost (processor.apvts.getParameterRange (gainParam->getParameterID())
                                              .convertTo0to1 (yToGain (event.position.y)));
    }
    repaint();
}

void SpectrumAnalyzer::mouseUp (const juce::MouseEvent&)
{
    if (momentarySoloSlot >= 0)
    {
        processor.setSolo (momentarySoloSlot, false);
        momentarySoloSlot = -1;
    }
    if (freqGestureStarted)
        if (auto* freqParam = processor.apvts.getParameter (ParamIDs::frequency (draggedBand)))
            freqParam->endChangeGesture();
    if (gainGestureStarted)
        if (auto* gainParam = processor.apvts.getParameter (ParamIDs::gain (draggedBand)))
            gainParam->endChangeGesture();
    draggedBand = -1;
    draggingGain = false;
    verticalOnlyDrag = false;
    freqGestureStarted = false;
    gainGestureStarted = false;
    repaint();
}

void SpectrumAnalyzer::mouseWheelMove (const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    // Scrolling directly over a node adjusts that band's Q; scrolling anywhere else on
    // the graph adjusts the currently-selected band's Q instead, so a click-then-scroll
    // workflow works without needing to stay pixel-perfect on the node.
    auto slot = bandNodeNear (event.position.x, event.position.y);
    if (slot < 0)
        slot = processor.getSelectedBand();
    if (slot < 0 || wheel.deltaY == 0.0f)
        return;

    if (auto* qParam = processor.apvts.getParameter (ParamIDs::q (slot)))
    {
        const auto range = processor.apvts.getParameterRange (qParam->getParameterID());
        const auto currentQ = processor.getBandParams (slot).q;
        // Multiplicative step so the same scroll gesture feels equally sized across
        // the whole (skewed, 0.1-18) Q range, not just near one end of it.
        const auto factor = std::exp (wheel.deltaY * 2.0f);
        const auto newQ = juce::jlimit (range.start, range.end, currentQ * factor);
        qParam->beginChangeGesture();
        qParam->setValueNotifyingHost (range.convertTo0to1 (newQ));
        qParam->endChangeGesture();
    }
    repaint();
}

void SpectrumAnalyzer::mouseDoubleClick (const juce::MouseEvent& event)
{
    if (const auto slot = bandNodeNear (event.position.x, event.position.y); slot >= 0)
    {
        processor.removeBand (slot);
    }
    else if (graphBounds().contains (event.position))
    {
        processor.addBand (xToFrequency (event.position.x));
    }
    repaint();
}

void SpectrumAnalyzer::timerCallback()
{
    bool changed = false;
    if (processor.getInputSpectrumFifo().pullLatestBlock (inputSamples))
    {
        updatePath (inputSamples, inputPath, inputMagnitudes);
        changed = true;
    }
    if (processor.getOutputSpectrumFifo().pullLatestBlock (outputSamples))
    {
        updatePath (outputSamples, outputPath, outputMagnitudes);
        changed = true;
    }
    updateResponseCurve();
    juce::ignoreUnused (changed);
    repaint();
}

void SpectrumAnalyzer::updatePath (const std::array<float, SpectrumFifo::fftSize>& samples, juce::Path& path,
                                    std::array<float, SpectrumFifo::fftSize / 2>& magnitudes)
{
    std::fill (fftData.begin(), fftData.end(), 0.0f);
    std::copy (samples.begin(), samples.end(), fftData.begin());
    window.multiplyWithWindowingTable (fftData.data(), (size_t) SpectrumFifo::fftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    for (int bin = 0; bin < SpectrumFifo::fftSize / 2; ++bin)
    {
        const auto value = fftData[(size_t) bin] * 2.0f / (float) SpectrumFifo::fftSize;
        magnitudes[(size_t) bin] = value > magnitudes[(size_t) bin]
                                     ? value : magnitudes[(size_t) bin] * releaseSmoothing;
    }

    const auto bounds = graphBounds();
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    path.clear();
    for (int pixel = 0; pixel <= (int) bounds.getWidth(); ++pixel)
    {
        const auto x = bounds.getX() + (float) pixel;
        const auto freqLo = xToFrequency (x - 0.5f);
        const auto freqHi = xToFrequency (x + 0.5f);
        const auto maxBin = SpectrumFifo::fftSize / 2 - 1;
        const auto binPosLo = freqLo * (float) SpectrumFifo::fftSize / (float) sampleRate;
        const auto binPosHi = freqHi * (float) SpectrumFifo::fftSize / (float) sampleRate;

        float value;
        if (binPosHi - binPosLo >= 1.0f)
        {
            auto binLo = juce::jlimit (0, maxBin, juce::roundToInt (binPosLo));
            auto binHi = juce::jlimit (0, maxBin, juce::roundToInt (binPosHi));
            if (binHi < binLo)
                std::swap (binLo, binHi);
            float sum = 0.0f;
            for (int bin = binLo; bin <= binHi; ++bin)
                sum += magnitudes[(size_t) bin];
            value = sum / (float) (binHi - binLo + 1);
        }
        else
        {
            const auto binPos = juce::jlimit (0.0f, (float) maxBin, (binPosLo + binPosHi) * 0.5f);
            const auto binLo = (int) binPos;
            const auto binHi = juce::jmin (maxBin, binLo + 1);
            const auto frac = binPos - (float) binLo;
            value = magnitudes[(size_t) binLo] + (magnitudes[(size_t) binHi] - magnitudes[(size_t) binLo]) * frac;
        }

        const auto db = juce::Decibels::gainToDecibels (value, graphMinDb);
        const auto y = juce::jmap (juce::jlimit (graphMinDb, graphMaxDb, db), graphMinDb, graphMaxDb,
                                   bounds.getBottom(), bounds.getY());
        if (pixel == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
}

// Standard RBJ Audio EQ Cookbook analytic magnitude response, evaluated purely for the
// on-screen curve -- the audio path itself always runs through SVFilter (see EQBand),
// not this. The two topologies are close enough for the same freq/gain/Q that this is
// a faithful visual guide without needing to numerically settle the real filter.
float SpectrumAnalyzer::bandMagnitudeResponseDb (const EQBand::Params& p, float frequencyHz) const
{
    const auto sampleRate = processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 44100.0;
    // Bell bands: use the exact TPT SVF formula at the actual oversampled processing rate.
    // All other filter types keep the existing RBJ visual approximation (per-task scope).
    if (p.filterType == FilterType::Bell)
    {
        const auto factor = juce::jlimit (0, 3, juce::roundToInt (
            processor.apvts.getRawParameterValue (ParamIDs::oversampling)->load()));
        return (float) MatchEQ::responseDb ({ p.frequencyHz, p.gainDb, p.q },
                                            (double) frequencyHz,
                                            sampleRate * (double) (1 << factor));
    }
    const auto w0 = juce::MathConstants<double>::twoPi * (double) p.frequencyHz / sampleRate;
    const auto q = juce::jmax (0.05, (double) p.q);
    const auto alpha = std::sin (w0) / (2.0 * q);
    const auto cosw0 = std::cos (w0);
    const auto A = std::pow (10.0, (double) p.gainDb / 40.0);
    const auto sqrtA = std::sqrt (A);
    // Shelf filters use RBJ's S (shelf slope) parameterization, not the plain Q-alpha
    // used for Bell/HP/LP -- feeding sin(w0)/(2Q) into the shelf b/a formulas directly
    // (a plausible-looking shortcut) was measured elsewhere to produce up to +37dB of
    // overshoot at high Q on a shelf, since Q doesn't map onto "shelf steepness" the
    // way it does onto "peak bandwidth". S is derived from Q and clamped to (0, 1] --
    // RBJ's alpha-via-S formula takes sqrt() of a negative value for S>1.
    const auto shelfSlope = juce::jlimit (0.1, 1.0, q / 2.0);
    const auto alphaShelf = std::sin (w0) / 2.0 * std::sqrt ((A + 1.0 / A) * (1.0 / shelfSlope - 1.0) + 2.0);

    double b0, b1, b2, a0, a1, a2;
    switch (p.filterType)
    {
        case FilterType::LowShelf:
            b0 = A * ((A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
            b1 = 2 * A * ((A - 1) - (A + 1) * cosw0);
            b2 = A * ((A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
            a0 = (A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
            a1 = -2 * ((A - 1) + (A + 1) * cosw0);
            a2 = (A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
            break;
        case FilterType::HighShelf:
            b0 = A * ((A + 1) + (A - 1) * cosw0 + 2 * sqrtA * alphaShelf);
            b1 = -2 * A * ((A - 1) + (A + 1) * cosw0);
            b2 = A * ((A + 1) + (A - 1) * cosw0 - 2 * sqrtA * alphaShelf);
            a0 = (A + 1) - (A - 1) * cosw0 + 2 * sqrtA * alphaShelf;
            a1 = 2 * ((A - 1) - (A + 1) * cosw0);
            a2 = (A + 1) - (A - 1) * cosw0 - 2 * sqrtA * alphaShelf;
            break;
        case FilterType::HighPass:
            b0 = (1 + cosw0) / 2; b1 = -(1 + cosw0); b2 = (1 + cosw0) / 2;
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
            break;
        case FilterType::LowPass:
            b0 = (1 - cosw0) / 2; b1 = 1 - cosw0; b2 = (1 - cosw0) / 2;
            a0 = 1 + alpha; a1 = -2 * cosw0; a2 = 1 - alpha;
            break;
        case FilterType::Bell:
        default:
            b0 = 1 + alpha * A; b1 = -2 * cosw0; b2 = 1 - alpha * A;
            a0 = 1 + alpha / A; a1 = -2 * cosw0; a2 = 1 - alpha / A;
            break;
    }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;

    const auto w = juce::MathConstants<double>::twoPi * (double) frequencyHz / sampleRate;
    const std::complex<double> z = std::polar (1.0, -w);
    const auto num = b0 + b1 * z + b2 * z * z;
    const auto den = 1.0 + a1 * z + a2 * z * z;
    const auto magnitude = std::abs (num / den);
    auto db = 20.0f * (float) std::log10 (juce::jmax (1.0e-6, magnitude));

    // The formula above is inherently a single 2-pole (12dB/oct) section; scaling its
    // dB deviation from 0 by order/2 approximates the selected slope (6/18/24 dB/oct)
    // for the on-screen curve without needing a distinct per-order analytic formula --
    // consistent with this function's existing "faithful visual guide, not an exact
    // match to the audio path" contract (see SlopeFilter.h for the real cascade).
    if (p.filterType == FilterType::HighPass || p.filterType == FilterType::LowPass)
    {
        const auto orderMultiplier = p.slope == FilterSlope::Slope6 ? 0.5f
                                    : p.slope == FilterSlope::Slope18 ? 1.5f
                                    : p.slope == FilterSlope::Slope24 ? 2.0f : 1.0f;
        db *= orderMultiplier;
    }
    return db;
}

float SpectrumAnalyzer::summedStaticResponseDb (float frequencyHz) const
{
    float totalDb = 0.0f;
    for (auto slot : processor.getActiveBandSlots())
    {
        if (processor.isBandMuted (slot)) continue;
        totalDb += bandMagnitudeResponseDb (processor.getBandParams (slot), frequencyHz);
    }
    return totalDb;
}

float SpectrumAnalyzer::summedMagnitudeResponseDb (float frequencyHz) const
{
    float totalDb = 0.0f;
    for (auto slot : processor.getActiveBandSlots())
    {
        if (processor.isBandMuted (slot)) continue;
        auto params = processor.getBandParams (slot);
        if (params.dynamicEnabled)
            params.gainDb -= processor.getBandDynamicReductionDb (slot);
        totalDb += bandMagnitudeResponseDb (params, frequencyHz);
    }
    return totalDb;
}

void SpectrumAnalyzer::updateResponseCurve()
{
    const auto bounds = graphBounds();
    staticCorrectionCurve.clear();
    dynamicEffectiveCurve.clear();
    hasDynamicBands = false;
    for (auto slot : processor.getActiveBandSlots())
        if (! processor.isBandMuted (slot) && processor.getBandParams (slot).dynamicEnabled)
            { hasDynamicBands = true; break; }
    for (int pixel = 0; pixel <= (int) bounds.getWidth(); ++pixel)
    {
        const auto x = bounds.getX() + (float) pixel;
        const auto freq = xToFrequency (x);
        const auto staticY = gainToY (getCorrectionResponseDb (freq, false));
        const auto dynamicY = hasDynamicBands ? gainToY (getCorrectionResponseDb (freq, true)) : staticY;
        if (pixel == 0)
        {
            staticCorrectionCurve.startNewSubPath (x, staticY);
            dynamicEffectiveCurve.startNewSubPath (x, dynamicY);
        }
        else
        {
            staticCorrectionCurve.lineTo (x, staticY);
            dynamicEffectiveCurve.lineTo (x, dynamicY);
        }
    }
}

float SpectrumAnalyzer::getCorrectionResponseDb (float frequencyHz, bool includeDynamics) const
{
    return includeDynamics ? summedMagnitudeResponseDb (frequencyHz)
                           : summedStaticResponseDb (frequencyHz);
}

void SpectrumAnalyzer::paint (juce::Graphics& g)
{
    const auto bounds = graphBounds();
    g.setColour (juce::Colour (0xff0b0d10));
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 6.0f);

    static constexpr std::array<float, 10> frequencies { 20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000 };
    for (auto frequency : frequencies)
    {
        const auto x = frequencyToX (frequency);
        g.setColour (juce::Colour (0xff23272c));
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        g.setColour (juce::Colour (0xff777f87));
        g.setFont (10.0f);
        g.drawText (formatFrequency (frequency), juce::Rectangle<float> (x - 28.0f, bounds.getBottom() + 3.0f, 56.0f, 15.0f),
                    juce::Justification::centred);
    }
    for (int db = -60; db <= 12; db += 12)
    {
        const auto y = spectrumDbToY ((float) db);
        g.setColour (juce::Colour (0xff1c1f23));
        g.drawHorizontalLine (juce::roundToInt (y), bounds.getX(), bounds.getRight());
        g.setColour (juce::Colour (0xff777f87));
        g.setFont (10.0f);
        const auto label = db > 0 ? "+" + juce::String (db) : juce::String (db);
        g.drawText (label, juce::Rectangle<float> (0.0f, y - 7.0f, bounds.getX() - 5.0f, 14.0f),
                    juce::Justification::centredRight);
    }

    // Right-hand gain scale: the range a Range selector (owned by PluginEditor, see
    // setGainRangeDb()) actually controls -- narrower ranges give more pixels per dB,
    // i.e. more precise node dragging, at the cost of clipping distant nodes off-screen.
    for (int i = -2; i <= 2; ++i)
    {
        const auto gainDb = gainRangeDb * (float) i * 0.5f;
        const auto y = gainToY (gainDb);
        g.setColour (i == 0 ? selectedNodeColour.withAlpha (0.8f) : juce::Colour (0xff3a3f45).withAlpha (0.75f));
        g.drawLine (bounds.getRight(), y, bounds.getRight() + 4.0f, y, 1.0f);
        g.setColour (i == 0 ? juce::Colour (0xffe4e8ec) : juce::Colour (0xff777f87));
        g.setFont (9.5f);
        const auto label = gainDb > 0.0f ? "+" + juce::String (gainDb, 1) : juce::String (gainDb, 1);
        g.drawText (label, juce::Rectangle<float> (bounds.getRight() + 5.0f, y - 7.0f, 32.0f, 14.0f),
                    juce::Justification::centredLeft);
    }

    g.setColour (inputColour.withAlpha (0.55f));
    g.strokePath (inputPath, juce::PathStrokeType (1.2f));
    g.setColour (outputColour.withAlpha (0.9f));
    g.strokePath (outputPath, juce::PathStrokeType (1.6f));

    const auto profiles = processor.getMatchEQ().getProfiles();
    const std::array<juce::Colour, 2> profileColours { referenceColour, targetColour };
    const std::array<juce::String, 2> profileLabels { "Captured Reference", "Captured Target" };
    auto legendX = bounds.getX() + 8.0f;
    for (size_t profileIndex = 0; profileIndex < profiles.size(); ++profileIndex)
    {
        const auto& profile = profiles[profileIndex];
        if (! profile.valid) continue;

        juce::Path path;
        bool started = false;
        for (int point = 0; point < MatchEQ::pointCount; ++point)
        {
            const auto frequency = MatchEQ::frequency (point);
            if (frequency >= profile.sampleRate * 0.49) break;
            const auto db = profile.db[(size_t) point];
            if (! std::isfinite (db)) { started = false; continue; }
            // Capture stores power / N^2; the live FFT uses amplitude * 2 / N.
            // Convert to the same one-sided amplitude scale without normalising
            // the profiles independently, preserving their relative levels.
            const auto x = frequencyToX ((float) frequency);
            const auto y = spectrumDbToY ((float) db + 6.0206f);
            if (! started) path.startNewSubPath (x, y); else path.lineTo (x, y);
            started = true;
        }
        g.setColour (profileColours[profileIndex]);
        g.strokePath (path, juce::PathStrokeType (2.0f));

        const juce::Rectangle<float> legend (legendX, bounds.getY() + 4.0f, 142.0f, 18.0f);
        g.setColour (juce::Colour (0xff0b0d10).withAlpha (0.9f));
        g.fillRoundedRectangle (legend, 3.0f);
        g.setColour (profileColours[profileIndex]);
        g.drawLine (legendX + 5.0f, legend.getCentreY(), legendX + 21.0f, legend.getCentreY(), 2.0f);
        g.setFont (11.0f);
        g.drawText (profileLabels[profileIndex], legend.withTrimmedLeft (27.0f), juce::Justification::centredLeft);
        legendX += legend.getWidth() + 6.0f;
    }

    // EQ correction legend entries -- only when bands exist, fitted after profile entries
    if (! processor.getActiveBandSlots().empty())
    {
        constexpr float eqLegendW = 122.0f;
        if (legendX + eqLegendW + 8.0f <= bounds.getRight())
        {
            const juce::Rectangle<float> eqLegend (legendX, bounds.getY() + 4.0f, eqLegendW, 18.0f);
            g.setColour (juce::Colour (0xff0b0d10).withAlpha (0.9f));
            g.fillRoundedRectangle (eqLegend, 3.0f);
            g.setColour (eqCorrectionColour);
            g.drawLine (legendX + 5.0f, eqLegend.getCentreY(), legendX + 21.0f, eqLegend.getCentreY(), 2.0f);
            g.setFont (11.0f);
            g.drawText ("EQ Correction", eqLegend.withTrimmedLeft (27.0f), juce::Justification::centredLeft);
            legendX += eqLegendW + 6.0f;
        }
        if (hasDynamicBands)
        {
            constexpr float dynLegendW = 140.0f;
            if (legendX + dynLegendW + 8.0f <= bounds.getRight())
            {
                const juce::Rectangle<float> dynLegend (legendX, bounds.getY() + 4.0f, dynLegendW, 18.0f);
                g.setColour (juce::Colour (0xff0b0d10).withAlpha (0.9f));
                g.fillRoundedRectangle (dynLegend, 3.0f);
                g.setColour (eqDynamicColour);
                g.drawLine (legendX + 5.0f, dynLegend.getCentreY(), legendX + 21.0f, dynLegend.getCentreY(), 1.5f);
                g.setFont (11.0f);
                g.drawText ("Dynamic Response", dynLegend.withTrimmedLeft (27.0f), juce::Justification::centredLeft);
            }
        }
    }

    // EQ correction curve (static configured gains, on gain scale) -- primary display
    g.setColour (eqCorrectionColour.withAlpha (0.9f));
    g.strokePath (staticCorrectionCurve, juce::PathStrokeType (2.0f));
    // Instantaneous dynamic effective response -- only drawn when dynamic bands are active
    if (hasDynamicBands)
    {
        g.setColour (eqDynamicColour.withAlpha (0.65f));
        g.strokePath (dynamicEffectiveCurve, juce::PathStrokeType (1.5f));
    }

    const auto selected = processor.getSelectedBand();
    for (auto slot : processor.getActiveBandSlots())
    {
        const auto p = processor.getBandParams (slot);
        const auto x = frequencyToX (p.frequencyHz);
        const auto y = gainToY (p.gainDb);
        const auto soloed = processor.isBandSoloed (slot);
        const auto colour = slot == selected ? selectedNodeColour : nodeColour;
        g.setColour (colour.withAlpha (0.35f));
        g.drawVerticalLine (juce::roundToInt (x), bounds.getY(), bounds.getBottom());
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (11.0f, 11.0f).withCentre ({ x, y }), soloed ? 3.0f : 1.6f);
        if (slot == selected)
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ x, y }));
        // Per-band dynamic reduction indicator: small vertical bar from configured gain to
        // effective gain, drawn to the right of the node so the node position stays readable.
        if (p.dynamicEnabled && ! processor.isBandMuted (slot))
        {
            const auto reductionDb = processor.getBandDynamicReductionDb (slot);
            if (std::abs (reductionDb) > 0.1f)
            {
                const auto effectiveY = gainToY (p.gainDb - reductionDb);
                g.setColour (eqDynamicColour.withAlpha (0.8f));
                g.drawLine (x + 8.0f, y, x + 8.0f, effectiveY, 2.0f);
                g.drawLine (x + 5.0f, effectiveY, x + 11.0f, effectiveY, 1.5f);
            }
        }
    }

    g.setColour (juce::Colour (0xff2a2e33));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 6.0f, 1.0f);
}
