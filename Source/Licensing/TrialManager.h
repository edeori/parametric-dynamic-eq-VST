#pragma once

#include <juce_core/juce_core.h>

#include "LicenseConfig.h"
#include "LicenseStatus.h"
#include "LicenseStore.h"

namespace licensing
{
class TrialManager
{
public:
    struct Result
    {
        LicenseStatus status = LicenseStatus::Unknown;
        int daysRemaining = 0;
        bool suspiciousClock = false;
    };

    static Result evaluate (LicenseStore::Data& data, const LicenseConfig& config, const juce::Time& now);
    static juce::String toIsoDay (const juce::Time& time);
    static juce::Time fromIsoDay (const juce::String& text);
};
}
