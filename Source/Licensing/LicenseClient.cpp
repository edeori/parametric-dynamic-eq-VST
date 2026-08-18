#include "LicenseClient.h"

namespace licensing
{
namespace
{
juce::var makeBody (const LicenseClient::Request& request)
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("licenseKey", request.licenseKey);
    object->setProperty ("productId", request.productId);
    object->setProperty ("machineId", request.machineId);
    object->setProperty ("pluginVersion", request.pluginVersion);
    object->setProperty ("platform", request.platform);

    if (request.activationId.isNotEmpty())
        object->setProperty ("activationId", request.activationId);

    if (request.signedLicense.isNotEmpty())
        object->setProperty ("signedLicense", request.signedLicense);

    return juce::var (object.release());
}
}

LicenseClient::LicenseClient (LicenseConfig configIn) : config (std::move (configIn))
{
}

LicenseClient::Response LicenseClient::activate (const Request& request) const
{
    return postJson (config.endpoints.activate, makeBody (request));
}

LicenseClient::Response LicenseClient::validate (const Request& request) const
{
    return postJson (config.endpoints.validate, makeBody (request));
}

LicenseClient::Response LicenseClient::refresh (const Request& request) const
{
    return postJson (config.endpoints.refresh, makeBody (request));
}

LicenseClient::Response LicenseClient::deactivate (const Request& request) const
{
    return postJson (config.endpoints.deactivate, makeBody (request));
}

LicenseClient::Response LicenseClient::postJson (const juce::String& path, const juce::var& body) const
{
    Response response;
    const auto bodyText = juce::JSON::toString (body, false);
    int statusCode = 0;

    auto options = juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inPostData)
                       .withConnectionTimeoutMs (config.requestTimeoutMs)
                       .withHttpRequestCmd ("POST")
                       .withExtraHeaders ("Content-Type: application/json\r\nAccept: application/json\r\n")
                       .withStatusCode (&statusCode);

    std::unique_ptr<juce::InputStream> stream (config.makeEndpointUrl (path)
                                                   .withPOSTData (bodyText)
                                                   .createInputStream (options));

    if (stream == nullptr)
    {
        response.networkError = true;
        response.status = LicenseStatus::ServerError;
        response.message = "Could not reach licensing server";
        response.httpStatusCode = statusCode;
        return response;
    }

    const auto text = stream->readEntireStreamAsString();
    response.httpStatusCode = statusCode;

    if (text.isEmpty())
    {
        response.networkError = true;
        response.status = LicenseStatus::ServerError;
        response.message = "Licensing server returned an empty response";
        return response;
    }

    const auto parsed = juce::JSON::parse (text);

    if (parsed.isVoid())
    {
        response.status = LicenseStatus::ServerError;
        response.message = "Licensing server returned invalid JSON";
        return response;
    }

    if (auto* object = parsed.getDynamicObject())
    {
        response.success = (bool) object->getProperty ("success");
        response.message = object->getProperty ("message").toString();
        response.activationId = object->getProperty ("activationId").toString();
        response.signedLicense = object->getProperty ("signedLicense").toString();
        response.expiresAt = object->getProperty ("expiresAt").toString();
        response.maxActivations = (int) object->getProperty ("maxActivations");
        response.status = parseStatus (object->getProperty ("status").toString(), response.success);
    }

    if (! response.success && response.status == LicenseStatus::Unknown)
        response.status = statusCode >= 500 ? LicenseStatus::ServerError : LicenseStatus::InvalidLicense;

    if (response.message.isEmpty())
        response.message = response.success ? "License request succeeded" : "License request failed";

    return response;
}

LicenseStatus LicenseClient::parseStatus (const juce::String& text, bool success)
{
    const auto normalised = text.trim().toLowerCase();

    if (normalised == "activated")
        return LicenseStatus::Activated;

    if (normalised == "trial_active")
        return LicenseStatus::TrialActive;

    if (normalised == "trial_expired")
        return LicenseStatus::TrialExpired;

    if (normalised == "activation_required")
        return LicenseStatus::ActivationRequired;

    if (normalised == "invalid_license")
        return LicenseStatus::InvalidLicense;

    if (normalised == "expired_license")
        return LicenseStatus::InvalidLicense;

    if (normalised == "offline_grace")
        return LicenseStatus::OfflineGracePeriod;

    if (normalised == "tampered")
        return LicenseStatus::Tampered;

    if (normalised == "server_error")
        return LicenseStatus::ServerError;

    return success ? LicenseStatus::Activated : LicenseStatus::Unknown;
}
}
