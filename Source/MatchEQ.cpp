#include "MatchEQ.h"
#include <algorithm>
#include <cmath>

namespace
{
    MatchEQ::Settings sanitise (MatchEQ::Settings s)
    {
        s.amount = std::isfinite (s.amount) ? juce::jlimit (0.0f, 100.0f, s.amount) : 100.0f;
        s.smoothing = std::isfinite (s.smoothing) ? juce::jlimit (0.15f, 2.0f, s.smoothing) : 0.5f;
        s.limitDb = std::isfinite (s.limitDb) ? juce::jlimit (1.0f, 18.0f, s.limitDb) : 9.0f;
        return s;
    }
}

MatchEQ::MatchEQ() : Thread ("Match EQ analysis"), frames (std::make_unique<std::array<Frame, queueSize>>()),
                     gridBank (std::make_unique<std::array<GridDetector, tempGridCount>>())
{
    status.message = "Learn a reference, then learn the destination track.";
    startThread();
}

MatchEQ::~MatchEQ()
{
    signalThreadShouldExit();
    notify();
    stopThread (-1);
}

void MatchEQ::prepare (double sampleRate) noexcept
{
    audioSampleRate = sampleRate;
    incomingSamples = 0;
    incoming.token = 0;
    // The consumer owns its read position. Never reset the shared FIFO here.
}

void MatchEQ::push (const juce::AudioBuffer<float>& buffer) noexcept
{
    const auto token = captureToken.load (std::memory_order_acquire);
    if (token == 0 || buffer.getNumChannels() == 0 || audioSampleRate < 8000.0)
        return;
    const int channels = juce::jmin (2, buffer.getNumChannels());
    if (incoming.token != token || std::abs (incoming.sampleRate - audioSampleRate) > 0.01 || incoming.channels != channels)
    {
        incomingSamples = 0;
        incoming.token = token;
        incoming.sampleRate = audioSampleRate;
        incoming.channels = channels;
    }
    for (int i = 0; i < buffer.getNumSamples(); ++i)
    {
        incoming.left[(size_t) incomingSamples] = buffer.getSample (0, i);
        incoming.right[(size_t) incomingSamples] = buffer.getSample (channels - 1, i);
        if (++incomingSamples != fftSize)
            continue;
        incomingSamples = 0;
        int a, n, b, m;
        fifo.prepareToWrite (1, a, n, b, m);
        if (n > 0)
        {
            (*frames)[(size_t) a] = incoming;
            fifo.finishedWrite (1);
        }
        // If a host renders faster than analysis, drop a frame rather than block.
    }
}

void MatchEQ::startCapture (Capture kind)
{
    if (kind == Capture::none) return;
    const juce::ScopedLock lock (stateLock);
    accumulatedPower.fill (0.0);
    acceptedFrames = 0;
    captureRate = 0.0;
    // Temporal stats only matter for the target; re-arm lazily once the first
    // frame establishes the capture sample rate (see analyse()).
    temporalArmed = false;
    tempSumLevel.fill (0.0); tempSumRel.fill (0.0); tempSumRelSq.fill (0.0); tempObs.fill (0.0);
    status.capture = kind;
    status.seconds = 0.0;
    status.message = kind == Capture::reference ? "Learning reference input..." : "Learning target input...";
    captureToken.store (++nextToken, std::memory_order_release);
    notify();
}

void MatchEQ::stopCapture()
{
    const juce::ScopedLock lock (stateLock);
    if (status.capture != Capture::none) finishLocked();
}

void MatchEQ::finishLocked()
{
    captureToken.store (0, std::memory_order_release);
    if (status.seconds >= minimumSeconds && acceptedFrames > 0)
    {
        Profile p;
        p.valid = true;
        p.sampleRate = captureRate;
        p.seconds = status.seconds;
        for (size_t i = 0; i < p.db.size(); ++i)
            p.db[i] = 10.0 * std::log10 (std::max (1.0e-20, accumulatedPower[i] / acceptedFrames));
        if (status.capture == Capture::target)
        {
            // Finalise temporal stats only if we gathered enough target audio.
            // A shorter (or silent) capture keeps the static profile but carries
            // no temporal stats -> static-only match, no automatic dynamics.
            Temporal t;
            t.sampleRate = captureRate;
            t.seconds = status.seconds;
            // Only require minObs for in-band grid points; out-of-band points are
            // never accumulating (by design) so must not gate the validity check.
            double minInBandObs = 1.0e18;
            bool anyInBand = false;
            for (int i = 0; i < tempGridCount; ++i)
            {
                if (! tempInBand[(size_t) i]) continue;
                anyInBand = true;
                minInBandObs = std::min (minInBandObs, tempObs[(size_t) i]);
            }
            if (status.seconds >= temporalMinSeconds && anyInBand && minInBandObs >= 64.0)
            {
                for (int i = 0; i < tempGridCount; ++i)
                {
                    const auto n = tempObs[(size_t) i];
                    t.obs[(size_t) i] = n;
                    if (n >= 1.0) // out-of-band points have n=0; skip to avoid divide-by-zero
                    {
                        t.meanDb[(size_t) i] = tempSumLevel[(size_t) i] / n;
                        const auto meanRel = tempSumRel[(size_t) i] / n;
                        t.stdRelDb[(size_t) i] = std::sqrt (std::max (0.0, tempSumRelSq[(size_t) i] / n - meanRel * meanRel));
                    }
                }
                t.valid = true;
            }
            p.temporal = t;
        }
        if (status.capture == Capture::reference) reference = p; else target = p;
        status.message = status.capture == Capture::reference ? "Reference ready. Learn the destination track."
                                                             : "Target ready. Apply Match replaces the current EQ bands.";
    }
    else
        status.message = "Too little audio: play at least 1 second. Previous profile kept.";
    status.capture = Capture::none;
}

MatchEQ::Status MatchEQ::getStatus() const
{
    const juce::ScopedLock lock (stateLock);
    auto result = status;
    result.hasReference = reference.valid;
    result.hasTarget = target.valid;
    result.targetNeedsRelearnForDynamics = target.valid && ! target.temporal.valid;
    return result;
}

std::array<MatchEQ::Profile, 2> MatchEQ::getProfiles() const
{
    const juce::ScopedLock lock (stateLock);
    return { reference, target };
}

MatchEQ::Settings MatchEQ::getSettings() const
{
    const juce::ScopedLock lock (stateLock);
    return settings;
}

void MatchEQ::setSettings (Settings s)
{
    const juce::ScopedLock lock (stateLock);
    settings = sanitise (s);
}

void MatchEQ::run()
{
    while (! threadShouldExit())
    {
        int a, n, b, m;
        fifo.prepareToRead (1, a, n, b, m);
        if (n > 0)
        {
            analyse ((*frames)[(size_t) a]);
            fifo.finishedRead (1);
        }
        else
            wait (captureToken.load (std::memory_order_acquire) == 0 ? -1 : 5);
    }
}

double MatchEQ::frequency (int index) noexcept
{
    return 20.0 * std::pow (1000.0, (double) index / (pointCount - 1));
}

void MatchEQ::analyse (const Frame& frame)
{
    if (frame.token != captureToken.load (std::memory_order_acquire)) return;
    double energy = 0.0;
    power.fill (0.0);
    for (int channel = 0; channel < frame.channels; ++channel)
    {
        const auto& samples = channel == 0 ? frame.left : frame.right;
        for (int i = 0; i < fftSize; ++i)
        {
            const auto sample = samples[(size_t) i];
            if (! std::isfinite (sample)) return;
            energy += (double) sample * sample / (fftSize * frame.channels);
            fftData[(size_t) i] = sample;
        }
        std::fill (fftData.begin() + fftSize, fftData.end(), 0.0f);
        window.multiplyWithWindowingTable (fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());
        for (int i = 1; i <= fftSize / 2; ++i)
        {
            if (! std::isfinite (fftData[(size_t) i])) return;
            power[(size_t) i] += (double) fftData[(size_t) i] * fftData[(size_t) i]
                                / ((double) fftSize * fftSize * frame.channels);
        }
    }
    if (energy < 1.0e-10) return; // ignore silence below -100 dBFS RMS

    Curve framePower {};
    const double binHz = frame.sampleRate / fftSize;
    const auto halfWidth = std::pow (1000.0, 0.5 / (pointCount - 1));
    for (int i = 0; i < pointCount; ++i)
    {
        const auto hz = frequency (i);
        if (hz >= frame.sampleRate * 0.49) continue;
        const int first = juce::jlimit (1, fftSize / 2, (int) std::ceil (hz / halfWidth / binHz));
        const int last = juce::jlimit (first, fftSize / 2, (int) std::floor (hz * halfWidth / binHz));
        if (hz * halfWidth - hz / halfWidth < binHz)
        {
            const auto position = juce::jlimit (1.0, (double) fftSize / 2 - 1.0, hz / binHz);
            const auto lo = (size_t) position;
            framePower[(size_t) i] = power[lo] + (position - (double) lo) * (power[lo + 1] - power[lo]);
        }
        else
        {
            for (int bin = first; bin <= last; ++bin) framePower[(size_t) i] += power[(size_t) bin];
            framePower[(size_t) i] /= last - first + 1;
        }
    }
    const juce::ScopedLock lock (stateLock);
    if (frame.token != captureToken.load (std::memory_order_acquire)) return;
    if (captureRate > 0.0 && std::abs (captureRate - frame.sampleRate) > 0.01)
    {
        accumulatedPower.fill (0.0);
        acceptedFrames = 0;
        status.seconds = 0.0;
        temporalArmed = false; // sample rate changed mid-capture: discard temporal history
        tempSumLevel.fill (0.0); tempSumRel.fill (0.0); tempSumRelSq.fill (0.0); tempObs.fill (0.0);
    }
    captureRate = frame.sampleRate;
    for (size_t i = 0; i < accumulatedPower.size(); ++i) accumulatedPower[i] += framePower[i];
    ++acceptedFrames;
    status.seconds += fftSize / captureRate;
    if (status.capture == Capture::target)
    {
        if (! temporalArmed) resetTemporalLocked (captureRate);
        accumulateTemporalLocked (frame);
    }
    if (status.seconds >= maximumSeconds) finishLocked();
}

double MatchEQ::tempFrequency (int index) noexcept
{
    return 20.0 * std::pow (1000.0, (double) index / (tempGridCount - 1));
}

int MatchEQ::nearestTempPoint (double frequencyHz) noexcept
{
    const auto clamped = juce::jlimit (20.0, 20000.0, frequencyHz);
    const auto position = std::log (clamped / 20.0) / std::log (1000.0) * (tempGridCount - 1);
    return juce::jlimit (0, tempGridCount - 1, (int) std::lround (position));
}

void MatchEQ::resetTemporalLocked (double sampleRate)
{
    tempPreSmoothCoeff = (float) std::exp (-1.0 / (0.001 * 3.0 * sampleRate)); // 3 ms, matches DynamicDetector
    tempHopCounter = 0;
    tempStartupSamples = (int) std::round (sampleRate * 0.1); // ~100ms: skip gain-change / startup transients
    for (int i = 0; i < tempGridCount; ++i)
    {
        auto& d = (*gridBank)[(size_t) i];
        d.svfL.reset();
        d.svfR.reset();
        d.rmsL = 0.0f;
        d.rmsR = 0.0f;
        const auto freq = (float) tempFrequency (i);
        // Only set up filters for in-band points to avoid Nyquist artefacts.
        const bool inBand = tempFrequency (i) < sampleRate * 0.45;
        tempInBand[(size_t) i] = inBand;
        if (inBand)
        {
            d.svfL.setCoefficients (sampleRate, freq, 0.0f, tempQ, FilterType::Bell);
            d.svfR.setCoefficients (sampleRate, freq, 0.0f, tempQ, FilterType::Bell);
        }
    }
    temporalArmed = true;
}

void MatchEQ::accumulateTemporalLocked (const Frame& frame)
{
    const auto hasRight = frame.channels > 1;
    std::array<double, tempGridCount> levelDb {};
    std::array<bool, tempGridCount> valid {};
    for (int s = 0; s < fftSize; ++s)
    {
        const auto leftSample  = frame.left[(size_t) s];
        const auto rightSample = hasRight ? frame.right[(size_t) s] : leftSample;

        for (int i = 0; i < tempGridCount; ++i)
        {
            if (! tempInBand[(size_t) i]) continue; // don't process above Nyquist
            auto& d = (*gridBank)[(size_t) i];
            // Independent L/R bandpass: avoids cancelling antiphase stereo content.
            const auto bpL = d.svfL.processSample (leftSample).bandpass;
            const auto bpR = d.svfR.processSample (rightSample).bandpass;
            d.rmsL = tempPreSmoothCoeff * d.rmsL + (1.0f - tempPreSmoothCoeff) * bpL * bpL;
            d.rmsR = tempPreSmoothCoeff * d.rmsR + (1.0f - tempPreSmoothCoeff) * bpR * bpR;
        }
        // Skip initial startup samples AFTER filters warm up, before recording stats.
        if (tempStartupSamples > 0) { --tempStartupSamples; continue; }
        if (++tempHopCounter < tempHop) continue;
        tempHopCounter = 0;

        double broadband = 0.0; int broadbandCount = 0;
        for (int i = 0; i < tempGridCount; ++i)
        {
            if (! tempInBand[(size_t) i]) continue;
            // Max of L/R matches the runtime stereo DynamicDetector's semantics.
            const auto rms = (double) std::max ((*gridBank)[(size_t) i].rmsL,
                                                 (*gridBank)[(size_t) i].rmsR);
            valid[(size_t) i] = rms > 1.0e-14;
            if (! valid[(size_t) i]) continue;
            levelDb[(size_t) i] = 10.0 * std::log10 (rms);
            broadband += levelDb[(size_t) i];
            ++broadbandCount;
        }
        if (broadbandCount < tempGridCount / 4) continue; // near silence: nothing to characterise
        broadband /= broadbandCount;
        for (int i = 0; i < tempGridCount; ++i)
        {
            if (! tempInBand[(size_t) i] || ! valid[(size_t) i]) continue;
            const auto rel = levelDb[(size_t) i] - broadband;
            tempSumLevel[(size_t) i] += levelDb[(size_t) i];
            tempSumRel[(size_t) i] += rel;
            tempSumRelSq[(size_t) i] += rel * rel;
            tempObs[(size_t) i] += 1.0;
        }
    }
}

double MatchEQ::responseDb (const Band& band, double hz, double rate) noexcept
{
    // Exact steady-state response of the existing TPT bell (SVFilter), using
    // its bilinear frequency mapping and gain-dependent damping.
    const auto centre = juce::jlimit (10.0, rate * 0.49, (double) band.frequencyHz);
    const auto r = std::tan (juce::MathConstants<double>::pi * hz / rate)
                 / std::tan (juce::MathConstants<double>::pi * centre / rate);
    const auto a = std::pow (10.0, band.gainDb / 40.0);
    const auto real = 1.0 - r * r;
    const auto numerator = r * a / band.q, denominator = r / (a * band.q);
    return 10.0 * std::log10 ((real * real + numerator * numerator)
                            / (real * real + denominator * denominator));
}

MatchEQ::Fit MatchEQ::fit (double rate) const
{
    Profile ref, dst;
    Settings s;
    {
        const juce::ScopedLock lock (stateLock);
        if (status.capture != Capture::none) return { {}, "Stop learning before applying Match." };
        ref = reference; dst = target; s = settings;
    }
    return fitProfiles (ref, dst, s, rate);
}

MatchEQ::Fit MatchEQ::fitProfiles (const Profile& ref, const Profile& dst, Settings s, double rate)
{
    if (! ref.valid || ! dst.valid) return { {}, "Learn both reference and target first." };
    if (! std::isfinite (ref.sampleRate) || ! std::isfinite (dst.sampleRate)
        || ref.sampleRate < 8000.0 || dst.sampleRate < 8000.0)
        return { {}, "Invalid spectral profile sample rate." };
    if (! std::isfinite (rate) || rate < 8000.0) return { {}, "Start audio playback before applying Match." };
    s = sanitise (s);
    Curve difference {}, wanted {}, response {}, usable {};
    const auto peakRef = *std::max_element (ref.db.begin(), ref.db.end());
    const auto peakDst = *std::max_element (dst.db.begin(), dst.db.end());
    const auto maxHz = std::min ({ 20000.0, ref.sampleRate * 0.45, dst.sampleRate * 0.45, rate * 0.45 });
    double offset = 0.0, count = 0.0;
    for (int i = 0; i < pointCount; ++i)
    {
        const auto k = (size_t) i;
        if (! std::isfinite (ref.db[k]) || ! std::isfinite (dst.db[k]))
            return { {}, "Invalid spectral profile." };
        if (frequency (i) <= maxHz && ref.db[k] > peakRef - 55.0 && dst.db[k] > peakDst - 55.0)
        {
            usable[k] = 1.0;
            difference[k] = ref.db[k] - dst.db[k];
            offset += difference[k];
            count += 1.0;
        }
    }
    if (count < 8.0) return { {}, "Not enough shared frequency content. Learn a broader passage." };
    offset /= count; // remove overall level, retaining spectral balance only
    const auto octaveStep = std::log2 (1000.0) / (pointCount - 1);
    const auto sigma = s.smoothing / 2.355; // full width at half maximum
    for (int i = 0; i < pointCount; ++i)
    {
        double sum = 0.0, weights = 0.0;
        for (int j = 0; j < pointCount; ++j)
        {
            const auto distance = (i - j) * octaveStep / sigma;
            const auto weight = usable[(size_t) j] * std::exp (-0.5 * distance * distance);
            sum += weight * (difference[(size_t) j] - offset);
            weights += weight;
        }
        if (usable[(size_t) i] > 0.0 && weights > 0.0)
            wanted[(size_t) i] = juce::jlimit (-(double) s.limitDb, (double) s.limitDb, sum / weights)
                                * s.amount / 100.0;
    }

    Fit result;
    constexpr int bandCount = 16;
    const auto highestCentre = std::min (16000.0, maxHz * 0.9);
    for (int i = 0; i < bandCount; ++i)
        result.bands.push_back ({ (float) (30.0 * std::pow (highestCentre / 30.0, (double) i / (bandCount - 1))),
                                 0.0f, 1.1f });
    // Coordinate descent fits the combined filter response, so overlapping bells
    // don't each apply the entire requested correction independently.
    for (auto step : { 3.0f, 1.0f, 0.25f, 0.05f })
        for (int pass = 0; pass < 4; ++pass)
            for (auto& band : result.bands)
            {
                Curve remainder {}, bestCurve {};
                double bestError = 0.0;
                for (int i = 0; i < pointCount; ++i)
                {
                    const auto k = (size_t) i;
                    bestCurve[k] = frequency (i) < rate * 0.49 ? responseDb (band, frequency (i), rate) : 0.0;
                    remainder[k] = response[k] - bestCurve[k];
                    bestError += std::pow (response[k] - wanted[k], 2.0);
                }
                auto bestGain = band.gainDb;
                for (const auto direction : { -1.0f, 1.0f })
                {
                    auto candidate = band;
                    candidate.gainDb = juce::jlimit (-s.limitDb, s.limitDb, band.gainDb + direction * step);
                    Curve curve {};
                    double error = 0.0;
                    for (int i = 0; i < pointCount; ++i)
                    {
                        const auto k = (size_t) i;
                        curve[k] = frequency (i) < rate * 0.49 ? responseDb (candidate, frequency (i), rate) : 0.0;
                        error += std::pow (remainder[k] + curve[k] - wanted[k], 2.0);
                    }
                    if (error < bestError) { bestError = error; bestGain = candidate.gainDb; bestCurve = curve; }
                }
                band.gainDb = bestGain;
                for (size_t i = 0; i < response.size(); ++i) response[i] = remainder[i] + bestCurve[i];
            }

    // Bound the combined EQ as well as individual bands, on a denser grid.
    const auto unscaled = result.bands;
    float low = 0.0f, high = 1.0f;
    for (int iteration = 0; iteration < 18; ++iteration)
    {
        const auto scale = iteration == 0 ? 1.0f : (low + high) * 0.5f;
        double peak = 0.0;
        for (int i = 0; i < 512; ++i)
        {
            const auto hz = 20.0 * std::pow (std::min (20000.0, rate * 0.49) / 20.0, i / 511.0);
            double db = 0.0;
            for (auto band : unscaled) { band.gainDb *= scale; db += responseDb (band, hz, rate); }
            peak = std::max (peak, std::abs (db));
        }
        if (peak <= s.limitDb) { low = scale; if (scale == 1.0f) break; } else high = scale;
    }
    for (auto& band : result.bands) band.gainDb *= low;

    // ── Automatic dynamic assignment ──────────────────────────────────────────
    // Only when amount > 0 and the target capture carries valid temporal stats
    // (requires >= 3s of target audio captured with the new bank).
    if (s.amount > 0.0f && dst.temporal.valid)
    {
        const auto targetNyquist = dst.sampleRate * 0.45;
        struct Candidate { int bandIdx; float absGain; float freq; };
        std::vector<Candidate> candidates;
        for (int bi = 0; bi < (int) result.bands.size(); ++bi)
        {
            const auto& band = result.bands[(size_t) bi];
            if (band.gainDb > -1.0f) continue;                           // cuts only
            if ((double) band.frequencyHz >= targetNyquist) continue;    // in-band only
            const int tp = nearestTempPoint ((double) band.frequencyHz);
            if (dst.temporal.obs[(size_t) tp] < 64.0) continue;         // need sufficient data
            if (dst.temporal.stdRelDb[(size_t) tp] < 5.0) continue;     // needs meaningful variance
            if (dst.temporal.meanDb[(size_t) tp] < -65.0) continue;     // must carry signal
            candidates.push_back ({ bi, std::abs (band.gainDb), band.frequencyHz });
        }
        // Sort by strongest cut first.
        std::sort (candidates.begin(), candidates.end(),
                   [] (const Candidate& a, const Candidate& b) { return a.absGain > b.absGain; });
        // Greedy non-adjacent selection: at most 3, >= 0.75 octave apart.
        std::vector<int> selectedIdx;
        std::vector<float> selectedFreqs;
        for (const auto& c : candidates)
        {
            if ((int) selectedIdx.size() >= 3) break;
            bool tooClose = false;
            for (auto sf : selectedFreqs)
                if (std::abs (std::log2 ((double) c.freq / (double) sf)) < 0.75) { tooClose = true; break; }
            if (tooClose) continue;
            selectedIdx.push_back (c.bandIdx);
            selectedFreqs.push_back (c.freq);
        }

        for (int bi : selectedIdx)
        {
            auto& band = result.bands[(size_t) bi];
            const float originalGain = band.gainDb;          // scaled by `low`, still negative
            const float rangeDb = std::abs (originalGain) * 0.5f;
            band.gainDb   = originalGain + rangeDb;          // half the cut is static
            band.rangeDb  = rangeDb;
            band.dynamic  = true;
            band.ratio    = 3.0f;
            band.attackMs = 15.0f;
            band.releaseMs = 150.0f;
            band.kneeDb   = 3.0f;
        }

        // Re-bound both the static curve (gainDb values after split) and the
        // fully-reduced curve (gainDb - rangeDb = originalGain) to stay within limitDb.
        // Monotonicity of bell response means checking endpoints is sufficient.
        if (! selectedIdx.empty())
        {
            const auto preDyn = result.bands;
            float low2 = 0.0f, high2 = 1.0f;
            for (int iteration = 0; iteration < 18; ++iteration)
            {
                const auto scale = iteration == 0 ? 1.0f : (low2 + high2) * 0.5f;
                double peak = 0.0;
                for (int i = 0; i < 512; ++i)
                {
                    const auto hz = 20.0 * std::pow (std::min (20000.0, rate * 0.49) / 20.0, i / 511.0);
                    double staticDb = 0.0, reducedDb = 0.0;
                    for (const auto& band : preDyn)
                    {
                        const auto sg = band.gainDb * scale;
                        const auto sr = band.rangeDb * scale;
                        staticDb  += responseDb ({ band.frequencyHz, (float) sg, band.q }, hz, rate);
                        reducedDb += responseDb ({ band.frequencyHz, (float) (sg - sr), band.q }, hz, rate);
                    }
                    peak = std::max (peak, std::max (std::abs (staticDb), std::abs (reducedDb)));
                }
                if (peak <= s.limitDb) { low2 = scale; if (scale == 1.0f) break; } else high2 = scale;
            }
            for (auto& band : result.bands)
            {
                band.gainDb  *= low2;
                band.rangeDb *= low2;
                // Don't create sub-step range values that round to 0 at 0.01 dB resolution.
                if (band.dynamic && band.rangeDb < 0.01f)
                {
                    band.dynamic = false;
                    band.rangeDb = 0.0f;
                }
            }
        }
        // Recompute thresholds after final gain scaling, using all prior bands' static
        // response at each dynamic band's centre. Correction term band.gainDb/2 accounts
        // for the bandpass detector seeing the static bell's gain at its own centre
        // (TPT bell v1 peak = Q*a where a=10^(gain/40), reducing to gainDb/2 in dB).
        for (int bi = 0; bi < (int) result.bands.size(); ++bi)
        {
            auto& band = result.bands[(size_t) bi];
            if (! band.dynamic) continue;
            const int tp = nearestTempPoint ((double) band.frequencyHz);
            double precedingDb = 0.0;
            for (int j = 0; j < bi; ++j)
                precedingDb += responseDb (result.bands[(size_t) j], (double) band.frequencyHz, rate);
            const float rawThreshold = (float) (dst.temporal.meanDb[(size_t) tp] + 3.0
                                              + precedingDb + (double) band.gainDb * 0.5);
            band.thresholdDb = juce::jlimit (-60.0f, 0.0f, rawThreshold);
        }
    }

    return result;
}

std::unique_ptr<juce::XmlElement> MatchEQ::profileXml (const Profile& p, const char* tag)
{
    auto xml = std::make_unique<juce::XmlElement> (tag);
    if (! p.valid) return xml;
    xml->setAttribute ("version", 1);
    xml->setAttribute ("sampleRate", p.sampleRate);
    xml->setAttribute ("seconds", p.seconds);
    for (auto db : p.db) xml->createNewChildElement ("Bin")->setAttribute ("db", db);
    // Optional temporal stats (target only). Readers that iterate only "Bin" children
    // (e.g. old standalone-reference loaders) skip this block; DAW state saved at
    // version 2 is rejected by a reader that only accepts version 1, so the block
    // is unreachable either way. A current reader validates it and, if malformed,
    // drops only the Temporal while keeping the static profile.
    if (p.temporal.valid)
    {
        auto* t = xml->createNewChildElement ("Temporal");
        t->setAttribute ("version", 1);
        t->setAttribute ("sampleRate", p.temporal.sampleRate);
        t->setAttribute ("seconds", p.temporal.seconds);
        t->setAttribute ("points", tempGridCount);
        for (int i = 0; i < tempGridCount; ++i)
        {
            auto* e = t->createNewChildElement ("T");
            e->setAttribute ("m", p.temporal.meanDb[(size_t) i]);
            e->setAttribute ("r", p.temporal.stdRelDb[(size_t) i]);
            e->setAttribute ("n", p.temporal.obs[(size_t) i]);
        }
    }
    return xml;
}

MatchEQ::Profile MatchEQ::readProfile (const juce::XmlElement* xml)
{
    Profile p;
    if (xml == nullptr || xml->getIntAttribute ("version") != 1) return p;
    p.sampleRate = xml->getDoubleAttribute ("sampleRate");
    p.seconds = xml->getDoubleAttribute ("seconds");
    if (! std::isfinite (p.sampleRate) || p.sampleRate < 8000.0 || p.sampleRate > 768000.0
        || ! std::isfinite (p.seconds) || p.seconds < minimumSeconds || p.seconds > maximumSeconds + 3.0) return {};
    int i = 0;
    for (auto* child : xml->getChildIterator())
    {
        if (! child->hasTagName ("Bin")) continue; // ignore optional sibling blocks (e.g. Temporal)
        const auto value = child->getDoubleAttribute ("db", -999.0);
        if (i >= pointCount || ! std::isfinite (value) || value < -200.0 || value > 100.0) return {};
        p.db[(size_t) i++] = value;
    }
    if (i != pointCount) return {};
    p.valid = true;

    // Temporal stats are optional. If present but malformed, drop them silently
    // rather than rejecting the whole (valid) static profile -- this guarantees an
    // invalid optional block never drives a bogus automatic-dynamics decision.
    if (const auto* t = xml->getChildByName ("Temporal"))
    {
        Temporal temporal;
        temporal.sampleRate = t->getDoubleAttribute ("sampleRate");
        temporal.seconds = t->getDoubleAttribute ("seconds");
        const bool header = t->getIntAttribute ("version") == 1 && t->getIntAttribute ("points") == tempGridCount
            && std::isfinite (temporal.sampleRate) && temporal.sampleRate >= 8000.0 && temporal.sampleRate <= 768000.0
            && std::abs (temporal.sampleRate - p.sampleRate) < 1.0  // must match profile's rate
            && std::isfinite (temporal.seconds) && temporal.seconds >= temporalMinSeconds
            && temporal.seconds <= p.seconds + 0.01                 // can't exceed the total capture
            && temporal.seconds <= maximumSeconds + 3.0;
        int j = 0;
        bool ok = header;
        const auto maxObs = std::ceil (temporal.seconds * temporal.sampleRate / (double) tempHop) + 1.0;
        if (ok)
            for (auto* e : t->getChildIterator())
            {
                if (! e->hasTagName ("T")) continue;
                const auto m = e->getDoubleAttribute ("m", -999.0);
                const auto r = e->getDoubleAttribute ("r", -999.0);
                const auto n = e->getDoubleAttribute ("n", -1.0);
                if (j >= tempGridCount || ! std::isfinite (m) || m < -200.0 || m > 100.0
                    || ! std::isfinite (r) || r < 0.0 || r > 200.0 || ! std::isfinite (n) || n < 0.0
                    || n > maxObs)
                    { ok = false; break; }
                temporal.meanDb[(size_t) j] = m;
                temporal.stdRelDb[(size_t) j] = r;
                temporal.obs[(size_t) j] = n;
                ++j;
            }
        if (ok && j == tempGridCount) { temporal.valid = true; p.temporal = temporal; }
    }
    return p;
}

juce::XmlElement MatchEQ::saveState() const
{
    const juce::ScopedLock lock (stateLock);
    juce::XmlElement xml ("MatchEQ");
    // Version 2 adds optional per-point temporal stats inside the Target profile.
    // Version-1 states (static profiles only) still load; see restoreState().
    xml.setAttribute ("version", 2);
    xml.setAttribute ("amount", settings.amount);
    xml.setAttribute ("smoothing", settings.smoothing);
    xml.setAttribute ("limit", settings.limitDb);
    xml.addChildElement (profileXml (reference, "Reference").release());
    xml.addChildElement (profileXml (target, "Target").release());
    return xml;
}

void MatchEQ::restoreState (const juce::XmlElement* xml)
{
    const juce::ScopedLock lock (stateLock);
    captureToken.store (0, std::memory_order_release);
    status = {};
    reference = {}; target = {}; settings = {};
    if (xml != nullptr && xml->hasTagName ("MatchEQ")
        && xml->getIntAttribute ("version") >= 1 && xml->getIntAttribute ("version") <= 2)
    {
        reference = readProfile (xml->getChildByName ("Reference"));
        target = readProfile (xml->getChildByName ("Target"));
        settings = sanitise ({ (float) xml->getDoubleAttribute ("amount", 100.0),
                               (float) xml->getDoubleAttribute ("smoothing", 0.5),
                               (float) xml->getDoubleAttribute ("limit", 9.0) });
    }
    status.message = reference.valid ? "Reference restored. Learn target or apply Match."
                                     : "Learn a reference, then learn the destination track.";
}

juce::Result MatchEQ::saveReference (const juce::File& file) const
{
    Profile p;
    { const juce::ScopedLock lock (stateLock); p = reference; }
    if (! p.valid) return juce::Result::fail ("Learn a reference first.");
    const auto xml = profileXml (p, "ParaDynEQReference");
    return file.replaceWithText (xml->toString()) ? juce::Result::ok()
                                                : juce::Result::fail ("Could not save reference file.");
}

juce::Result MatchEQ::loadReference (const juce::File& file)
{
    if (file.getSize() > 128 * 1024) return juce::Result::fail ("Reference file is too large.");
    const auto xml = juce::XmlDocument::parse (file);
    const auto p = xml != nullptr && xml->hasTagName ("ParaDynEQReference") ? readProfile (xml.get()) : Profile {};
    if (! p.valid) return juce::Result::fail ("Invalid ParaDyn EQ reference file.");
    const juce::ScopedLock lock (stateLock);
    captureToken.store (0, std::memory_order_release);
    status.capture = Capture::none;
    reference = p;
    status.message = "Reference loaded. Learn target or apply Match.";
    return juce::Result::ok();
}
