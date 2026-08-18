#pragma once

#include <juce_core/juce_core.h>

#include "LicenseConfig.h"

namespace licensing
{
class LicenseStore
{
public:
    struct Data
    {
        juce::String licenseKey;
        juce::String activationId;
        juce::String signedLicense;
        juce::String firstLaunchIso;
        juce::String lastSuccessfulValidationIso;
        juce::String trialStartIso;
        juce::String lastSeenIso;
        juce::String machineIdHash;
        juce::String checksum;

        [[nodiscard]] juce::var toVar() const;
        static Data fromVar (const juce::var& value);
    };

    explicit LicenseStore (LicenseConfig configIn);

    [[nodiscard]] bool load (Data& data, juce::String& errorMessage) const;
    [[nodiscard]] bool save (Data data, juce::String& errorMessage) const;
    [[nodiscard]] bool remove (juce::String& errorMessage) const;
    [[nodiscard]] bool verifyTamper (const Data& data) const;
    [[nodiscard]] juce::File getBackingFile() const;

private:
    [[nodiscard]] juce::String calculateChecksum (const Data& data) const;

    LicenseConfig config;
};
}
