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

MatchEQ::MatchEQ() : Thread ("Match EQ analysis"), frames (std::make_unique<std::array<Frame, queueSize>>())
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
    return result;
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
    }
    captureRate = frame.sampleRate;
    for (size_t i = 0; i < accumulatedPower.size(); ++i) accumulatedPower[i] += framePower[i];
    ++acceptedFrames;
    status.seconds += fftSize / captureRate;
    if (status.seconds >= maximumSeconds) finishLocked();
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
    return xml;
}

MatchEQ::Profile MatchEQ::readProfile (const juce::XmlElement* xml)
{
    Profile p;
    if (xml == nullptr || xml->getIntAttribute ("version") != 1 || xml->getNumChildElements() != pointCount) return p;
    p.sampleRate = xml->getDoubleAttribute ("sampleRate");
    p.seconds = xml->getDoubleAttribute ("seconds");
    if (! std::isfinite (p.sampleRate) || p.sampleRate < 8000.0 || p.sampleRate > 768000.0
        || ! std::isfinite (p.seconds) || p.seconds < minimumSeconds || p.seconds > maximumSeconds + 3.0) return {};
    int i = 0;
    for (auto* child : xml->getChildIterator())
    {
        const auto value = child->getDoubleAttribute ("db", -999.0);
        if (! child->hasTagName ("Bin") || ! std::isfinite (value) || value < -200.0 || value > 100.0) return {};
        p.db[(size_t) i++] = value;
    }
    p.valid = true;
    return p;
}

juce::XmlElement MatchEQ::saveState() const
{
    const juce::ScopedLock lock (stateLock);
    juce::XmlElement xml ("MatchEQ");
    xml.setAttribute ("version", 1);
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
    if (xml != nullptr && xml->hasTagName ("MatchEQ") && xml->getIntAttribute ("version") == 1)
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
