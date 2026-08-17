#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include "SpectrumFifo.h"
#include "Parameters.h"
#include "EQBand.h"

class ParaDynAudioProcessor;

// FFT spectrum panel: draws the pre-processing and post-processing signal as two
// distinctly-coloured traces (no green -- see the .cpp), a summed EQ response curve,
// and one draggable node per active band (horizontal drag -> frequency, vertical drag
// -> gain). Interaction pattern (mouseDown/mouseDrag/mouseUp hit-testing, log-frequency
// axis mapping) is adapted from MultiSqueeze's SpectrumAnalyzer.
class SpectrumAnalyzer final : public juce::Component, private juce::Timer
{
public:
    explicit SpectrumAnalyzer (ParaDynAudioProcessor&);
    ~SpectrumAnalyzer() override;
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    static juce::String formatFrequency (float);

    // Selects how many +-dB the gain axis (node dragging, response curve, the right-
    // hand scale) spans -- a tighter range means more pixels per dB, so nodes can be
    // dragged more precisely. Purely a display/interaction setting, not an audio
    // parameter, so it isn't persisted in the APVTS.
    void setGainRangeDb (float newRangeDb) noexcept;
    float getGainRangeDb() const noexcept { return gainRangeDb; }

private:
    void timerCallback() override;
    juce::Rectangle<float> graphBounds() const;
    float frequencyToX (float) const;
    float xToFrequency (float) const;
    float spectrumDbToY (float) const;
    float gainToY (float) const;
    float yToGain (float) const;
    int bandNodeNear (float x, float y) const;
    void updatePath (const std::array<float, SpectrumFifo::fftSize>&, juce::Path&, std::array<float, SpectrumFifo::fftSize / 2>&);
    float bandMagnitudeResponseDb (const EQBand::Params&, float frequencyHz) const;
    float summedMagnitudeResponseDb (float frequencyHz) const;
    void updateResponseCurve();

    ParaDynAudioProcessor& processor;
    juce::dsp::FFT fft { SpectrumFifo::fftOrder };
    juce::dsp::WindowingFunction<float> window { (size_t) SpectrumFifo::fftSize,
                                                  juce::dsp::WindowingFunction<float>::hann };
    std::array<float, SpectrumFifo::fftSize> inputSamples {}, outputSamples {};
    std::array<float, SpectrumFifo::fftSize * 2> fftData {};
    std::array<float, SpectrumFifo::fftSize / 2> inputMagnitudes {}, outputMagnitudes {};
    juce::Path inputPath, outputPath, responseCurve;

    int draggedBand = -1;
    bool draggingGain = false;
    bool verticalOnlyDrag = false;
    bool freqGestureStarted = false;
    bool gainGestureStarted = false;
    int momentarySoloSlot = -1;
    float gainRangeDb = 24.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SpectrumAnalyzer)
};
