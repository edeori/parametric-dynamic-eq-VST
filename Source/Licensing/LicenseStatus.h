#pragma once

namespace licensing
{
enum class LicenseStatus
{
    TrialActive = 0,
    TrialExpired,
    Activated,
    ActivationRequired,
    InvalidLicense,
    OfflineGracePeriod,
    ServerError,
    Tampered,
    Unknown
};
}
