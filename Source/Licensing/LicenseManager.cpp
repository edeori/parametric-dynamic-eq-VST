#include "LicenseManager.h"

#include <cstring>

#include <juce_events/juce_events.h>
#include <juce_cryptography/juce_cryptography.h>

#include "MachineFingerprint.h"

namespace licensing
{
class LicenseManager::BackgroundJob final : public juce::ThreadPoolJob
{
public:
    BackgroundJob (std::function<OperationResult()> workIn, CompletionHandler completionIn)
        : juce::ThreadPoolJob ("LicenseManagerJob"),
          work (std::move (workIn)),
          completion (std::move (completionIn))
    {
    }

    JobStatus runJob() override
    {
        auto result = work ? work() : OperationResult {};

        if (completion)
            juce::MessageManager::callAsync ([callback = completion, result] { callback (result); });

        return jobHasFinished;
    }

private:
    std::function<OperationResult()> work;
    CompletionHandler completion;
};

namespace
{
juce::String sha256Hex (const juce::String& text)
{
    return juce::SHA256 (text.toRawUTF8(), std::strlen (text.toRawUTF8())).toHexString();
}

bool verifyRsaSha256Signature (const juce::String& payloadText, const juce::String& signatureHex,
                               const juce::String& publicKeyText)
{
    if (payloadText.isEmpty() || signatureHex.isEmpty() || publicKeyText.isEmpty())
        return false;

    juce::BigInteger signatureValue;
    signatureValue.parseString (signatureHex.trim(), 16);

    if (signatureValue <= 0)
        return false;

    juce::RSAKey publicKey (publicKeyText.trim());

    if (! publicKey.isValid() || ! publicKey.applyToValue (signatureValue))
        return false;

    juce::BigInteger expectedDigest;
    expectedDigest.parseString (sha256Hex (payloadText), 16);
    return signatureValue == expectedDigest;
}

juce::String statusToMessage (LicenseStatus status, int daysRemaining)
{
    switch (status)
    {
        case LicenseStatus::TrialActive:        return juce::String (daysRemaining) + " trial day(s) remaining";
        case LicenseStatus::TrialExpired:       return "Trial expired. Activation required for audio.";
        case LicenseStatus::Activated:          return "License active";
        case LicenseStatus::ActivationRequired: return "Activation required";
        case LicenseStatus::InvalidLicense:     return "License invalid";
        case LicenseStatus::OfflineGracePeriod: return "Offline grace period active";
        case LicenseStatus::ServerError:        return "Licensing server unavailable";
        case LicenseStatus::Tampered:           return "Licensing state appears tampered";
        case LicenseStatus::Unknown:            return "License status unknown";
    }

    return "License status unknown";
}
}

LicenseManager::LicenseManager() = default;

LicenseManager::~LicenseManager()
{
    threadPool.removeAllJobs (true, 2000);
}

bool LicenseManager::initialise (const LicenseConfig& configToUse, juce::String& errorMessage)
{
    const juce::ScopedLock lock (stateLock);

    config = configToUse;

    if (! config.isValid())
    {
        errorMessage = "Licensing config is invalid";
        return false;
    }

    store = std::make_unique<LicenseStore> (config);
    client = std::make_unique<LicenseClient> (config);
    const auto currentMachineId = MachineFingerprint::createMachineIdHash();

    if (currentMachineId.isEmpty())
    {
        errorMessage = "Could not read a stable machine identifier";
        cachedStatus.store ((int) LicenseStatus::ActivationRequired);
        cachedMessage = errorMessage;
        return false;
    }

    state.machineIdHash = currentMachineId;

    if (! store->load (state, errorMessage))
        return false;

    if (state.machineIdHash.isEmpty())
        state.machineIdHash = currentMachineId;

    if (state.machineIdHash != currentMachineId)
    {
        const auto hasStoredActivation = state.activationId.isNotEmpty()
                                      || state.signedLicense.isNotEmpty()
                                      || state.licenseKey.isNotEmpty();

        if (hasStoredActivation)
        {
            updateCachedState (LicenseStatus::Tampered, 0, "Stored machine ID does not match this machine", false);
            return false;
        }

        state.machineIdHash = currentMachineId;
    }

    if (! state.checksum.isEmpty() && ! store->verifyTamper (state))
    {
        updateCachedState (LicenseStatus::Tampered, 0, "Stored licensing state failed tamper check", false);
        return false;
    }

    initialised = true;
    auto result = rebuildStateFromStore (true);
    errorMessage = result.message.startsWithIgnoreCase ("Configuration") ? result.message : juce::String();
    persistState();

    if (getStatus() == LicenseStatus::Activated || getStatus() == LicenseStatus::OfflineGracePeriod)
        refreshLicense();

    return true;
}

LicenseStatus LicenseManager::getStatus() const noexcept
{
    return (LicenseStatus) cachedStatus.load();
}

bool LicenseManager::isAudioEnabled() const noexcept
{
    return cachedAudioEnabled.load();
}

int LicenseManager::getDaysRemainingInTrial() const noexcept
{
    return cachedTrialDays.load();
}

juce::String LicenseManager::getUserVisibleStatusMessage() const
{
    const juce::ScopedLock lock (stateLock);
    return cachedMessage;
}

void LicenseManager::startActivation (juce::String licenseKey, CompletionHandler completion)
{
    enqueueJob ([this, trimmedKey = licenseKey.trim()] { return activateSynchronously (trimmedKey); }, std::move (completion));
}

void LicenseManager::deactivate (CompletionHandler completion)
{
    enqueueJob ([this] { return deactivateSynchronously(); }, std::move (completion));
}

void LicenseManager::refreshLicense (CompletionHandler completion)
{
    enqueueJob ([this] { return refreshSynchronously (true); }, std::move (completion));
}

void LicenseManager::enqueueJob (std::function<OperationResult()> work, CompletionHandler completion)
{
    threadPool.addJob (new BackgroundJob (std::move (work), std::move (completion)), true);
}

void LicenseManager::updateCachedState (LicenseStatus status, int daysRemaining, juce::String message, bool audioEnabled)
{
    cachedStatus.store ((int) status);
    cachedTrialDays.store (daysRemaining);
    cachedAudioEnabled.store (audioEnabled);

    const juce::ScopedLock lock (stateLock);
    cachedMessage = message.isNotEmpty() ? std::move (message) : statusToMessage (status, daysRemaining);
}

void LicenseManager::persistState()
{
    if (store == nullptr)
        return;

    juce::String ignored;
    [[maybe_unused]] const auto saved = store->save (state, ignored);
}

LicenseManager::OperationResult LicenseManager::activateSynchronously (const juce::String& licenseKey)
{
    if (! initialised)
        return { false, LicenseStatus::Unknown, "License manager is not initialised" };

    if (licenseKey.isEmpty())
        return { false, LicenseStatus::InvalidLicense, "Enter a license key" };

    const auto response = client->activate ({ licenseKey, config.productId, state.machineIdHash, config.productVersion,
                                              config.getPlatformName(), state.activationId, state.signedLicense });

    if (! response.success)
    {
        updateCachedState (response.status, getDaysRemainingInTrial(), response.message, isAudioEnabled());
        return { false, response.status, response.message };
    }

    state.licenseKey = licenseKey;
    state.activationId = response.activationId;
    state.signedLicense = response.signedLicense;
    state.lastSuccessfulValidationIso = TrialManager::toIsoDay (juce::Time::getCurrentTime());
    auto result = rebuildStateFromStore (false);
    persistState();
    return result;
}

LicenseManager::OperationResult LicenseManager::refreshSynchronously (bool userInitiated)
{
    if (! initialised)
        return { false, LicenseStatus::Unknown, "License manager is not initialised" };

    if (state.signedLicense.isEmpty() || state.licenseKey.isEmpty())
        return rebuildStateFromStore (true);

    const auto response = client->refresh ({ state.licenseKey, config.productId, state.machineIdHash, config.productVersion,
                                             config.getPlatformName(), state.activationId, state.signedLicense });

    if (! response.success)
    {
        auto fallback = rebuildStateFromStore (! userInitiated);

        if ((response.networkError || response.status == LicenseStatus::ServerError)
             && fallback.status == LicenseStatus::Activated
             && isOfflineGraceValid (juce::Time::getCurrentTime()))
        {
            updateCachedState (LicenseStatus::OfflineGracePeriod, 0, response.message, true);
            return { true, LicenseStatus::OfflineGracePeriod, response.message };
        }

        updateCachedState (response.status, getDaysRemainingInTrial(), response.message, false);
        return { false, response.status, response.message };
    }

    if (response.signedLicense.isNotEmpty())
        state.signedLicense = response.signedLicense;

    if (response.activationId.isNotEmpty())
        state.activationId = response.activationId;

    state.lastSuccessfulValidationIso = TrialManager::toIsoDay (juce::Time::getCurrentTime());
    auto result = rebuildStateFromStore (false);
    persistState();
    return result;
}

LicenseManager::OperationResult LicenseManager::deactivateSynchronously()
{
    if (! initialised)
        return { false, LicenseStatus::Unknown, "License manager is not initialised" };

    if (state.activationId.isNotEmpty())
        [[maybe_unused]] const auto deactivated = client->deactivate ({ state.licenseKey, config.productId, state.machineIdHash,
                                                                        config.productVersion, config.getPlatformName(),
                                                                        state.activationId, state.signedLicense });

    state.licenseKey = {};
    state.activationId = {};
    state.signedLicense = {};
    state.lastSuccessfulValidationIso = {};
    auto result = rebuildStateFromStore (false);
    persistState();
    return result;
}

LicenseManager::OperationResult LicenseManager::rebuildStateFromStore (bool allowServerFallbackMessage)
{
    if (! state.signedLicense.isEmpty())
    {
        OperationResult activationResult;

        if (evaluateStoredActivation (activationResult))
            return activationResult;

        if (! allowServerFallbackMessage)
            return activationResult;
    }

    auto trialResult = TrialManager::evaluate (state, config, juce::Time::getCurrentTime());
    updateCachedState (trialResult.status, trialResult.daysRemaining, statusToMessage (trialResult.status, trialResult.daysRemaining),
                       trialResult.status == LicenseStatus::TrialActive);
    return { trialResult.status == LicenseStatus::TrialActive, trialResult.status, getUserVisibleStatusMessage() };
}

bool LicenseManager::evaluateStoredActivation (OperationResult& result)
{
    juce::String error;
    const auto decoded = decodeSignedLicense (state.signedLicense, error);

    if (! decoded.valid)
    {
        updateCachedState (LicenseStatus::InvalidLicense, 0, error, false);
        result = { false, LicenseStatus::InvalidLicense, error };
        return false;
    }

    if (decoded.productId != config.productId || decoded.machineId != state.machineIdHash)
    {
        updateCachedState (LicenseStatus::Tampered, 0, "License payload does not match this product or machine", false);
        result = { false, LicenseStatus::Tampered, getUserVisibleStatusMessage() };
        return false;
    }

    if (decoded.licenseKey.isNotEmpty() && state.licenseKey.isNotEmpty() && decoded.licenseKey != state.licenseKey)
    {
        updateCachedState (LicenseStatus::Tampered, 0, "License payload does not match the stored license key", false);
        result = { false, LicenseStatus::Tampered, getUserVisibleStatusMessage() };
        return false;
    }

    if (decoded.activationId.isNotEmpty() && state.activationId.isNotEmpty() && decoded.activationId != state.activationId)
    {
        updateCachedState (LicenseStatus::Tampered, 0, "License payload does not match the stored activation", false);
        result = { false, LicenseStatus::Tampered, getUserVisibleStatusMessage() };
        return false;
    }

    if (decoded.expiresAtIso.isNotEmpty())
    {
        const auto expiresAt = juce::Time::fromISO8601 (decoded.expiresAtIso);

        if (expiresAt.toMilliseconds() > 0 && expiresAt < juce::Time::getCurrentTime())
        {
            updateCachedState (LicenseStatus::InvalidLicense, 0, "License has expired", false);
            result = { false, LicenseStatus::InvalidLicense, getUserVisibleStatusMessage() };
            return false;
        }
    }

    const auto decodedStatus = decoded.status.trim().toLowerCase();

    if (decodedStatus.isNotEmpty() && decodedStatus != "activated")
    {
        updateCachedState (LicenseStatus::InvalidLicense, 0,
                           decoded.message.isNotEmpty() ? decoded.message : "License is not active",
                           false);
        result = { false, LicenseStatus::InvalidLicense, getUserVisibleStatusMessage() };
        return false;
    }

    updateCachedState (LicenseStatus::Activated, 0,
                       decoded.message.isNotEmpty() ? decoded.message : statusToMessage (LicenseStatus::Activated, 0),
                       true);
    result = { true, LicenseStatus::Activated, getUserVisibleStatusMessage() };
    return true;
}

bool LicenseManager::isOfflineGraceValid (const juce::Time& now) const
{
    if (state.lastSuccessfulValidationIso.isEmpty())
        return false;

    const auto lastValidation = TrialManager::fromIsoDay (state.lastSuccessfulValidationIso);
    const auto elapsedMs = now.toMilliseconds() - lastValidation.toMilliseconds();
    return elapsedMs <= (juce::int64) config.offlineGracePeriodDays * 24 * 60 * 60 * 1000;
}

LicenseManager::DecodedLicense LicenseManager::decodeSignedLicense (const juce::String& signedLicense, juce::String& errorMessage) const
{
    DecodedLicense decoded;

    if (signedLicense.isEmpty())
    {
        errorMessage = "Missing signed license payload";
        return decoded;
    }

    const auto parsed = juce::JSON::parse (signedLicense);

    if (parsed.isVoid())
    {
        errorMessage = "Signed license payload is not valid JSON";
        return decoded;
    }

    auto* envelope = parsed.getDynamicObject();

    if (envelope == nullptr)
    {
        errorMessage = "Signed license payload has no envelope";
        return decoded;
    }

    const auto algorithm = envelope->getProperty ("algorithm").toString();
    juce::ignoreUnused (envelope->getProperty ("keyId").toString());
    const auto payloadText = envelope->getProperty ("payload").toString();
    const auto signature = envelope->getProperty ("signature").toString();

    if (payloadText.isEmpty())
    {
        errorMessage = "Signed license payload is empty";
        return decoded;
    }

    if (algorithm == "none")
    {
        if (! config.allowUnsignedDevelopmentLicenses)
        {
            errorMessage = "Unsigned development licenses are disabled";
            return decoded;
        }
    }
    else if (algorithm == "rsa-sha256")
    {
        if (config.publicKey.isEmpty())
        {
            errorMessage = "Missing licensing public key";
            return decoded;
        }

        if (! verifyRsaSha256Signature (payloadText, signature, config.publicKey))
        {
            errorMessage = "Signed license signature verification failed";
            return decoded;
        }
    }
    else
    {
        errorMessage = "Signature verification for algorithm '" + algorithm + "' is not supported";
        return decoded;
    }

    const auto payload = juce::JSON::parse (payloadText);

    if (auto* object = payload.getDynamicObject())
    {
        decoded.licenseKey = object->getProperty ("licenseKey").toString();
        decoded.productId = object->getProperty ("productId").toString();
        decoded.machineId = object->getProperty ("machineId").toString();
        decoded.activationId = object->getProperty ("activationId").toString();
        decoded.issuedAtIso = object->getProperty ("issuedAt").toString();
        decoded.lastValidatedAtIso = object->getProperty ("lastValidatedAt").toString();
        decoded.expiresAtIso = object->getProperty ("expiresAt").toString();
        decoded.offlineGraceDays = (int) object->getProperty ("offlineGraceDays");
        decoded.maxActivations = (int) object->getProperty ("maxActivations");
        decoded.status = object->getProperty ("status").toString();
        decoded.message = object->getProperty ("message").toString();
        decoded.valid = true;
        return decoded;
    }

    errorMessage = "Signed license payload body is invalid";
    return decoded;
}

LicenseClient::Request LicenseManager::makeRequest() const
{
    return { state.licenseKey, config.productId, state.machineIdHash, config.productVersion,
             config.getPlatformName(), state.activationId, state.signedLicense };
}
}
