#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Capture is independent of the editor. The audio thread only fills a bounded
// SPSC queue; FFT, averaging, file/state conversion and fitting never run there.
class MatchEQ final : private juce::Thread
{
public:
    static constexpr int pointCount = 128;
    static constexpr int fftOrder = 14;
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr double minimumSeconds = 1.0;
    static constexpr double maximumSeconds = 12.0;
    using Curve = std::array<double, pointCount>;

    struct Profile
    {
        Curve db {};
        double sampleRate = 0.0;
        double seconds = 0.0;
        bool valid = false;
    };
    struct Settings
    {
        float amount = 100.0f;
        float smoothing = 0.5f; // octaves
        float limitDb = 9.0f;
    };
    enum class Capture { none, reference, target };
    struct Status
    {
        Capture capture = Capture::none;
        double seconds = 0.0;
        bool hasReference = false, hasTarget = false;
        juce::String message;
    };
    struct Band { float frequencyHz = 1000.0f, gainDb = 0.0f, q = 1.0f; };
    struct Fit
    {
        std::vector<Band> bands;
        juce::String error;
    };

    MatchEQ();
    ~MatchEQ() override;
    void startCapture (Capture);
    void stopCapture();
    Status getStatus() const;
    Settings getSettings() const;
    void setSettings (Settings);
    Fit fit (double processingSampleRate) const;

    // Single audio producer only. prepare() is called while processBlock is stopped.
    void prepare (double sampleRate) noexcept;
    void push (const juce::AudioBuffer<float>&) noexcept;

    juce::XmlElement saveState() const;
    void restoreState (const juce::XmlElement*);
    juce::Result saveReference (const juce::File&) const;
    juce::Result loadReference (const juce::File&);

    static double frequency (int index) noexcept;
    static Fit fitProfiles (const Profile&, const Profile&, Settings, double processingSampleRate);
    static double responseDb (const Band&, double frequencyHz, double sampleRate) noexcept;

private:
    struct Frame
    {
        std::array<float, fftSize> left {}, right {};
        uint64_t token = 0;
        double sampleRate = 0.0;
        int channels = 0;
    };
    static constexpr int queueSize = 8;
    void run() override;
    void analyse (const Frame&);
    void finishLocked();
    static std::unique_ptr<juce::XmlElement> profileXml (const Profile&, const char* tag);
    static Profile readProfile (const juce::XmlElement*);

    juce::AbstractFifo fifo { queueSize };
    std::unique_ptr<std::array<Frame, queueSize>> frames;
    Frame incoming;
    int incomingSamples = 0;
    double audioSampleRate = 44100.0;
    std::atomic<uint64_t> captureToken { 0 };
    uint64_t nextToken = 0;
    juce::dsp::FFT fft { fftOrder };
    juce::dsp::WindowingFunction<float> window { fftSize, juce::dsp::WindowingFunction<float>::hann };
    std::array<float, fftSize * 2> fftData {};
    std::array<double, fftSize / 2 + 1> power {};

    mutable juce::CriticalSection stateLock;
    Profile reference, target;
    Settings settings;
    Status status;
    Curve accumulatedPower {};
    int acceptedFrames = 0;
    double captureRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchEQ)
};
