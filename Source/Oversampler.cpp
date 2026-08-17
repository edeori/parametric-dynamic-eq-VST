#include "Oversampler.h"

void Oversampler::prepare (double newSampleRate, int newSamplesPerBlock, int newNumChannels)
{
    sampleRate = newSampleRate;
    samplesPerBlock = newSamplesPerBlock;
    numChannels = juce::jmax (1, newNumChannels);
    rebuild();
}

void Oversampler::setOversamplingFactor (int newFactorChoice)
{
    if (newFactorChoice == factorChoice)
        return;
    factorChoice = newFactorChoice;
    rebuild();
}

void Oversampler::rebuild()
{
    if (factorChoice <= 0)
    {
        oversampling.reset();
        return;
    }

    // factorChoice 1/2/3 -> oversampling order 1/2/3 -> 2x/4x/8x (order is log2 of the factor).
    const auto order = (size_t) factorChoice;
    oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
        (size_t) numChannels, order, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false);
    oversampling->initProcessing ((size_t) samplesPerBlock);
}

juce::dsp::AudioBlock<float> Oversampler::processSamplesUp (const juce::dsp::AudioBlock<float>& input)
{
    if (oversampling == nullptr)
        return input;
    return oversampling->processSamplesUp (input);
}

void Oversampler::processSamplesDown (juce::dsp::AudioBlock<float>& outputBlock)
{
    if (oversampling == nullptr)
        return;
    oversampling->processSamplesDown (outputBlock);
}
