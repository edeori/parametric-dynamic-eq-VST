#include "LicenseConfig.h"

namespace licensing
{
namespace
{
juce::String normaliseConfigString (juce::String value)
{
    value = value.trim();

    if (value.length() >= 2 && value.startsWithChar ('"') && value.endsWithChar ('"'))
        value = value.substring (1, value.length() - 1);

    return value.trim();
}

juce::String readString (const juce::var& source, const juce::Identifier& key)
{
    if (auto* object = source.getDynamicObject())
        return normaliseConfigString (object->getProperty (key).toString());

    return {};
}

int readInt (const juce::var& source, const juce::Identifier& key, int fallback)
{
    if (auto* object = source.getDynamicObject())
    {
        const auto value = object->getProperty (key);

        if (value.isInt() || value.isInt64() || value.isDouble())
            return (int) value;
    }

    return fallback;
}

bool readBool (const juce::var& source, const juce::Identifier& key, bool fallback)
{
    if (auto* object = source.getDynamicObject())
    {
        const auto value = object->getProperty (key);

        if (value.isBool())
            return (bool) value;
    }

    return fallback;
}
}

bool LicenseConfig::isValid() const noexcept
{
    return companyName.isNotEmpty()
        && applicationName.isNotEmpty()
        && productId.isNotEmpty()
        && serverBaseUrl.isNotEmpty()
        && trialLengthDays > 0
        && offlineGracePeriodDays > 0
        && requestTimeoutMs > 0;
}

juce::String LicenseConfig::getStoreFileName() const
{
    return productId + "_license_state.json";
}

juce::String LicenseConfig::getPlatformName() const
{
   #if JUCE_MAC
    return "macOS";
   #elif JUCE_WINDOWS
    return "Windows";
   #else
    return juce::SystemStats::getOperatingSystemName();
   #endif
}

juce::URL LicenseConfig::makeEndpointUrl (const juce::String& path) const
{
    auto base = serverBaseUrl.trimEnd().trimCharactersAtEnd ("/");
    auto suffix = path.startsWithChar ('/') ? path : "/" + path;
    return juce::URL (base + suffix);
}

LicenseConfig LicenseConfig::fromJsonString (const juce::String& jsonText, juce::String& errorMessage)
{
    LicenseConfig config;
    const auto parsed = juce::JSON::parse (jsonText);

    if (parsed.isVoid())
    {
        errorMessage = "Licensing config JSON could not be parsed";
        return {};
    }

    config.companyName = readString (parsed, "companyName");
    config.applicationName = readString (parsed, "applicationName");
    config.productId = readString (parsed, "productId");
    config.productVersion = readString (parsed, "productVersion");
    config.serverBaseUrl = readString (parsed, "serverBaseUrl");
    config.publicKey = readString (parsed, "publicKey");
    config.trialLengthDays = readInt (parsed, "trialLengthDays", config.trialLengthDays);
    config.offlineGracePeriodDays = readInt (parsed, "offlineGracePeriodDays", config.offlineGracePeriodDays);
    config.requestTimeoutMs = readInt (parsed, "requestTimeoutMs", config.requestTimeoutMs);
    config.allowUnsignedDevelopmentLicenses = readBool (parsed, "allowUnsignedDevelopmentLicenses",
                                                        config.allowUnsignedDevelopmentLicenses);

    if (auto* object = parsed.getDynamicObject())
    {
        const auto endpointsVar = object->getProperty ("endpoints");

        if (auto* endpoints = endpointsVar.getDynamicObject())
        {
            config.endpoints.activate   = endpoints->getProperty ("activate").toString();
            config.endpoints.validate   = endpoints->getProperty ("validate").toString();
            config.endpoints.refresh    = endpoints->getProperty ("refresh").toString();
            config.endpoints.deactivate = endpoints->getProperty ("deactivate").toString();
        }
    }

    if (! config.isValid())
        errorMessage = "Licensing config is missing one or more required values";

    return config;
}

LicenseConfig LicenseConfig::fromPreprocessorDefinitions (juce::String& errorMessage)
{
    LicenseConfig config;
    applyPreprocessorOverrides (config);

    if (! config.isValid())
        errorMessage = "Licensing preprocessor configuration is incomplete";

    return config;
}

void LicenseConfig::applyPreprocessorOverrides (LicenseConfig& config)
{
   #if defined (LICENSE_COMPANY_NAME)
    config.companyName = normaliseConfigString (JUCE_STRINGIFY (LICENSE_COMPANY_NAME));
   #endif
   #if defined (LICENSE_APPLICATION_NAME)
    config.applicationName = normaliseConfigString (JUCE_STRINGIFY (LICENSE_APPLICATION_NAME));
   #endif
   #if defined (LICENSE_PRODUCT_ID)
    config.productId = normaliseConfigString (JUCE_STRINGIFY (LICENSE_PRODUCT_ID));
   #endif
   #if defined (LICENSE_PRODUCT_VERSION)
    config.productVersion = normaliseConfigString (JUCE_STRINGIFY (LICENSE_PRODUCT_VERSION));
   #endif
   #if defined (LICENSE_SERVER_BASE_URL)
    config.serverBaseUrl = normaliseConfigString (JUCE_STRINGIFY (LICENSE_SERVER_BASE_URL));
   #endif
   #if defined (LICENSE_PUBLIC_KEY)
    config.publicKey = normaliseConfigString (JUCE_STRINGIFY (LICENSE_PUBLIC_KEY));
   #endif
   #if defined (LICENSE_ENDPOINT_ACTIVATE)
    config.endpoints.activate = normaliseConfigString (JUCE_STRINGIFY (LICENSE_ENDPOINT_ACTIVATE));
   #endif
   #if defined (LICENSE_ENDPOINT_VALIDATE)
    config.endpoints.validate = normaliseConfigString (JUCE_STRINGIFY (LICENSE_ENDPOINT_VALIDATE));
   #endif
   #if defined (LICENSE_ENDPOINT_REFRESH)
    config.endpoints.refresh = normaliseConfigString (JUCE_STRINGIFY (LICENSE_ENDPOINT_REFRESH));
   #endif
   #if defined (LICENSE_ENDPOINT_DEACTIVATE)
    config.endpoints.deactivate = normaliseConfigString (JUCE_STRINGIFY (LICENSE_ENDPOINT_DEACTIVATE));
   #endif
   #if defined (LICENSE_TRIAL_LENGTH_DAYS)
    config.trialLengthDays = LICENSE_TRIAL_LENGTH_DAYS;
   #endif
   #if defined (LICENSE_OFFLINE_GRACE_DAYS)
    config.offlineGracePeriodDays = LICENSE_OFFLINE_GRACE_DAYS;
   #endif
   #if defined (LICENSE_REQUEST_TIMEOUT_MS)
    config.requestTimeoutMs = LICENSE_REQUEST_TIMEOUT_MS;
   #endif
   #if defined (LICENSE_ALLOW_UNSIGNED_DEVELOPMENT)
    config.allowUnsignedDevelopmentLicenses = LICENSE_ALLOW_UNSIGNED_DEVELOPMENT != 0;
   #endif
}
}
