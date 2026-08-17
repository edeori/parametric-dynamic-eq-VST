#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

namespace
{
    std::unique_ptr<juce::AudioParameterFloat> floatParam (const juce::String& id,
                                                            const juce::String& name,
                                                            juce::NormalisableRange<float> range,
                                                            float value,
                                                            const juce::String& unit = {})
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, name,
                                                             range, value, unit);
    }
}

ParaDynAudioProcessor::ParaDynAudioProcessor()
    : AudioProcessor (BusesProperties()
                           .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    bypassParam = apvts.getRawParameterValue (ParamIDs::bypass);
    outputParam = apvts.getRawParameterValue (ParamIDs::output);
    oversamplingParam = apvts.getRawParameterValue (ParamIDs::oversampling);

    for (int i = 0; i < ParaDynConstants::maxBands; ++i)
    {
        activeParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::active (i));
        frequencyParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::frequency (i));
        gainParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::gain (i));
        qParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::q (i));
        filterTypeParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::filterType (i));
        filterSlopeParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::filterSlope (i));
        channelModeParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::channelMode (i));
        dynamicEnabledParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::dynamicEnabled (i));
        thresholdParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::threshold (i));
        ratioParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::ratio (i));
        attackParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::attack (i));
        releaseParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::release (i));
        kneeParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::knee (i));
        soloParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::solo (i));
        muteParams[(size_t) i] = apvts.getRawParameterValue (ParamIDs::mute (i));
    }
}

juce::AudioProcessorValueTreeState::ParameterLayout ParaDynAudioProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;
    layout.add (std::make_unique<juce::AudioParameterBool> (juce::ParameterID { ParamIDs::bypass, 1 }, "Bypass", false));
    layout.add (floatParam (ParamIDs::output, "Output", { -24.0f, 24.0f, 0.01f }, 0.0f, "dB"));
    layout.add (std::make_unique<juce::AudioParameterChoice> (
        juce::ParameterID { ParamIDs::oversampling, 1 }, "Oversampling", oversamplingNames(), 0));

    juce::NormalisableRange<float> frequencyRange { ParaDynConstants::minFrequencyHz,
                                                     ParaDynConstants::maxFrequencyHz, 0.1f, 0.25f };
    juce::NormalisableRange<float> qRange { 0.1f, 18.0f, 0.001f, 0.4f };
    juce::NormalisableRange<float> ratioRange { 1.0f, 20.0f, 0.01f, 0.45f };
    juce::NormalisableRange<float> attackRange { 0.1f, 200.0f, 0.01f, 0.35f };
    juce::NormalisableRange<float> releaseRange { 5.0f, 2000.0f, 0.1f, 0.35f };

    for (int i = 0; i < ParaDynConstants::maxBands; ++i)
    {
        const auto suffix = " " + juce::String (i + 1);
        // New bands default inactive (not part of the automatable-but-hidden slot pool
        // until addBand() claims them) and, crucially, dynamic mode defaults OFF -- the
        // request is explicit that dynamic must not be on by default for every band.
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::active (i), 1 }, "Active" + suffix, false));
        layout.add (floatParam (ParamIDs::frequency (i), "Frequency" + suffix, frequencyRange, 1000.0f, "Hz"));
        layout.add (floatParam (ParamIDs::gain (i), "Gain" + suffix, { -24.0f, 24.0f, 0.01f }, 0.0f, "dB"));
        layout.add (floatParam (ParamIDs::q (i), "Q" + suffix, qRange, 0.707f));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::filterType (i), 1 }, "Filter type" + suffix, filterTypeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::filterSlope (i), 1 }, "Filter slope" + suffix, filterSlopeNames(), 1));
        layout.add (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { ParamIDs::channelMode (i), 1 }, "Channel mode" + suffix, channelModeNames(), 0));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::dynamicEnabled (i), 1 }, "Dynamic" + suffix, false));
        layout.add (floatParam (ParamIDs::threshold (i), "Threshold" + suffix, { -60.0f, 0.0f, 0.01f }, -24.0f, "dB"));
        layout.add (floatParam (ParamIDs::ratio (i), "Ratio" + suffix, ratioRange, 2.0f, ":1"));
        layout.add (floatParam (ParamIDs::attack (i), "Attack" + suffix, attackRange, 15.0f, "ms"));
        layout.add (floatParam (ParamIDs::release (i), "Release" + suffix, releaseRange, 150.0f, "ms"));
        layout.add (floatParam (ParamIDs::knee (i), "Knee" + suffix, { 0.0f, 24.0f, 0.01f }, 6.0f, "dB"));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::solo (i), 1 }, "Solo" + suffix, false));
        layout.add (std::make_unique<juce::AudioParameterBool> (
            juce::ParameterID { ParamIDs::mute (i), 1 }, "Mute" + suffix, false));
    }
    return layout;
}

bool ParaDynAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto mainIn = layouts.getChannelSet (true, 0);
    const auto mainOut = layouts.getChannelSet (false, 0);
    if ((mainIn != juce::AudioChannelSet::mono() && mainIn != juce::AudioChannelSet::stereo()) || mainIn != mainOut)
        return false;
    return true;
}

void ParaDynAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const auto channels = juce::jmax (1, getMainBusNumOutputChannels());
    oversampler.prepare (sampleRate, samplesPerBlock, channels);
    oversampler.setOversamplingFactor (juce::roundToInt (oversamplingParam->load()));

    const auto oversampledRate = sampleRate * (double) oversampler.getFactorMultiplier();
    for (auto& band : bands)
        band.prepare (oversampledRate);

    setLatencySamples (oversampler.getLatencySamples());
}

int ParaDynAudioProcessor::getNumBands() const noexcept
{
    int count = 0;
    for (auto* active : activeParams)
        count += active->load() >= 0.5f ? 1 : 0;
    return count;
}

std::vector<int> ParaDynAudioProcessor::getActiveBandSlots() const
{
    std::vector<int> slots;
    for (int i = 0; i < ParaDynConstants::maxBands; ++i)
        if (activeParams[(size_t) i]->load() >= 0.5f)
            slots.push_back (i);
    return slots;
}

bool ParaDynAudioProcessor::isBandActive (int slot) const noexcept
{
    return slot >= 0 && slot < ParaDynConstants::maxBands && activeParams[(size_t) slot]->load() >= 0.5f;
}

bool ParaDynAudioProcessor::addBand (float preferredFrequencyHz)
{
    int freeSlot = -1;
    for (int i = 0; i < ParaDynConstants::maxBands; ++i)
        if (activeParams[(size_t) i]->load() < 0.5f) { freeSlot = i; break; }
    if (freeSlot < 0)
        return false;

    if (preferredFrequencyHz <= 0.0f)
    {
        // Spread new bands across a musical octave sequence so several
        // quickly-added bands don't all land on the same default frequency.
        static constexpr std::array<float, 8> defaults { 100.0f, 200.0f, 400.0f, 800.0f,
                                                           1600.0f, 3200.0f, 6400.0f, 12800.0f };
        preferredFrequencyHz = defaults[(size_t) getNumBands() % defaults.size()];
    }

    if (auto* frequency = apvts.getParameter (ParamIDs::frequency (freeSlot)))
    {
        frequency->beginChangeGesture();
        frequency->setValueNotifyingHost (apvts.getParameterRange (frequency->getParameterID()).convertTo0to1 (
            juce::jlimit (ParaDynConstants::minFrequencyHz, ParaDynConstants::maxFrequencyHz, preferredFrequencyHz)));
        frequency->endChangeGesture();
    }
    if (auto* active = apvts.getParameter (ParamIDs::active (freeSlot)))
    {
        active->beginChangeGesture();
        active->setValueNotifyingHost (1.0f);
        active->endChangeGesture();
    }
    bands[(size_t) freeSlot].reset();
    setSelectedBand (freeSlot);
    return true;
}

bool ParaDynAudioProcessor::removeBand (int slot)
{
    if (! isBandActive (slot))
        return false;
    if (auto* active = apvts.getParameter (ParamIDs::active (slot)))
    {
        active->beginChangeGesture();
        active->setValueNotifyingHost (0.0f);
        active->endChangeGesture();
    }
    if (selectedBand.load() == slot)
    {
        const auto remaining = getActiveBandSlots();
        setSelectedBand (remaining.empty() ? -1 : remaining.front());
    }
    return true;
}

void ParaDynAudioProcessor::setSolo (int slot, bool shouldBeSoloed) noexcept
{
    if (! isBandActive (slot))
        return;
    if (auto* solo = apvts.getParameter (ParamIDs::solo (slot)))
    {
        solo->beginChangeGesture();
        solo->setValueNotifyingHost (shouldBeSoloed ? 1.0f : 0.0f);
        solo->endChangeGesture();
    }
}

bool ParaDynAudioProcessor::isBandSoloed (int slot) const noexcept
{
    return slot >= 0 && slot < ParaDynConstants::maxBands && soloParams[(size_t) slot]->load() >= 0.5f;
}

float ParaDynAudioProcessor::getBandDynamicReductionDb (int slot) const noexcept
{
    if (slot < 0 || slot >= ParaDynConstants::maxBands)
        return 0.0f;
    return bands[(size_t) slot].getCurrentDynamicReductionDb();
}

EQBand::Params ParaDynAudioProcessor::getBandParams (int slot) const noexcept
{
    EQBand::Params p;
    if (slot < 0 || slot >= ParaDynConstants::maxBands)
        return p;
    const auto s = (size_t) slot;
    p.active = activeParams[s]->load() >= 0.5f;
    p.frequencyHz = frequencyParams[s]->load();
    p.gainDb = gainParams[s]->load();
    p.q = qParams[s]->load();
    p.filterType = static_cast<FilterType> (juce::roundToInt (filterTypeParams[s]->load()));
    p.slope = static_cast<FilterSlope> (juce::roundToInt (filterSlopeParams[s]->load()));
    p.channelMode = static_cast<ChannelMode> (juce::roundToInt (channelModeParams[s]->load()));
    p.dynamicEnabled = dynamicEnabledParams[s]->load() >= 0.5f;
    p.thresholdDb = thresholdParams[s]->load();
    p.ratio = ratioParams[s]->load();
    p.attackMs = attackParams[s]->load();
    p.releaseMs = releaseParams[s]->load();
    p.kneeDb = kneeParams[s]->load();
    return p;
}

void ParaDynAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    auto mainBuffer = getBusBuffer (buffer, true, 0);
    const auto samples = mainBuffer.getNumSamples();

    const auto factorChoice = juce::roundToInt (oversamplingParam->load());
    const auto previousMultiplier = oversampler.getFactorMultiplier();
    oversampler.setOversamplingFactor (factorChoice);
    if (oversampler.getFactorMultiplier() != previousMultiplier)
    {
        const auto oversampledRate = getSampleRate() * (double) oversampler.getFactorMultiplier();
        for (auto& band : bands)
            band.prepare (oversampledRate);
        setLatencySamples (oversampler.getLatencySamples());
    }

    for (int sample = 0; sample < samples; ++sample)
        inputSpectrum.push (mainBuffer.getSample (0, sample));

    if (bypassParam->load() >= 0.5f)
    {
        for (int sample = 0; sample < samples; ++sample)
            outputSpectrum.push (mainBuffer.getSample (0, sample));
        return;
    }

    juce::dsp::AudioBlock<float> baseBlock (mainBuffer);
    auto processingBlock = oversampler.processSamplesUp (baseBlock);

    bool anySolo = false;
    for (auto* solo : soloParams)
        anySolo = anySolo || solo->load() >= 0.5f;

    for (int slot = 0; slot < ParaDynConstants::maxBands; ++slot)
    {
        if (activeParams[(size_t) slot]->load() < 0.5f)
            continue;
        if (muteParams[(size_t) slot]->load() >= 0.5f)
            continue;
        if (anySolo && soloParams[(size_t) slot]->load() < 0.5f)
            continue;

        // Every band that reaches this point while anySolo is true survived the
        // solo filter above, i.e. it IS one of the soloed bands -- switch it into
        // audition/preview mode (see EQBand::Params::soloListen) instead of its
        // normal shaped output, so Solo is audibly obvious even at 0dB gain.
        auto params = getBandParams (slot);
        params.soloListen = anySolo;
        bands[(size_t) slot].process (processingBlock, params);
    }

    oversampler.processSamplesDown (baseBlock);
    mainBuffer.applyGain (juce::Decibels::decibelsToGain (outputParam->load()));

    for (int sample = 0; sample < samples; ++sample)
        outputSpectrum.push (mainBuffer.getSample (0, sample));
}

juce::AudioProcessorEditor* ParaDynAudioProcessor::createEditor()
{
    return new ParaDynAudioProcessorEditor (*this);
}

juce::AudioProcessorParameter* ParaDynAudioProcessor::getBypassParameter() const
{
    return apvts.getParameter (ParamIDs::bypass);
}

void ParaDynAudioProcessor::getStateInformation (juce::MemoryBlock& destination)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destination);
}

void ParaDynAudioProcessor::setStateInformation (const void* data, int size)
{
    if (auto xml = getXmlFromBinary (data, size))
        if (xml->hasTagName (apvts.state.getType()))
            apvts.replaceState (juce::ValueTree::fromXml (*xml));
    const auto slots = getActiveBandSlots();
    setSelectedBand (slots.empty() ? -1 : slots.front());
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ParaDynAudioProcessor();
}
