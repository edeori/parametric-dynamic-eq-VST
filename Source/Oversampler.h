#pragma once

#include <juce_dsp/juce_dsp.h>
#include <memory>

// Thin wrapper around juce::dsp::Oversampling selectable at Off/2x/4x/8x (factorChoice
// 0..3, matching the `oversampling` AudioParameterChoice index). IIR polyphase halfband
// filters are used for lower added latency than an equiripple FIR would give, which
// matters for an EQ likely to be used while tracking/mixing, not only at mastering time.
class Oversampler
{
public:
    void prepare (double sampleRate, int samplesPerBlock, int numChannels);
    void setOversamplingFactor (int newFactorChoice);
    int getFactorMultiplier() const noexcept { return oversampling != nullptr ? (int) oversampling->getOversamplingFactor() : 1; }
    int getLatencySamples() const noexcept { return oversampling != nullptr ? (int) oversampling->getLatencyInSamples() : 0; }

    // Returns a (possibly higher-sample-rate) block to process in place. When
    // oversampling is Off, this is simply `input` itself.
    juce::dsp::AudioBlock<float> processSamplesUp (const juce::dsp::AudioBlock<float>& input);
    // Downsamples the internal buffer (as modified in place after processSamplesUp)
    // back into `outputBlock`, which must be at the original sample rate. No-op when
    // oversampling is Off (the caller already processed `outputBlock` directly).
    void processSamplesDown (juce::dsp::AudioBlock<float>& outputBlock);

private:
    void rebuild();

    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    int factorChoice = 0;
    double sampleRate = 44100.0;
    int samplesPerBlock = 512;
    int numChannels = 2;
};
