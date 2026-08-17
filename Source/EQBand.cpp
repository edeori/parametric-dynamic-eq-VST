#include "EQBand.h"
#include "MidSide.h"
#include <cmath>

namespace
{
    constexpr float smoothingSeconds = 0.02f;
}

void EQBand::prepare (double newSampleRate) noexcept
{
    sampleRate = juce::jmax (1.0, newSampleRate);
    filterA.reset();
    filterB.reset();
    detector.prepare (sampleRate);
    freqSmoothed.reset (sampleRate, smoothingSeconds);
    gainSmoothed.reset (sampleRate, smoothingSeconds);
    qSmoothed.reset (sampleRate, smoothingSeconds);
    reset();
}

void EQBand::reset() noexcept
{
    filterA.reset();
    filterB.reset();
    detector.reset();
    lastDynamicReductionDb = 0.0f;
    samplesSinceRecompute = controlRateBlockSize;
}

void EQBand::process (juce::dsp::AudioBlock<float>& block, const Params& p) noexcept
{
    if (! p.active)
        return;

    const auto numSamples = (int) block.getNumSamples();
    const auto numChannels = (int) block.getNumChannels();
    if (numSamples == 0 || numChannels == 0)
        return;

    freqSmoothed.setTargetValue (p.frequencyHz);
    qSmoothed.setTargetValue (p.q);
    gainSmoothed.setTargetValue (p.gainDb);
    detector.setParameters ({ p.thresholdDb, p.ratio, p.attackMs, p.releaseMs, p.kneeDb });

    auto* left = block.getChannelPointer (0);
    auto* right = numChannels > 1 ? block.getChannelPointer (1) : nullptr;

    for (int i = 0; i < numSamples; ++i)
    {
        // getNextValue() must advance every sample regardless of whether this is a
        // coefficient-recompute sample, or its internal ramp counter runs at 1/
        // controlRateBlockSize of real time -- e.g. a configured 20ms smoothingSeconds
        // would actually take ~640ms to settle. Only the *use* of the smoothed value
        // (recomputing the SVF's coefficients from it) is gated to the control rate.
        const auto freq = freqSmoothed.getNextValue();
        const auto q = qSmoothed.getNextValue();
        const auto staticGain = gainSmoothed.getNextValue();

        if (samplesSinceRecompute >= controlRateBlockSize)
        {
            samplesSinceRecompute = 0;
            const auto dynamicReduction = p.dynamicEnabled ? detector.getCurrentReductionDb() : 0.0f;
            lastDynamicReductionDb.store (dynamicReduction);
            const auto effectiveGain = staticGain - dynamicReduction;

            filterA.setCoefficients (sampleRate, freq, effectiveGain, q, p.filterType, p.slope);
            if (p.channelMode == ChannelMode::Stereo)
                filterB.setCoefficients (sampleRate, freq, effectiveGain, q, p.filterType, p.slope);
        }
        ++samplesSinceRecompute;

        if (p.soloListen)
        {
            // Preview whichever channel/domain this band actually operates in, through
            // filterA alone, and play the result identically to both output channels --
            // an unambiguous mono audition of exactly the frequency content this band
            // is centred on, regardless of its own routing mode.
            const auto rIn = right != nullptr ? right[i] : left[i];
            float previewInput = left[i];
            if (p.channelMode == ChannelMode::Right)
                previewInput = rIn;
            else if (p.channelMode == ChannelMode::Mid)
                previewInput = MidSide::encode (left[i], rIn).first;
            else if (p.channelMode == ChannelMode::Side)
                previewInput = MidSide::encode (left[i], rIn).second;

            const auto res = filterA.processSample (previewInput);
            // A resonant 2-pole bandpass's peak gain at its centre frequency scales
            // linearly with Q (measured: doubling Q exactly doubles the raw v1 output
            // there) -- undivided, a high-Q preview blasts out ~20dB+ louder than a
            // low-Q one, which drowns out the very width difference this preview
            // exists to demonstrate. Dividing by Q keeps the centre-frequency level
            // constant across the whole Q range, so only the width audibly changes.
            const auto previewOutput = res.bandpass / juce::jmax (0.05f, q);
            left[i] = previewOutput;
            if (right != nullptr)
                right[i] = previewOutput;
            if (p.dynamicEnabled)
                detector.processSample (std::abs (res.bandpass));
            continue;
        }

        switch (p.channelMode)
        {
            case ChannelMode::Stereo:
            {
                const auto rIn = right != nullptr ? right[i] : left[i];
                const auto resA = filterA.processSample (left[i]);
                const auto resB = filterB.processSample (rIn);
                left[i] = resA.output;
                if (right != nullptr)
                    right[i] = resB.output;
                if (p.dynamicEnabled)
                    detector.processSample (juce::jmax (std::abs (resA.bandpass), std::abs (resB.bandpass)));
                break;
            }
            case ChannelMode::Mid:
            case ChannelMode::Side:
            {
                const auto rIn = right != nullptr ? right[i] : left[i];
                auto [m, s] = MidSide::encode (left[i], rIn);
                auto& target = p.channelMode == ChannelMode::Mid ? m : s;
                const auto res = filterA.processSample (target);
                target = res.output;
                if (p.dynamicEnabled)
                    detector.processSample (std::abs (res.bandpass));
                const auto [l2, r2] = MidSide::decode (m, s);
                left[i] = l2;
                if (right != nullptr)
                    right[i] = r2;
                break;
            }
            case ChannelMode::Left:
            {
                const auto res = filterA.processSample (left[i]);
                left[i] = res.output;
                if (p.dynamicEnabled)
                    detector.processSample (std::abs (res.bandpass));
                break;
            }
            case ChannelMode::Right:
            {
                if (right != nullptr)
                {
                    const auto res = filterA.processSample (right[i]);
                    right[i] = res.output;
                    if (p.dynamicEnabled)
                        detector.processSample (std::abs (res.bandpass));
                }
                break;
            }
            default:
                break;
        }
    }
}
