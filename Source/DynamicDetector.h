#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Per-band dynamic-EQ detector. The gain-computation math (asymmetric attack/release
// envelope, quadratic soft-knee threshold/ratio curve) is a direct port of
// MultiSqueeze's CompressorBand::process, reused rather than reinvented. Two
// differences from a plain compressor:
//  1. It is fed the *band-passed* signal from this band's own SVFilter (see EQBand),
//     not the broadband mix -- it only reacts to energy in the frequency region this
//     band is shaping, which is what makes it a dynamic EQ band rather than a compressor.
//  2. The input is pre-smoothed over a short (~3ms) squared-average window before the
//     attack/release follower sees it. Raw peak detection jumps on every transient
//     (e.g. every cymbal hit); block RMS reacts too late. A short RMS pre-smoothing
//     stage removes single-sample jumpiness while staying fast enough that the user's
//     own attack/release settings -- not this detector -- decide how quickly the band
//     reacts.
class DynamicDetector
{
public:
    struct Settings
    {
        float thresholdDb = -24.0f;
        float ratio = 2.0f;
        float attackMs = 15.0f;
        float releaseMs = 150.0f;
        float kneeDb = 6.0f;
    };

    void prepare (double sampleRate) noexcept;
    void reset() noexcept;
    void setParameters (const Settings&) noexcept;

    // Call once per sample with the rectified (or raw, sign doesn't matter -- it is
    // squared internally) band-passed magnitude. Updates the internal envelope and
    // the current gain-reduction-in-dB estimate.
    void processSample (float bandpassMagnitude) noexcept;

    float getCurrentReductionDb() const noexcept { return currentReductionDb; }

private:
    static constexpr float preSmoothMs = 3.0f;

    double sampleRate = 44100.0;
    Settings settings;
    float preSmoothCoeff = 0.0f;
    float rmsSquaredAverage = 0.0f;
    float envelope = 0.0f;
    float currentReductionDb = 0.0f;
};
