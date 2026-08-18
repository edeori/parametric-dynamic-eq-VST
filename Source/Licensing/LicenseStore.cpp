#include "LicenseStore.h"

#include <cstring>

#if JUCE_MAC
 #include <CommonCrypto/CommonDigest.h>
#elif JUCE_WINDOWS
 #include <windows.h>
 #include <bcrypt.h>
 #pragma comment(lib, "bcrypt.lib")
#endif

namespace licensing
{
namespace
{
juce::String sha256Hex (const juce::String& text)
{
   #if JUCE_MAC
    unsigned char digest[CC_SHA256_DIGEST_LENGTH] {};
    CC_SHA256 (text.toRawUTF8(), (CC_LONG) std::strlen (text.toRawUTF8()), digest);
   #elif JUCE_WINDOWS
    unsigned char digest[32] {};
    BCRYPT_ALG_HANDLE algorithmHandle = nullptr;
    DWORD hashObjectSize = 0;
    DWORD dataSize = 0;
    DWORD hashSize = 0;
    BCRYPT_HASH_HANDLE hashHandle = nullptr;

    if (BCryptOpenAlgorithmProvider (&algorithmHandle, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0)
        return {};

    BCryptGetProperty (algorithmHandle, BCRYPT_OBJECT_LENGTH, (PUCHAR) &hashObjectSize, sizeof (hashObjectSize), &dataSize, 0);
    BCryptGetProperty (algorithmHandle, BCRYPT_HASH_LENGTH, (PUCHAR) &hashSize, sizeof (hashSize), &dataSize, 0);
    juce::HeapBlock<unsigned char> hashObject ((size_t) hashObjectSize);

    if (BCryptCreateHash (algorithmHandle, &hashHandle, hashObject.getData(), hashObjectSize, nullptr, 0, 0) != 0)
    {
        BCryptCloseAlgorithmProvider (algorithmHandle, 0);
        return {};
    }

    BCryptHashData (hashHandle, (PUCHAR) text.toRawUTF8(), (ULONG) std::strlen (text.toRawUTF8()), 0);
    BCryptFinishHash (hashHandle, digest, (ULONG) hashSize, 0);
    BCryptDestroyHash (hashHandle);
    BCryptCloseAlgorithmProvider (algorithmHandle, 0);
   #else
    juce::ignoreUnused (text);
    return {};
   #endif

    juce::String hex;

    for (auto byte : digest)
        hex << juce::String::toHexString ((int) byte).paddedLeft ('0', 2);

    return hex;
}

juce::String getString (const juce::var& value, const juce::Identifier& key)
{
    if (auto* object = value.getDynamicObject())
        return object->getProperty (key).toString();

    return {};
}
}

juce::var LicenseStore::Data::toVar() const
{
    auto object = std::make_unique<juce::DynamicObject>();
    object->setProperty ("licenseKey", licenseKey);
    object->setProperty ("activationId", activationId);
    object->setProperty ("signedLicense", signedLicense);
    object->setProperty ("firstLaunchIso", firstLaunchIso);
    object->setProperty ("lastSuccessfulValidationIso", lastSuccessfulValidationIso);
    object->setProperty ("trialStartIso", trialStartIso);
    object->setProperty ("lastSeenIso", lastSeenIso);
    object->setProperty ("machineIdHash", machineIdHash);
    object->setProperty ("checksum", checksum);
    return juce::var (object.release());
}

LicenseStore::Data LicenseStore::Data::fromVar (const juce::var& value)
{
    Data data;
    data.licenseKey = getString (value, "licenseKey");
    data.activationId = getString (value, "activationId");
    data.signedLicense = getString (value, "signedLicense");
    data.firstLaunchIso = getString (value, "firstLaunchIso");
    data.lastSuccessfulValidationIso = getString (value, "lastSuccessfulValidationIso");
    data.trialStartIso = getString (value, "trialStartIso");
    data.lastSeenIso = getString (value, "lastSeenIso");
    data.machineIdHash = getString (value, "machineIdHash");
    data.checksum = getString (value, "checksum");
    return data;
}

LicenseStore::LicenseStore (LicenseConfig configIn) : config (std::move (configIn))
{
}

bool LicenseStore::load (Data& data, juce::String& errorMessage) const
{
    const auto file = getBackingFile();

    if (! file.existsAsFile())
        return true;

    const auto text = file.loadFileAsString();

    if (text.isEmpty())
    {
        errorMessage = "Stored licensing state is unreadable";
        return false;
    }

    const auto parsed = juce::JSON::parse (text);

    if (parsed.isVoid())
    {
        errorMessage = "Stored licensing state is invalid JSON";
        return false;
    }

    data = Data::fromVar (parsed);
    return true;
}

bool LicenseStore::save (Data data, juce::String& errorMessage) const
{
    const auto file = getBackingFile();

    if (! file.getParentDirectory().createDirectory())
    {
        errorMessage = "Could not create licensing data directory";
        return false;
    }

    data.checksum = calculateChecksum (data);
    const auto json = juce::JSON::toString (data.toVar(), true);

    if (! file.replaceWithText (json))
    {
        errorMessage = "Could not write licensing state";
        return false;
    }

    return true;
}

bool LicenseStore::remove (juce::String& errorMessage) const
{
    const auto file = getBackingFile();

    if (file.existsAsFile() && ! file.deleteFile())
    {
        errorMessage = "Could not delete licensing state";
        return false;
    }

    return true;
}

bool LicenseStore::verifyTamper (const Data& data) const
{
    return data.checksum.isNotEmpty() && data.checksum == calculateChecksum (data);
}

juce::File LicenseStore::getBackingFile() const
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                    .getChildFile (config.companyName)
                    .getChildFile (config.applicationName)
                    .getChildFile ("Licensing");
    return root.getChildFile (config.getStoreFileName());
}

juce::String LicenseStore::calculateChecksum (const Data& data) const
{
    juce::StringArray parts;
    parts.add (config.companyName);
    parts.add (config.applicationName);
    parts.add (config.productId);
    parts.add (data.licenseKey);
    parts.add (data.activationId);
    parts.add (data.signedLicense);
    parts.add (data.firstLaunchIso);
    parts.add (data.lastSuccessfulValidationIso);
    parts.add (data.trialStartIso);
    parts.add (data.lastSeenIso);
    parts.add (data.machineIdHash);
    parts.add ("v1");
    return sha256Hex (parts.joinIntoString ("|"));
}
}
