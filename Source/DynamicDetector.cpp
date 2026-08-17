#include "DynamicDetector.h"
#include <cmath>

void DynamicDetector::prepare (double newSampleRate) noexcept
{
    sampleRate = juce::jmax (1.0, newSampleRate);
    preSmoothCoeff = (float) std::exp (-1.0 / (0.001 * preSmoothMs * sampleRate));
    reset();
}

void DynamicDetector::reset() noexcept
{
    rmsSquaredAverage = 0.0f;
    envelope = 0.0f;
    currentReductionDb = 0.0f;
}

void DynamicDetector::setParameters (const Settings& newSettings) noexcept
{
    settings = newSettings;
}

void DynamicDetector::processSample (float bandpassMagnitude) noexcept
{
    const auto squared = bandpassMagnitude * bandpassMagnitude;
    rmsSquaredAverage = preSmoothCoeff * rmsSquaredAverage + (1.0f - preSmoothCoeff) * squared;
    const auto preSmoothedLevel = std::sqrt (rmsSquaredAverage);

    const auto attack = std::exp (-1.0f / (0.001f * juce::jmax (0.01f, settings.attackMs) * (float) sampleRate));
    const auto release = std::exp (-1.0f / (0.001f * juce::jmax (1.0f, settings.releaseMs) * (float) sampleRate));
    const auto coefficient = preSmoothedLevel > envelope ? attack : release;
    envelope = coefficient * envelope + (1.0f - coefficient) * preSmoothedLevel;

    const auto levelDb = juce::Decibels::gainToDecibels (envelope, -120.0f);
    const auto overDb = levelDb - settings.thresholdDb;
    const auto ratio = juce::jmax (1.0f, settings.ratio);
    const auto kneeDb = juce::jmax (0.0f, settings.kneeDb);
    const auto slope = 1.0f - 1.0f / ratio;

    // Same quadratic soft-knee curve as CompressorBand::process: flat below the
    // knee, a smooth parabolic blend through it, plain Threshold/Ratio slope above.
    if (overDb <= -kneeDb * 0.5f)
        currentReductionDb = 0.0f;
    else if (overDb >= kneeDb * 0.5f)
        currentReductionDb = overDb * slope;
    else
    {
        const auto x = overDb + kneeDb * 0.5f;
        currentReductionDb = slope * (x * x) / (2.0f * juce::jmax (0.001f, kneeDb));
    }
}
