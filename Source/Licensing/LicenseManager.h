#pragma once

#include <atomic>
#include <functional>

#include <juce_core/juce_core.h>

#include "LicenseClient.h"
#include "LicenseConfig.h"
#include "LicenseStatus.h"
#include "LicenseStore.h"
#include "TrialManager.h"

namespace licensing
{
class LicenseManager
{
public:
    struct OperationResult
    {
        bool success = false;
        LicenseStatus status = LicenseStatus::Unknown;
        juce::String message;
    };

    using CompletionHandler = std::function<void (const OperationResult&)>;

    LicenseManager();
    ~LicenseManager();

    bool initialise (const LicenseConfig& configToUse, juce::String& errorMessage);

    [[nodiscard]] LicenseStatus getStatus() const noexcept;
    [[nodiscard]] bool isAudioEnabled() const noexcept;
    [[nodiscard]] int getDaysRemainingInTrial() const noexcept;
    [[nodiscard]] juce::String getUserVisibleStatusMessage() const;

    void startActivation (juce::String licenseKey, CompletionHandler completion = {});
    void deactivate (CompletionHandler completion = {});
    void refreshLicense (CompletionHandler completion = {});

private:
    struct DecodedLicense
    {
        bool valid = false;
        juce::String licenseKey;
        juce::String productId;
        juce::String machineId;
        juce::String activationId;
        juce::String issuedAtIso;
        juce::String lastValidatedAtIso;
        juce::String expiresAtIso;
        int offlineGraceDays = 0;
        int maxActivations = 0;
        juce::String status;
        juce::String message;
    };

    class BackgroundJob;

    void enqueueJob (std::function<OperationResult()> work, CompletionHandler completion);
    void updateCachedState (LicenseStatus status, int daysRemaining, juce::String message, bool audioEnabled);
    void persistState();
    [[nodiscard]] OperationResult activateSynchronously (const juce::String& licenseKey);
    [[nodiscard]] OperationResult refreshSynchronously (bool userInitiated);
    [[nodiscard]] OperationResult deactivateSynchronously();
    [[nodiscard]] OperationResult rebuildStateFromStore (bool allowServerFallbackMessage);
    [[nodiscard]] bool evaluateStoredActivation (OperationResult& result);
    [[nodiscard]] bool isOfflineGraceValid (const juce::Time& now) const;
    [[nodiscard]] DecodedLicense decodeSignedLicense (const juce::String& signedLicense, juce::String& errorMessage) const;
    [[nodiscard]] LicenseClient::Request makeRequest() const;

    mutable juce::CriticalSection stateLock;
    LicenseConfig config;
    std::unique_ptr<LicenseStore> store;
    std::unique_ptr<LicenseClient> client;
    LicenseStore::Data state;
    juce::ThreadPool threadPool { 1 };
    std::atomic<int> cachedStatus { (int) LicenseStatus::Unknown };
    std::atomic<bool> cachedAudioEnabled { false };
    std::atomic<int> cachedTrialDays { 0 };
    juce::String cachedMessage;
    bool initialised = false;
};
}
