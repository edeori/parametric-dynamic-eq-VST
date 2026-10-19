#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <atomic>
#include <vector>

#include "EQBand.h"
#include "Oversampler.h"
#include "Parameters.h"
#include "SpectrumFifo.h"
#include "MatchEQ.h"

#if PARADYNEQ_ENABLE_LICENSING
#include "Licensing/LicenseManager.h"
#endif

class ParaDynAudioProcessor final : public juce::AudioProcessor
{
public:
    ParaDynAudioProcessor();
    ~ParaDynAudioProcessor() override = default;

#if PARADYNEQ_ENABLE_LICENSING
    licensing::LicenseManager& getLicenseManager() noexcept { return licenseManager; }
    const licensing::LicenseManager& getLicenseManager() const noexcept { return licenseManager; }
#endif

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Bands are identified by their fixed parameter "slot" (0..maxBands-1). A slot is
    // either active (a real, visible band) or inactive (free, hidden). addBand() claims
    // the lowest free slot; removeBand() frees one. UI code lists active slots via
    // getActiveBandSlots() and always addresses a band by its slot index -- no separate
    // "visible index" indirection is needed because, unlike a crossover splitter, band
    // order carries no processing meaning here.
    int getNumBands() const noexcept;
    std::vector<int> getActiveBandSlots() const;
    bool isBandActive (int slot) const noexcept;
    int getSelectedBand() const noexcept { return selectedBand.load(); }
    void setSelectedBand (int slot) noexcept { selectedBand.store (slot); }
    bool addBand (float preferredFrequencyHz = 0.0f);
    bool removeBand (int slot);
    void setSolo (int slot, bool shouldBeSoloed) noexcept;
    bool isBandSoloed (int slot) const noexcept;

    float getBandDynamicReductionDb (int slot) const noexcept;
    EQBand::Params getBandParams (int slot) const noexcept;
    SpectrumFifo& getInputSpectrumFifo() noexcept { return inputSpectrum; }
    SpectrumFifo& getOutputSpectrumFifo() noexcept { return outputSpectrum; }
    MatchEQ& getMatchEQ() noexcept { return matchEQ; }
    juce::Result applyMatchEQ();
    bool undoMatchEQ();
    bool canUndoMatchEQ() const;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    std::array<EQBand, ParaDynConstants::maxBands> bands;
    Oversampler oversampler;
    SpectrumFifo inputSpectrum, outputSpectrum;
    MatchEQ matchEQ;
    mutable juce::CriticalSection matchUndoLock;
    std::vector<std::pair<juce::String, float>> matchUndo;
    void setMatchParameter (const juce::String&, float);

    std::array<std::atomic<float>*, ParaDynConstants::maxBands> activeParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> frequencyParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> gainParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> qParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> filterTypeParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> filterSlopeParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> channelModeParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> dynamicEnabledParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> thresholdParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> ratioParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> attackParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> releaseParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> kneeParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> soloParams {};
    std::array<std::atomic<float>*, ParaDynConstants::maxBands> muteParams {};
    std::atomic<float>* bypassParam = nullptr;
    std::atomic<float>* outputParam = nullptr;
    std::atomic<float>* oversamplingParam = nullptr;
    std::atomic<int> selectedBand { -1 };

#if PARADYNEQ_ENABLE_LICENSING
    licensing::LicenseManager licenseManager;
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParaDynAudioProcessor)
};
