#pragma once

#include <juce_core/juce_core.h>

namespace licensing
{
struct LicenseConfig
{
    struct EndpointPaths
    {
        juce::String activate   { "/activate" };
        juce::String validate   { "/validate" };
        juce::String refresh    { "/refresh" };
        juce::String deactivate { "/deactivate" };
    };

    juce::String companyName;
    juce::String applicationName;
    juce::String productId;
    juce::String productVersion;
    juce::String serverBaseUrl;
    juce::String publicKey;
    EndpointPaths endpoints;
    int trialLengthDays = 15;
    int offlineGracePeriodDays = 14;
    int requestTimeoutMs = 5000;
    bool allowUnsignedDevelopmentLicenses = false;

    [[nodiscard]] bool isValid() const noexcept;
    [[nodiscard]] juce::String getStoreFileName() const;
    [[nodiscard]] juce::String getPlatformName() const;
    [[nodiscard]] juce::URL makeEndpointUrl (const juce::String& path) const;

    static LicenseConfig fromJsonString (const juce::String& jsonText, juce::String& errorMessage);
    static LicenseConfig fromPreprocessorDefinitions (juce::String& errorMessage);
    static void applyPreprocessorOverrides (LicenseConfig& config);
};
}
