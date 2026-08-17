#include "../Source/SVFilter.h"
#include "../Source/DynamicDetector.h"
#include "../Source/MidSide.h"
#include "../Source/EQBand.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    int failures = 0;

    bool closeEnough (float a, float b, float tolerance = 1.0e-4f)
    {
        return std::abs (a - b) <= tolerance;
    }

    void check (bool condition, const std::string& name)
    {
        if (! condition)
        {
            std::cerr << "FAILED: " << name << '\n';
            ++failures;
        }
        else
        {
            std::cout << "  ok: " << name << '\n';
        }
    }

    constexpr double sampleRate = 48000.0;

    // Feeds a sine of the given frequency/amplitude through the filter for enough
    // samples to reach a settled steady state, returning the RMS amplitude of the
    // final `measureSamples` (skipping the settling window entirely).
    float settledRms (SVFilter& filter, float sineFreqHz, float amplitude, int totalSamples, int measureSamples)
    {
        std::vector<float> tail;
        tail.reserve ((size_t) measureSamples);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * sineFreqHz / sampleRate;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto x = amplitude * (float) std::sin (phase);
            phase += phaseInc;
            const auto result = filter.processSample (x);
            if (i >= totalSamples - measureSamples)
                tail.push_back (result.output);
        }
        double sumSquares = 0.0;
        for (auto v : tail)
            sumSquares += (double) v * (double) v;
        return (float) std::sqrt (sumSquares / (double) tail.size());
    }

    // Same settling idea as settledRms, but through a whole EQBand (block-based API,
    // stereo) rather than a bare SVFilter.
    float eqBandSettledRms (EQBand& band, const EQBand::Params& params, float sineFreqHz, float amplitude,
                            int totalSamples, int measureSamples)
    {
        juce::AudioBuffer<float> buffer (2, totalSamples);
        double phase = 0.0;
        const auto phaseInc = juce::MathConstants<double>::twoPi * sineFreqHz / sampleRate;
        for (int i = 0; i < totalSamples; ++i)
        {
            const auto x = amplitude * (float) std::sin (phase);
            phase += phaseInc;
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        juce::dsp::AudioBlock<float> block (buffer);
        band.process (block, params);

        double sumSquares = 0.0;
        for (int i = totalSamples - measureSamples; i < totalSamples; ++i)
        {
            const auto v = buffer.getSample (0, i);
            sumSquares += (double) v * (double) v;
        }
        return (float) std::sqrt (sumSquares / (double) measureSamples);
    }

    // Finds the frequency above centreHz where a soloListen preview's output has
    // dropped 3dB from its own centre-frequency level, via log-domain binary search.
    float minus3dBFrequencyAbove (const EQBand::Params& params, float centreHz, float centreRms)
    {
        const auto targetRms = centreRms / std::sqrt (2.0f);
        float lo = centreHz, hi = 20000.0f;
        for (int iter = 0; iter < 20; ++iter)
        {
            const auto mid = std::sqrt (lo * hi);
            EQBand probe;
            probe.prepare (sampleRate);
            const auto rms = eqBandSettledRms (probe, params, mid, 1.0f, 20000, 2000);
            if (rms > targetRms) lo = mid; else hi = mid;
        }
        return std::sqrt (lo * hi);
    }

    void testSoloListenWidthTracksQNotLoudness()
    {
        // The raw v1 bandpass output of a resonant 2-pole filter has a peak gain at
        // its centre frequency that scales linearly with Q -- left undivided (see
        // EQBand::process's soloListen branch), a high-Q preview would blast out
        // ~20dB+ louder than a low-Q one, drowning out the very width difference the
        // preview exists to demonstrate. This verifies the fix holds: centre-frequency
        // loudness stays constant across the whole Q range, while the -3dB width still
        // narrows as Q increases -- i.e. only the width changes, never the volume.
        float previousMinus3dBFreq = 0.0f;
        bool widthNarrowsMonotonically = true;
        bool loudnessStaysConstant = true;
        float firstCentreRms = -1.0f;

        for (float q : { 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f })
        {
            EQBand::Params p;
            p.active = true; p.frequencyHz = 1000.0f; p.gainDb = 0.0f; p.q = q;
            p.filterType = FilterType::Bell; p.channelMode = ChannelMode::Stereo; p.soloListen = true;

            EQBand centreBand;
            centreBand.prepare (sampleRate);
            const auto centreRms = eqBandSettledRms (centreBand, p, 1000.0f, 1.0f, 20000, 2000);
            if (firstCentreRms < 0.0f)
                firstCentreRms = centreRms;
            else if (! closeEnough (centreRms, firstCentreRms, firstCentreRms * 0.02f))
                loudnessStaysConstant = false;

            const auto minus3dBFreq = minus3dBFrequencyAbove (p, 1000.0f, centreRms);
            if (previousMinus3dBFreq > 0.0f && minus3dBFreq >= previousMinus3dBFreq)
                widthNarrowsMonotonically = false;
            previousMinus3dBFreq = minus3dBFreq;
        }

        check (loudnessStaysConstant,
              "EQBand solo-listen: centre-frequency loudness is Q-independent (normalized by Q)");
        check (widthNarrowsMonotonically,
              "EQBand solo-listen: the -3dB width narrows monotonically as Q increases");
    }

    void testSoloListenIsolatesFrequencyRegion()
    {
        // Deliberately 0dB gain: a plain "skip every other band" solo would sound
        // identical whether soloed or not whenever gain is small/zero, which is
        // exactly the bug being guarded against here. soloListen must attenuate a
        // frequency far from the band regardless of gain, because it previews the
        // filter's own band-pass component, not the (at 0dB, do-nothing) shaped output.
        EQBand::Params params;
        params.active = true;
        params.frequencyHz = 1000.0f;
        params.gainDb = 0.0f;
        params.q = 4.0f;
        params.filterType = FilterType::Bell;
        params.channelMode = ChannelMode::Stereo;
        params.soloListen = true;

        EQBand onCentreBand;
        onCentreBand.prepare (sampleRate);
        const auto onCentreRms = eqBandSettledRms (onCentreBand, params, 1000.0f, 1.0f, 20000, 2000);

        EQBand offCentreBand;
        offCentreBand.prepare (sampleRate);
        const auto offCentreRms = eqBandSettledRms (offCentreBand, params, 50.0f, 1.0f, 20000, 2000);

        check (onCentreRms > 0.3f,
              "EQBand solo-listen: the band's own centre frequency passes through at an audible level");
        check (offCentreRms < onCentreRms * 0.2f,
              "EQBand solo-listen: a frequency far from the band is heavily attenuated even at 0dB gain "
              "(previews the frequency region, not the -- at 0dB, silent -- normal shaped output)");
    }

    void testFilterSlopeIncreasesRolloffSteepness()
    {
        EQBand::Params p;
        p.active = true; p.frequencyHz = 1000.0f; p.gainDb = 0.0f; p.q = 0.707f;
        p.filterType = FilterType::LowPass; p.channelMode = ChannelMode::Stereo;

        EQBand passbandBand;
        passbandBand.prepare (sampleRate);
        const auto passbandRms = eqBandSettledRms (passbandBand, p, 100.0f, 1.0f, 20000, 2000);

        float previousAttenuationDb = -1.0e9f;
        bool monotonic = true;
        bool allFinite = true;
        for (auto slope : { FilterSlope::Slope6, FilterSlope::Slope12, FilterSlope::Slope18, FilterSlope::Slope24 })
        {
            p.slope = slope;
            EQBand stopbandBand;
            stopbandBand.prepare (sampleRate);
            const auto stopbandRms = eqBandSettledRms (stopbandBand, p, 4000.0f, 1.0f, 20000, 2000);
            if (! std::isfinite (stopbandRms))
                allFinite = false;
            const auto attenuationDb = 20.0f * std::log10 (passbandRms / juce::jmax (1.0e-9f, stopbandRms));
            if (attenuationDb <= previousAttenuationDb)
                monotonic = false;
            previousAttenuationDb = attenuationDb;
        }
        check (allFinite, "SlopeFilter: never NaN/Inf across the whole 6/12/18/24 dB/oct range");
        check (monotonic, "SlopeFilter: a fixed stopband frequency is attenuated more as slope increases "
                          "(6 < 12 < 18 < 24 dB/oct)");
    }

    void testBellUnityGainAtZeroDb()
    {
        SVFilter filter;
        filter.setCoefficients (sampleRate, 1000.0f, 0.0f, 0.707f, FilterType::Bell);
        const auto rms = settledRms (filter, 1000.0f, 1.0f, 20000, 2000);
        const auto expectedRms = 1.0f / std::sqrt (2.0f);
        check (closeEnough (rms, expectedRms, 0.01f), "SVF bell: 0 dB gain is unity at the centre frequency");
    }

    void testBellMeasuredGainMatchesTarget()
    {
        for (float targetDb : { -12.0f, -6.0f, 6.0f, 12.0f })
        {
            SVFilter filter;
            filter.setCoefficients (sampleRate, 1000.0f, targetDb, 1.0f, FilterType::Bell);
            const auto rmsAtGain = settledRms (filter, 1000.0f, 1.0f, 20000, 2000);

            SVFilter reference;
            reference.setCoefficients (sampleRate, 1000.0f, 0.0f, 1.0f, FilterType::Bell);
            const auto rmsUnity = settledRms (reference, 1000.0f, 1.0f, 20000, 2000);

            const auto measuredDb = 20.0f * std::log10 (rmsAtGain / rmsUnity);
            check (closeEnough (measuredDb, targetDb, 0.3f),
                  "SVF bell: measured gain at centre frequency matches the configured dB ("
                      + std::to_string (targetDb) + " dB)");
        }
    }

    // Methodology borrowed from a sibling project's ShelfNaNSweepTest: an RBJ-cookbook
    // shelf filter that derives alpha directly from Q (instead of RBJ's S/shelf-slope
    // parameterization) is well-defined and NaN-free, but was independently measured
    // to overshoot its target gain by up to +37dB at high Q -- Q controls peak
    // bandwidth, not shelf steepness, so feeding it into a shelf's alpha creates a
    // resonant bump at the corner frequency instead of a monotonic shelf. Our SVFilter
    // (see SVFilter.h) uses Cytomic's own k=1/Q shelf design, a different formula
    // family -- this sweep confirms it doesn't have the same failure mode.
    void testShelfStableAndAccurateAcrossQRange()
    {
        bool allFinite = true;
        bool allNearTarget = true;
        float worstErrorDb = 0.0f;
        for (float targetDb : { -18.0f, -12.0f, -6.0f, 6.0f, 12.0f, 18.0f })
        {
            for (float q : { 0.1f, 0.5f, 1.0f, 4.0f, 8.0f, 18.0f })
            {
                SVFilter low, high;
                low.setCoefficients (sampleRate, 1000.0f, targetDb, q, FilterType::LowShelf);
                high.setCoefficients (sampleRate, 1000.0f, targetDb, q, FilterType::HighShelf);

                // Deep in each shelf's plateau: a low shelf at 1kHz should sit at the
                // target gain well below 1kHz; a high shelf, well above it. Measured at
                // the true spectrum edges (20Hz/20kHz, ~5.6 octaves from the 1kHz corner)
                // rather than a fixed offset -- a very low Q (wide, gentle slope) shelf's
                // transition genuinely spans several octaves, so a closer measurement
                // point hasn't reached the plateau yet even though the filter itself is
                // behaving correctly.
                const auto lowRms = settledRms (low, 20.0f, 1.0f, 20000, 2000);
                const auto highRms = settledRms (high, 20000.0f, 1.0f, 20000, 2000);
                if (! std::isfinite (lowRms) || ! std::isfinite (highRms))
                    allFinite = false;

                const auto refRms = 1.0f / std::sqrt (2.0f);
                const auto lowErrorDb = std::abs (20.0f * std::log10 (lowRms / refRms) - targetDb);
                const auto highErrorDb = std::abs (20.0f * std::log10 (highRms / refRms) - targetDb);
                worstErrorDb = std::max ({ worstErrorDb, lowErrorDb, highErrorDb });
                if (lowErrorDb > 1.5f || highErrorDb > 1.5f)
                    allNearTarget = false;
            }
        }
        check (allFinite, "SVF shelf: never NaN/Inf across a gain x Q sweep");
        check (allNearTarget, "SVF shelf: plateau gain matches target within 1.5dB across the full Q range "
                              "(worst error " + std::to_string (worstErrorDb) + " dB) -- no RBJ-Q-alpha-style overshoot");
    }

    void testCoefficientRecomputeMidStreamStaysFinite()
    {
        SVFilter filter;
        filter.setCoefficients (sampleRate, 200.0f, 18.0f, 8.0f, FilterType::Bell);
        double phase = 0.0;
        bool allFinite = true;
        for (int i = 0; i < 50000; ++i)
        {
            if (i == 10000)
                filter.setCoefficients (sampleRate, 8000.0f, -18.0f, 0.2f, FilterType::HighShelf);
            if (i == 30000)
                filter.setCoefficients (sampleRate, 1000.0f, 0.0f, 0.707f, FilterType::LowPass);

            const auto x = (float) std::sin (phase);
            phase += juce::MathConstants<double>::twoPi * 440.0 / sampleRate;
            const auto result = filter.processSample (x);
            if (! std::isfinite (result.output) || std::abs (result.output) > 100.0f)
            {
                allFinite = false;
                break;
            }
        }
        check (allFinite, "SVF: repeated control-rate coefficient changes never produce NaN/instability");
    }

    void testMidSideRoundTrip()
    {
        bool allExact = true;
        for (float l : { -1.0f, -0.37f, 0.0f, 0.21f, 1.0f })
        {
            for (float r : { -1.0f, -0.6f, 0.0f, 0.5f, 1.0f })
            {
                const auto [m, s] = MidSide::encode (l, r);
                const auto [l2, r2] = MidSide::decode (m, s);
                if (! closeEnough (l, l2, 1.0e-6f) || ! closeEnough (r, r2, 1.0e-6f))
                    allExact = false;
            }
        }
        check (allExact, "MidSide: decode(encode(l, r)) reproduces (l, r) exactly");
    }

    float standardReductionDb (float overDb, float ratio, float kneeDb)
    {
        const auto slope = 1.0f - 1.0f / ratio;
        if (overDb <= -kneeDb * 0.5f)
            return 0.0f;
        if (overDb >= kneeDb * 0.5f)
            return overDb * slope;
        const auto x = overDb + kneeDb * 0.5f;
        return slope * (x * x) / (2.0f * juce::jmax (0.001f, kneeDb));
    }

    void feedConstant (DynamicDetector& detector, float magnitude, int numSamples)
    {
        for (int i = 0; i < numSamples; ++i)
            detector.processSample (magnitude);
    }

    void testDetectorBelowThresholdNoReduction()
    {
        DynamicDetector detector;
        detector.prepare (sampleRate);
        detector.setParameters ({ -20.0f, 4.0f, 5.0f, 100.0f, 6.0f });
        feedConstant (detector, juce::Decibels::decibelsToGain (-40.0f), 48000);
        check (closeEnough (detector.getCurrentReductionDb(), 0.0f, 0.05f),
              "DynamicDetector: no reduction while well below threshold");
    }

    void testDetectorAboveThresholdMatchesSoftKnee()
    {
        DynamicDetector detector;
        detector.prepare (sampleRate);
        DynamicDetector::Settings settings { -20.0f, 4.0f, 1.0f, 100.0f, 0.0f };
        detector.setParameters (settings);
        feedConstant (detector, 1.0f, 48000); // 0 dBFS, settled
        const auto expected = standardReductionDb (0.0f - settings.thresholdDb, settings.ratio, settings.kneeDb);
        check (closeEnough (detector.getCurrentReductionDb(), expected, 0.3f),
              "DynamicDetector: settled reduction matches the soft-knee threshold/ratio formula");
    }

    void testDetectorReleasesAfterSignalDrops()
    {
        DynamicDetector detector;
        detector.prepare (sampleRate);
        detector.setParameters ({ -20.0f, 4.0f, 1.0f, 50.0f, 0.0f });
        feedConstant (detector, 1.0f, 48000);
        const auto peakReduction = detector.getCurrentReductionDb();
        feedConstant (detector, 0.0f, 48000);
        const auto releasedReduction = detector.getCurrentReductionDb();
        check (peakReduction > 1.0f, "DynamicDetector: builds up meaningful reduction while driven hard");
        check (releasedReduction < peakReduction * 0.01f,
              "DynamicDetector: reduction releases back towards 0 once the signal drops to silence");
    }

    void testDetectorKneeIsContinuousAtBoundary()
    {
        // The closed-form curve itself (shared with CompressorBand's design) must not
        // jump at the knee boundary -- verified directly on the formula, matching how
        // MultiSqueeze's own DspTests verify CompressorBand's identical curve.
        constexpr float ratio = 4.0f, kneeDb = 6.0f;
        const auto justBelow = standardReductionDb (-kneeDb * 0.5f - 0.001f, ratio, kneeDb);
        const auto atBoundary = standardReductionDb (-kneeDb * 0.5f, ratio, kneeDb);
        const auto justAboveUpper = standardReductionDb (kneeDb * 0.5f + 0.001f, ratio, kneeDb);
        const auto atUpperBoundary = standardReductionDb (kneeDb * 0.5f, ratio, kneeDb);
        check (closeEnough (justBelow, atBoundary, 0.01f), "soft knee: continuous at the lower knee boundary");
        check (closeEnough (justAboveUpper, atUpperBoundary, 0.01f), "soft knee: continuous at the upper knee boundary");
    }
}

int main()
{
    testSoloListenIsolatesFrequencyRegion();
    testSoloListenWidthTracksQNotLoudness();
    testFilterSlopeIncreasesRolloffSteepness();
    testBellUnityGainAtZeroDb();
    testBellMeasuredGainMatchesTarget();
    testShelfStableAndAccurateAcrossQRange();
    testCoefficientRecomputeMidStreamStaysFinite();
    testMidSideRoundTrip();
    testDetectorBelowThresholdNoReduction();
    testDetectorAboveThresholdMatchesSoftKnee();
    testDetectorReleasesAfterSignalDrops();
    testDetectorKneeIsContinuousAtBoundary();

    if (failures == 0)
    {
        std::cout << "All ParaDyn EQ DSP tests passed.\n";
        return 0;
    }
    std::cerr << failures << " ParaDyn EQ DSP test(s) failed.\n";
    return 1;
}
