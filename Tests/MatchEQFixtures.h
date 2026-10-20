#pragma once

#include "../Source/MatchEQ.h"
#include <cmath>
#include <stdexcept>

namespace MatchEQFixtures
{
    enum class Signal { reference, steadyResonance, intermittentResonance, pumping };

    // Deterministic audio, consumed by the actual background capture worker.
    // The only difference from the reference is the explicitly requested signal.
    inline void capture (MatchEQ& match, MatchEQ::Capture kind, Signal signal,
                         double rate = 48000.0, bool antiPhase = false, int frames = 12)
    {
        match.prepare (rate);
        match.startCapture (kind);
        juce::Random random (7319);
        juce::AudioBuffer<float> buffer (2, 512);
        int position = 0;
        for (int frame = 0; frame < frames; ++frame)
        {
            for (int block = 0; block < MatchEQ::fftSize / 512; ++block)
            {
                for (int i = 0; i < 512; ++i, ++position)
                {
                    const auto seconds = position / rate;
                    auto sample = 0.03f * (random.nextFloat() * 2.0f - 1.0f);
                    const bool burst = std::fmod (seconds, 0.65) >= 0.4;
                    if (signal == Signal::steadyResonance || (signal == Signal::intermittentResonance && burst))
                        sample += 0.18f * (float) std::sin (juce::MathConstants<double>::twoPi * 1000.0 * seconds);
                    if (signal == Signal::pumping) sample *= burst ? 0.2f : 1.0f;
                    buffer.setSample (0, i, sample);
                    buffer.setSample (1, i, antiPhase ? -sample : sample);
                }
                match.push (buffer);
            }
            const auto expected = (frame + 1) * MatchEQ::fftSize / rate;
            bool consumed = false;
            for (int attempt = 0; attempt < 1500; ++attempt)
            {
                if (match.getStatus().seconds >= expected - 0.00001) { consumed = true; break; }
                juce::Thread::sleep (2);
            }
            if (! consumed) throw std::runtime_error ("temporal fixture capture worker timed out");
        }
        match.stopCapture();
    }

    inline int dynamicCount (const MatchEQ::Fit& fit)
    {
        int count = 0;
        for (const auto& band : fit.bands) if (band.dynamic) ++count;
        return count;
    }
}
