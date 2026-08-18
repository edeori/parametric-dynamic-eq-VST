#pragma once

#include <juce_core/juce_core.h>

#include "LicenseConfig.h"
#include "LicenseStatus.h"

namespace licensing
{
class LicenseClient
{
public:
    struct Request
    {
        juce::String licenseKey;
        juce::String productId;
        juce::String machineId;
        juce::String pluginVersion;
        juce::String platform;
        juce::String activationId;
        juce::String signedLicense;
    };

    struct Response
    {
        bool success = false;
        LicenseStatus status = LicenseStatus::Unknown;
        juce::String message;
        juce::String activationId;
        juce::String signedLicense;
        juce::String expiresAt;
        int maxActivations = 0;
        int httpStatusCode = 0;
        bool networkError = false;
    };

    explicit LicenseClient (LicenseConfig configIn);

    [[nodiscard]] Response activate (const Request& request) const;
    [[nodiscard]] Response validate (const Request& request) const;
    [[nodiscard]] Response refresh (const Request& request) const;
    [[nodiscard]] Response deactivate (const Request& request) const;

private:
    [[nodiscard]] Response postJson (const juce::String& path, const juce::var& body) const;
    static LicenseStatus parseStatus (const juce::String& text, bool success);

    LicenseConfig config;
};
}
