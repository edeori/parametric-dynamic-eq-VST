#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>
#include "SVFilter.h"

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

    // Temporal-analysis grid (coarser than the FFT grid): a bank of band-pass
    // detector front-ends whose output level over time characterises how
    // *intermittent* each frequency region is. Only populated for the target
    // capture, and only used to decide/configure automatic dynamic cuts.
    static constexpr int tempGridCount = 48;
    static constexpr int tempHop = 64;          // envelope sampled every 64 samples
    static constexpr double temporalMinSeconds = 3.0; // need this much target audio for dynamics
    static constexpr float tempQ = 1.1f;        // matches the fitted bells' Q (see fitProfiles)

    // Per-grid-point temporal statistics, expressed in the runtime dynamic
    // detector's own scale (SVFilter band-pass v1 -> 3 ms RMS -> dB). Optional:
    // absent on legacy (pre-feature) captures, which stay valid for static matching.
    struct Temporal
    {
        std::array<double, tempGridCount> meanDb {};    // mean band level (detector dB)
        std::array<double, tempGridCount> stdRelDb {};  // std-dev of (band level - broadband level)
        std::array<double, tempGridCount> obs {};       // observation count per point
        double sampleRate = 0.0;
        double seconds = 0.0;
        bool valid = false;
    };

    struct Profile
    {
        Curve db {};
        double sampleRate = 0.0;
        double seconds = 0.0;
        bool valid = false;
        Temporal temporal {};
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
        // True when a valid target profile exists but carries no temporal stats
        // (a legacy capture, or one too short) -- automatic dynamics need a relearn.
        bool targetNeedsRelearnForDynamics = false;
        juce::String message;
    };
    struct Band
    {
        float frequencyHz = 1000.0f, gainDb = 0.0f, q = 1.0f;
        // Automatic dynamic cut, chosen by the fitter from captured temporal stats.
        // When dynamic is false the band is a plain static bell (legacy behaviour).
        bool dynamic = false;
        float thresholdDb = -24.0f, ratio = 2.0f, attackMs = 15.0f, releaseMs = 150.0f, kneeDb = 6.0f;
        float rangeDb = 0.0f; // maximum dynamic reduction (0 = unlimited / legacy)
    };
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
    // Consistent reference/target snapshot for display; never called by audio processing.
    std::array<Profile, 2> getProfiles() const;
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
    void resetTemporalLocked (double sampleRate);   // (re)arm the band-pass bank for a new rate
    void accumulateTemporalLocked (const Frame&);    // one frame of detector-scale stats
    static double tempFrequency (int index) noexcept;
    static int nearestTempPoint (double frequencyHz) noexcept;
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

    // Temporal band-pass bank (worker-thread state, guarded by stateLock like the
    // other capture accumulators). Independent L/R SVFilter + RMS per grid point so
    // antiphase stereo content isn't cancelled by premature mono mixing.
    struct GridDetector { SVFilter svfL, svfR; float rmsL = 0.0f, rmsR = 0.0f; };
    std::unique_ptr<std::array<GridDetector, tempGridCount>> gridBank;
    float tempPreSmoothCoeff = 0.0f;
    int tempHopCounter = 0;
    int tempStartupSamples = 0;       // skip ~100ms on arm to avoid startup transients
    std::array<bool, tempGridCount> tempInBand {};  // points below sampleRate*0.45
    std::array<double, tempGridCount> tempSumLevel {}, tempSumRel {}, tempSumRelSq {}, tempObs {};
    bool temporalArmed = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MatchEQ)
};
