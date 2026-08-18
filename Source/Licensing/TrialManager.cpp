#include "TrialManager.h"

namespace licensing
{
namespace
{
constexpr auto msPerDay = 24 * 60 * 60 * 1000LL;
}

juce::String TrialManager::toIsoDay (const juce::Time& time)
{
    return time.formatted ("%Y-%m-%d");
}

juce::Time TrialManager::fromIsoDay (const juce::String& text)
{
    if (text.isEmpty())
        return {};

    const auto pieces = juce::StringArray::fromTokens (text, "-", "");

    if (pieces.size() != 3)
        return {};

    return { pieces[0].getIntValue(),
             pieces[1].getIntValue(),
             pieces[2].getIntValue(),
             0, 0, 0, 0 };
}

TrialManager::Result TrialManager::evaluate (LicenseStore::Data& data, const LicenseConfig& config, const juce::Time& now)
{
    Result result;
    const auto todayIso = toIsoDay (now);
    const auto today = fromIsoDay (todayIso);

    if (data.firstLaunchIso.isEmpty())
        data.firstLaunchIso = todayIso;

    if (data.trialStartIso.isEmpty())
        data.trialStartIso = data.firstLaunchIso;

    if (data.lastSeenIso.isNotEmpty())
    {
        const auto lastSeen = fromIsoDay (data.lastSeenIso);

        if (lastSeen.toMilliseconds() > 0 && today < lastSeen)
        {
            result.status = LicenseStatus::Tampered;
            result.suspiciousClock = true;
            result.daysRemaining = 0;
            data.lastSeenIso = todayIso;
            return result;
        }
    }

    data.lastSeenIso = todayIso;

    const auto start = fromIsoDay (data.trialStartIso);
    const auto elapsed = juce::jmax<juce::int64> (0, (today.toMilliseconds() - start.toMilliseconds()) / msPerDay);
    result.daysRemaining = juce::jmax (0, config.trialLengthDays - (int) elapsed);
    result.status = result.daysRemaining > 0 ? LicenseStatus::TrialActive : LicenseStatus::TrialExpired;
    return result;
}
}
