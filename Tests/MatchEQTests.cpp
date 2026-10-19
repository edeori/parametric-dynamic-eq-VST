#include "../Source/MatchEQ.h"
#include "../Source/SVFilter.h"
#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace
{
    void require (bool passed, const char* name)
    {
        if (! passed) throw std::runtime_error (name);
        std::cout << "  ok: " << name << '\n';
    }

    MatchEQ::Profile flat (double rate = 48000.0)
    {
        MatchEQ::Profile p;
        p.valid = true; p.seconds = 3.0; p.sampleRate = rate; p.db.fill (-30.0);
        return p;
    }

    double combined (const MatchEQ::Fit& fit, double hz, double rate)
    {
        double db = 0.0;
        for (const auto& band : fit.bands) db += MatchEQ::responseDb (band, hz, rate);
        return db;
    }

    void testFitting()
    {
        auto ref = flat(), target = flat();
        auto fit = MatchEQ::fitProfiles (ref, target, {}, 48000.0);
        require (fit.error.isEmpty() && fit.bands.size() == 16, "identical profiles produce an editable fit");
        require (std::all_of (fit.bands.begin(), fit.bands.end(), [] (auto b) { return std::abs (b.gainDb) < 0.001f; }),
                 "identical profiles produce zero correction");
        for (auto& db : target.db) db -= 24.0;
        fit = MatchEQ::fitProfiles (ref, target, {}, 48000.0);
        require (std::all_of (fit.bands.begin(), fit.bands.end(), [] (auto b) { return std::abs (b.gainDb) < 0.001f; }),
                 "24 dB overall level difference does not produce EQ");

        for (double rate : { 32000.0, 44100.0, 48000.0, 96000.0, 192000.0 })
        {
            ref = flat (rate); target = flat (rate);
            // Independent broad spectral tilt: six dB from 100 Hz to 10 kHz.
            for (int i = 0; i < MatchEQ::pointCount; ++i)
                ref.db[(size_t) i] += 3.0 * std::log10 (MatchEQ::frequency (i) / 1000.0);
            fit = MatchEQ::fitProfiles (ref, target, {}, rate);
            require (fit.error.isEmpty(), "tilt can be fitted across sample rates");
            const auto lows = combined (fit, 100.0, rate), highs = combined (fit, 10000.0, rate);
            require (highs - lows > 4.0 && highs - lows < 8.0, "spectral tilt corrected in the right direction and magnitude");
            double mean = 0.0, count = 0.0;
            for (int i = 0; i < MatchEQ::pointCount; ++i)
                if (MatchEQ::frequency (i) <= rate * 0.45) { mean += ref.db[(size_t) i] - target.db[(size_t) i]; ++count; }
            mean /= count;
            double originalError = 0.0, fittedError = 0.0;
            for (int i = 0; i < MatchEQ::pointCount; ++i)
            {
                const auto hz = MatchEQ::frequency (i);
                if (hz < 50.0 || hz > std::min (14000.0, rate * 0.4)) continue;
                const auto wanted = ref.db[(size_t) i] - target.db[(size_t) i] - mean;
                originalError += wanted * wanted;
                fittedError += std::pow (wanted - combined (fit, hz, rate), 2.0);
            }
            require (fittedError < originalError * 0.15, "fitted EQ removes at least 85 percent of broad tonal error");
        }
        auto zero = MatchEQ::fitProfiles (ref, target, { 0.0f, 0.5f, 9.0f }, 192000.0);
        require (std::abs (combined (zero, 1000.0, 192000.0)) < 0.001, "Amount zero is flat");
        auto half = MatchEQ::fitProfiles (ref, target, { 50.0f, 0.5f, 9.0f }, 192000.0);
        require (std::abs (combined (half, 10000.0, 192000.0) / combined (fit, 10000.0, 192000.0) - 0.5) < 0.12,
                 "Amount 50 percent approximately halves the fitted correction");

        for (int i = 0; i < MatchEQ::pointCount; ++i)
            ref.db[(size_t) i] = i < MatchEQ::pointCount / 2 ? -45.0 : -5.0;
        fit = MatchEQ::fitProfiles (ref, target, { 100.0f, 0.15f, 3.0f }, 48000.0);
        double peak = 0.0;
        for (int i = 0; i < 2048; ++i)
            peak = std::max (peak, std::abs (combined (fit, 20.0 * std::pow (1000.0, i / 2047.0), 48000.0)));
        require (peak <= 3.01, "combined overlapping EQ response respects maximum boost/cut");
        ref.db[50] = std::numeric_limits<double>::quiet_NaN();
        require (MatchEQ::fitProfiles (ref, target, {}, 48000.0).error.isNotEmpty(), "non-finite profile rejected");
        require (MatchEQ::fitProfiles ({}, target, {}, 48000.0).error.isNotEmpty(), "missing profile rejected");
        require (MatchEQ::fitProfiles (target, target, {}, 0.0).error.isNotEmpty(), "unprepared sample rate rejected");

        ref = flat(); target = flat();
        for (int i = 0; i < MatchEQ::pointCount; ++i)
            ref.db[(size_t) i] += 6.0 * std::exp (-0.5 * std::pow (std::log2 (MatchEQ::frequency (i) / 1000.0) / 0.2, 2.0));
        const auto detailed = MatchEQ::fitProfiles (ref, target, { 100, 0.15f, 9 }, 48000);
        const auto smooth = MatchEQ::fitProfiles (ref, target, { 100, 2.0f, 9 }, 48000);
        require (combined (smooth, 1000, 48000) < combined (detailed, 1000, 48000), "smoothing reduces narrow spectral features");
    }

    void testResponseAgainstAudio()
    {
        for (const auto rate : { 44100.0, 96000.0 })
            for (const auto gain : { -9.0f, 6.0f })
                for (const auto hz : { 250.0, 1000.0, 7000.0 })
                {
                    SVFilter filter;
                    filter.setCoefficients (rate, 1000.0f, gain, 1.1f, FilterType::Bell);
                    double inputEnergy = 0.0, outputEnergy = 0.0;
                    const auto sampleCount = (int) rate;
                    for (int i = 0; i < sampleCount; ++i)
                    {
                        const auto x = (float) std::sin (2.0 * juce::MathConstants<double>::pi * hz * i / rate);
                        const auto y = filter.processSample (x).output;
                        if (i > sampleCount / 2) { inputEnergy += x * x; outputEnergy += y * y; }
                    }
                    const auto actual = 10.0 * std::log10 (outputEnergy / inputEnergy);
                    const auto predicted = MatchEQ::responseDb ({ 1000.0f, gain, 1.1f }, hz, rate);
                    require (std::abs (actual - predicted) < 0.025, "fitter response matches actual SVFilter audio");
                }
    }

    // Wait for a bounded worker observation; no UI/message loop is involved.
    bool awaitSeconds (MatchEQ& match, double seconds)
    {
        for (int i = 0; i < 1000; ++i)
        {
            if (match.getStatus().seconds >= seconds) return true;
            juce::Thread::sleep (2);
        }
        return false;
    }

    void feedFrames (MatchEQ& match, double rate, int count, int channels, bool antiPhase,
                     float amplitude = 0.1f, int blockSize = 257, SVFilter* shaping = nullptr)
    {
        juce::Random random (12345);
        juce::AudioBuffer<float> block (channels, blockSize);
        const auto initialSeconds = match.getStatus().seconds;
        int total = 0, frame = 0;
        while (total < count * MatchEQ::fftSize)
        {
            const auto n = juce::jmin (blockSize, count * MatchEQ::fftSize - total);
            block.setSize (channels, n, false, false, true);
            for (int i = 0; i < n; ++i)
            {
                auto x = amplitude * (random.nextFloat() * 2.0f - 1.0f);
                if (shaping != nullptr) x = shaping->processSample (x).output;
                block.setSample (0, i, x);
                if (channels == 2) block.setSample (1, i, antiPhase ? -x : x);
            }
            match.push (block);
            total += n;
            if (total / MatchEQ::fftSize > frame)
            {
                frame = total / MatchEQ::fftSize;
                if (amplitude != 0.0f)
                    require (awaitSeconds (match, initialSeconds + frame * MatchEQ::fftSize / rate - 0.00001),
                             "worker consumes complete frames independently of editor and block boundaries");
                else juce::Thread::sleep (10);
            }
        }
    }

    void testCaptureAndPersistence()
    {
        auto match = std::make_unique<MatchEQ>();
        match->prepare (48000.0);
        match->startCapture (MatchEQ::Capture::reference);
        feedFrames (*match, 48000.0, 4, 2, false);
        match->stopCapture();
        require (match->getStatus().hasReference, "reference can be learned and stopped");
        match->startCapture (MatchEQ::Capture::target);
        feedFrames (*match, 48000.0, 4, 2, true, 0.025f, 1024);
        match->stopCapture();
        require (match->getStatus().hasTarget, "anti-phase stereo target retains its energy");
        const auto fit = match->fit (48000.0);
        require (fit.error.isEmpty() && std::abs (combined (fit, 1000, 48000)) < 0.1,
                 "captured anti-phase and quieter copy matches without tonal changes");

        match->setSettings ({ 65.0f, 0.8f, 6.0f });
        const auto xml = match->saveState();
        auto restored = std::make_unique<MatchEQ>();
        restored->restoreState (&xml);
        require (restored->getStatus().hasReference && restored->getStatus().hasTarget,
                 "both profiles survive state restore and plugin copying");
        require (restored->getSettings().amount == 65.0f && std::abs (restored->getSettings().smoothing - 0.8f) < 0.001f,
                 "match settings survive state restore");
        require (restored->fit (48000.0).error.isEmpty(), "restored profiles remain usable");

        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("paradyn-reference-test", ".pdeqref");
        struct Cleanup { juce::File file; ~Cleanup() { file.deleteFile(); } } cleanup { file };
        require (match->saveReference (file).wasOk(), "reference export succeeds");
        auto imported = std::make_unique<MatchEQ>();
        require (imported->loadReference (file).wasOk() && imported->getStatus().hasReference,
                 "reference imports into another instance");
        require (! imported->getStatus().hasTarget, "reference import does not invent a target profile");
        file.replaceWithText ("<ParaDynEQReference version=\"1\" sampleRate=\"48000\" seconds=\"2\"><Bin db=\"nan\"/></ParaDynEQReference>");
        require (imported->loadReference (file).failed() && imported->getStatus().hasReference,
                 "malformed reference rejected without losing previous reference");

        match->startCapture (MatchEQ::Capture::reference);
        feedFrames (*match, 48000.0, 2, 1, false, 0.0f);
        match->stopCapture();
        require (match->getStatus().hasReference && match->getStatus().seconds == 0.0,
                 "silence does not replace a valid reference");
        match->startCapture (MatchEQ::Capture::reference);
        feedFrames (*match, 48000.0, 1, 1, false);
        match->stopCapture();
        require (match->getStatus().message.contains ("Too little"), "short capture is rejected");
        match->restoreState (nullptr);
        require (! match->getStatus().hasReference && ! match->getStatus().hasTarget,
                 "legacy project restore clears stale profiles");

        match->startCapture (MatchEQ::Capture::target);
        feedFrames (*match, 48000.0, 36, 1, false, 0.1f, 4096);
        require (match->getStatus().capture == MatchEQ::Capture::none && match->getStatus().hasTarget,
                 "learning automatically completes after 12 seconds");

        match->prepare (96000.0);
        match->startCapture (MatchEQ::Capture::reference);
        feedFrames (*match, 96000.0, 6, 1, false);
        match->stopCapture();
        require (match->getStatus().hasReference && match->fit (96000.0).error.isEmpty(),
                 "profiles captured at different sample rates can be matched");
    }

    void testLearnedColouration()
    {
        auto match = std::make_unique<MatchEQ>();
        match->prepare (48000.0);
        SVFilter colour;
        colour.setCoefficients (48000.0, 1200.0f, 6.0f, 0.7f, FilterType::Bell);
        match->startCapture (MatchEQ::Capture::reference);
        feedFrames (*match, 48000.0, 8, 1, false, 0.1f, 257, &colour);
        match->stopCapture();
        match->startCapture (MatchEQ::Capture::target);
        feedFrames (*match, 48000.0, 8, 1, false);
        match->stopCapture();
        const auto fitted = match->fit (48000.0);
        require (fitted.error.isEmpty(), "filtered reference audio and raw target audio produce a match");
        require (combined (fitted, 1200, 48000) - combined (fitted, 100, 48000) > 4.0,
                 "learned match recovers reference colouration from actual audio");
        // Spectral matching intentionally excludes constant gain. Compare the
        // frequency-dependent difference after removing its mean.
        double mean = 0.0, squared = 0.0;
        for (int i = 0; i < 80; ++i)
        {
            const auto hz = 80.0 * std::pow (150.0, i / 79.0);
            const auto error = combined (fitted, hz, 48000) - MatchEQ::responseDb ({ 1200, 6, 0.7f }, hz, 48000);
            mean += error / 80.0;
            squared += error * error / 80.0;
        }
        require (std::sqrt (std::max (0.0, squared - mean * mean)) < 0.6,
                 "end-to-end audio learning and fitting recovers tonal shape within 0.6 dB RMS");
    }
}

int main()
{
    try
    {
        testFitting();
        testResponseAgainstAudio();
        testCaptureAndPersistence();
        testLearnedColouration();
        std::cout << "All Match EQ tests passed.\n";
        return 0;
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAILED: " << e.what() << '\n';
        return 1;
    }
}
