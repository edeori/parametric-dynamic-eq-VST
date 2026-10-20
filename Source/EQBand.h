#pragma once

#include <juce_dsp/juce_dsp.h>
#include <atomic>
#include "SlopeFilter.h"
#include "DynamicDetector.h"
#include "Parameters.h"

// One parametric EQ band: a SlopeFilter (or a pair, for independent-per-channel modes),
// an optional DynamicDetector, and the mid/side/left/right routing around them.
//
// Coefficient-modulation safety: freq/gain/Q are smoothed continuously via
// juce::SmoothedValue, but the SVF's coefficients are only *recomputed* from those
// smoothed values once every controlRateBlockSize samples (not every sample and not
// with a raw jump) -- see SVFilter.h for why the TPT topology makes this safe. The
// dynamic detector's reduction is sampled at the same control-rate boundary and
// folded into the same recompute, so a fast-reacting detector never causes a
// per-sample coefficient jump either.
class EQBand
{
public:
    struct Params
    {
        bool active = false;
        float frequencyHz = 1000.0f;
        float gainDb = 0.0f;
        float q = 0.707f;
        FilterType filterType = FilterType::Bell;
        FilterSlope slope = FilterSlope::Slope12;
        ChannelMode channelMode = ChannelMode::Stereo;
        bool dynamicEnabled = false;
        float thresholdDb = -24.0f;
        float ratio = 2.0f;
        float attackMs = 15.0f;
        float releaseMs = 150.0f;
        float kneeDb = 6.0f;
        // When true, the output is the filter's own band-pass component (the same
        // signal the dynamic detector listens to) instead of the shaped EQ output --
        // an audition/"solo" preview of exactly which frequency region this band is
        // centred on, at the band's own freq/Q, independent of its gain. This is what
        // makes Solo audible even when a band's gain is at (or near) 0dB, and is the
        // standard meaning of "solo" on a parametric EQ band (as opposed to simply
        // skipping every other band while still passing the rest of the signal
        // through this one band's boost/cut, which is silent whenever gain is small).
        bool soloListen = false;
        // Maximum dynamic gain reduction in dB. 0 = unlimited (legacy: full detector output
        // applied verbatim). > 0 caps how much the dynamic cut can deepen beyond the static gain.
        float rangeDb = 0.0f;
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // Processes the block in place. Assumes 1 or 2 channels.
    void process (juce::dsp::AudioBlock<float>& block, const Params&) noexcept;

    float getCurrentDynamicReductionDb() const noexcept { return lastDynamicReductionDb.load(); }

private:
    static constexpr int controlRateBlockSize = 32;

    double sampleRate = 44100.0;
    SlopeFilter filterA, filterB;
    DynamicDetector detector;
    juce::SmoothedValue<float> freqSmoothed, gainSmoothed, qSmoothed;
    std::atomic<float> lastDynamicReductionDb { 0.0f };
    int samplesSinceRecompute = 0;
};
